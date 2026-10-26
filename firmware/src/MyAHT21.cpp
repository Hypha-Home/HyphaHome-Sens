// ===================================================================
// File:            MyAHT21.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MyAHT21.h"

namespace MyAHT21 {
    Adafruit_AHTX0 aht;
    bool isConnected = false;
    float temperature = 0.0f;
    float humidity = 0.0f;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "AHT21";

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------
    
    /// @brief Startet den Sensor, ohne Ausgabe auf Serial.
    /// @return true bei Erfolg
    static bool startSensor() {
        return aht.begin(&Wire, 0, AHTX0_I2CADDR_DEFAULT);
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return SensorHealth::i2cPing(AHTX0_I2CADDR_DEFAULT);
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.aht21.enabled) {
            isConnected = false;
            Serial.println(F("[AHT21] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor();
        Serial.print(F("[AHT21] Init: "));
        Serial.println(isConnected ? F("OK") : F("FEHLER"));
    }

    void read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.aht21.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[AHT21] deaktiviert"));
            }
            return;
        }

        // Wenn der Sensor nicht verbunden ist, versuchen wir ihn zu starten, wenn das Retry-Intervall abgelaufen ist.
        if (!isConnected) {
            if (SensorHealth::retryDue(health)
                && SensorHealth::i2cPing(AHTX0_I2CADDR_DEFAULT)
                && startSensor()) {
                SensorHealth::reportOk(isConnected, health, TAG);
            }
            return;
        }

        // getEvent() liefert false, wenn die Messung oder der I2C-Zugriff fehlschlägt.
        sensors_event_t humidityEvent, tempEvent;
        if (!aht.getEvent(&humidityEvent, &tempEvent)) {
            SensorHealth::reportFail(isConnected, health, TAG);
            return; // letzte gültige Werte behalten
        }

        // Sernsorwerte abspeichern
        SensorHealth::reportOk(isConnected, health, TAG);
        temperature = tempEvent.temperature;
        humidity = humidityEvent.relative_humidity;

        // Ausgabe auf Serial, wenn gewünscht
        if (print) {
            Serial.print(F("AHT21:   "));
            Serial.print(temperature);
            Serial.print(F(" °C   Feuchte: "));
            Serial.print(humidity);
            Serial.println(F(" %"));
        }
    }
}
