// ===================================================================
// File:            PasswordReset.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "PasswordReset.h"

namespace PasswordReset {

    static volatile bool armed = false;
    static volatile uint32_t armedAt = 0;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Schreibt bzw. löscht das Freigabe-Flag im NVS.
    /// @param value true setzt das Flag, false löscht den Namespace
    static void writeFlag(bool value) {
        Preferences prefs;
        if (!prefs.begin(PWRESET_NVS_NAMESPACE, false)) return;
        if (value) prefs.putBool(PWRESET_NVS_KEY_ARMED, true);
        else       prefs.clear();
        prefs.end();
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void arm() {
        writeFlag(true);
        armedAt = millis();
        armed = true;
        Serial.println("[PWRESET] Freigegeben (5 min gueltig)");
    }

    uint32_t remainingSeconds() {
        if (!armed) return 0;
        uint32_t elapsed = millis() - armedAt;
        if (elapsed >= PWRESET_VALUD_MS) return 0;
        return (PWRESET_VALUD_MS - elapsed + 999) / 1000;
    }

    void applyIfArmed() {
        // Schlägt begin() im Read-only-Modus fehl, existiert der Namespace noch nicht.
        bool wasArmed = false;
        Preferences prefs;
        if (prefs.begin(PWRESET_NVS_NAMESPACE, true)) {
            wasArmed = prefs.getBool(PWRESET_NVS_KEY_ARMED, false);
            prefs.end();
        }
        if (!wasArmed) return;

        Serial.println("[PWRESET] War freigegeben -> Geraetepasswort wird geloescht");
        Config::system.password = "";
        Config::saveConfig();
        writeFlag(false);
    }

    void handleTimeout() {
        if (!armed) return;
        if (millis() - armedAt < PWRESET_VALUD_MS) return;

        armed = false;
        writeFlag(false);
        Serial.println("[PWRESET] Freigabe abgelaufen");
    }
}
