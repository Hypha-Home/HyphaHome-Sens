// ===================================================================
// File:            UiUpdate.cpp
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "UiUpdate.h"

namespace {

    const char UI_PARTITION[] = "spiffs";
    const size_t BLOCK = 4096;
    const size_t MULTIPART_OVERHEAD = 4096;   // Grenzen und Kopfzeilen der Formularfelder
    const char LITTLEFS_NAME[] = "littlefs";  // steht ab Byte 8 im ersten Block jedes Abbilds

    enum class Result : uint8_t {
        Pending,        // Upload laeuft oder wurde noch nicht begonnen
        Ok,
        WrongPassword,
        NoPartition,
        BadSize,
        BadImage,
        WriteError,
        Aborted
    };

    struct Job {
        Result result = Result::Pending;
        bool unmounted = false;           // Dateisystem ausgehaengt, Remount noetig
        const esp_partition_t* part = nullptr;
        uint8_t* block = nullptr;
        size_t fill = 0;                  // Bytes im aktuellen Block
        size_t written = 0;               // Bytes, die schon im Flash stehen
    };
    Job job;

    const char STYLE[] PROGMEM =
        "<style>body{font:16px system-ui,sans-serif;max-width:28em;margin:2em auto;padding:0 1em}"
        "input,button{font:inherit;width:100%;box-sizing:border-box;margin:.3em 0;padding:.4em}"
        "hr{margin:1.5em 0}</style>";

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// @brief Ersetzt die Zeichen < > & und " durch HTML-Entitäten.
    /// @param text Nullterminierter Text, unverändert
    /// @return Der Text, sicher für die Verwendung im HTML
    String escape(const char* text) {
        String out;
        for (; *text; text++) {
            switch (*text) {
                case '<': out += "&lt;";   break;
                case '>': out += "&gt;";   break;
                case '&': out += "&amp;";  break;
                case '"': out += "&quot;"; break;
                default:  out += *text;
            }
        }
        return out;
    }

