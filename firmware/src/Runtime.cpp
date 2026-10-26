// ===================================================================
// File:            runtime.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "Runtime.h"

namespace Runtime {

    static uint32_t seconds = 0;        // Gesamtzähler
    static uint32_t sessionStart = 0;   // Zählerstand beim Hochfahren (seconds - sessionStart = Sessiontime)
    static uint32_t lastTick = 0;       // millis() beim letzten Hochzählen
    static uint32_t lastWrite = 0;      // millis() beim letzten Schreiben
    static bool firstBoot = false;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Schreibt den Zählerstand in das NVS.
    /// @param value Zu speichernder Sekundenstand
    static void write(uint32_t value) {
        Preferences prefs;
        if (!prefs.begin(RUNTIME_NVS_NAMESPACE, false)) return;
        prefs.putUInt(RUNTIME_NVS_KEY_SECONDS, value);
        prefs.end();
    }


    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void begin() {
        uint32_t stored = 0;

        // Schlägt begin() im Read-only-Modus fehl, existiert der Namespace noch nicht.
        Preferences prefs;
        if (prefs.begin(RUNTIME_NVS_NAMESPACE, true)) {
            stored = prefs.getUInt(RUNTIME_NVS_KEY_SECONDS, 0);
            prefs.end();
        }

        seconds      = stored;
        sessionStart = stored;
        lastTick     = millis();
        lastWrite    = millis();
        firstBoot    = (stored == 0);

        if (firstBoot) {
            Serial.println(F("[RUNTIME] Kein Zaehlerstand vorhanden -> Erstinbetriebnahme"));
        } else {
            Serial.printf("[RUNTIME] Bisherige Betriebszeit: %u s (%u h)\n",
                          seconds, seconds / 3600);
        }
    }

    void handle() {
        uint32_t now = millis();
        uint32_t elapsed = (now - lastTick) / 1000;
        if (elapsed > 0) {
            seconds += elapsed;
            lastTick += elapsed * 1000; // angebrochene Sekunde nicht verwerfen
        }

        if (now - lastWrite >= RUNTIME_WRITE_INTERVAL_MS) {
            lastWrite = now;
            write(seconds);
        }
    }

    void flush() {
        handle();          // erst den aktuellen Stand verrechnen
        lastWrite = millis();
        write(seconds);
    }

    uint32_t totalSeconds() {
        return seconds;
    }

    uint32_t sessionSeconds() {
        return seconds - sessionStart;
    }

    bool isFirstBoot() {
        return firstBoot;
    }

    void reset() {
        seconds      = 0;
        sessionStart = 0;
        lastTick     = millis();
        lastWrite    = millis();
        write(0);
        Serial.println(F("[RUNTIME] Zaehler zurueckgesetzt"));
    }
}
