// ===================================================================
// File:            MySCD40.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <SensirionI2CScd4x.h>
#include "SensorHealth.h"
#include "config.h"

#define SCD40_ADDRESS 0x62 // ggf. 0x5C je nach ADDR-Pin

namespace MySCD40 {
    enum class Level { Super, Gut, Mittel, Schlecht, Hoch };

    extern SensirionI2CScd4x scd4x;
    extern bool isConnected;
    extern uint16_t co2;
    extern float temperature;
    extern float humidity;
    extern Level level;

    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();

    /// @brief Liest den Sensor aus.
    /// @param print true gibt den Messwert auf Serial aus
    void read(bool print = true);

    /// @brief Prüft ohne Initialisierung, ob der Sensor am Bus antwortet.
    /// @return true wenn der Sensor antwortet
    bool probe();

    /// @brief Gibt das Level anhand des Index zurück
    /// @param aqiIndex Index
    /// @return Level
    Level co2ToLevel(uint16_t co2ppm);

    /// @brief Wandelt das Level in einen String um (für MQTT)
    /// @param l Level
    /// @return String für MQTT
    const char* levelToString(Level l);
}
