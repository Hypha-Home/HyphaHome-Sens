// ===================================================================
// File:            ShellyProbe.cpp
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "ShellyProbe.h"

// Bewusst ohne HTTPClient: Dessen Header bindet WiFiClientSecure bedingungslos ein und zieht
// damit den gesamten TLS-Stack in die Firmware — gemessen 162 KB, fuer Klartext-HTTP an ein
// Geraet im eigenen Netz. Ein GET von Hand sind ein paar Zeilen.

namespace ShellyProbe {

    static volatile State currentState = State::Idle;
    static Result lastResult;

    // Schützt lastResult: geschrieben vom Abfrage-Task, gelesen vom Webserver-Task.
    static SemaphoreHandle_t resultMutex = nullptr;

    // Auftragsdaten, vom Aufrufer gesetzt und vom Abfrage-Task gelesen.
    static String pendingAddress;
    static String pendingPassword;

    /// @brief Antwort auf einen GET, soweit sie hier gebraucht wird.
    struct Reply {
        int code = -1;       // HTTP-Status, -1 wenn gar keine Verbindung zustande kam
        String body;         // Rumpf, nur bei Status 200 gelesen
        String challenge;    // Wert von WWW-Authenticate, steht bei 401 drin
    };

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Setzt das Ergebnis auf "nichts bekannt" zurück.
    static void resetResult(const String& address) {
        lastResult = Result{};
        lastResult.address = address;
        lastResult.gen = 0;
        lastResult.authRequired = false;
        lastResult.passwordState = PasswordState::Unchecked;
    }

    /// @brief Liest den Body einer Antwort, auch in Teilstuecken ("chunked").
    /// @param client Offene Verbindung, Kopfzeilen bereits gelesen
    /// @param chunked Ob die Kopfzeilen Transfer-Encoding: chunked angekuendigt haben
    /// @return Der Body
    static String readBody(WiFiClient& client, bool chunked) {
        String body;

        if (!chunked) {
            while (client.connected() || client.available()) {
                if (!client.available()) { delay(1); continue; }
                body += (char)client.read();
                if (body.length() > SHELLY_PROBE_MAX_BODY) break;
            }
            return body;
        }

        while (true) {
            const String sizeLine = client.readStringUntil('\n');
            const long size = strtol(sizeLine.c_str(), nullptr, 16);   // Laenge ist hexadezimal
            if (size <= 0) break;                                      // 0 beendet die Folge

            for (long i = 0; i < size; i++) {
                while (!client.available() && client.connected()) delay(1);
                if (!client.available()) return body;
                body += (char)client.read();
            }
            client.readStringUntil('\n');   // Zeilenumbruch hinter dem Teilstueck

            if (body.length() > SHELLY_PROBE_MAX_BODY) break;
        }
        return body;
    }

    /// @brief Führt einen GET gegen ein Geraet im eigenen Netz aus.
    /// @param host Adresse oder Hostname
    /// @param path Pfad samt fuehrendem Schraegstrich
    /// @param authorization Fertiger Wert der Authorization-Kopfzeile, leer fuer ohne
    static Reply httpGet(const String& host, const String& path, const String& authorization) {
        Reply reply;

        // Verbindung aufbauen
        WiFiClient client;
        if (!client.connect(host.c_str(), 80, SHELLY_PROBE_CONNECT_TIMEOUT_MS)) {
            Serial.printf("[SHELLY] %s%s: Verbindung fehlgeschlagen (WLAN %s, freier Heap %u)\n",
                          host.c_str(), path.c_str(),
                          WiFi.status() == WL_CONNECTED ? "verbunden" : "nicht verbunden",
                          (unsigned)ESP.getFreeHeap());
            return reply;
        }
        client.setTimeout(SHELLY_PROBE_READ_TIMEOUT_MS / 1000);

        // Header bauen
        String request = "GET " + path + " HTTP/1.0\r\n"
                       + "Host: " + host + "\r\n"
                       + "Connection: close\r\n";
        if (authorization.length() > 0) {
            request += "Authorization: " + authorization + "\r\n";
        }
        request += "\r\n";
        client.print(request);

        // Statuszeile: "HTTP/1.1 200 OK"
        const String status = client.readStringUntil('\n');
        const int firstSpace = status.indexOf(' ');
        if (firstSpace < 0) {
            Serial.printf("[SHELLY] %s%s: verbunden, aber keine Antwort (Statuszeile leer)\n",
                          host.c_str(), path.c_str());
            client.stop();
            return reply;
        }
        reply.code = status.substring(firstSpace + 1, firstSpace + 4).toInt();

        // Kopfzeilen bis zur Leerzeile durchgehen. Gebraucht werden zwei: die Stueckelung und
        // die Aufforderung zur Anmeldung.
        bool chunked = false;
        while (client.connected() || client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;

            String lower = line;
            lower.toLowerCase();
            if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
                chunked = true;
            } else if (lower.startsWith("www-authenticate:")) {
                reply.challenge = line.substring(17);
                reply.challenge.trim();
            }
        }

