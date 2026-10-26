// ===================================================================
// File:            MySCD40.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MySCD40.h"

namespace MySCD40 {
    SensirionI2CScd4x scd4x;
    bool isConnected = false;
    uint16_t co2 = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    Level level = Level::Super;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "SCD40";


    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    // Der Start des SCD40 enthält ein blockierendes delay(500) (laut Datenblatt nach dem
    // Stoppen nötig). Deshalb wird ein fehlender Sensor seltener gesucht als die übrigen,
    // damit die loop() nicht alle 15 s für eine halbe Sekunde steht.
    static const uint32_t RETRY_INTERVAL_MS = 30000;

    /// @brief Startet die zyklische Messung, ohne Ausgabe auf Serial.
    /// @param verbose true gibt Fehlermeldungen auf Serial aus (nur beim Hochfahren sinnvoll)
    /// @return true wenn die Messung läuft
    static bool startSensor(bool verbose) {
        uint16_t err;
        char errMsg[256];

        scd4x.begin(Wire);

        // Falls der Sensor noch von einer vorherigen Session misst -> stoppen
        err = scd4x.stopPeriodicMeasurement();
        if (err && verbose) {
            errorToString(err, errMsg, sizeof(errMsg));
            Serial.print(F("[SCD40] stopPeriodicMeasurement Fehler: "));
            Serial.println(errMsg);
        }
        delay(500); // laut Datenblatt nach Stop nötig

        err = scd4x.startPeriodicMeasurement();
        if (err) {
            if (verbose) {
                errorToString(err, errMsg, sizeof(errMsg));
                Serial.print(F("[SCD40] startPeriodicMeasurement Fehler: "));
                Serial.println(errMsg);
            }
            return false;
        }
        return true;
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return SensorHealth::i2cPing(SCD40_ADDRESS);
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.scd40.enabled) {
            isConnected = false;
            Serial.println(F("[SCD40] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor(true);
        Serial.print(F("[SCD40] Init: "));
        Serial.println(isConnected ? F("OK (Messung gestartet, erster Wert nach ~5s)") : F("FEHLER"));
    }

    void read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.scd40.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[SCD40] deaktiviert"));
            }
            return;
        }

        if (!isConnected) {
            if (SensorHealth::retryDue(health, RETRY_INTERVAL_MS)
                && SensorHealth::i2cPing(SCD40_ADDRESS)
                && startSensor(false)) {
                SensorHealth::reportOk(isConnected, health, TAG);
            }
            return;
        }

        uint16_t err;
        char errMsg[256];
        bool dataReady = false;

        err = scd4x.getDataReadyFlag(dataReady);
        if (err) {
            // Kommunikationsfehler: der Sensor ist nicht mehr erreichbar.
            errorToString(err, errMsg, sizeof(errMsg));
            Serial.print(F("[SCD40] getDataReadyFlag Fehler: "));
            Serial.println(errMsg);
            SensorHealth::reportFail(isConnected, health, TAG);
            return;
        }

        // Der Sensor hat geantwortet, er ist also da - auch wenn noch kein neuer Wert vorliegt.
        SensorHealth::reportOk(isConnected, health, TAG);

        if (!dataReady) {
            if (print) {
                Serial.print(F("CO2:     "));
                Serial.print(co2);
                Serial.print(F(" ppm ("));
                Serial.print(levelToString(level));
                Serial.println(F(")"));
            }
            return;
        }

        // Sernsorwerte abspeichern
        err = scd4x.readMeasurement(co2, temperature, humidity);
        if (err) {
            errorToString(err, errMsg, sizeof(errMsg));
            Serial.print(F("[SCD40] readMeasurement Fehler: "));
            Serial.println(errMsg);
            SensorHealth::reportFail(isConnected, health, TAG);
        } else if (co2 == 0) {
            // Ungültige Probe. Der Sensor antwortet aber, gilt also weiter als vorhanden.
            Serial.println(F("CO2:     ungültiger Messwert (0 ppm)"));
        } else {
            level = co2ToLevel(co2);
            // Ausgabe auf Serial, wenn gewünscht
            if (print) {
                Serial.print(F("CO2:     "));
                Serial.print(co2);
                Serial.print(F(" ppm ("));
                Serial.print(levelToString(level));
                Serial.println(F(")"));
            }
        }
    }

    Level co2ToLevel(uint16_t co2ppm) {
        if (co2ppm < 800) return Level::Super;
        if (co2ppm < 1000) return Level::Gut;
        if (co2ppm < 1200) return Level::Mittel;
        if (co2ppm < 1600) return Level::Schlecht;
        return Level::Hoch;
    }

    const char* levelToString(Level l) {
        switch (l) {
            case Level::Super:    return "Super";
            case Level::Gut:      return "Gut";
            case Level::Mittel:   return "Mittel";
            case Level::Schlecht: return "Schlecht";
            case Level::Hoch:     return "Hoch";
        }
        return "?";
    }
}
