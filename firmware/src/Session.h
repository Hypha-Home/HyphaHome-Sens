// ===================================================================
// File:            Session.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <esp_random.h>
#include "config.h"

#ifndef SESSION_TIMEOUT_MINUTES
/// @brief Gültigkeitsdauer einer Sitzung in Minuten.
#define SESSION_TIMEOUT_MINUTES 30
#endif

/// @brief Anmeldung am Webinterface über ein Sitzungstoken.
/// Es gibt nur ein Sitzungstoken! Mehrfachanmeldung ist nicht möglich.
namespace Session {

    /// @brief Prüft ob das Gerät überhaupt ein Passwort hat.
    /// @return true wenn ein Passwort gesetzt ist
    bool isProtected();

    /// @brief Eröffnet eine Sitzung.
    /// @param password Das eingegebene Passwort
    /// @return Das neue Token, oder ein leerer String wenn das Passwort nicht stimmt
    String login(const String& password);

    /// @brief Prueft das Passwort, ohne eine Sitzung anzulegen oder zu beenden.
    bool checkPassword(const String& password);

    /// @brief Prüft ein Token und verlängert bei Erfolg die Frist.
    /// @param token Das vom Aufrufer mitgeschickte Token
    /// @return true wenn die Anfrage ausgeführt werden darf
    bool isValid(const String& token);

    /// @brief Beendet die laufende Sitzung sofort.
    void logout();

    /// @brief Verwirft eine abgelaufene Sitzung.
    void handleTimeout();

    /// @brief Restlaufzeit der Sitzung, für die Anzeige in der Oberfläche.
    /// @return Verbleibende Sekunden, 0 wenn keine Sitzung läuft
    uint32_t remainingSeconds();
}