    /// @brief Verpackt einen Rumpf in die Notfallseite und schickt sie.
    /// @param status HTTP-Status der Antwort
    /// @param body   HTML zwischen <body> und </body>, schon maskiert (siehe escape())
    void sendHtml(int status, const String& body) {
        WebServer& ws = wlanManager.getWebserver();
        String html = F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
                        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                        "<title>Install web UI</title>");
        html += FPSTR(STYLE);
        html += F("</head><body>");
        html += body;
        html += F("</body></html>");
        ws.sendHeader("Cache-Control", "no-store");
        ws.send(status, "text/html", html);
    }

    /// @brief Erklärt in einem Satz, in welchem Zustand die Web-UI ist.
    /// @return Ein Satz als HTML, die eingesetzten Versionen sind maskiert
    String statusLine() {
        const Webfiles::UiInfo& ui = Webfiles::ui();
        switch (ui.state) {
            case Webfiles::UiState::Ok:
                return "The web UI " + escape(ui.version) + " matches the firmware.";
            case Webfiles::UiState::Limited:
                return "The web UI " + escape(ui.version) + " runs with this firmware, but with limitations. "
                       "Recommended firmware is " + escape(ui.recommendedApi) + ".";
            case Webfiles::UiState::TooOld:
                return "The web UI " + escape(ui.version) + " needs firmware " + escape(ui.minApi) +
                       " or newer, installed is " + VERSION + ". Upload an older web UI or update the firmware.";
            default:
                return "No web UI is installed, or its version.json cannot be read.";
        }
    }

    /// @brief Prüft, ob die laufende Anfrage von der Web-UI kommt statt von der Notfallseite.
    /// @return true wenn der Pfad unter /api/ liegt
    bool isApiRequest() {
        return wlanManager.getWebserver().uri().startsWith("/api/");
    }

    /// @brief Gibt den Pufferspeicher des laufenden Uploads frei und setzt den Zeiger zurück.
    void freeJob() {
        free(job.block);
        job.block = nullptr;
    }

    /// @brief Schreibt den vollen Puffer (BLOCK Bytes) an die nächste Stelle der Partition.
    void flushBlock() {
        if (job.written == 0 && memcmp(job.block + 8, LITTLEFS_NAME, sizeof(LITTLEFS_NAME) - 1) != 0) {
            // Vor dem ersten Loeschen: Das ist kein LittleFS-Abbild, die alte UI bleibt stehen.
            job.result = Result::BadImage;
            return;
        }
        if (job.written + BLOCK > job.part->size) {
            job.result = Result::BadSize;
            return;
        }
        if (esp_partition_erase_range(job.part, job.written, BLOCK) != ESP_OK ||
            esp_partition_write(job.part, job.written, job.block, BLOCK) != ESP_OK) {
            job.result = Result::WriteError;
            return;
        }
        job.written += BLOCK;
        job.fill = 0;
    }

    /// @brief Verarbeitet den Upload der Web-UI in Stücken, während er eintrifft.
    ///
    /// Wird vom Webserver für jeden Abschnitt des hochgeladenen Formulars aufgerufen:
    ///   - START: Anmeldung prüfen (Passwort oder Sitzungstoken), Partition suchen, Dateigröße
    ///     gegen die Partition prüfen und den Puffer anlegen. Erst danach wird das UI-Dateisystem
    ///     ausgehängt, damit eine abgelehnte Datei die laufende Web-UI nicht antastet.
    ///   - WRITE: Daten in Blöcken zu je BLOCK Bytes sammeln und mit flushBlock() schreiben.
    ///   - END: Prüfen, dass die Partition genau gefüllt wurde.
    ///   - ABORTED: Abbruch vermerken.
    /// Das Ergebnis steht in job.result; ausgewertet wird es in onUploadDone().
    void onUpload() {
        WebServer& ws = wlanManager.getWebserver();
        HTTPUpload& up = ws.upload();

        if (up.status == UPLOAD_FILE_START) {
            freeJob();
            job = Job();

            // Notfallseite: Passwortfeld (steht im Formular vor der Datei und ist deshalb schon
            // gelesen). Einstellungen: Sitzungstoken wie bei jeder anderen Schnittstelle.
            const bool allowed = isApiRequest() ? Session::isValid(ws.header("X-Auth-Token"))
                                                : Session::checkPassword(ws.arg("pw"));
            if (!allowed) {
                job.result = Result::WrongPassword;
                return;
            }
            job.part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, UI_PARTITION);
            if (!job.part) {
                job.result = Result::NoPartition;
                return;
            }
            // Ein Abbild fuellt die Partition genau; sonst gar nicht erst anfangen zu loeschen.
            const long length = ws.clientContentLength();
            if (length < (long)job.part->size || length > (long)(job.part->size + MULTIPART_OVERHEAD)) {
                job.result = Result::BadSize;
                return;
            }
            job.block = (uint8_t*)malloc(BLOCK);
            if (!job.block) {
                job.result = Result::WriteError;
                return;
            }
            LittleFS.end();
            job.unmounted = true;
            Serial.println(F("[UiUpdate] Upload beginnt, Web-UI wird ersetzt"));

        } else if (up.status == UPLOAD_FILE_WRITE) {
            size_t offset = 0;
            while (job.result == Result::Pending && job.block && offset < up.currentSize) {
                size_t part = BLOCK - job.fill;
                if (part > up.currentSize - offset) part = up.currentSize - offset;
                memcpy(job.block + job.fill, up.buf + offset, part);
                job.fill += part;
                offset += part;
                if (job.fill == BLOCK) flushBlock();
            }

        } else if (up.status == UPLOAD_FILE_END) {
            if (job.result == Result::Pending) {
                job.result = (job.fill == 0 && job.written == job.part->size) ? Result::Ok : Result::BadSize;
            }

        } else if (up.status == UPLOAD_FILE_ABORTED) {
            if (job.result == Result::Pending) job.result = Result::Aborted;
        }
    }

    /// @brief Schließt den Upload ab und schickt die Antwort.
    void onUploadDone() {
        freeJob();

        if (job.unmounted) {
            // Kein Neuformatieren (begin(false)): Ein misslungener Upload soll nicht noch
            // eine leere UI anlegen, sondern die Notfallseite stehen lassen.
            const bool mounted = LittleFS.begin(false);
            job.unmounted = false;
            Webfiles::recheckUi();
            if (!mounted && job.result == Result::Ok) job.result = Result::BadImage;
        }

        const Webfiles::UiInfo& ui = Webfiles::ui();
        const Result result = job.result;
        job = Job();

        // text: Klartext fuer die Notfallseite (HTML, steckt in der Firmware). code: Schluessel
        // fuer die JSON-Antwort, den die Web-UI selbst uebersetzt.
        int status = 200;
        const char* text = "";
        const char* code = "";
        switch (result) {
            case Result::Ok:            text = "Web UI installed"; break;
            case Result::WrongPassword: status = isApiRequest() ? 401 : 403; text = isApiRequest() ? "Not logged in" : "Wrong password"; code = "NOT_AUTHENTICATED"; break;
            case Result::BadSize:       status = 400; text = "The file does not have the size of the web UI partition. Upload littlefs.bin"; code = "UI_BAD_SIZE"; break;
            case Result::BadImage:      status = 400; text = "This is not a web UI image (littlefs.bin)"; code = "UI_BAD_IMAGE"; break;
            case Result::NoPartition:   status = 500; text = "Web UI partition not found"; code = "NO_PARTITION"; break;
            case Result::WriteError:    status = 500; text = "Writing failed. Please upload again"; code = "WRITE_FAILED"; break;
            case Result::Aborted:       status = 500; text = "Upload aborted. Please upload again"; code = "ABORTED"; break;
            default:                    status = 400; text = "No file received"; code = "NO_FILE"; break;
        }

        if (isApiRequest()) {
            JsonDocument doc;
            doc["ok"] = (result == Result::Ok);
            if (result == Result::Ok) {
                doc["version"] = ui.version;
                doc["compatible"] = Webfiles::uiUsable();
            } else {
                doc["error"] = code;
            }
            String out;
            serializeJson(doc, out);
            wlanManager.getWebserver().sendHeader("Cache-Control", "no-store");
            wlanManager.getWebserver().send(status, "application/json", out);
            return;
        }

        String body = "<p>" + escape(text) + "</p>";
        if (result == Result::Ok) {
            if (ui.version[0] != '\0') body += "<p>Version " + escape(ui.version) + "</p>";
            if (ui.state != Webfiles::UiState::Ok) body += "<p>" + statusLine() + "</p>";
            body += "<p><a href=\"/\">Continue</a></p>";
        } else {
            body += "<p><a href=\"/ui-update\">Back</a></p>";
        }
        sendHtml(status, body);
    }
}

namespace UiUpdate {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void sendPage() {
        String body = "<h1>Install web UI</h1><p>" + statusLine() + "</p>";
        body += F("<form method=\"post\" action=\"/ui-update\" enctype=\"multipart/form-data\">"
                  "<input type=\"password\" name=\"pw\" placeholder=\"Password\" autocomplete=\"current-password\">"
                  "<input type=\"file\" name=\"ui\" accept=\".bin\" required>"
                  "<button type=\"submit\">Upload</button></form>"
                  "<hr>"
                  "<button type=\"button\" onclick=\"if(confirm('Really reset the password?'))"
                  "fetch('/api/pwreset',{method:'POST'}).then(function(){alert('Allowed for 5 minutes. "
                  "Restart the device now, afterwards the password is empty.')})\">Forgot password</button>");
        sendHtml(200, body);
    }

    void setupRoutes() {
        WebServer& ws = wlanManager.getWebserver();
        ws.on("/ui-update", HTTP_GET, sendPage);
        ws.on("/ui-update", HTTP_POST, onUploadDone, onUpload);
        ws.on("/api/update/ui", HTTP_POST, onUploadDone, onUpload);
    }
}
