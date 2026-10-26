// ===================================================================
// File:            webfiles.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "webfiles.h"

namespace {

    struct Page {
        const char* route;
        const char* file;
    };

    // Die Seiten
    const Page PAGES[] = {
        { "/",                      "/index.html"               },
        { "/login",                 "/login.html"               },
        { "/pwreset",               "/pwreset.html"             },
        { "/connections",           "/connections.html"         },
        { "/sensors",               "/sensors.html"             },
        { "/sensors/bh1750",        "/sensor-bh1750.html"       },
        { "/sensors/sht41",         "/sensor-sht41.html"        },
        { "/sensors/scd40",         "/sensor-scd40.html"        },
        { "/sensors/aht21",         "/sensor-aht21.html"        },
        { "/sensors/ens160",        "/sensor-ens160.html"       },
        { "/sensors/ld2410",        "/sensor-ld2410.html"       },
        { "/sensors/ld2410/debug",  "/sensor-ld2410-debug.html" },
        { "/settings",              "/settings.html"            },
        { "/control",               "/control.html"             },
    };

    /// @brief Liefert eine Datei aus dem LittleFS aus.
    /// Erkennt eine danebenliegende `.gz`-Fassung und schickt die stattdessen mit passender
    /// Kopfzeile — der Browser packt selbst aus. Der Bauschritt (scripts/build_frontend.py)
    /// packt alle Seiten vorab.
    ///
    /// @param path Pfad im Dateisystem
    /// @return false wenn die Datei nicht da ist
    bool sendFileFromFs(const char* path) {
        WebServer& ws = wlanManager.getWebserver();

        String gzipped = String(path) + ".gz";
        bool useGzip = LittleFS.exists(gzipped);
        if (!useGzip && !LittleFS.exists(path)) return false;

        File file = LittleFS.open(useGzip ? gzipped.c_str() : path, "r");
        if (!file) return false;

        // Kein sendHeader("Content-Encoding", "gzip") hier! streamFile() setzt den Header
        // selbst, sobald der Dateiname auf .gz endet (_streamFileCore in WebServer.cpp).
        // Setzt man ihn zusaetzlich, steht er zweimal in der Antwort, der Browser versucht
        // zweimal auszupacken und meldet ERR_CONTENT_DECODING_FAILED.
        const size_t total = file.size();
        const uint32_t started = millis();
        const size_t sent = ws.streamFile(file, "text/html");
        const uint32_t took = millis() - started;
        file.close();

        // Bricht WiFiClient::write() ab, kommt hier weniger an als die Datei gross ist und der
        // Browser meldet eine abgebrochene Antwort. Ohne diese Zeile sieht man davon nichts.
        if (sent < total) {
            Serial.printf("[Web] %s ABGEBROCHEN: %u von %u Bytes nach %u ms, RSSI %d dBm, Heap frei %u\n",
                          path, (unsigned)sent, (unsigned)total, (unsigned)took,
                          (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
        } else if (took > 1000) {
            Serial.printf("[Web] %s langsam: %u Bytes in %u ms, RSSI %d dBm\n",
                          path, (unsigned)sent, (unsigned)took, (int)WiFi.RSSI());
        }
        return true;
    }

    /// @brief Antwort fuer unbekannte Pfade und fehlende Dateien.
    void handleNotFound() {
        if (!Webfiles::uiUsable()) { UiUpdate::sendPage(); return; }
        wlanManager.getWebserver().send(404, "text/plain", "Not found");
    }

    /// @brief Notfallseite ausgeben
    /// @param file Datei die gesendet werden soll
    void sendPage(const char* file) {
        // Passt die Web-UI nicht (oder fehlt sie), kommt statt jeder Seite die Notfallseite.
        if (!Webfiles::uiUsable()) { UiUpdate::sendPage(); return; }
        if (!sendFileFromFs(file)) handleNotFound();
    }
}

namespace Webfiles {

    static UiInfo uiInfo;

    /// Liest /version.json der Web-UI und stellt fest, ob die Firmware (= API) dazu passt.
    static void checkUiVersion() {
        uiInfo = UiInfo();

        File file = LittleFS.open("/version.json", "r");
        JsonDocument doc;
        const DeserializationError error = file ? deserializeJson(doc, file) : DeserializationError::InvalidInput;
        if (file) file.close();

        const char* version = doc["ui_version"] | "";
        const char* minApi  = doc["min_api_version"] | "";
        const char* recApi  = doc["recommended_api_version"] | "";
        if (error || version[0] == '\0' || minApi[0] == '\0') {
            Serial.println(F("[Web] Web-UI: /version.json fehlt oder ist unvollstaendig"));
            return;
        }
        if (recApi[0] == '\0') recApi = minApi;

        strlcpy(uiInfo.version,        version,                       sizeof(uiInfo.version));
        strlcpy(uiInfo.name,           doc["ui_name"] | "",           sizeof(uiInfo.name));
        strlcpy(uiInfo.vendor,         doc["vendor"] | "",            sizeof(uiInfo.vendor));
        strlcpy(uiInfo.link,           doc["link"] | "",              sizeof(uiInfo.link));
        strlcpy(uiInfo.minApi,         minApi,                        sizeof(uiInfo.minApi));
        strlcpy(uiInfo.recommendedApi, recApi,                        sizeof(uiInfo.recommendedApi));

        const Version api = Version::parse(VERSION);
        uiInfo.state = api < Version::parse(minApi)  ? UiState::TooOld
                     : api < Version::parse(recApi)  ? UiState::Limited
                     :                                  UiState::Ok;

        const char* text = uiInfo.state == UiState::Ok ? "voll kompatibel"
                         : uiInfo.state == UiState::Limited ? "eingeschraenkt" : "FIRMWARE ZU ALT";
        Serial.printf("[Web] Web-UI %s (%s), braucht API ab %s, empfohlen %s, Firmware %s: %s\n",
                      uiInfo.version, uiInfo.name, minApi, recApi, VERSION, text);
    }

    void setupRoutes() {
        // Die Web-UI liegt in der Partition "spiffs", die Konfiguration getrennt davon in
        // "config" (siehe partitions.csv). Das true formatiert eine unbenutzbare Partition -
        // hier liegt nichts, was nicht ein "uploadfs" wiederherstellt.
        if (!LittleFS.begin(true)) {
            Serial.println(F("[Web] Dateisystem der Web-UI konnte nicht eingehaengt werden"));
        }
        checkUiVersion();

        for (const Page& page : PAGES) {
            const char* file = page.file;
            wlanManager.addRoute(page.route, HTTP_GET, [file]() { sendPage(file); });
        }

        // Kein Cache-Header: Nach einem "uploadfs" soll der Browser die neue Fassung nehmen
        // und nicht die alte aus seinem Speicher.
        WebServer& ws = wlanManager.getWebserver();
        ws.serveStatic("/css/", LittleFS, "/css/");
        ws.serveStatic("/js/",  LittleFS, "/js/");
        ws.serveStatic("/images/", LittleFS, "/images/");
        ws.onNotFound(handleNotFound);
        UiUpdate::setupRoutes();
        FirmwareUpdate::setupRoutes();
    }

    const UiInfo& ui() { return uiInfo; }

    bool uiUsable() { return uiInfo.state == UiState::Ok || uiInfo.state == UiState::Limited; }

    void recheckUi() { checkUiVersion(); }

}
