// ===================================================================
// File:            runtime.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>



#ifndef RUNTIME_WRITE_INTERVAL_MS
/// @brief Abstand zwischen zwei Schreibvorgängen in das NVS.
/// Achtung: Das schreiben in den Flash (NVS) benötigt Schreibzyklen. Der ESP32 hat ca. 100k davon.
/// NVS wechselt die zu schreibenden Zellen, daher kann man hier durchaus alle 60sek. schreiben. Niedriger würde ich aber nicht gehen
#define RUNTIME_WRITE_INTERVAL_MS 60000UL
#endif

#ifndef RUNTIME_NVS_NAMESPACE
///@brief Name des NVS Namespace
#define RUNTIME_NVS_NAMESPACE "runtime"
#endif

#ifndef RUNTIME_NVS_KEY_SECONDS
///@brief Name des Key im NVS-Namespace
#define RUNTIME_NVS_KEY_SECONDS "seconds"
#endif


/// @brief Zählt die gesamte Betriebszeit des Geräts über alle Einschaltvorgänge hinweg.
namespace Runtime {

    /// @brief Liest den gespeicherten Zählerstand.
    void begin();

    /// @brief Rechnet die vergangene Zeit auf den Zähler und schreibt ihn im Abstand von
    /// WRITE_INTERVAL_MS in das NVS.
    void handle();

    /// @brief Schreibt den aktuellen Stand sofort, unabhängig vom Intervall.
    void flush();

    /// @brief Gesamte Betriebszeit über alle Einschaltvorgänge.
    /// @return Sekunden
    uint32_t totalSeconds();

    /// @brief Betriebszeit seit dem letzten Einschalten.
    /// @return Sekunden
    uint32_t sessionSeconds();

    /// @brief Prüft ob das Gerät jemanals gelaufen ist (timer = 0). 
    /// Muss nach begin() aufgrufen werden!
    /// @return true bei der Erstinbetriebnahme
    bool isFirstBoot();

    /// @brief Setzt den Zähler auf 0 zurück. Das Gerät gilt beim nächsten Start wieder als
    /// fabrikneu und sucht erneut nach Sensoren.
    void reset();
}