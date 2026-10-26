// ===================================================================
// File:            MyENS160.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MyENS160.h"
#include <ScioSense_ENS16x.h>

namespace MyENS160 {
    static ENS160 ens160;
    bool isConnected = false;
    uint8_t aqi = 0;
    uint16_t tvoc = 0;
    uint16_t eco2 = 0;
    Validity validity = Validity::Invalid;
    Level level = Level::Unbekannt;

    // Vom Start ermittelte I2C-Adresse
    static uint8_t ensAddress = ENS160_ADDRESS_HIGH;

    // gesetzte Kompensationswerte, gültig bis zum nächsten read().
    static bool haveCompensation = false;
    static float compTemperature = 0.0f;
    static float compHumidity = 0.0f;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "ENS160";

    
    // --------------------------------------------------------------
    // Weitere Fehlerüberwachung:
    // Der Sensor kann sich im Betrieb unbemerkt zurücksetzen (Spannungseinbruch, Störung auf dem
    // Bus, ...). Er antwortet am Bus dann weiter, steht aber nicht mehr im Messmodus und liefert keine
    // Daten. Ein Ping oder ein fehlerfreies Lesen merkt davon nichts, deshalb wird hier auf die
    // Daten selbst geachtet.
    // --------------------------------------------------------------

    /// STATAS im Statusregister: gesetzt, solange ein Betriebsmodus läuft.
    static const uint8_t STATUS_OPMODE_RUNNING = 0x80;

    /// Ohne gültige Daten länger als das gelten die Werte als nicht vorhanden (Anzeige "–"),
    /// und der Sensor wird neu gestartet. Der ENS160 liefert etwa jede Sekunde einen Wert, auch
    /// in der Aufwärmphase; drei Minuten sind also eine klare Störung.
    static const uint32_t NO_DATA_TIMEOUT_MS = 180000;

    /// Mindestabstand zwischen zwei Neustarts des Messmodus bzw. des ganzen Sensors.
    static const uint32_t RESTART_INTERVAL_MS = 60000;

    static uint32_t lastGoodData = 0;    // letzter Zeitpunkt mit gültigen Messwerten (oder Sensorstart)
    static uint32_t lastRestart = 0;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Startet den Sensor und probiert dabei beide üblichen Adressen durch, ohne Ausgabe
    /// auf Serial. Merkt sich die gefundene Adresse in ensAddress.
    /// @return true bei Erfolg
    static bool startSensor() {
        const uint8_t addresses[] = { ENS160_ADDRESS_HIGH, ENS160_ADDRESS_LOW };
        for (uint8_t address : addresses) {
            // Erst anpingen: spart den vollen Init-Durchlauf (inkl. Reset und Wartezeiten)
            // an einer Adresse, unter der ohnehin niemand antwortet.
            if (!SensorHealth::i2cPing(address)) continue;

            ens160.begin(&Wire, address);
            if (ens160.init() && ens160.startStandardMeasure() == RESULT_OK) {
                ensAddress = address;
                lastGoodData = millis();   // Schonfrist: Der Sensor braucht nach dem Start einen Moment
                lastRestart = lastGoodData;
                return true;
            }
        }
        return false;
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return SensorHealth::i2cPing(ENS160_ADDRESS_HIGH)
            || SensorHealth::i2cPing(ENS160_ADDRESS_LOW);
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.ens160.enabled) {
            isConnected = false;
            Serial.println(F("[ENS160] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor();
        Serial.print(F("[ENS160] Init: "));
        if (isConnected) {
            const uint8_t* fw = ens160.getFirmwareVersion();
            Serial.print(F("OK (Adresse 0x"));
            Serial.print(ensAddress, HEX);
            Serial.print(F(", FW "));
            Serial.print(fw[0]);
            Serial.print('.');
            Serial.print(fw[1]);
            Serial.print('.');
            Serial.print(fw[2]);
            Serial.println(F(", Messwerte belastbar nach ~3 min, beim ersten Betrieb ~1 h)"));
        } else {
            Serial.println(F("FEHLER"));
        }
    }

    void read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.ens160.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[ENS160] deaktiviert"));
            }
            return;
        }

        // Wenn der Sensor nicht verbunden ist, versuchen wir ihn zu starten, wenn das Retry-Intervall abgelaufen ist.
        if (!isConnected) {
            if (SensorHealth::retryDue(health) && startSensor()) {
                SensorHealth::reportOk(isConnected, health, TAG);
            }
            return;
        }

        // Der ScioSense-Treiber wartet bei fehlendem ACK endlos in seiner Lese-Schleife
        // -> vorher prüfen, ob der Sensor noch auf dem Bus antwortet.
        if (!SensorHealth::i2cPing(ensAddress)) {
            SensorHealth::reportFail(isConnected, health, TAG);
            return;
        }