        if (reply.code == 200) reply.body = readBody(client, chunked);
        client.stop();
        return reply;
    }

    /// @brief Wie httpGet(), erwartet aber ein JSON-Dokument.
    /// @param httpCode Nimmt den HTTP-Status auf, oder -1 wenn keine Verbindung zustande kam
    /// @return true wenn der Status 200 war und gueltiges JSON kam
    static bool fetchJson(const String& host, const String& path, const String& authorization, JsonDocument& doc, int& httpCode, const JsonDocument* filter = nullptr) {
        const Reply reply = httpGet(host, path, authorization);
        httpCode = reply.code;
        if (reply.code != 200) return false;

        const DeserializationError error = filter
            ? deserializeJson(doc, reply.body, DeserializationOption::Filter(*filter))
            : deserializeJson(doc, reply.body);
        if (error != DeserializationError::Ok) {
            Serial.printf("[SHELLY] %s: JSON nicht lesbar (%s, %u Bytes)\n", path.c_str(),
                          error.c_str(), (unsigned)reply.body.length());
            return false;
        }
        return true;
    }

    /// @brief SHA-256 als Kleinbuchstaben-Hex, wie es die Digest-Authentifizierung verlangt.
    static String sha256Hex(const String& text) {
        uint8_t hash[32];
        mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(text.c_str()),
                           text.length(), hash, 0);
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
        return String(hex);
    }

    /// @brief Liest einen Parameter aus einer WWW-Authenticate-Zeile, mit oder ohne Anfuehrungszeichen.
    static String challengeValue(const String& challenge, const char* key) {
        const String needle = String(key) + "=";
        int pos = 0;
        while (true) {
            pos = challenge.indexOf(needle, pos);
            if (pos < 0) return "";
            // Nur am Wortanfang gelten lassen, damit "nonce" nicht in "cnonce" gefunden wird.
            if (pos == 0 || challenge[pos - 1] == ' ' || challenge[pos - 1] == ',') break;
            pos += needle.length();
        }
        pos += needle.length();

        if (pos < (int)challenge.length() && challenge[pos] == '"') {
            const int end = challenge.indexOf('"', pos + 1);
            return (end < 0) ? "" : challenge.substring(pos + 1, end);
        }
        int end = challenge.indexOf(',', pos);
        if (end < 0) end = challenge.length();
        String value = challenge.substring(pos, end);
        value.trim();
        return value;
    }

    /// @brief Baut die Authorization-Zeile fuer Digest mit SHA-256 (Gen 2 und neuer).
    ///
    /// Benutzer ist immer "admin". Der Hersteller schreibt SHA-256 vor, MD5 kennen die Geraete
    /// nicht. Die Pruefsumme laeuft ueber mbedTLS, das ohnehin im Framework steckt; der
    /// TLS-Stack kommt dadurch nicht dazu.
    ///
    /// @return Leer, wenn die Aufforderung des Geraets nicht lesbar war
    static String digestAuthorization(const String& challenge, const String& password, const String& uri) {
        const String realm = challengeValue(challenge, "realm");
        const String nonce = challengeValue(challenge, "nonce");
        if (realm.length() == 0 || nonce.length() == 0) return "";

        const String cnonce = String((uint32_t)esp_random(), HEX);
        const String nc = "00000001";

        const String ha1 = sha256Hex("admin:" + realm + ":" + password);
        const String ha2 = sha256Hex("GET:" + uri);
        const String response = sha256Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce
                                          + ":auth:" + ha2);

        return "Digest username=\"admin\", realm=\"" + realm + "\", nonce=\"" + nonce
             + "\", uri=\"" + uri + "\", algorithm=SHA-256, qop=auth, nc=" + nc
             + ", cnonce=\"" + cnonce + "\", response=\"" + response + "\"";
    }

    /// @brief Gen 2 und neuer: Shelly.GetDeviceInfo lesen.
    static void readGen2DeviceInfo(const String& host) {
        JsonDocument doc;
        int code = 0;
        if (!fetchJson(host, "/rpc/Shelly.GetDeviceInfo", "", doc, code)) {
            // Nicht schlimm: Modell und MAC stehen schon aus /shelly fest.
            Serial.printf("[SHELLY] GetDeviceInfo fehlgeschlagen (HTTP %d)\n", code);
            return;
        }

        if (doc["model"].is<const char*>())   lastResult.model    = doc["model"].as<const char*>();
        if (doc["app"].is<const char*>())     lastResult.app      = doc["app"].as<const char*>();
        if (doc["ver"].is<const char*>())     lastResult.firmware = doc["ver"].as<const char*>();
        if (doc["mac"].is<const char*>())     lastResult.mac      = doc["mac"].as<const char*>();
        lastResult.authRequired = doc["auth_en"] | lastResult.authRequired;

        // profile steht nur bei Mehrprofilgeraeten drin. Fehlt es, kann das Geraet nur eine
        // Betriebsart.
        if (doc["profile"].is<const char*>()) {
            lastResult.profile = doc["profile"].as<const char*>();
        }
    }

    /// @brief Gen 2 und neuer: Passwort mit einem geschuetzten Aufruf pruefen.
    static PasswordState checkGen2Password(const String& host, const String& password) {
        const String path = "/rpc/Sys.GetConfig";

        const Reply first = httpGet(host, path, "");
        if (first.code == 200) return PasswordState::Valid;
        if (first.code != 401) return PasswordState::Unchecked;

        const String header = digestAuthorization(first.challenge, password, path);
        if (header.length() == 0) return PasswordState::Unchecked;

        const Reply second = httpGet(host, path, header);
        if (second.code == 200) return PasswordState::Valid;
        if (second.code == 401) return PasswordState::Wrong;
        return PasswordState::Unchecked;
    }

    /// @brief Gen 1: /settings lesen. Das prueft zugleich das Passwort.
    static PasswordState readGen1Settings(const String& host, const String& password) {
        const bool protectedDevice = lastResult.authRequired;
        if (protectedDevice && password.length() == 0) {
            Serial.println(F("[SHELLY] Gen 1: Passwort noetig, keines angegeben"));
            return PasswordState::Missing;
        }

        const String authorization = protectedDevice
            ? String("Basic ") + base64::encode("admin:" + password)
            : String();

        // Die Einstellungen sind riesig; gebraucht werden zwei Felder.
        JsonDocument filter;
        filter["mode"] = true;
        filter["device"]["type"] = true;

        JsonDocument doc;
        int code = 0;
        if (!fetchJson(host, "/settings", authorization, doc, code, &filter)) {
            Serial.printf("[SHELLY] /settings fehlgeschlagen (HTTP %d)\n", code);
            return (code == 401) ? PasswordState::Wrong : PasswordState::Unchecked;
        }

        if (doc["mode"].is<const char*>()) {
            // Gen 1 nennt es "relay" und "roller"; auf die Gen-2-Begriffe umgestellt, damit
            // die Oberflaeche nur eine Schreibweise kennen muss.
            const String mode = doc["mode"].as<const char*>();
            lastResult.profile = (mode == "roller") ? "cover" : "switch";
        }
        if (doc["device"]["type"].is<const char*>()) {
            lastResult.model = doc["device"]["type"].as<const char*>();
        }
        return protectedDevice ? PasswordState::Valid : PasswordState::NotNeeded;
    }

    /// @brief Leitet aus Modell, Anwendungsname und Betriebsart die Einsatzart ab.
    static String classify(const Result& r) {
        if (r.gen == 1) {
            String model = r.model;
            model.toUpperCase();
            if (model.startsWith("SHDM-")) return "dimmer";
            if (model == "SHSW-21" || model == "SHSW-25") {
                if (r.profile == "cover")  return "cover";
                if (r.profile == "switch") return "switch2";
                return "";
            }
            if (model.startsWith("SHSW-") || model.startsWith("SHPLG")) return "switch1";
            return "";
        }

        String app = r.app;
        app.toLowerCase();
        if (app.indexOf("dimmer") >= 0) return "dimmer";
        if (r.profile == "cover")       return "cover";
        if (app.indexOf("plug") >= 0)   return "switch1";

        for (unsigned i = 0; i < app.length(); i++) {
            const char c = app[i];
            if (c < '0' || c > '9') continue;
            if (c == '1') return "switch1";
            if (c == '2') return "switch2";
            return "";
        }
        return "";
    }

    /// @brief Führt die Abfrage aus. Hält dabei die Sperre auf lastResult, weil das Ergebnis
    /// in mehreren Schritten entsteht und der Leser keinen halbfertigen Stand sehen soll.
    static void runLocked(const String& address, const String& password) {
        resetResult(address);

        JsonDocument doc;
        int code = 0;
        if (!fetchJson(address, "/shelly", "", doc, code)) {
            // Beide Zweige ausdruecklich als String: Ein F()-Literal und das Ergebnis von
            // String + int haben verschiedene Typen, die der Ternaer-Operator nicht vereint.
            lastResult.error = (code > 0) ? String("HTTP_ERROR") : String("NO_RESPONSE");
            lastResult.httpStatus = (code > 0) ? code : 0;
            currentState = State::Failed;
            return;
        }

        if (doc["mac"].is<const char*>()) lastResult.mac = doc["mac"].as<const char*>();

        if (doc["gen"].is<int>()) {
            // --- Gen 2 und neuer ---
            lastResult.gen = doc["gen"].as<int>();
            if (doc["model"].is<const char*>()) lastResult.model = doc["model"].as<const char*>();
            if (doc["app"].is<const char*>())   lastResult.app = doc["app"].as<const char*>();
            if (doc["ver"].is<const char*>())   lastResult.firmware = doc["ver"].as<const char*>();
            lastResult.authRequired = doc["auth_en"] | false;

            readGen2DeviceInfo(address);

            if (!lastResult.authRequired)     lastResult.passwordState = PasswordState::NotNeeded;
            else if (password.length() == 0)  lastResult.passwordState = PasswordState::Missing;
            else                              lastResult.passwordState = checkGen2Password(address, password);

        } else if (doc["type"].is<const char*>()) {
            // --- Gen 1 ---
            lastResult.gen = 1;
            lastResult.model = doc["type"].as<const char*>();
            if (doc["fw"].is<const char*>()) lastResult.firmware = doc["fw"].as<const char*>();
            lastResult.authRequired = doc["auth"] | false;

            lastResult.passwordState = readGen1Settings(address, password);

        } else {
            lastResult.error = F("NOT_A_SHELLY");
            currentState = State::Failed;
            return;
        }

        lastResult.kind = classify(lastResult);

        Serial.printf("[SHELLY] %s: Gen %u, %s, Art '%s', MAC %s%s\n",
                      address.c_str(), lastResult.gen, lastResult.model.c_str(),
                      lastResult.kind.c_str(), lastResult.mac.c_str(),
                      lastResult.authRequired ? ", Passwort noetig" : "");
        currentState = State::Done;
    }

    /// @brief Der Abfrage-Task. Führt genau eine Abfrage aus und beendet sich.
    static void probeTask(void*) {
        currentState = State::Running;

        if (xSemaphoreTake(resultMutex, portMAX_DELAY) == pdTRUE) {
            runLocked(pendingAddress, pendingPassword);
            xSemaphoreGive(resultMutex);
        }

        // Das Passwort nicht länger als nötig im Speicher halten.
        pendingPassword = "";

        vTaskDelete(nullptr);
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void request(const String& address, const String& password) {
        if (currentState == State::Pending || currentState == State::Running) return;

        if (resultMutex == nullptr) resultMutex = xSemaphoreCreateMutex();

        pendingAddress = address;
        pendingPassword = password;
        currentState = State::Pending;

        // 8 KB Stack: WiFiClient, der gelesene Rumpf und das JSON-Dokument liegen darauf.
        // Priorität 1, also unterhalb des Webservers (2) — eine Abfrage darf die Oberfläche
        // nicht ausbremsen.
        if (xTaskCreate(probeTask, "shellyProbe", 8192, nullptr, 1, nullptr) != pdPASS) {
            // Auch dieser Pfad schreibt lastResult, also auch hier unter die Sperre: Der
            // Webserver-Task koennte gerade das vorige Ergebnis auslesen.
            if (xSemaphoreTake(resultMutex, portMAX_DELAY) == pdTRUE) {
                resetResult(address);
                lastResult.error = String("OUT_OF_MEMORY");
                xSemaphoreGive(resultMutex);
            }
            currentState = State::Failed;
        }
    }

    State state() {
        return currentState;
    }

    bool getJson(const String& host, const String& path, const String& password, uint8_t gen,
                 JsonDocument& doc, int& httpCode) {
        String authorization;
        if (gen == 1 && password.length() > 0) {
            authorization = String("Basic ") + base64::encode("admin:" + password);
        }
        Reply reply = httpGet(host, path, authorization);
        if (reply.code == 401 && gen >= 2 && password.length() > 0 && reply.challenge.length() > 0) {
            const String header = digestAuthorization(reply.challenge, password, path);
            if (header.length() > 0) reply = httpGet(host, path, header);
        }
        httpCode = reply.code;
        if (reply.code != 200) return false;
        return deserializeJson(doc, reply.body) == DeserializationError::Ok;
    }

    Result result() {
        if (resultMutex == nullptr) return lastResult;

        Result copy;
        if (xSemaphoreTake(resultMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            copy = lastResult;
            xSemaphoreGive(resultMutex);
        }
        return copy;
    }
}
