// ===================================================================
// File:            Session.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "Session.h"

namespace Session {

    static String sessionToken = "";
    static uint32_t sessionTime = 0;
    static const uint32_t TIMEOUT_MS = (uint32_t)SESSION_TIMEOUT_MINUTES * 60UL * 1000UL;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Erzeugt ein neues Zufallstoken.
    /// @return Token aus 32 Hexadezimalziffern
    static String generateToken() {
        char buffer[33];
        snprintf(buffer, sizeof(buffer), "%08x%08x%08x%08x",
                 esp_random(), esp_random(), esp_random(), esp_random());
        return String(buffer);
    }

    /// @brief Prüft ob die laufende Sitzung abgelaufen ist.
    /// @return true wenn keine Sitzung läuft oder die Frist vorbei ist
    static bool expired() {
        if (sessionToken.length() == 0) return true;
        return (millis() - sessionTime) >= TIMEOUT_MS;
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool isProtected() {
        return Config::system.password.length() > 0;
    }

    bool checkPassword(const String& password) {
        return !isProtected() || password == Config::system.password;
    }

    String login(const String& password) {
        if (isProtected() && password != Config::system.password) {
            Serial.println(F("[SESSION] Anmeldung abgelehnt: falsches Passwort"));
            return "";
        }

        sessionToken = generateToken();
        sessionTime = millis();
        Serial.printf("[SESSION] Angemeldet, gueltig %d Minuten\n", SESSION_TIMEOUT_MINUTES);
        return sessionToken;
    }

    bool isValid(const String& token) {
        // Ohne gesetztes Passwort ist das Gerät offen.
        if (!isProtected()) return true;

        // Ein leeres Token darf nie durchkommen.
        if (token.length() == 0) return false;

        if (expired()) {
            logout();
            return false;
        }

        if (token != sessionToken) return false;

        // Gültig verwendet -> Frist verlängern.
        sessionTime = millis();
        return true;
    }

    void logout() {
        if (sessionToken.length() == 0) return;
        sessionToken = "";
        sessionTime = 0;
        Serial.println(F("[SESSION] Abgemeldet"));
    }

    void handleTimeout() {
        if (sessionToken.length() == 0) return;
        if (!expired()) return;
        Serial.println(F("[SESSION] Sitzung abgelaufen"));
        logout();
    }

    uint32_t remainingSeconds() {
        if (expired()) return 0;
        return (TIMEOUT_MS - (millis() - sessionTime)) / 1000;
    }
}
