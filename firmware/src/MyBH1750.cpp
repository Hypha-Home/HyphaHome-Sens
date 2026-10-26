// ===================================================================
// File:            MyBH1750.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MyBH1750.h"

namespace MyBH1750 {
    BH1750 lightMeter(BH1750_ADDRESS);
    bool isConnected = false;
    float lux = 0.0f;
    float luxCalibrated = 0.0f;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "BH1750";

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Rechnet den Rohwert über die Ausgleichsgerade der Kalibrierpunkte um.
    /// @param raw Rohwert in lx
    /// @return Korrigierter Wert in lx, nie negativ
    static float applyCalibration(float raw) {
        float corrected = Calibration::apply(Config::sensors.bh1750.luxCalibration, raw, Calibration::Fallback::Factor);
        return corrected < 0.0f ? 0.0f : corrected;
    }

    /// @brief Startet den Sensor im Dauermessmodus und setzt das Messzeitregister.
    /// @return true bei Erfolg
    static bool startSensor() {
        if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) return false;
        lightMeter.setMTreg(254);
        return true;
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return SensorHealth::i2cPing(BH1750_ADDRESS);
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.bh1750.enabled) {
            isConnected = false;
            Serial.println(F("[BH1750] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor();
        Serial.print(F("[BH1750] Init: "));
        Serial.println(isConnected ? F("OK") : F("FEHLER"));
    }

    float read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.bh1750.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[BH1750] deaktiviert"));
            }
            return luxCalibrated;
        }

        // Wenn der Sensor nicht verbunden ist, versuchen wir ihn zu starten, wenn das Retry-Intervall abgelaufen ist.
        if (!isConnected) {
            if (SensorHealth::retryDue(health) && SensorHealth::i2cPing(BH1750_ADDRESS) && startSensor()) {
                SensorHealth::reportOk(isConnected, health, TAG);
            }
            return luxCalibrated;
        }

        // readLightLevel() meldet Fehler über negative Rückgabewerte:
        //   -1.0 = I2C-Lesefehler (Sensor antwortet nicht),
        //   -2.0 = Sensor nicht konfiguriert.
        float level = lightMeter.readLightLevel();
        if (level < 0.0f) {
            SensorHealth::reportFail(isConnected, health, TAG);
            // letzten gültigen Wert zurückgeben statt mit dem Fehlercode zu überschreiben
            return luxCalibrated;
        }

        // Sernsorwerte abspeichern
        SensorHealth::reportOk(isConnected, health, TAG);
        lux = level;
        luxCalibrated = applyCalibration(lux);

        // Ausgabe auf Serial, wenn gewünscht
        if (print) {
            Serial.print(F("Licht:   "));
            Serial.print(luxCalibrated);
            Serial.print(F(" lx"));
            // Den Rohwert nur dann dazuschreiben, wenn eine Kalibrierung ihn verändert hat.
            if (luxCalibrated != lux) {
                Serial.print(F(" (roh: "));
                Serial.print(lux);
                Serial.print(F(" lx)"));
            }
            Serial.println();
        }

        return luxCalibrated;
    }
}
