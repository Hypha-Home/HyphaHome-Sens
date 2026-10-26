// ===================================================================
// File:            MyAHT21.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Adafruit_AHTX0.h>
#include "SensorHealth.h"
#include "config.h"

/// @brief Klimasensor AHT21 (Temperatur und Luftfeuchte), I2C-Adresse 0x38. (Auf einem Chip mit dem ENS160-Gas-Sensor verbaut.)
namespace MyAHT21 {
    extern Adafruit_AHTX0 aht;
    extern bool isConnected;
    extern float temperature;   // °C
    extern float humidity;      // % rF

    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();
    
    /// @brief Liest die Sensordaten aus.
    /// @param print true: Ausgabe auf Serial, false: keine Ausgabe
    void read(bool print = true);
    
    /// @brief Prüft ohne Initialisierung, ob der Sensor am Bus antwortet.
    /// @return true wenn der Sensor antwortet
    bool probe();
}
