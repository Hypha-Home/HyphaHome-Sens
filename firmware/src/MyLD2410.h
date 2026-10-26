// ===================================================================
// File:            MyLD2410.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <ld2410.h>
#include "config.h"
#include "pinmapping.h"
#include "SensorHealth.h"
#include "config.h"


#ifndef LD2410_GATE_COUNT
    /// @brief Anzahl der Gates des LD2410
    #define LD2410_GATE_COUNT 9
#endif

#ifndef LD2410_SENSITIVITY_UNSET
    /// @brief Empfindlichkeitswert, der "nicht eingestellt" bedeutet.
    ///
    /// Gültige Empfindlichkeiten reichen von 0 bis 100. Die 0 taugt dabei nicht als Markierung für
    /// 255 liegt außerhalb des gültigen Bereichs und ist deshalb eindeutig.
    #define LD2410_SENSITIVITY_UNSET 255
#endif

#ifndef LD2410_MOVING_HOLD_MAX_S
    /// @brief Obergrenze der Haltezeiten des LD2410 (Bewegung, Stillstand, Präsenz), in Sekunden.
    #define LD2410_MOVING_HOLD_MAX_S 600
#endif

namespace MyLD2410 {
    extern ld2410 radar;
    extern bool isConnected;
    extern bool presence;
    extern bool movingTarget;
    extern bool stationaryTarget;
    extern uint16_t movingDistance;
    extern uint8_t movingEnergy;
    extern uint16_t stationaryDistance;
    extern uint8_t stationaryEnergy;

    /// @brief Initialisiert den Sensor, wenn er in der Konfiguration aktiviert ist.
    void init();

    /// @brief Liest den Sensor aus.
    /// @param print true gibt den Messwert auf Serial aus
    void read(bool print = true);

    /// @brief Prüft, ob der Radarsensor antwortet.
    /// Der LD2410 ist via UART, nicht I2C angeschlossen. Ein Probe prüft die Firmwareversion
    /// des LD2410. Das ist im gegensatz zu einem I2C-ACK Check nicht billig und dauert bis zu 1 sek.
    /// @return true wenn der Sensor antwortet
    bool probe();

    /// @brief Überträgt die Gate-Empfindlichkeiten aus der Konfiguration an den Sensor.
    /// @return true wenn nichts zu tun war oder alle nötigen Schreibvorgänge geklappt haben
    bool applyGateConfig();

    /// @brief Aktuelle Konfiguration vom Sensor abfragen (füllt radar.motion_sensitivity/stationary_sensitivity)
    /// und zur Kontrolle auf dem Serial-Monitor ausgeben.
    void printConfiguration();

    // --------------------------------------------------------------
    // Entwicklermodus
    // --------------------------------------------------------------
    // Der "Engineering mode" des LD2410 liefert zusaetzlich zu den Zielen die Signalenergie
    // jeder Zone/Gate. Er belastet die serielle Leitung staerker und ist nach dem Abschalten des
    // Sensors wieder aus, deshalb laeuft er nur auf Anforderung: Wer ihn braucht, ruft
    // debugKeepAlive() auf; bleibt der Aufruf laenger als LD2410_DEBUG_LEASE_MS aus, schaltet
    // read() ihn wieder ab.

    /// Wie lange der Entwicklermodus nach der letzten Anforderung weiterlaeuft.
    static const uint32_t LD2410_DEBUG_LEASE_MS = 8000;

    /// Messwerte einer Zone im Entwicklermodus.
    struct DebugGate {
        uint8_t moving = 0;              // Energie Bewegung, 0..100
        uint8_t stationary = 0;          // Energie Stillstand, 0..100
        uint8_t movingThreshold = 0;     // Schwelle im Sensor (Empfindlichkeit)
        uint8_t stationaryThreshold = 0;
        uint8_t movingPeak = 0;          // Hoechstwert seit Beginn bzw. debugResetPeaks()
        uint8_t stationaryPeak = 0;
        uint32_t movingTriggeredAt = 0;      // millis() der letzten Ueberschreitung, 0 = nie
        uint32_t stationaryTriggeredAt = 0;
    };

    struct DebugSnapshot {
        bool active = false;             // Entwicklermodus laeuft und liefert Daten
        uint8_t gateCount = 0;           // Zahl der gueltigen Eintraege in gates[]
        uint16_t detectionDistance = 0;  // cm
        DebugGate gates[LD2410_GATE_COUNT];
    };

    /// @brief Fordert den Entwicklermodus an bzw. verlaengert die Anforderung.
    /// Das Einschalten selbst uebernimmt read().
    void debugKeepAlive();

    /// @brief Beendet den Entwicklermodus sofort (beim naechsten read()).
    void debugStop();

    /// @brief Setzt die Hoechstwerte und Zeitpunkte der Zonen zurueck.
    void debugResetPeaks();

    /// @brief Kopiert den aktuellen Stand der Zonen.
    /// @return false wenn der Entwicklermodus (noch) keine Daten liefert
    bool debugSnapshot(DebugSnapshot& out);
}
