// ===================================================================
// File:            MyBH1750.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <BH1750.h>
#include "SensorHealth.h"
#include "config.h"

#define BH1750_ADDRESS 0x23 // ggf. 0x5C je nach ADDR-Pin

/// @brief Helligkeitssensor BH1750
namespace MyBH1750 {
    extern BH1750 lightMeter;
    extern bool isConnected;
    extern float lux;            // Rohwert des Sensors in lx
    extern float luxCalibrated;  // Mit dem Kalibrierpunkt korrigierter Wert in lx

    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();

    /// @brief Liest den Sensor aus.
    /// @param print true gibt den Messwert auf Serial aus
    /// @return Der korrigierte Wert in lx (identisch mit luxCalibrated)
    float read(bool print = true);

    /// @brief Prüft ohne Initialisierung, ob der Sensor am Bus antwortet. Für die
    /// Erstinbetriebnahme, die daraus ableitet welche Sensoren bestückt sind.
    /// @return true wenn der Sensor antwortet
    bool probe();
}