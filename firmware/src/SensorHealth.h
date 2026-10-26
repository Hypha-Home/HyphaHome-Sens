// ===================================================================
// File:            SensorHealth.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pinmapping.h"

/// @brief Gemeinsame Logik für die Erkennung, ob ein Sensor gerade erreichbar ist.
/// Diese Hilfsfunktionen kapseln drei Dinge, die sonst in jedem Sensormodul separat vorhanden 
/// sein müsste:
///   1. Entprellen  - ein einzelner Fehlschlag meldet einen Sensor noch nicht ab. 
///      Auf einem I2C-Bus mit mehreren Teilnehmern gibt es gelegentlich Aussetzer. 
///      Gerechnet wird in Zeit, nicht in Versuchen!
///   2. Wiederfinden - ein als fehlend markierter Sensor wird in Abständen erneut gesucht.
///   3. Bus-Freigabe - wird ein I2C-Sensor mitten in einer Übertragung abgezogen, kann er SDA
///      dauerhaft auf Low halten und damit den gesamten Bus blockieren.
namespace SensorHealth {

    /// @brief So lange darf ein Sensor am Stück ohne gültige Antwort bleiben, bevor er als fehlend gilt.
    constexpr uint32_t FAIL_TIMEOUT_MS = 10000;

    /// @brief Standardabstand zwischen zwei Suchversuchen für einen fehlenden Sensor.
    constexpr uint32_t RETRY_INTERVAL_MS = 15000;

    /// @brief Fehlerzähler und Zeitstempel eines einzelnen Sensors. Wird im jeweiligen
    /// Sensormodul als static angelegt. Das "isConnected"-Flag bleibt dagegen öffentlich im
    /// Modul, weil MQTT und später das Webinterface es auslesen.
    struct State {
        uint8_t fails = 0;         /// Fehlschläge seit dem letzten Erfolg (nur für die Log-Ausgabe)
        uint32_t failSince = 0;    /// Zeitpunkt des ersten Fehlschlags der laufenden Folge
        uint32_t lastRetry = 0;    /// Zeitpunkt des letzten Suchversuchs (nur für die Log-Ausgabe)
    };

    /// @brief Meldet einen erfolgreichen Zugriff. Setzt den Fehlerzähler zurück und markiert den
    /// Sensor als vorhanden. Ausgabe auf Serial nur beim Wechsel des Zustands, nicht bei jedem
    /// Durchlauf.
    /// @param present Flag des Sensormoduls (wird gesetzt)
    /// @param s       Zustand des Sensors
    /// @param tag     Kurzname für die Log-Ausgabe, z.B. "BH1750"
    void reportOk(bool& present, State& s, const char* tag);

    /// @brief Meldet einen fehlgeschlagenen Zugriff. Erst wenn FAIL_TIMEOUT_MS seit dem ersten
    /// Fehlschlag der Folge vergangen sind, ohne dass dazwischen ein Zugriff glückte, wird der
    /// Sensor als fehlend markiert.
    /// @param present Flag des Sensormoduls (wird ggf. zurückgesetzt)
    /// @param s       Zustand des Sensors
    /// @param tag     Kurzname für die Log-Ausgabe
    void reportFail(bool& present, State& s, const char* tag);

    /// @brief Prüft, ob ein neuer Suchversuch fällig ist, und merkt sich den Zeitpunkt.
    /// @param s        Zustand des Sensors
    /// @param interval Mindestabstand zwischen zwei Versuchen in ms
    /// @return true wenn jetzt ein Versuch unternommen werden soll
    bool retryDue(State& s, uint32_t interval = RETRY_INTERVAL_MS);

    /// @brief Prüft, ob unter einer I2C-Adresse überhaupt ein Gerät antwortet. Deutlich billiger
    /// als ein vollständiger Sensor-Start.
    /// @param address 7-Bit-I2C-Adresse
    /// @return true wenn das Gerät mit ACK antwortet
    bool i2cPing(uint8_t address);

    /// @brief Versucht einen blockierten I2C-Bus wieder freizubekommen, indem neun Taktflanken
    /// auf SCL ausgegeben werden. Das schiebt einen Slave heraus, der SDA festhält. Danach wird Wire neu gestartet.
    void i2cRecoverBus();
}
