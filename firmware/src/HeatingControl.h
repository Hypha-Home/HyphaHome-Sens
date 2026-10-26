// ===================================================================
// File:            HeatingControl.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "MySHT41.h"
#include "ShellySwitch.h"

#ifndef HEATINGCONTROL_TICK_MS
    /// @brief Intervall in der die mainloop durchlaufen wird
    #define HEATINGCONTROL_TICK_MS 1000UL
#endif

#ifndef HEATING_SYNC_INTERVAL_MS
    /// @brief Abstand, in dem die Heizung die Shelly-Kanäle abfragt und dem Soll-Zustand angleicht (ms).
    #define HEATING_SYNC_INTERVAL_MS 60000UL
#endif

#ifndef HEATING_SENSOR_LOSS_OFF_MIN
    /// @brief Zeit (minuten) ohne Temperaturmesswert schaltet die Regelung die Heizung ab.
    #define HEATING_SENSOR_LOSS_OFF_MIN 10
#endif

/// @brief Regelung der Heizung: schaltet die gewählten Shelly-Kanäle nach der Raumtemperatur.
/// Sehr rudimentär bisher
namespace HeatingControl {

    /// @brief Soll-Zustand der Heizung.
    enum class Desired : uint8_t { Unknown, Off, On };

    /// @brief Ist-Zustand aller Ausgänge zusammen.
    enum class Actual : uint8_t {
        Unknown,      // Noch nicht abgefragt
        Off,          // Alle Kanäle aus
        On,           // Alle Kanäle an
        Mixed,        // Teils an, teils aus
        Unreachable   // Mindestens ein Kanal war nicht erreichbar oder unbekannt
    };

    /// @brief Status der Regelung
    struct Status {
        bool    enabled = false;        // Regelung eingeschaltet und mit Ausgängen
        bool    sensorOk = false;       // Temperatur liegt vor
        Desired desired = Desired::Unknown;
        Actual  actual = Actual::Unknown;
        /// Restsekunden der Sperre, solange ein Wechsel des Soll-Zustands anstünde, ihn die Hysterese
        /// aber noch hält; sonst -1.
        int32_t pendingSeconds = -1;
        /// Sekunden seit der letzten Abfrage der Shellys, -1 wenn noch keine lief.
        int32_t lastSyncAgoSeconds = -1;
    };

    /// @brief Startet den Regelungs-Task. Einmal in setup() aufrufen.
    void begin();

    /// @brief Fordert eine sofortige Abfrage und Angleichung der Shellys an.
    void requestSync();

    /// @brief Aktueller Stand, als Kopie.
    Status status();

    /// @brief Schlüssel für Schnittstelle und Oberfläche ("on", "off", ...).
    const char* key(Desired value);
    const char* key(Actual value);
}
