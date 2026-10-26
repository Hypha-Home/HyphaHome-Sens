// ===================================================================
// File:            Calibration.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>

/// @brief Höchstzahl der Kalibrierpunkte je Messgröße.
#define CALIBRATION_MAX_POINTS 8

/// @brief Kalibrierung eines Messwerts über eine Ausgleichsgerade.
///
/// Hinterlegt werden Wertepaare aus dem, was der Sensor angezeigt hat, und dem, was ein
/// Referenzgerät angezeigt hat. Daraus wird per linearer Regression die Gerade
/// bestimmt, die am besten durch alle Punkte passt, und mit ihr jeder weitere Rohwert
/// umgerechnet:
///
///     korrigiert = slope * roh + intercept
///
/// Die Gerade kann beides gleichzeitig ausgleichen: einen Verstärkungsfehler (slope weicht
/// von 1 ab) und eine Verschiebung (intercept weicht von 0 ab). Mit einem einzelnen Punkt
/// lässt sich das nicht trennen. Welche der beiden Korrekturen dann greift, entscheidet der
/// jeweilige Sensor über Fallback.
namespace Calibration {

    /// @brief Ein Messpunkt, aufgenommen gegen ein Referenzgerät.
    struct Point {
        float measured;   /// Vom Sensor angezeigter Rohwert
        float reference;  /// Tatsächlicher Wert laut Referenzgerät
    };

    /// @brief Sammlung der Messpunkte einer Messgröße.
    struct Table {
        uint8_t count;                           /// Belegte Einträge in points
        Point points[CALIBRATION_MAX_POINTS];

        /// @brief Hängt einen Punkt an.
        /// @param measured Angezeigter Rohwert
        /// @param reference Wert laut Referenzgerät
        /// @return false wenn die Tabelle voll ist; der Punkt wird dann verworfen
        bool add(float measured, float reference);

        /// @brief Verwirft alle Punkte. Danach gilt die Messgröße als nicht kalibriert.
        void clear();
    };

    /// @brief Verhalten bei genau einem Kalibrierpunkt.
    ///
    /// Durch einen einzelnen Punkt passen unendlich viele Geraden — die Regression allein kann
    /// hier nichts entscheiden. Welche Annahme sinnvoll ist, hängt an der Messgröße und wird
    /// deshalb vom Sensor vorgegeben. Ab zwei Punkten ist der Wert ohne Bedeutung.
    enum class Fallback {
        /// @brief Gerade parallel verschieben (slope bleibt 1).
        ///
        /// Für Größen mit willkürlichem Nullpunkt wie Grad Celsius, und für Fehler, die als
        /// konstanter Betrag auftreten, etwa die Eigenerwärmung.
        Offset,

        /// @brief Gerade durch den Ursprung kippen (intercept bleibt 0).
        ///
        /// Für Größen mit echtem Nullpunkt wie Lux, und für Fehler, die anteilig auftreten.
        /// etwa eine getönte Gehäusescheibe, die immer denselben Anteil des Lichts schluckt.
        /// Null bleibt dadurch null.
        Factor
    };

    /// @brief Die aus den Punkten bestimmte Ausgleichsgerade.
    struct Line {
        float slope;        /// Steigung; 1 bedeutet keine Verstärkungskorrektur
        float intercept;    /// Achsenabschnitt; 0 bedeutet keine Verschiebung
        uint8_t pointsUsed; /// Zahl der eingeflossenen Punkte, 0 = unkalibriert
    };

    /// @brief Bestimmt die Ausgleichsgerade durch die hinterlegten Punkte.
    ///
    /// Liefert immer eine brauchbare Gerade, auch in den Randfällen:
    ///   - keine Punkte: die Identität (slope 1, intercept 0), der Rohwert bleibt unverändert
    ///   - ein Punkt: je nach fallback verschoben oder gekippt
    ///   - mehrere Punkte: Methode der kleinsten Quadrate
    ///   - mehrere Punkte, aber alle mit demselben Rohwert: Eine Steigung ist daraus nicht
    ///     bestimmbar, deshalb nur die mittlere Verschiebung
    ///
    /// @param table Hinterlegte Punkte
    /// @param fallback Annahme für den Fall eines einzelnen Punktes
    /// @return Die Gerade
    Line computeLine(const Table& table, Fallback fallback);

    /// @brief Rechnet einen Rohwert über die Ausgleichsgerade um.    ///
    /// @param table Hinterlegte Punkte
    /// @param raw Rohwert des Sensors
    /// @param fallback Annahme für den Fall eines einzelnen Punktes
    /// @return Korrigierter Wert, oder unverändert der Rohwert wenn keine Punkte hinterlegt sind
    float apply(const Table& table, float raw, Fallback fallback);
}
