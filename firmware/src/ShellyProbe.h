// ===================================================================
// File:            ShellyProbe.h
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

#ifndef SHELLY_PROBE_CONNECT_TIMEOUT_MS
/// @brief Zeitlimit für den Verbindungsaufbau zu einem Shelly, in Millisekunden.
#define SHELLY_PROBE_CONNECT_TIMEOUT_MS 1500
#endif

#ifndef SHELLY_PROBE_READ_TIMEOUT_MS
/// @brief Zeitlimit für die Antwort, in Millisekunden.
#define SHELLY_PROBE_READ_TIMEOUT_MS 2500
#endif

#ifndef SHELLY_PROBE_MAX_BODY
/// @brief Obergrenze für den Antwortrumpf in Bytes.
/// Die Größe nicht zu knapp wählen! Vor allem bei einem Shelly 2.5 kann die Geräteinformation schon 10kB groß sein.
/// Ein zu kleiner Puffer hat ein defektes JSON zur folge.
#define SHELLY_PROBE_MAX_BODY 32768
#endif

/// @brief Fragt ein Shelly-Gerät nach seiner Identität.
namespace ShellyProbe {

    /// @brief Stand der laufenden Abfrage.
    enum class State {
        Idle,       // Noch nichts angefordert
        Pending,    // Vorgemerkt, wartet auf den nächsten loop()
        Running,    // Läuft gerade
        Done,       // Fertig, result() gilt
        Failed      // Fehlgeschlagen, result().error sagt warum
    };

    /// @brief Ergebnis der Passwortprüfung.
    enum class PasswordState {
        Unchecked,   // Nicht prüfbar (Verbindungsfehler, unlesbare Antwort)
        NotNeeded,   // Das Gerät verlangt kein Passwort
        Missing,     // Das Gerät verlangt eines, es wurde keines angegeben
        Wrong,       // Das Gerät hat das angegebene Passwort abgelehnt
        Valid        // Das Gerät hat das Passwort akzeptiert
    };

    /// @brief Was über das Gerät in Erfahrung gebracht wurde.
    struct Result {
        String address;       // Abgefragte Adresse
        String error;         // Schlüssel der Begründung bei Misserfolg: HTTP_ERROR, NO_RESPONSE, NOT_A_SHELLY, OUT_OF_MEMORY
        int httpStatus = 0;   // HTTP-Status bei error == HTTP_ERROR
        String mac;           // Kennung des Geräts — bleibt, auch wenn die Adresse wechselt
        uint8_t gen;          // 1 bis 4, 0 wenn unbekannt
        String model;         // Gen 1: "SHSW-25", Gen 2+: "SNSW-102P16EU"
        String app;           // Anwendungsname, nur Gen 2+: "Plus2PM"
        String firmware;      // Firmwarestand, zur Anzeige
        bool authRequired;    // Ob das Gerät ein Passwort verlangt
        PasswordState passwordState;   ///< Ergebnis der Passwortprüfung
        /// @brief Betriebsart bei Mehrprofilgeräten: "switch", "cover", "light", …
        String profile;
        /// @brief Einsatzart: "switch1", "switch2", "cover" oder "dimmer".
        String kind;
    };

    /// @brief Holt ein JSON-Dokument von einem Shelly mit Anmeldung
    /// Blockiert für die Dauer der Anfrage, nicht aus loop() oder dem Webserver aufrufen.
    ///
    /// @param path      Pfad samt Abfrageparametern, mit führendem Schrägstrich
    /// @param gen       Generation des Geräts (1 bis 4)
    /// @param httpCode  Erhält den HTTP-Status, -1 wenn keine Verbindung zustande kam
    /// @return true wenn der Status 200 war und gültiges JSON kam
    bool getJson(const String& host, const String& path, const String& password, uint8_t gen, JsonDocument& doc, int& httpCode);

    /// @brief Startet eine Abfrage.
    /// @param address Adresse oder Hostname des Geräts
    /// @param password Gerätepasswort, leer wenn keines bekannt ist
    void request(const String& address, const String& password);

    /// @brief Stand der Abfrage.
    State state();

    /// @brief Das Ergebnis der letzten abgeschlossenen Abfrage.
    Result result();
}
