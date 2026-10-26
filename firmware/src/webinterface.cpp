// ===================================================================
// File:            webinterface.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "webinterface.h"

// --------------------------------------------------------------
// Helfer
// --------------------------------------------------------------
namespace {

    /// @brief Schickt ein JSON-Dokument als Antwort.
    /// @param doc Das zu sendende Dokument
    static void sendJson(const JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        WebServer& ws = wlanManager.getWebserver();
        ws.sendHeader("Cache-Control", "no-store");
        ws.send(200, "application/json", out);
    }

    /// @brief Prueft einen Hostnamen: ein DNS-Label aus Buchstaben, Ziffern und Bindestrichen,
    /// hoechstens 63 Zeichen, nicht am Rand mit Bindestrich. Leer ist erlaubt und heisst
    /// "automatisch" (main.cpp vergibt dann HyphaHome-Sens-<MAC>).
    static bool hostnameValid(const String& name) {
        const size_t len = name.length();
        if (len == 0) return true;
        if (len > 63) return false;
        if (name[0] == '-' || name[len - 1] == '-') return false;
        for (size_t i = 0; i < len; i++) {
            const char c = name[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9') || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    /// @brief Lesbarer Name eines Verbindungszustands.
    static const char* wlanStateToString(WLANManager::ConnectionState state) {
        switch (state) {
            case WLANManager::ConnectionState::Connected:        return "connected";
            case WLANManager::ConnectionState::Connecting:       return "connecting";
            case WLANManager::ConnectionState::Hotspot:          return "hotspot";
            case WLANManager::ConnectionState::Disconnected:     return "disconnected";
            case WLANManager::ConnectionState::ConnectionLost:   return "connectionLost";
            case WLANManager::ConnectionState::ConnectionFailed: return "connectionFailed";
            case WLANManager::ConnectionState::SSIDNotFound:     return "ssidNotFound";
            case WLANManager::ConnectionState::Unknown:          return "unknown";
        }
        return "unknown";
    }

    /// @brief Schluessel fuer die Bewertung des CO2-Werts (die Oberflaeche uebersetzt sie).
    static const char* levelKey(MySCD40::Level level) {
        switch (level) {
            case MySCD40::Level::Super:    return "great";
            case MySCD40::Level::Gut:      return "good";
            case MySCD40::Level::Mittel:   return "medium";
            case MySCD40::Level::Schlecht: return "bad";
            case MySCD40::Level::Hoch:     return "high";
        }
        return "unknown";
    }

    /// @brief Schluessel fuer die Bewertung der Luftqualitaet.
    static const char* levelKey(MyENS160::Level level) {
        switch (level) {
            case MyENS160::Level::Super:     return "great";
            case MyENS160::Level::Gut:       return "good";
            case MyENS160::Level::Mittel:    return "medium";
            case MyENS160::Level::Schlecht:  return "bad";
            case MyENS160::Level::Hoch:      return "high";
            case MyENS160::Level::Unbekannt: break;
        }
        return "unknown";
    }

    /// @brief Schluessel fuer den Betriebszustand des ENS160.
    static const char* validityKey(MyENS160::Validity validity) {
        switch (validity) {
            case MyENS160::Validity::Normal:  return "normal";
            case MyENS160::Validity::WarmUp:  return "warmUp";
            case MyENS160::Validity::StartUp: return "startUp";
            case MyENS160::Validity::Invalid: return "invalid";
        }
        return "unknown";
    }

    /// @brief Schluessel fuer den Grund des letzten Neustarts (die Oberflaeche uebersetzt ihn).
    static const char* resetReasonToString(esp_reset_reason_t reason) {
        switch (reason) {
            case ESP_RST_POWERON:   return "powerOn";
            case ESP_RST_EXT:       return "externalReset";
            case ESP_RST_SW:        return "software";
            case ESP_RST_PANIC:     return "panic";
            case ESP_RST_INT_WDT:   return "interruptWatchdog";
            case ESP_RST_TASK_WDT:  return "taskWatchdog";
            case ESP_RST_WDT:       return "watchdog";
            case ESP_RST_DEEPSLEEP: return "deepSleep";
            case ESP_RST_BROWNOUT:  return "brownout";
            default:                return "unknown";
        }
    }

    /// @brief Startet den ESP neu, nachdem die Antwort draussen ist.
    static void restartSoon() {
        Runtime::flush();
        delay(500);
        ESP.restart();
    }
}


// --------------------------------------------------------------
// Authentifikation und Login
// --------------------------------------------------------------
namespace {
    /// @brief Holt das Sitzungstoken aus der Anfrage.
    /// @return Das Token oder ein leerer String
    static String tokenFromRequest() {
        return wlanManager.getWebserver().header("X-Auth-Token");
    }

    /// @brief Prüft ob der Nutzer Authentifiziert ist
    /// @return true wenn Authentifiziert
    static bool isAuthenticated() {
        return Session::isValid(tokenFromRequest());
    }

    /// @brief Anmeldung über die Schnittstelle: Passwort hinein, Token heraus.
    /// Erwartet einen JSON-Rumpf `{"password": "..."}`.
    /// Antwort bei Erfolg: `{"token":"...","expiresIn":1800}`. Bei falschem Passwort 401.
    static void handleApiLogin() {
        WebServer& ws = wlanManager.getWebserver();

        String password;
        if (ws.hasArg("plain")) {
            JsonDocument doc;
            if (deserializeJson(doc, ws.arg("plain")) == DeserializationError::Ok) {
                password = doc["password"] | "";
            }
        }

        String token = Session::login(password);
        if (token.length() == 0) {
            ws.send(401, "application/json", "{\"error\":\"WRONG_PASSWORD\"}");
            return;
        }

        ws.send(200, "application/json",
                "{\"token\":\"" + token + "\",\"expiresIn\":"
                + String(SESSION_TIMEOUT_MINUTES * 60) + "}");
    }

    /// @brief Abmeldung über die Schnittstelle.
    static void handleApiLogout() {
        WebServer& ws = wlanManager.getWebserver();
        Session::logout();
        ws.send(200, "application/json", "{\"ok\":true}");
    }

    /// @brief Auskunft über die laufende Sitzung.
    static void handleApiSession() {
        WebServer& ws = wlanManager.getWebserver();

        bool valid = isAuthenticated();
        String json = "{\"authenticated\":";
        json += valid ? "true" : "false";
        json += ",\"protected\":";
        json += Session::isProtected() ? "true" : "false";
        json += ",\"expiresIn\":";
        json += String(valid ? Session::remainingSeconds() : 0);
        // Der Hostname steht links oben in der Seitenleiste jeder Seite. Er ist auch ohne
        // Anmeldung über mDNS sichtbar und besteht nur aus Buchstaben, Ziffern und Bindestrichen
        // (hostnameValid), braucht hier also kein Maskieren.
        json += ",\"hostname\":\"";
        json += Config::system.hostname;
        json += "\"}";

        ws.send(200, "application/json", json);
    }

    /// @brief Allgemeine Angaben zum Gerät. Ohne Anmeldung abrufbar, damit sich das Gerät im Netz
    /// identifizieren lässt (z. B. von einer App oder einem Skript).
    /// Antwort: `{"systemType":"Hypha Home - Sens","hostname":"...","firmwareVersion":"...",
    /// "webinterfaceVersion":"..."}`. Ist keine Web-UI installiert, bleibt die Version leer.
    static void handleApiInfo() {
        JsonDocument doc;
        doc["systemType"]          = "Hypha Home - Sens";
        doc["hostname"]            = Config::system.hostname;
        doc["firmwareVersion"]     = Config::version.toString();
        doc["webinterfaceVersion"] = Webfiles::ui().version;
        sendJson(doc);
    }

    /// @brief Bricht eine Schnittstellen-Anfrage ab, wenn niemand angemeldet ist.
    /// @return true wenn die Anfrage ausgeführt werden darf
    static bool requireAuth() {
        if (isAuthenticated()) return true;
        wlanManager.getWebserver().send(401, "application/json",
                                        "{\"error\":\"NOT_AUTHENTICATED\"}");
        return false;
    }
}


// --------------------------------------------------------------
// Passwort
// --------------------------------------------------------------
namespace {
    /// @brief Restzeit der Passwort-Reset-Freigabe.
    static void handleApiPwResetState() {
        JsonDocument doc;
        doc["remaining"] = PasswordReset::remainingSeconds();
        sendJson(doc);
    }

    /// @brief Gibt das Zuruecksetzen des Geraetepassworts fuer 5 Minuten frei.
    static void handleApiPwResetArm() {
        PasswordReset::arm();
        handleApiPwResetState();
    }
}

namespace {

    /// @brief Liefert die vollständige Konfiguration 
    static void handleApiConfig() {
        if (!requireAuth()) return;

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        Config::toJson(root, false);

        JsonObject runtime = root["runtime"].to<JsonObject>();
        runtime["totalSeconds"]         = Runtime::totalSeconds();
        runtime["sessionSeconds"]       = Runtime::sessionSeconds();
        runtime["firstBoot"]            = Runtime::isFirstBoot();

        JsonObject limits = root["limits"].to<JsonObject>();
        limits["calibrationPoints"]     = CALIBRATION_MAX_POINTS;
        limits["ld2410Gates"]           = LD2410_GATE_COUNT;
        limits["sht41SmoothingMax"]     = SHT41_SMOOTHING_MAX;
        limits["ld2410MovingHoldMax"]   = LD2410_MOVING_HOLD_MAX_S;
        limits["shellys"]               = SHELLY_MAX_COUNT;
        limits["shellyChannels"]        = SHELLY_MAX_CHANNELS;
        limits["controlOutputs"]        = CONTROL_MAX_OUTPUTS;
        limits["heatingTargetMin"]      = HEATING_TARGET_MIN_C;
        limits["heatingTargetMax"]      = HEATING_TARGET_MAX_C;

        sendJson(doc);
    }

    /// @brief Nimmt Aenderungen an der Konfiguration an.
    /// Erwartet im Rumpf ein JSON-Objekt mit dem Aufbau von `config.json`, das aber nur die
    /// zu aendernden Felder enthalten muss.
    ///
    /// Beispiel fuer die Gate-Empfindlichkeiten des LD2410:
    ///     { "sensors": { "ld2410": { "gates": [ { "moving": 50, "stationary": 0 }, ... ] } } }
    static void handleApiConfigSave() {
        if (!requireAuth()) return;

        WebServer& ws = wlanManager.getWebserver();
        if (!ws.hasArg("plain")) {
            ws.send(400, "application/json", "{\"error\":\"NO_JSON_BODY\"}");
            return;
        }

        JsonDocument patch;
        DeserializationError error = deserializeJson(patch, ws.arg("plain"));
        if (error) {
            ws.send(400, "application/json",
                    String("{\"error\":\"INVALID_JSON\",\"detail\":\"") + error.c_str() + "\"}");
            return;
        }
        if (!patch.is<JsonObject>()) {
            ws.send(400, "application/json", "{\"error\":\"JSON_OBJECT_EXPECTED\"}");
            return;
        }

        if (patch["system"]["hostname"].is<const char*>()
            && !hostnameValid(patch["system"]["hostname"].as<String>())) {
            ws.send(400, "application/json",
                    "{\"error\":\"INVALID_HOSTNAME\"}");
            return;
        }

        // Vor dem Speichern merken, ob der Radarsensor betroffen ist. Danach steht es nicht
        // mehr fest, weil applyJson() nur die Laufzeitvariablen hinterlaesst.
        bool touchesLd2410 = !patch["sensors"]["ld2410"].isNull();
        bool touchesMqtt   = !patch["mqtt"].isNull();
        bool touchesHeating = !patch["controls"]["heating"].isNull();

        if (!Config::applyJson(patch.as<JsonObjectConst>())) {
            ws.send(500, "application/json", "{\"error\":\"SAVE_FAILED\"}");
            return;
        }

        if (touchesLd2410) MyLD2410::applyGateConfig();
        if (touchesMqtt)   MyMQTT::requestReconnect();
        if (touchesHeating) {
            MyMQTT::requestHeatingSync();
            HeatingControl::requestSync();
        }

        ws.send(200, "application/json", "{\"ok\":true}");
    }

    /// @brief Liefert den Verbindungszustand von WLAN und MQTT.
    static void handleApiStatus() {
        if (!requireAuth()) return;

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        const WLANManager::ConnectionState state = wlanManager.takeConnectionState();

        JsonObject wlan = root["wlan"].to<JsonObject>();
        wlan["state"]     = wlanStateToString(state);
        wlan["connected"] = (state == WLANManager::ConnectionState::Connected);
        wlan["hotspot"]   = (state == WLANManager::ConnectionState::Hotspot);
        wlan["ssid"]      = wlanManager.takeSSID();
        wlan["rssi"]      = wlanManager.takeRSSI();
        wlan["bars"]      = wlanManager.takeRSSIBars();
        wlan["ip"]        = wlanManager.takeIP();
        wlan["mac"]       = WiFi.macAddress();
        wlan["hostname"]  = Config::system.hostname;

        JsonObject mqtt = root["mqtt"].to<JsonObject>();
        mqtt["connected"] = MyMQTT::isConnected();
        // Der Broker, mit dem tatsächlich gearbeitet wird. Weicht er von url:port ab, ist
        // eine Änderung noch nicht übernommen — die Oberfläche kann das anzeigen, statt ein
        // irreführendes "verbunden" neben einer neuen Adresse stehen zu lassen.
        mqtt["broker"]    = MyMQTT::activeBroker();
        mqtt["url"]       = Config::mqtt.url;
        mqtt["port"]      = Config::mqtt.port;

        sendJson(doc);
    }

    /// @brief Startet eine Suche nach WLAN-Netzen.
    static void handleApiWifiScanStart() {
        if (!requireAuth()) return;

        WebServer& ws = wlanManager.getWebserver();

        // Laeuft schon eine Suche, keine zweite anstossen.
        if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
            ws.send(200, "application/json", "{\"running\":true}");
            return;
        }

        WiFi.scanDelete();          // Ergebnis der vorigen Suche freigeben
        WiFi.scanNetworks(true);    // true = nicht blockieren
        ws.send(200, "application/json", "{\"running\":true}");
    }

    /// @brief Liefert das Ergebnis der letzten Suche.
    /// Solange sie laeuft, kommt `{"running":true}` zurueck und die Seite fragt erneut nach.
    static void handleApiWifiScanResult() {
        if (!requireAuth()) return;

        WebServer& ws = wlanManager.getWebserver();
        const int16_t found = WiFi.scanComplete();

        if (found == WIFI_SCAN_RUNNING) {
            ws.send(200, "application/json", "{\"running\":true}");
            return;
        }
        if (found == WIFI_SCAN_FAILED) {
            // Noch nie gesucht, oder die Suche ist fehlgeschlagen.
            ws.send(200, "application/json", "{\"running\":false,\"networks\":[]}");
            return;
        }

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["running"] = false;
        JsonArray networks = root["networks"].to<JsonArray>();

        // Einfache Auswahlsortierung ueber die Indizes: bei hoechstens ein paar Dutzend
        // Netzen ist das schnell genug und kommt ohne zusaetzlichen Speicher aus.
        for (int16_t rank = 0; rank < found; rank++) {
            int16_t best = -1;
            for (int16_t i = 0; i < found; i++) {
                const String ssid = WiFi.SSID(i);
                if (ssid.length() == 0) continue;

                // Schon ausgegeben? Dann war der staerkere Zugangspunkt desselben Netzes
                // bereits dran.
                bool already = false;
                for (JsonObject out : networks) {
                    if (ssid == out["ssid"].as<const char*>()) { already = true; break; }
                }
                if (already) continue;

                if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
            }
            if (best < 0) break;

            JsonObject net = networks.add<JsonObject>();
            net["ssid"]   = WiFi.SSID(best);
            net["rssi"]   = WiFi.RSSI(best);
            net["secure"] = (WiFi.encryptionType(best) != WIFI_AUTH_OPEN);
        }

        sendJson(doc);
    }

    /// @brief Stößt die Erkennung eines Shelly-Geräts an.
    /// Rumpf: `{"address":"192.168.1.100","password":"optional"}`
    static void handleApiShellyProbeStart() {
        if (!requireAuth()) return;

        WebServer& ws = wlanManager.getWebserver();

        JsonDocument body;
        if (!ws.hasArg("plain") || deserializeJson(body, ws.arg("plain"))) {
            ws.send(400, "application/json", "{\"error\":\"INVALID_JSON\"}");
            return;
        }

        const String address = body["address"] | "";
        if (address.length() == 0) {
            ws.send(400, "application/json", "{\"error\":\"NO_ADDRESS\"}");
            return;
        }

        // Ein leeres Feld heißt in der Oberfläche "unverändert". Dann gilt das gespeicherte
        // Passwort dieser Adresse, genau wie beim Speichern.
        String password = body["password"] | "";
        if (password.length() == 0) {
            for (uint8_t i = 0; i < Config::shellys.count; i++) {
                if (Config::shellys.items[i].address == address) {
                    password = Config::shellys.items[i].password;
                    break;
                }
            }
        }

        ShellyProbe::request(address, password);
        ws.send(200, "application/json", "{\"running\":true}");
    }

    /// @brief Liefert das Ergebnis der Geräteerkennung.
    static void handleApiShellyProbeResult() {
        if (!requireAuth()) return;

        const ShellyProbe::State state = ShellyProbe::state();
        WebServer& ws = wlanManager.getWebserver();

        if (state == ShellyProbe::State::Pending || state == ShellyProbe::State::Running) {
            ws.send(200, "application/json", "{\"running\":true}");
            return;
        }
        if (state == ShellyProbe::State::Idle) {
            ws.send(200, "application/json", "{\"running\":false,\"ok\":false}");
            return;
        }

        const ShellyProbe::Result found = ShellyProbe::result();

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["running"] = false;
        root["ok"]      = (state == ShellyProbe::State::Done);
        root["address"] = found.address;

        if (state == ShellyProbe::State::Done) {
            root["mac"]          = found.mac;
            root["gen"]          = found.gen;
            root["model"]        = found.model;
            root["app"]          = found.app;
            root["profile"]      = found.profile;
            root["firmware"]     = found.firmware;
            root["kind"]         = found.kind;
            root["authRequired"] = found.authRequired;
            const char* password = "unchecked";
            switch (found.passwordState) {
                case ShellyProbe::PasswordState::NotNeeded: password = "notNeeded"; break;
                case ShellyProbe::PasswordState::Missing:   password = "missing";   break;
                case ShellyProbe::PasswordState::Wrong:     password = "wrong";     break;
                case ShellyProbe::PasswordState::Valid:     password = "valid";     break;
                default: break;
            }
            root["passwordState"] = password;
        } else {
            root["error"] = found.error;
            if (found.httpStatus > 0) root["httpStatus"] = found.httpStatus;
        }

        sendJson(doc);
    }

    /// @brief Liefert alle Messwerte aller Sensoren.
    static void handleApiSensors() {
        if (!requireAuth()) return;

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        JsonObject bh = root["bh1750"].to<JsonObject>();
        bh["enabled"]       = Config::sensors.bh1750.enabled;
        bh["connected"]     = MyBH1750::isConnected;
        bh["lux"]           = MyBH1750::lux;
        bh["luxCalibrated"] = MyBH1750::luxCalibrated;

        JsonObject sht = root["sht41"].to<JsonObject>();
        sht["enabled"]               = Config::sensors.sht41.enabled;
        sht["connected"]             = MySHT41::isConnected;
        sht["temperature"]           = MySHT41::temperature;
        sht["temperatureCalibrated"] = MySHT41::temperatureCalibrated;
        sht["humidity"]              = MySHT41::humidity;
        sht["humidityCalibrated"]    = MySHT41::humidityCalibrated;
        sht["smoothing"]             = Config::sensors.sht41.smoothing;
        sht["temperatureSmoothed"]   = MySHT41::temperatureSmoothed;
        sht["humiditySmoothed"]      = MySHT41::humiditySmoothed;
        sht["dewPoint"]              = MySHT41::dewPoint;

        JsonObject scd = root["scd40"].to<JsonObject>();
        scd["enabled"]     = Config::sensors.scd40.enabled;
        scd["connected"]   = MySCD40::isConnected;
        scd["co2"]         = MySCD40::co2;
        scd["temperature"] = MySCD40::temperature;
        scd["humidity"]    = MySCD40::humidity;
        scd["level"]       = levelKey(MySCD40::level);

        JsonObject aht = root["aht21"].to<JsonObject>();
        aht["enabled"]     = Config::sensors.aht21.enabled;
        aht["connected"]   = MyAHT21::isConnected;
        aht["temperature"] = MyAHT21::temperature;
        aht["humidity"]    = MyAHT21::humidity;

        JsonObject ens = root["ens160"].to<JsonObject>();
        ens["enabled"]   = Config::sensors.ens160.enabled;
        ens["connected"] = MyENS160::isConnected;
        // AQI 0 heißt "kein gültiger Wert" (siehe MyENS160)
        if (MyENS160::aqi > 0) {
            ens["aqi"]   = MyENS160::aqi;
            ens["tvoc"]  = MyENS160::tvoc;
            ens["eco2"]  = MyENS160::eco2;
        } else {
            ens["aqi"]   = nullptr;
            ens["tvoc"]  = nullptr;
            ens["eco2"]  = nullptr;
        }
        ens["level"]     = levelKey(MyENS160::level);
        ens["validity"]  = validityKey(MyENS160::validity);

        JsonObject ld = root["ld2410"].to<JsonObject>();
        ld["enabled"]            = Config::sensors.ld2410.enabled;
        ld["connected"]          = MyLD2410::isConnected;
        ld["presence"]           = MyLD2410::presence;
        ld["movingTarget"]       = MyLD2410::movingTarget;
        ld["movingDistance"]     = MyLD2410::movingDistance;
        ld["movingEnergy"]       = MyLD2410::movingEnergy;
        ld["stationaryTarget"]   = MyLD2410::stationaryTarget;
        ld["stationaryDistance"] = MyLD2410::stationaryDistance;
        ld["stationaryEnergy"]   = MyLD2410::stationaryEnergy;

        
        const HeatingControl::Status heat = HeatingControl::status();
        JsonObject h = root["heating"].to<JsonObject>();
        h["enabled"]        = heat.enabled;
        h["connected"]      = heat.sensorOk;
        h["desired"]        = HeatingControl::key(heat.desired);
        h["actual"]         = HeatingControl::key(heat.actual);
        if (heat.pendingSeconds >= 0) h["pendingMinutes"] = (heat.pendingSeconds + 59) / 60;
        else                          h["pendingMinutes"] = nullptr;
        

        // Brandfrüherkennung: `enabled` ist die Einstellung, `connected` heißt "mindestens ein nutzbarer Sensor"
        JsonObject fire = root["fire"].to<JsonObject>();
        fire["enabled"]   = Config::system.fireDetection;
        fire["connected"] = FireDetect::sensorsAvailable();
        fire["state"]     = FireDetect::key(FireDetect::state());
        auto channel = [&](const char* name, const FireDetect::Channel& info) {
            JsonObject o = fire[name].to<JsonObject>();
            o["available"] = info.available;
            o["ready"]     = info.ready;
            o["value"]     = info.value;
            o["baseline"]  = info.baseline;
            o["rate"]      = info.rate;
            o["level"]     = info.level;
        };
        channel("tvoc", FireDetect::tvoc());
        channel("co2", FireDetect::co2());
        channel("temperature", FireDetect::temperature());

        sendJson(doc);
    }

    /// @brief Löst einen Testalarm aus (LED, Summer, MQTT) für FIRE_TEST_S Sekunden.
    static void handleApiFireTest() {
        if (!requireAuth()) return;
        WebServer& ws = wlanManager.getWebserver();
        if (!FireDetect::startTest()) {
            ws.send(400, "application/json", "{\"error\":\"FIRE_NOT_AVAILABLE\"}");
            return;
        }
        ws.send(200, "application/json", "{\"ok\":true}");
    }

    /// @brief Entwicklermodus des LD2410: Energie, Schwelle und Ausloeser je Zone.
    static void handleApiLd2410Debug() {
        if (!requireAuth()) return;

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["enabled"]   = Config::sensors.ld2410.enabled;
        root["connected"] = MyLD2410::isConnected;

        MyLD2410::DebugSnapshot snap;
        bool active = false;
        if (Config::sensors.ld2410.enabled && MyLD2410::isConnected) {
            MyLD2410::debugKeepAlive();
            active = MyLD2410::debugSnapshot(snap);
        }
        root["active"] = active;

        // Basisdaten des Sensors, roh und ohne Haltezeit: Sie zeigen, was der Sensor wirklich
        // sieht, nicht was die Oberflaeche daraus macht.
        root["presence"]           = MyLD2410::radar.presenceDetected();
        root["movingTarget"]       = MyLD2410::radar.movingTargetDetected();
        root["movingDistance"]     = MyLD2410::radar.movingTargetDistance();
        root["movingEnergy"]       = MyLD2410::radar.movingTargetEnergy();
        root["stationaryTarget"]   = MyLD2410::radar.stationaryTargetDetected();
        root["stationaryDistance"] = MyLD2410::radar.stationaryTargetDistance();
        root["stationaryEnergy"]   = MyLD2410::radar.stationaryTargetEnergy();

        JsonArray gates = root["gates"].to<JsonArray>();
        if (active) {
            root["detectionDistance"] = snap.detectionDistance;
            const uint32_t now = millis();
            auto ago = [&](JsonObject o, const char* key, uint32_t at) {
                if (at == 0) o[key] = nullptr; else o[key] = now - at;
            };
            for (uint8_t g = 0; g < snap.gateCount; g++) {
                const MyLD2410::DebugGate& in = snap.gates[g];
                JsonObject o = gates.add<JsonObject>();
                o["moving"]              = in.moving;
                o["movingThreshold"]     = in.movingThreshold;
                o["movingPeak"]          = in.movingPeak;
                ago(o, "movingTriggeredAgoMs", in.movingTriggeredAt);
                o["stationary"]          = in.stationary;
                o["stationaryThreshold"] = in.stationaryThreshold;
                o["stationaryPeak"]      = in.stationaryPeak;
                ago(o, "stationaryTriggeredAgoMs", in.stationaryTriggeredAt);
            }
        }
        sendJson(doc);
    }

    /// @brief Steuert den Entwicklermodus: `{"action":"stop"}` oder `{"action":"reset"}`.
    /// "reset" setzt Hoechstwerte und Ausloesezeitpunkte zurueck.
    static void handleApiLd2410DebugControl() {
        if (!requireAuth()) return;
        WebServer& ws = wlanManager.getWebserver();

        JsonDocument body;
        if (!ws.hasArg("plain") || deserializeJson(body, ws.arg("plain"))) {
            ws.send(400, "application/json", "{\"error\":\"INVALID_JSON\"}");
            return;
        }
        const String action = body["action"] | "";
        if (action == "stop") {
            MyLD2410::debugStop();
        } else if (action == "reset") {
            MyLD2410::debugResetPeaks();
        } else {
            ws.send(400, "application/json", "{\"error\":\"INVALID_JSON\"}");
            return;
        }
        ws.send(200, "application/json", "{\"ok\":true}");
    }

    /// @brief Liefert alles, was die Startseite braucht, in einer Antwort.
    static void handleApiDashboard() {
        if (!requireAuth()) return;

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        root["hostname"] = Config::system.hostname;

        JsonObject sens = root["sensors"].to<JsonObject>();
        auto sensor = [&](const char* name, bool enabled, bool connected) {
            JsonObject o = sens[name].to<JsonObject>();
            o["enabled"]   = enabled;
            o["connected"] = connected;
        };
        sensor("bh1750", Config::sensors.bh1750.enabled, MyBH1750::isConnected);
        sensor("sht41",  Config::sensors.sht41.enabled,  MySHT41::isConnected);
        sensor("scd40",  Config::sensors.scd40.enabled,  MySCD40::isConnected);
        sensor("aht21",  Config::sensors.aht21.enabled,  MyAHT21::isConnected);
        sensor("ens160", Config::sensors.ens160.enabled, MyENS160::isConnected);
        sensor("ld2410", Config::sensors.ld2410.enabled, MyLD2410::isConnected);

        const WLANManager::ConnectionState state = wlanManager.takeConnectionState();
        JsonObject wlan = root["wlan"].to<JsonObject>();
        wlan["state"]     = wlanStateToString(state);
        wlan["connected"] = (state == WLANManager::ConnectionState::Connected);
        wlan["hotspot"]   = (state == WLANManager::ConnectionState::Hotspot);
        wlan["ssid"]      = wlanManager.takeSSID();
        wlan["rssi"]      = wlanManager.takeRSSI();

        JsonObject mqtt = root["mqtt"].to<JsonObject>();
        mqtt["configured"] = Config::mqtt.url.length() > 0;
        mqtt["connected"]  = MyMQTT::isConnected();
        mqtt["broker"]     = MyMQTT::activeBroker();

        JsonObject shelly = root["shelly"].to<JsonObject>();
        shelly["used"] = Config::shellys.count;
        shelly["max"]  = SHELLY_MAX_COUNT;

        JsonObject sys = root["system"].to<JsonObject>();

        JsonObject heap = sys["heap"].to<JsonObject>();
        heap["total"]    = ESP.getHeapSize();
        heap["free"]     = ESP.getFreeHeap();
        heap["minFree"]  = ESP.getMinFreeHeap();
        heap["maxBlock"] = ESP.getMaxAllocHeap();

        // fs: Dateisystem der Web-UI, configFs: das der Konfiguration (eigene Partition).
        JsonObject fs = sys["fs"].to<JsonObject>();
        fs["total"] = LittleFS.totalBytes();
        fs["used"]  = LittleFS.usedBytes();

        JsonObject configFs = sys["configFs"].to<JsonObject>();
        configFs["total"] = Config::storageTotalBytes();
        configFs["used"]  = Config::storageUsedBytes();

        // Welcher der beiden App-Plaetze laeuft. Die Partitionstabelle hat app0 und app1; ein
        // Update (OTA) schreibt in den jeweils anderen.
        JsonObject app = sys["app"].to<JsonObject>();
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) {
            app["label"]        = running->label;
            app["partitionSize"] = running->size;
        } else {
            app["label"]        = nullptr;
            app["partitionSize"] = nullptr;
        }
        app["firmwareSize"] = ESP.getSketchSize();

        // Was auf dem anderen App-Platz liegt, wurde beim Start einmal ermittelt (BuildInfo).
        const BuildInfo::Other& otherApp = BuildInfo::other();
        JsonObject other = sys["otherApp"].to<JsonObject>();
        switch (otherApp.state) {
            case BuildInfo::OtherState::Found:    other["state"] = "found";    break;
            case BuildInfo::OtherState::Empty:    other["state"] = "empty";    break;
            case BuildInfo::OtherState::Unmarked: other["state"] = "unmarked"; break;
            case BuildInfo::OtherState::None:     other["state"] = "none";     break;
            default:                              other["state"] = "pending";  break;
        }
        if (otherApp.label[0] != '\0') other["label"] = otherApp.label;
        if (otherApp.imageSize > 0) {
            other["firmwareSize"]  = otherApp.imageSize;
            other["partitionSize"] = otherApp.partitionSize;
        }
        if (otherApp.state == BuildInfo::OtherState::Found) {
            other["version"] = otherApp.info.version;
            other["build"]   = otherApp.info.build;
            other["built"]   = otherApp.info.time;
            other["git"]     = otherApp.info.git;
        }

        // Passt die Web-UI im Dateisystem zu dieser Firmware? (data/version.json gegen VERSION)
        const Webfiles::UiInfo& ui = Webfiles::ui();
        JsonObject webui = sys["webui"].to<JsonObject>();
        switch (ui.state) {
            case Webfiles::UiState::Ok:      webui["state"] = "ok";      break;
            case Webfiles::UiState::Limited: webui["state"] = "limited"; break;
            case Webfiles::UiState::TooOld:  webui["state"] = "tooOld";  break;
            default:                         webui["state"] = "unknown"; break;
        }
        if (ui.version[0] != '\0') {
            webui["version"]        = ui.version;
            webui["name"]           = ui.name;
            webui["vendor"]         = ui.vendor;
            webui["link"]           = ui.link;
            webui["minApi"]         = ui.minApi;
            webui["recommendedApi"] = ui.recommendedApi;
        }

        JsonObject run = sys["runtime"].to<JsonObject>();
        run["totalSeconds"]   = Runtime::totalSeconds();
        run["sessionSeconds"] = Runtime::sessionSeconds();
        run["resetReason"]    = resetReasonToString(esp_reset_reason());

        JsonObject fw = sys["firmware"].to<JsonObject>();
        fw["version"] = Config::version.toString();
        // Build-Nummer, Zeitpunkt und Git-Stand stammen aus scripts/build_info.py.
        const BuildInfo::Info& build = BuildInfo::own();
        fw["build"]   = build.build;
        fw["built"]   = build.time;
        fw["git"]     = build.git;
        fw["sdk"]     = ESP.getSdkVersion();
#ifdef ARDUINO_ESP32_RELEASE
        fw["core"]    = ARDUINO_ESP32_RELEASE;
#endif
        fw["chip"]     = ESP.getChipModel();
        fw["revision"] = ESP.getChipRevision();
        fw["cores"]    = ESP.getChipCores();
        fw["cpuMHz"]   = ESP.getCpuFreqMHz();

        sys["tasks"] = uxTaskGetNumberOfTasks();

        const float chipTemp = temperatureRead();
        if (isnan(chipTemp) || chipTemp < -40.0f || chipTemp > 150.0f) {
            sys["cpuTemperature"] = nullptr;
        } else {
            sys["cpuTemperature"] = chipTemp;
        }

        sendJson(doc);
    }

    /// @brief Neustart ueber die Schnittstelle.
    static void handleApiRestart() {
        if (!requireAuth()) return;
        wlanManager.getWebserver().send(200, "application/json", "{\"ok\":true}");
        restartSoon();
    }

    /// @brief Stellt den Start auf die andere Firmware um und startet neu. Nur moeglich, wenn dort
    /// eine Firmware liegt (mit oder ohne Kennung).
    static void handleApiSwitchFirmware() {
        if (!requireAuth()) return;
        WebServer& ws = wlanManager.getWebserver();

        const BuildInfo::OtherState state = BuildInfo::other().state;
        const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
        if (!target || (state != BuildInfo::OtherState::Found && state != BuildInfo::OtherState::Unmarked)) {
            ws.send(400, "application/json", "{\"error\":\"NO_FIRMWARE_IN_OTHER_SLOT\"}");
            return;
        }
        if (esp_ota_set_boot_partition(target) != ESP_OK) {
            ws.send(500, "application/json", "{\"error\":\"SWITCH_FAILED\"}");
            return;
        }
        Serial.printf("[OTA] Wechsel auf %s\n", target->label);
        ws.send(200, "application/json", "{\"ok\":true}");
        restartSoon();
    }

    /// @brief Setzt die Konfiguration auf den Auslieferungszustand zurueck und startet neu.
    static void handleApiReset() {
        if (!requireAuth()) return;
        WebServer& ws = wlanManager.getWebserver();

        if (!Config::clearConfig()) {
            ws.send(500, "application/json", "{\"error\":\"RESET_FAILED\"}");
            return;
        }
        ws.send(200, "application/json", "{\"ok\":true}");
        restartSoon();
    }
};

namespace Webinterface {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    /// @brief Legt die Routen für die API an.
    void setupWebserverRoutes() {
        wlanManager.addRoute("/api/login",              HTTP_POST, handleApiLogin               );
        wlanManager.addRoute("/api/logout",             HTTP_POST, handleApiLogout              );
        wlanManager.addRoute("/api/session",            HTTP_GET,  handleApiSession             );
        wlanManager.addRoute("/api/info",               HTTP_GET,  handleApiInfo                );
        wlanManager.addRoute("/api/config",             HTTP_GET,  handleApiConfig              );
        wlanManager.addRoute("/api/config",             HTTP_POST, handleApiConfigSave          );
        wlanManager.addRoute("/api/sensors",            HTTP_GET,  handleApiSensors             );
        wlanManager.addRoute("/api/status",             HTTP_GET,  handleApiStatus              );
        wlanManager.addRoute("/api/dashboard",          HTTP_GET,  handleApiDashboard           );
        wlanManager.addRoute("/api/restart",            HTTP_POST, handleApiRestart             );
        wlanManager.addRoute("/api/fire/test",          HTTP_POST, handleApiFireTest            );
        wlanManager.addRoute("/api/ld2410/debug",       HTTP_GET,  handleApiLd2410Debug         );
        wlanManager.addRoute("/api/ld2410/debug",       HTTP_POST, handleApiLd2410DebugControl  );
        wlanManager.addRoute("/api/firmware/switch",    HTTP_POST, handleApiSwitchFirmware      );
        wlanManager.addRoute("/api/reset",              HTTP_POST, handleApiReset               );
        wlanManager.addRoute("/api/pwreset",            HTTP_GET,  handleApiPwResetState        );
        wlanManager.addRoute("/api/pwreset",            HTTP_POST, handleApiPwResetArm          );
        wlanManager.addRoute("/api/wifi/scan",          HTTP_POST, handleApiWifiScanStart       );
        wlanManager.addRoute("/api/wifi/scan",          HTTP_GET,  handleApiWifiScanResult      );
        wlanManager.addRoute("/api/shelly/probe",       HTTP_POST, handleApiShellyProbeStart    );
        wlanManager.addRoute("/api/shelly/probe",       HTTP_GET,  handleApiShellyProbeResult   );
    }
}
