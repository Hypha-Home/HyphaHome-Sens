// ===================================================================
// File:            MySHT41.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Adafruit_SHT4x.h>
#include "SensorHealth.h"
#include "config.h"

#ifndef SHT41_SMOOTHING_MAX
    /// @brief max. anzahl an zu mittelnden Sensorwerten
    #define SHT41_SMOOTHING_MAX 60
#endif

/// @brief Klimasensor SHT41, I2C-Adresse 0x44.
namespace MySHT41 {
    extern Adafruit_SHT4x sht4;
    extern bool isConnected;
    extern float temperature;            // Rohwert des Sensors in °C
    extern float humidity;               // Rohwert der relativen Luftfeuchte in %

    extern float temperatureCalibrated;  // Über die Ausgleichsgerade korrigierter Wert in °C
    extern float humidityCalibrated;     // Wird angepasst aufgrund der geänderten Temperatur

    /// Smoothed enthält eine gemittelte Temperatur. Die Anzahl der Messwerte für die Mittelung lässt sich einstellen
    /// Dieser Wert wird per MQTT übertragen.
    extern float temperatureSmoothed;    // Mittelwert der Temperatur
    extern float humiditySmoothed;       // Mittelwert der Luftfeuchte

    /// @brief Taupunkt in °C, berechnet aus den gemittelten Werten
    extern float dewPoint;

    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();

    /// @brief Liest den Sensor aus.
    /// @param print true gibt den Messwert auf Serial aus
    void read(bool print = true);

    /// @brief Prüft ohne Initialisierung, ob der Sensor am Bus antwortet.
    /// @return true wenn der Sensor antwortet
    bool probe();
}