        // Kompensationswerte setzen, sofern gesetzt
        if (haveCompensation) {
            ens160.writeCompensation(Ens16x_CalcTempInFromCelsius(compTemperature),
                                     Ens16x_CalcRhIn(compHumidity));
            haveCompensation = false;
        }

        // Fehler beim lesen erkennen
        if (ens160.update() == RESULT_IO_ERROR) {
            SensorHealth::reportFail(isConnected, health, TAG);
            return;
        }
        
        // Messmodus auslesen
        SensorHealth::reportOk(isConnected, health, TAG);
        const uint8_t status = ens160.getDeviceStatus();
        validity = static_cast<Validity>((status >> 2) & 0x03);

        const uint32_t now = millis();

        // Steht der Sensor nicht mehr im Messmodus, ist er zurückgesetzt worden: Modus neu starten.
        if ((status & STATUS_OPMODE_RUNNING) == 0 && now - lastRestart >= RESTART_INTERVAL_MS) {
            lastRestart = now;
            const Result result = ens160.startStandardMeasure();
            Serial.printf("[ENS160] Messmodus lief nicht, neu gestartet (%s)\n",
                          result == RESULT_OK ? "OK" : "FEHLER");
        }

        // Sernsorwerte abspeichern
        if (ens160.hasNewData() && validity != Validity::Invalid) {
            const uint8_t newAqi = ens160.getAirQualityIndex_UBA();
            // AQI 0 ist kein Messwert (gültig sind 1 bis 5), sondern das Zeichen für "nichts".
            if (newAqi > 0) {
                aqi = newAqi;
                tvoc = ens160.getTvoc();
                eco2 = ens160.getEco2();
                level = aqiToLevel(aqi);
                lastGoodData = now;
            }
        }

        // Lange keine gültigen Daten: Werte verwerfen, damit nicht alte oder leere Zahlen als
        // Messung durchgehen, und den Sensor ganz neu starten.
        if (now - lastGoodData >= NO_DATA_TIMEOUT_MS) {
            if (aqi != 0) {
                aqi = 0;
                tvoc = 0;
                eco2 = 0;
                level = Level::Unbekannt;
                Serial.println(F("[ENS160] Keine gueltigen Daten, Werte verworfen"));
            }
            // Meldet der Sensor selbst Aufwärmen oder Erststart (bis zu einer Stunde bei einem neuen
            // Sensor), ist das Warten gewollt: Ein Neustart finge die Phase von vorn an.
            const bool warming = (validity == Validity::WarmUp || validity == Validity::StartUp);
            if (!warming && now - lastRestart >= RESTART_INTERVAL_MS) {
                lastRestart = now;
                Serial.printf("[ENS160] Keine gueltigen Daten seit %lu s (Status 0x%02X), starte neu\n",
                              (unsigned long)((now - lastGoodData) / 1000UL), status);
                startSensor();   // setzt bei Erfolg die Fristen neu
            }
        }

        // Ausgabe auf Serial, wenn gewünscht
        if (print) {
            Serial.print(F("ENS160:  AQI "));
            Serial.print(aqi);
            Serial.print(F(" ("));
            Serial.print(levelToString(level));
            Serial.print(F(")   TVOC: "));
            Serial.print(tvoc);
            Serial.print(F(" ppb   eCO2: "));
            Serial.print(eco2);
            Serial.print(F(" ppm   Status: "));
            Serial.println(validityToString(validity));
        }
    }

    Level aqiToLevel(uint8_t aqiIndex) {
        switch (aqiIndex) {
            case AQI_UBA_EXCELLENT: return Level::Super;
            case AQI_UBA_GOOD:      return Level::Gut;
            case AQI_UBA_MODERATE:  return Level::Mittel;
            case AQI_UBA_POOR:      return Level::Schlecht;
            case AQI_UBA_UNHEALTHY: return Level::Hoch;
        }
        return Level::Unbekannt;
    }

    const char* levelToString(Level l) {
        switch (l) {
            case Level::Unbekannt: return "Unbekannt";
            case Level::Super:     return "Super";
            case Level::Gut:       return "Gut";
            case Level::Mittel:    return "Mittel";
            case Level::Schlecht:  return "Schlecht";
            case Level::Hoch:      return "Hoch";
        }
        return "?";
    }

    const char* validityToString(Validity v) {
        switch (v) {
            case Validity::Normal:  return "Normal";
            case Validity::WarmUp:  return "Aufwärmen";
            case Validity::StartUp: return "Erststart";
            case Validity::Invalid: return "Ungültig";
        }
        return "?";
    }

    void setCompensation(float celsius, float relativeHumidity) {
        compTemperature = celsius;
        compHumidity = relativeHumidity;
        haveCompensation = true;
    }
}
