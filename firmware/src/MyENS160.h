// ===================================================================
// File:            MyENS160.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "SensorHealth.h"
#include "config.h"
// Der ScioSense-Treiber (ScioSense_ENS16x.h) wird bewusst nur in MyENS160.cpp eingebunden: Er definiert
// Member-Funktionen ohne "inline" im Header und würde bei mehrfacher Einbindung den Linker
// mit Mehrfachdefinitionen abbrechen lassen. Die ENS160-Instanz ist deshalb dort "static".

#define ENS160_ADDRESS_HIGH 0x53 // ADDR-Pin auf VCC (Standard bei den meisten Breakouts)
#define ENS160_ADDRESS_LOW  0x52 // ADDR-Pin auf GND

/// @brief Luftqualitätssensor ENS160 ( Auf einem Board mit dem AHT21 )
namespace MyENS160 {
    /// @brief Ampel-Stufe aus dem Luftqualitätsindex (AQI 1-5)
    enum class Level { Unbekannt, Super, Gut, Mittel, Schlecht, Hoch };

    /// @brief Validity-Flag des ENS160: nur bei Normal sind die Messwerte belastbar
    enum class Validity : uint8_t { Normal = 0, WarmUp = 1, StartUp = 2, Invalid = 3 };

    extern bool isConnected;
    extern uint8_t aqi;         // Index 1 (sehr gut) bis 5 (schlecht), 0 = noch kein Wert
    extern uint16_t tvoc;       // ppb
    extern uint16_t eco2;       // ppm
    extern Validity validity;
    extern Level level;
    
    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();

    /// @brief Liest den Sensor aus.
    /// @param print true gibt den Messwert auf Serial aus
    void read(bool print = true);

    /// @brief Prüft ohne Initialisierung, ob der Sensor am Bus antwortet.
    /// @return true wenn der Sensor antwortet
    bool probe();

    /// @brief Übergibt Umgebungswerte zur Kompensation der Gasmessung.
    /// Muss vor read() aufgerufen werden, damit die Werte in denselben Messzyklus eingehen.
    /// @param celsius Temperatur in °C
    /// @param relativeHumidity Luftfeuchte in % rF
    void setCompensation(float celsius, float relativeHumidity);

    /// @brief Gibt das Level anhand des AQI zurück
    /// @param aqiIndex AQI Index
    /// @return Level
    Level aqiToLevel(uint8_t aqiIndex);

    /// @brief Wandelt das Level in einen String um (für MQTT)
    /// @param l Level
    /// @return String für MQTT
    const char* levelToString(Level l);

    /// @brief Wandelt das Validity-Flag in einen String im (für MQTT)
    /// @param v Status
    /// @return String für MQTT
    const char* validityToString(Validity v);
}
