// ===================================================================
// File:            MySHT41.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MySHT41.h"

namespace MySHT41 {
    Adafruit_SHT4x sht4 = Adafruit_SHT4x();
    bool isConnected = false;
    float temperature = 0.0f;
    float temperatureCalibrated = 0.0f;
    float humidity = 0.0f;
    float humidityCalibrated = 0.0f;
    float temperatureSmoothed = 0.0f;
    float humiditySmoothed = 0.0f;
    float dewPoint = 0.0f;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "SHT41";

    // Ringpuffer der letzten korrigierten Messungen.
    static float temperatureHistory[SHT41_SMOOTHING_MAX];
    static float humidityHistory[SHT41_SMOOTHING_MAX];
    static uint8_t historyNext = 0;    // Platz der naechsten Messung
    static uint8_t historyCount = 0;   // gueltige Eintraege, hoechstens SHT41_SMOOTHING_MAX

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Taupunkt nach der Magnus-Formel, mit denselben Beiwerten wie der Sättigungsdampfdruck.
    /// @param celsius Temperatur in °C
    /// @param humidity Relative Luftfeuchte in %
    /// @return Taupunkt in °C
    static float dewPointOf(float celsius, float humidity) {
        const float a = 17.62f, b = 243.12f;
        if (humidity < 0.1f) humidity = 0.1f;
        if (humidity > 100.0f) humidity = 100.0f;
        const float gamma = logf(humidity / 100.0f) + (a * celsius) / (b + celsius);
        return (b * gamma) / (a - gamma);
    }

    /// @brief Nimmt die Messung auf und bildet den Mittelwert ueber die letzten `smoothing`.
    /// @param temperature Weitere Temperatur für den Ringpuffer
    /// @param humidity Weitere Luftfeuchte für den Ringpuffer
    static void addToHistory(float temperature, float humidity) {
        temperatureHistory[historyNext] = temperature;
        humidityHistory[historyNext] = humidity;
        historyNext = (historyNext + 1) % SHT41_SMOOTHING_MAX;
        if (historyCount < SHT41_SMOOTHING_MAX) historyCount++;

        uint8_t window = Config::sensors.sht41.smoothing;
        if (window < 1) window = 1;
        if (window > historyCount) window = historyCount;

        float temperatureSum = 0.0f, humiditySum = 0.0f;
        for (uint8_t i = 1; i <= window; i++) {
            const uint8_t index = (historyNext + SHT41_SMOOTHING_MAX - i) % SHT41_SMOOTHING_MAX;
            temperatureSum += temperatureHistory[index];
            humiditySum += humidityHistory[index];
        }
        temperatureSmoothed = temperatureSum / window;
        humiditySmoothed = humiditySum / window;

        // Taupunkt wird hier berechnet.
        dewPoint = dewPointOf(temperatureSmoothed, humiditySmoothed);
    }

    /// @brief Rechnet den Rohwert über die Ausgleichsgerade der Kalibrierpunkte um.    ///
    /// @param raw Rohwert in °C
    /// @return Korrigierter Wert in °C
    static float applyCalibration(float raw) {
        return Calibration::apply(Config::sensors.sht41.temperatureCalibration, raw, Calibration::Fallback::Offset);
    }

    /// @brief Sättigungsdampfdruck über Wasser nach der Magnus-Formel.
    /// @param celsius Temperatur in °C
    /// @return Sättigungsdampfdruck in hPa
    static float saturationVapourPressure(float celsius) {
        return 6.112f * expf((17.62f * celsius) / (243.12f + celsius));
    }

    /// @brief Rechnet die relative Luftfeuchte auf die korrigierte Temperatur um.
    /// @param rawHumidity Vom Sensor gemessene relative Feuchte in %
    /// @param rawTemperature Vom Sensor gemessene Temperatur in °C
    /// @param calibratedTemperature Korrigierte Temperatur in °C
    /// @return Relative Feuchte in % bei der korrigierten Temperatur, begrenzt auf 0 bis 100
    static float applyHumidityCorrection(float rawHumidity, float rawTemperature, float calibratedTemperature) {
        // Ohne Temperaturkalibrierung sind beide Werte bitgleich und das Verhältnis wäre 1.
        if (rawTemperature == calibratedTemperature) return rawHumidity;

        float corrected = rawHumidity * saturationVapourPressure(rawTemperature) / saturationVapourPressure(calibratedTemperature);

        if (corrected > 100.0f) return 100.0f;
        if (corrected < 0.0f) return 0.0f;
        return corrected;
    }

    /// @brief Startet den Sensor und setzt Präzision und Heizer.
    /// @return true bei Erfolg
    static bool startSensor() {
        if (!sht4.begin()) return false;
        sht4.setPrecision(SHT4X_HIGH_PRECISION);
        sht4.setHeater(SHT4X_NO_HEATER);
        return true;
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return SensorHealth::i2cPing(SHT4x_DEFAULT_ADDR);
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.sht41.enabled) {
            isConnected = false;
            Serial.println(F("[SHT41] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor();
        Serial.print(F("[SHT41] Init: "));
        Serial.println(isConnected ? F("OK") : F("FEHLER"));
    }

    void read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.sht41.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[SHT41] deaktiviert"));
            }
            return;
        }

        // Wenn der Sensor nicht verbunden ist, versuchen wir ihn zu starten, wenn das Retry-Intervall abgelaufen ist.
        if (!isConnected) {
            historyCount = 0;
            if (SensorHealth::retryDue(health) && SensorHealth::i2cPing(SHT4x_DEFAULT_ADDR) && startSensor()) {
                SensorHealth::reportOk(isConnected, health, TAG);
            }
            return;
        }

        // getEvent() liefert false, wenn die Messung oder der I2C-Zugriff fehlschlägt.
        sensors_event_t humidityEvent, tempEvent;
        if (!sht4.getEvent(&humidityEvent, &tempEvent)) {
            SensorHealth::reportFail(isConnected, health, TAG);
            return;
        }

        
        // Sernsorwerte abspeichern
        SensorHealth::reportOk(isConnected, health, TAG);
        temperature = tempEvent.temperature;
        temperatureCalibrated = applyCalibration(temperature);
        humidity = humidityEvent.relative_humidity;

        // Die Feuchte hängt an der Temperatur und muss deshalb mitgezogen werden, obwohl sie selbst nicht kalibriert wird.
        humidityCalibrated = applyHumidityCorrection(humidity, temperature, temperatureCalibrated);
        addToHistory(temperatureCalibrated, humidityCalibrated);

        // Ausgabe auf Serial, wenn gewünscht
        if (print) {
            // Die Rohwerte nur dann dazuschreiben, wenn eine Kalibrierung sie verändert hat.
            Serial.print(F("Temp:    "));
            Serial.print(temperatureCalibrated);
            Serial.print(F(" °C"));
            if (temperatureCalibrated != temperature) {
                Serial.print(F(" (roh: "));
                Serial.print(temperature);
                Serial.print(F(" °C)"));
            }
            Serial.print(F("   Feuchte: "));
            Serial.print(humidityCalibrated);
            Serial.print(F(" %"));
            if (humidityCalibrated != humidity) {
                Serial.print(F(" (roh: "));
                Serial.print(humidity);
                Serial.print(F(" %)"));
            }
            Serial.println();
        }
    }
}
