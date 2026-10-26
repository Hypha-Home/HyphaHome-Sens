// ===================================================================
// File:            Calibration.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "Calibration.h"

namespace Calibration {

    // --------------------------------------------------------------
    // struct Table functions
    // --------------------------------------------------------------

    /// @brief Fügt einen Messpunkt hinzu.
    /// @param measured Angezeigter Rohwert
    /// @param reference Wert laut Referenzgerät
    /// @return false wenn die Tabelle voll ist; der Punkt wird dann verworfen
    bool Table::add(float measured, float reference) {
        if (count >= CALIBRATION_MAX_POINTS) return false;
        points[count].measured = measured;
        points[count].reference = reference;
        count++;
        return true;
    }

    /// @brief Verwirft alle Punkte. Danach gilt die Messgröße als nicht kalibriert.
    void Table::clear() {
        count = 0;
        for (uint8_t i = 0; i < CALIBRATION_MAX_POINTS; i++) {
            points[i].measured = 0.0f;
            points[i].reference = 0.0f;
        }
    }
    
    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Die Gerade, die nichts verändert.
    static Line identity() {
        return { 1.0f, 0.0f, 0 };
    }

    /// @brief Gerade aus einem einzelnen Punkt, je nach Annahme des Sensors.
    static Line fromSinglePoint(const Point& p, Fallback fallback) {
        if (fallback == Fallback::Factor) {
            // Ein Punkt im Nullpunkt oder darunter gibt keinen Faktor her
            // Durch null lässt sich nicht teilen, und ein negativer Faktor würde das Vorzeichen umdrehen.
            if (p.measured <= 0.0f || p.reference <= 0.0f) return identity();
            return { p.reference / p.measured, 0.0f, 1 };
        }
        return { 1.0f, p.reference - p.measured, 1 };
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    Line computeLine(const Table& table, Fallback fallback) {
        if (table.count == 0) return identity();
        if (table.count == 1) return fromSinglePoint(table.points[0], fallback);

        // Methode der kleinsten Quadrate, um die Mittelwerte zentriert gerechnet. Die
        // Lehrbuchform mit den rohen Summen (Sxy - n*x*y) löscht bei Messwerten, die alle
        // dicht beieinanderliegen, die signifikanten Stellen gegeneinander aus — genau der
        // Fall, der hier auftritt, wenn jemand im selben Raum mehrfach nachkalibriert.
        // In double gerechnet, weil die Summen sonst schnell an Genauigkeit verlieren.
        double sumX = 0.0, sumY = 0.0;
        for (uint8_t i = 0; i < table.count; i++) {
            sumX += table.points[i].measured;
            sumY += table.points[i].reference;
        }
        const double meanX = sumX / table.count;
        const double meanY = sumY / table.count;

        double covariance = 0.0;  // Summe der (x - x) * (y - y)
        double variance   = 0.0;  // Summe der (x - x)^2
        for (uint8_t i = 0; i < table.count; i++) {
            const double dx = table.points[i].measured - meanX;
            const double dy = table.points[i].reference - meanY;
            covariance += dx * dy;
            variance   += dx * dx;
        }

        // Alle Punkte beim selben Rohwert aufgenommen: Sie liegen senkrecht übereinander, eine
        // Steigung ist daraus nicht ablesbar. Was sich sagen lässt, ist die mittlere
        // Abweichung — also eine reine Verschiebung.
        if (variance <= 0.0) {
            return { 1.0f, (float)(meanY - meanX), table.count };
        }

        const double slope = covariance / variance;
        return { (float)slope, (float)(meanY - slope * meanX), table.count };
    }

    float apply(const Table& table, float raw, Fallback fallback) {
        const Line line = computeLine(table, fallback);
        return raw * line.slope + line.intercept;
    }
}
