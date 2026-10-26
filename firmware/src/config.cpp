// ===================================================================
// File:            config.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "config.h"

namespace Config {

    // ---- Laufzeitwerte ----
    Version    version = Version::parse(VERSION);
    SystemCfg  system;
    WlanCfg    wlan;
    MqttCfg    mqtt;
    SensorsCfg sensors;
    ShellysCfg shellys;
    ControlsCfg controls;

    // ---- Dateipfade ----
    static const char* PATH_JSON = "/" CONFIG_FILE_BASE ".json";
    static const char* PATH_BAK  = "/" CONFIG_FILE_BASE ".bak";
    static const char* PATH_TMP  = "/" CONFIG_FILE_BASE ".tmp";

    // Die Konfiguration hat ein eigenes Dateisystem in der Partition "config" (partitions.csv),
    // getrennt von dem der Web-UI. So kann die UI mit "uploadfs" ersetzt werden, ohne dass die
    // Konfiguration angefasst wird. Beide Apps (app0/app1) greifen auf dasselbe zu.
    static const char* CONFIG_PARTITION = "config";
    static fs::LittleFSFS configFs;
    static bool fsMounted = false;

    // --------------------------------------------------------------
    // Statische funktionen anmelden
    // --------------------------------------------------------------
    // Vorab-Deklarationen aller static-Funktionen, damit die Reihenfolge der Definitionen
    // unten frei nach Thema gewählt werden kann. Die öffentlichen Funktionen stehen in config.h.

    // Defaultvalues
    static void setGateDefaults();
    static void setControlDefaults();
    static void setDefaults();
    static bool mountFs();

    // JSON und Formatierungen
    static int   intFromJson(JsonVariantConst value, int fallback, int minimum, int maximum);
    static float tenthsFromJson(JsonVariantConst value, float fallback, float minimum, float maximum);
    static void  fromJson(JsonObjectConst root);
    static void  mergeJson(JsonObject target, JsonObjectConst patch);

    // Shellyfunktionen
    static String outputKey(const String& raw);
    static void   outputsToJson(JsonObject parent, const ControlOutputs& outputs);
    static void   outputsFromJson(JsonObjectConst parent, ControlOutputs& outputs);

    // LD2410
    static void    gatesToJson(JsonObject node, const Ld2410GateCfg gates[]);
    static uint8_t gateValueFromJson(JsonVariantConst value);
    static void    gatesFromJson(JsonObjectConst node, Ld2410GateCfg gates[]);

    // Kalibrierung
    static void calibrationToJson(JsonObject parent, const char* key, const Calibration::Table& table);
    static void calibrationFromJson(JsonObjectConst parent, const char* key, Calibration::Table& table);

    // Dateioperationen
    static bool readFile(const char* path);
    static bool writeFile(const char* path);


    // --------------------------------------------------------------
    // Defaultvalues
    // --------------------------------------------------------------

    // Vorgabewerte, die nicht leer sind, stehen hier einmal zentral: setDefaults() und
    // fromJson() brauchen sie beide, und sie dürfen nicht auseinanderlaufen.
    static const uint16_t DEFAULT_MQTT_PORT = 1883;

    // Sensoren sind ab Werk eingeschaltet: Das Gerät ist als Hypha Home - Sens mit allen Sensoren
    // bestückt gedacht. Wer einen nicht bestückt hat, schaltet ihn ab. Ein eingeschalteter,
    // aber fehlender Sensor ist unkritisch — er wird als nicht verbunden gemeldet.
    static const bool DEFAULT_SENSOR_ENABLED = true;

    /// @brief Setzt die Vorgabe-Empfindlichkeiten der LD2410-Gates.
    static void setGateDefaults() {
        for (uint8_t gate = 0; gate < LD2410_GATE_COUNT; gate++) {
            if (gate <= 1) {
                sensors.ld2410.gates[gate] = { 50, 0 };
            } else if (gate <= 5) {
                sensors.ld2410.gates[gate] = { 40, 40 };
            } else {
                sensors.ld2410.gates[gate] = { 30, 15 };
            }
        }
    }

    /// @brief Setzt die Regelkreise auf die Auslieferungsvorgaben.
    static void setControlDefaults() {
        controls.heating.enabled = false;
        controls.heating.outputs = ControlOutputs{};
        controls.heating.targetTemp       = HEATING_TARGET_DEFAULT_C;
        controls.heating.hysteresisOnMin  = 10;
        controls.heating.hysteresisOffMin = 10;

        controls.light.enabled = false;
        controls.light.outputs = ControlOutputs{};
        controls.light.luxThreshold  = 10;
        controls.light.brightnessPct = 100;
        controls.light.fadeSeconds   = 2;

        controls.ventilation.enabled = false;
        controls.ventilation.outputs = ControlOutputs{};
        controls.ventilation.aqiThreshold      = 3;
        controls.ventilation.humidityThreshold = 65;
    }

    /// @brief Setzt alle Werte auf die Auslieferungsvorgaben.
    static void setDefaults() {
        system.hostname = "";   // main.cpp macht daraus "HyphaHome-Sens-<MAC>"
        system.fireDetection = false;
        system.password = "";   // kein Standardpasswort, wie nach einem Passwort-Reset
        wlan.ssid       = "";
        wlan.password   = "";
        mqtt.url        = "";
        mqtt.port       = DEFAULT_MQTT_PORT;
        mqtt.username   = "";
        mqtt.password   = "";

        sensors.bh1750.enabled = DEFAULT_SENSOR_ENABLED;
        sensors.sht41.enabled  = DEFAULT_SENSOR_ENABLED;
        sensors.scd40.enabled  = DEFAULT_SENSOR_ENABLED;
        sensors.aht21.enabled  = DEFAULT_SENSOR_ENABLED;
        sensors.ens160.enabled = DEFAULT_SENSOR_ENABLED;
        sensors.ld2410.enabled = DEFAULT_SENSOR_ENABLED;

        // Ab Werk nicht kalibriert: Die Rohwerte gehen unverändert durch.
        sensors.bh1750.luxCalibration.clear();
        sensors.sht41.temperatureCalibration.clear();
        sensors.sht41.smoothing = 1;

        setGateDefaults();
        sensors.ld2410.movingHoldSeconds     = 5;
        sensors.ld2410.stationaryHoldSeconds = 5;
        sensors.ld2410.presenceHoldSeconds   = 5;

        // Ab Werk ist kein Shelly eingetragen.
        shellys.count = 0;
        for (uint8_t i = 0; i < SHELLY_MAX_COUNT; i++) {
            shellys.items[i] = ShellyCfg{};
        }

        setControlDefaults();
    }

    /// @brief Hängt das Konfigurations-Dateisystem ein. 
    /// @return true wenn das Dateisystem benutzbar ist
    static bool mountFs() {
        if (fsMounted) return true;
        fsMounted = configFs.begin(true, "/config", 5, CONFIG_PARTITION);
        if (!fsMounted) Serial.println(F("[CONFIG] Konfigurations-Dateisystem konnte nicht eingehaengt werden"));
        return fsMounted;
    }

    // --------------------------------------------------------------
    // JSON und Formatierungen
    // --------------------------------------------------------------

    /// @brief Liest eine ganze Zahl und begrenzt sie auf einen Bereich.
    ///
    /// Fehlt der Wert oder ist er keine Zahl, gilt die Vorgabe. Liegt er außerhalb, wird er an
    /// die Grenze gesetzt statt verworfen: Wer 5000 Minuten für etwas einträgt, das höchstens
    /// 1440 verträgt, meint "viel", nicht "Vorgabe".
    static int intFromJson(JsonVariantConst value, int fallback, int minimum, int maximum) {
        if (!value.is<int>()) return fallback;
        int raw = value.as<int>();
        if (raw < minimum) return minimum;
        if (raw > maximum) return maximum;
        return raw;
    }

    /// @brief Liest eine Dezimalzahl, begrenzt sie auf einen Bereich und rundet auf eine Stelle.
    ///
    /// Fehlt der Wert oder ist er keine Zahl, gilt die Vorgabe; außerhalb des Bereichs wird an
    /// die Grenze gesetzt (siehe intFromJson).
    static float tenthsFromJson(JsonVariantConst value, float fallback, float minimum, float maximum) {
        if (!value.is<float>()) return fallback;
        float raw = roundf(value.as<float>() * 10.0f) / 10.0f;
        if (raw < minimum) return minimum;
        if (raw > maximum) return maximum;
        return raw;
    }

    // Nicht static: Die Schnittstelle des Webinterfaces benutzt dieselbe Serialisierung.
    // Beschreibung in config.h.
    void toJson(JsonObject root, bool includeSecrets) {
        root["version"] = version.toString();

        JsonObject sys = root["system"].to<JsonObject>();
        sys["hostname"] = system.hostname;
        sys["fireDetection"] = system.fireDetection;
        if (includeSecrets) {
            sys["password"] = system.password;
        } else {
            sys["hasPassword"] = (system.password.length() > 0);
        }

        JsonObject w = root["wlan"].to<JsonObject>();
        w["ssid"]     = wlan.ssid;
        if (includeSecrets) {
            w["password"] = wlan.password;
        } else {
            w["hasPassword"] = (wlan.password.length() > 0);
        }

        JsonObject m = root["mqtt"].to<JsonObject>();
        m["url"]      = mqtt.url;
        m["port"]     = mqtt.port;
        m["username"] = mqtt.username;
        if (includeSecrets) {
            m["password"] = mqtt.password;
        } else {
            m["hasPassword"] = (mqtt.password.length() > 0);
        }

        JsonArray shellyArr = root["shellys"].to<JsonArray>();
        for (uint8_t i = 0; i < shellys.count; i++) {
            JsonObject item = shellyArr.add<JsonObject>();
            item["name"]         = shellys.items[i].name;
            item["address"]      = shellys.items[i].address;
            item["mac"]          = shellys.items[i].mac;
            item["gen"]          = shellys.items[i].gen;
            item["kind"]         = shellys.items[i].kind;
            if (includeSecrets) {
                item["password"] = shellys.items[i].password;
            } else {
                item["hasPassword"] = (shellys.items[i].password.length() > 0);
            }
        }

        JsonObject ctl = root["controls"].to<JsonObject>();

        JsonObject heat = ctl["heating"].to<JsonObject>();
        heat["enabled"]          = controls.heating.enabled;
        heat["targetTemp"]       = controls.heating.targetTemp;
        heat["hysteresisOnMin"]  = controls.heating.hysteresisOnMin;
        heat["hysteresisOffMin"] = controls.heating.hysteresisOffMin;
        outputsToJson(heat, controls.heating.outputs);

        JsonObject lgt = ctl["light"].to<JsonObject>();
        lgt["enabled"]       = controls.light.enabled;
        lgt["luxThreshold"]  = controls.light.luxThreshold;
        lgt["brightnessPct"] = controls.light.brightnessPct;
        lgt["fadeSeconds"]   = controls.light.fadeSeconds;
        outputsToJson(lgt, controls.light.outputs);

        JsonObject vent = ctl["ventilation"].to<JsonObject>();
        vent["enabled"]           = controls.ventilation.enabled;
        vent["aqiThreshold"]      = controls.ventilation.aqiThreshold;
        vent["humidityThreshold"] = controls.ventilation.humidityThreshold;
        outputsToJson(vent, controls.ventilation.outputs);

        JsonObject sens = root["sensors"].to<JsonObject>();

        JsonObject bh = sens["bh1750"].to<JsonObject>();
        bh["enabled"] = sensors.bh1750.enabled;
        calibrationToJson(bh, "luxCalibration", sensors.bh1750.luxCalibration);

        JsonObject sht = sens["sht41"].to<JsonObject>();
        sht["enabled"] = sensors.sht41.enabled;
        calibrationToJson(sht, "temperatureCalibration", sensors.sht41.temperatureCalibration);
        sht["smoothing"] = sensors.sht41.smoothing;

        sens["scd40"].to<JsonObject>()["enabled"]  = sensors.scd40.enabled;
        sens["aht21"].to<JsonObject>()["enabled"]  = sensors.aht21.enabled;
        sens["ens160"].to<JsonObject>()["enabled"] = sensors.ens160.enabled;

        JsonObject ld = sens["ld2410"].to<JsonObject>();
        ld["enabled"] = sensors.ld2410.enabled;
        gatesToJson(ld, sensors.ld2410.gates);
        ld["movingHold"]     = sensors.ld2410.movingHoldSeconds;
        ld["stationaryHold"] = sensors.ld2410.stationaryHoldSeconds;
        ld["presenceHold"]   = sensors.ld2410.presenceHoldSeconds;
    }

    /// @brief Liest die Laufzeitwerte aus einem JSON-Objekt.
    ///
    /// Jedes Feld hat hinter dem |-Operator seinen Vorgabewert. Fehlt ein Schlüssel in der
    /// Datei oder hat er den falschen Typ, greift dieser Wert. Dadurch braucht ein später
    /// hinzukommendes Feld keine eigene Migration: Eine ältere Datei ohne das Feld bekommt
    /// einfach die Vorgabe.
    /// @param root Wurzelobjekt des Dokuments
    static void fromJson(JsonObjectConst root) {
        JsonObjectConst sys = root["system"];
        system.hostname = sys["hostname"] | "";
        system.fireDetection = sys["fireDetection"] | false;
        system.password = sys["password"] | "";

        JsonObjectConst w = root["wlan"];
        wlan.ssid     = w["ssid"]     | "";
        wlan.password = w["password"] | "";

        JsonObjectConst m = root["mqtt"];
        mqtt.url      = m["url"]      | "";
        mqtt.port     = m["port"]     | DEFAULT_MQTT_PORT;
        mqtt.username = m["username"] | "";
        mqtt.password = m["password"] | "";

        JsonObjectConst sens = root["sensors"];
        sensors.bh1750.enabled = sens["bh1750"]["enabled"] | DEFAULT_SENSOR_ENABLED;
        sensors.sht41.enabled  = sens["sht41"]["enabled"]  | DEFAULT_SENSOR_ENABLED;
        sensors.scd40.enabled  = sens["scd40"]["enabled"]  | DEFAULT_SENSOR_ENABLED;
        sensors.aht21.enabled  = sens["aht21"]["enabled"]  | DEFAULT_SENSOR_ENABLED;
        sensors.ens160.enabled = sens["ens160"]["enabled"] | DEFAULT_SENSOR_ENABLED;
        sensors.ld2410.enabled = sens["ld2410"]["enabled"] | DEFAULT_SENSOR_ENABLED;

        calibrationFromJson(sens["bh1750"], "luxCalibration", sensors.bh1750.luxCalibration);
        calibrationFromJson(sens["sht41"], "temperatureCalibration",
                            sensors.sht41.temperatureCalibration);
        sensors.sht41.smoothing =
            intFromJson(sens["sht41"]["smoothing"], 1, 1, SHT41_SMOOTHING_MAX);
        gatesFromJson(sens["ld2410"], sensors.ld2410.gates);
        sensors.ld2410.movingHoldSeconds =
            intFromJson(sens["ld2410"]["movingHold"], 5, 0, LD2410_MOVING_HOLD_MAX_S);
        sensors.ld2410.stationaryHoldSeconds =
            intFromJson(sens["ld2410"]["stationaryHold"], 5, 0, LD2410_MOVING_HOLD_MAX_S);
        sensors.ld2410.presenceHoldSeconds =
            intFromJson(sens["ld2410"]["presenceHold"], 5, 0, LD2410_MOVING_HOLD_MAX_S);

        // Die bisherigen Geräte kurz festhalten. Grund: Die Schnittstelle liefert die
        // Passwörter nicht mehr aus, also kann die Oberfläche sie auch nicht zurückschicken.
        // Kommt zu einem Gerät kein Passwort mit, bleibt das bisherige stehen — erkannt an der
        // MAC, denn die gehört zum Gerät und überlebt eine neue IP. Ohne bekannte MAC gilt
        // ersatzweise die Adresse.
        const ShellysCfg previous = shellys;

        // Ein Eintrag ohne Adresse ist kein Gerät, sondern eine leer gebliebene Zeile aus der
        // Oberfläche. Er wird übersprungen, damit die Liste nicht mit Platzhaltern wächst.
        shellys.count = 0;
        JsonArrayConst shellyArr = root["shellys"];
        for (JsonObjectConst item : shellyArr) {
            if (shellys.count >= SHELLY_MAX_COUNT) {
                Serial.printf("[CONFIG] mehr als %d Shellys, Rest verworfen\n", SHELLY_MAX_COUNT);
                break;
            }
            String address = item["address"] | "";
            if (address.length() == 0) continue;

            ShellyCfg& target = shellys.items[shellys.count];
            target.name         = item["name"] | "";
            target.address      = address;
            target.mac          = normalizeMac(item["mac"] | "");
            target.gen          = item["gen"] | 0;
            target.kind         = item["kind"] | "";
            if (target.kind != "switch1" && target.kind != "switch2"
                && target.kind != "cover" && target.kind != "dimmer") {
                target.kind = "";
            }

            if (item["password"].is<const char*>()) {
                target.password = item["password"].as<const char*>();
            } else {
                target.password = "";
                for (uint8_t i = 0; i < previous.count; i++) {
                    const bool sameDevice = target.mac.length() > 0 && previous.items[i].mac.length() > 0
                        ? previous.items[i].mac == target.mac
                        : previous.items[i].address == address;
                    if (sameDevice) {
                        target.password = previous.items[i].password;
                        break;
                    }
                }
            }

            shellys.count++;
        }

        JsonObjectConst ctl = root["controls"];

        JsonObjectConst heat = ctl["heating"];
        controls.heating.enabled = heat["enabled"] | false;
        controls.heating.targetTemp = tenthsFromJson(heat["targetTemp"], HEATING_TARGET_DEFAULT_C,
                                                     HEATING_TARGET_MIN_C, HEATING_TARGET_MAX_C);
        controls.heating.hysteresisOnMin  = intFromJson(heat["hysteresisOnMin"], 10, 0, 1440);
        controls.heating.hysteresisOffMin = intFromJson(heat["hysteresisOffMin"], 10, 0, 1440);
        outputsFromJson(heat, controls.heating.outputs);

        JsonObjectConst lgt = ctl["light"];
        controls.light.enabled       = lgt["enabled"] | false;
        controls.light.luxThreshold  = intFromJson(lgt["luxThreshold"], 10, 0, 65535);
        controls.light.brightnessPct = intFromJson(lgt["brightnessPct"], 100, 1, 100);
        controls.light.fadeSeconds   = intFromJson(lgt["fadeSeconds"], 2, 0, 60);
        outputsFromJson(lgt, controls.light.outputs);

        JsonObjectConst vent = ctl["ventilation"];
        controls.ventilation.enabled           = vent["enabled"] | false;
        controls.ventilation.aqiThreshold      = intFromJson(vent["aqiThreshold"], 3, 1, 5);
        controls.ventilation.humidityThreshold = intFromJson(vent["humidityThreshold"], 65, 0, 100);
        outputsFromJson(vent, controls.ventilation.outputs);
    }

    /// @brief Führt ein JSON-Objekt rekursiv in ein anderes ein.
    /// @param target Zielobjekt, wird verändert
    /// @param patch Zu übernehmende Felder
    static void mergeJson(JsonObject target, JsonObjectConst patch) {
        for (JsonPairConst entry : patch) {
            if (entry.value().is<JsonObjectConst>()) {
                JsonVariant slot = target[entry.key()];
                JsonObject child = slot.is<JsonObject>() ? slot.as<JsonObject>()
                                                         : slot.to<JsonObject>();
                mergeJson(child, entry.value().as<JsonObjectConst>());
            } else {
                target[entry.key()] = entry.value();
            }
        }
    }

    // --------------------------------------------------------------
    // Shellyfunktionen
    // --------------------------------------------------------------

    String normalizeMac(const String& mac) {
        String out;
        for (unsigned i = 0; i < mac.length(); i++) {
            const char c = toupper((unsigned char)mac[i]);
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out += c;
        }
        return out;
    }

    /// @brief Findet einen Shelly anhand seiner MAC 
    const ShellyCfg* findShelly(const String& key) {
        const String mac = normalizeMac(key);
        if (mac.length() > 0) {
            for (uint8_t i = 0; i < shellys.count; i++) {
                if (shellys.items[i].mac.length() > 0 && shellys.items[i].mac == mac) return &shellys.items[i];
            }
        }
        for (uint8_t i = 0; i < shellys.count; i++) {
            if (shellys.items[i].address == key) return &shellys.items[i];
        }
        return nullptr;
    }

    /// @brief Macht aus dem Schlüssel eines Ausgangs die MAC des Shelly, wenn sie bekannt ist.
    static String outputKey(const String& raw) {
        const ShellyCfg* found = findShelly(raw);
        if (found && found->mac.length() > 0) return found->mac;
        // Keinen Shelly dazu gefunden: Sieht es wie eine MAC aus, normalisiert behalten.
        const String mac = normalizeMac(raw);
        if (mac.length() == 12 && raw.indexOf('.') < 0) return mac;
        return raw;
    }

    /// @brief Schreibt die Ausgänge eines Regelkreises als Array.
    static void outputsToJson(JsonObject parent, const ControlOutputs& outputs) {
        JsonArray arr = parent["outputs"].to<JsonArray>();
        for (uint8_t i = 0; i < outputs.count; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["shelly"]  = outputs.items[i].shelly;
            o["channel"] = outputs.items[i].channel;
        }
    }

    /// @brief Liest die Ausgänge eines Regelkreises.
    static void outputsFromJson(JsonObjectConst parent, ControlOutputs& outputs) {
        outputs.count = 0;
        for (JsonObjectConst entry : parent["outputs"].as<JsonArrayConst>()) {
            if (outputs.count >= CONTROL_MAX_OUTPUTS) {
                Serial.printf("[CONFIG] mehr als %d Ausgaenge in einem Regelkreis, Rest verworfen\n",
                              CONTROL_MAX_OUTPUTS);
                break;
            }
            String shelly = entry["shelly"] | "";
            if (shelly.length() == 0) continue;
            shelly = outputKey(shelly);
            uint8_t channel = intFromJson(entry["channel"], 0, 0, SHELLY_MAX_CHANNELS - 1);

            bool duplicate = false;
            for (uint8_t i = 0; i < outputs.count; i++) {
                if (outputs.items[i].shelly == shelly && outputs.items[i].channel == channel) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            outputs.items[outputs.count].shelly  = shelly;
            outputs.items[outputs.count].channel = channel;
            outputs.count++;
        }
    }

    // --------------------------------------------------------------
    // LD2410
    // --------------------------------------------------------------

    /// @brief Schreibt die Gate-Empfindlichkeiten als Array in den Sensorknoten.
    /// @param node Knoten des LD2410
    /// @param gates Zu schreibende Gates
    static void gatesToJson(JsonObject node, const Ld2410GateCfg gates[]) {
        JsonArray arr = node["gates"].to<JsonArray>();
        for (uint8_t gate = 0; gate < LD2410_GATE_COUNT; gate++) {
            JsonObject g = arr.add<JsonObject>();
            if (gates[gate].moving != LD2410_SENSITIVITY_UNSET) {
                g["moving"] = gates[gate].moving;
            }
            if (gates[gate].stationary != LD2410_SENSITIVITY_UNSET) {
                g["stationary"] = gates[gate].stationary;
            }
        }
    }

    /// @brief Liest einen einzelnen Empfindlichkeitswert.
    /// @param value Feld aus dem JSON
    /// @return Empfindlichkeit 0-100 oder LD2410_SENSITIVITY_UNSET
    static uint8_t gateValueFromJson(JsonVariantConst value) {
        if (!value.is<int>()) return LD2410_SENSITIVITY_UNSET;
        int raw = value.as<int>();
        if (raw < 0 || raw > 100) return LD2410_SENSITIVITY_UNSET;
        return (uint8_t)raw;
    }

    /// @brief Liest die Gate-Empfindlichkeiten aus dem Sensorknoten.
    /// @param node Knoten des LD2410
    /// @param gates Ziel
    static void gatesFromJson(JsonObjectConst node, Ld2410GateCfg gates[]) {
        JsonArrayConst arr = node["gates"];
        if (arr.isNull()) return;

        uint8_t gate = 0;
        for (JsonObjectConst entry : arr) {
            if (gate >= LD2410_GATE_COUNT) {
                Serial.printf("[CONFIG] ld2410: mehr als %d Gates in der Datei, Rest verworfen\n",
                              LD2410_GATE_COUNT);
                break;
            }
            gates[gate].moving     = gateValueFromJson(entry["moving"]);
            gates[gate].stationary = gateValueFromJson(entry["stationary"]);
            gate++;
        }

        for (; gate < LD2410_GATE_COUNT; gate++) {
            gates[gate].moving     = LD2410_SENSITIVITY_UNSET;
            gates[gate].stationary = LD2410_SENSITIVITY_UNSET;
        }
    }

    // --------------------------------------------------------------
    // Kalibrierung
    // --------------------------------------------------------------

    /// @brief Schreibt die Kalibrierpunkte als Array in den Sensorknoten.
    /// @param parent Knoten des Sensors
    /// @param key Name des Arrays
    /// @param table Zu schreibende Punkte
    static void calibrationToJson(JsonObject parent, const char* key, const Calibration::Table& table) {
        JsonArray arr = parent[key].to<JsonArray>();
        for (uint8_t i = 0; i < table.count; i++) {
            JsonObject p = arr.add<JsonObject>();
            p["measured"]  = table.points[i].measured;
            p["reference"] = table.points[i].reference;
        }
    }

    /// @brief Liest die Kalibrierpunkte aus dem Sensorknoten.
    /// @param parent Knoten des Sensors
    /// @param key Name des Arrays
    /// @param table Ziel
    static void calibrationFromJson(JsonObjectConst parent, const char* key, Calibration::Table& table) {
        table.clear();

        JsonArrayConst arr = parent[key];
        for (JsonObjectConst p : arr) {
            if (!p["measured"].is<float>() || !p["reference"].is<float>()) {
                Serial.printf("[CONFIG] %s: unvollstaendiger Kalibrierpunkt uebersprungen\n", key);
                continue;
            }
            if (!table.add(p["measured"], p["reference"])) {
                Serial.printf("[CONFIG] %s: mehr als %d Kalibrierpunkte, Rest verworfen\n",
                              key, CALIBRATION_MAX_POINTS);
                break;
            }
        }
    }

    // --------------------------------------------------------------
    // Dateioperationen
    // --------------------------------------------------------------

    /// @brief Liest eine Konfigurationsdatei und übernimmt ihren Inhalt.
    /// @param path Pfad im LittleFS
    /// @return true wenn die Datei existierte und gültiges JSON enthielt
    static bool readFile(const char* path) {
        if (!configFs.exists(path)) return false;

        File f = configFs.open(path, "r");
        if (!f) {
            Serial.printf("[CONFIG] %s laesst sich nicht oeffnen\n", path);
            return false;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();

        if (err) {
            Serial.printf("[CONFIG] %s ist beschaedigt: %s\n", path, err.c_str());
            return false;
        }

        // Nur protokollieren. Dank der Vorgabewerte in fromJson() ist eine ältere Datei
        // lesbar, ohne dass hier nach Versionen verzweigt werden muss.
        Version fileVersion = Version::parse(doc["version"] | "0.0.0");
        if (fileVersion != version) {
            Serial.printf("[CONFIG] Datei stammt aus Version %s, Firmware ist %s\n",
                          fileVersion.toString().c_str(), version.toString().c_str());
        }

        fromJson(doc.as<JsonObjectConst>());
        Serial.printf("[CONFIG] Geladen aus %s\n", path);
        return true;
    }

    /// @brief Serialisiert die Laufzeitwerte in eine Datei.
    /// @param path Pfad im LittleFS
    /// @return true bei Erfolg
    static bool writeFile(const char* path) {
        JsonDocument doc;
        toJson(doc.to<JsonObject>());

        File f = configFs.open(path, "w");
        if (!f) {
            Serial.printf("[CONFIG] %s laesst sich nicht schreiben\n", path);
            return false;
        }

        // Eingerückt, damit die Datei beim Debuggen von Hand lesbar ist. Der Mehrverbrauch
        // fällt bei 200 KB Partition nicht ins Gewicht.
        size_t written = serializeJsonPretty(doc, f);
        f.close();

        if (written == 0) {
            Serial.printf("[CONFIG] %s: nichts geschrieben\n", path);
            configFs.remove(path);
            return false;
        }
        return true;
    }

    /// @brief Lädt die Konfiguration aus dem LittleFS in die Laufzeitvariablen.
    /// @return true wenn eine Datei gelesen werden konnte,
    /// false wenn die Vorgabewerte gelten
    bool loadConfig() {
        Serial.println(F("[CONFIG] Lade Konfiguration..."));
        setDefaults();

        if (!mountFs()) return false;

        if (readFile(PATH_JSON)) return true;

        // Hauptdatei fehlt oder ist beschädigt -> Sicherung versuchen.
        if (readFile(PATH_BAK)) {
            Serial.println(F("[CONFIG] Sicherung verwendet, schreibe sie als Hauptdatei zurueck"));
            saveConfig();
            return true;
        }

        // Gar nichts Brauchbares vorhanden: Vorgaben nehmen und eine Datei damit anlegen,
        // damit beim nächsten Start etwas zu lesen da ist.
        Serial.println(F("[CONFIG] Keine Konfiguration gefunden, lege Standarddatei an"));
        setDefaults();
        saveConfig();
        return false;
    }

    /// @brief Schreibt die Laufzeitvariablen als JSON in das LittleFS.
    /// @return true wenn die neue Fassung an ihrem Platz liegt
    bool saveConfig() {
        if (!mountFs()) return false;

        // Schritt 1: Neue Fassung erst in eine Zwischendatei schreiben. Bricht hier der Strom
        // weg, ist die bestehende Konfiguration unberührt.
        if (!writeFile(PATH_TMP)) {
            Serial.println(F("[CONFIG] Speichern fehlgeschlagen"));
            return false;
        }

        // Schritt 2: Bisherige Fassung zur Sicherung machen. Das Ziel wird vorher gelöscht,
        // weil rename() ein vorhandenes Ziel nicht zuverlässig überschreibt.
        if (configFs.exists(PATH_JSON)) {
            configFs.remove(PATH_BAK);
            configFs.rename(PATH_JSON, PATH_BAK);
        }

        // Schritt 3: Zwischendatei an ihren endgültigen Platz ziehen. Schlägt das fehl, bleibt
        // die Sicherung aus Schritt 2 liegen und wird beim nächsten Start verwendet.
        if (!configFs.rename(PATH_TMP, PATH_JSON)) {
            Serial.println(F("[CONFIG] Umbenennen fehlgeschlagen, Sicherung bleibt gueltig"));
            return false;
        }

        Serial.println(F("[CONFIG] Konfiguration gespeichert"));
        return true;
    }

    /// @brief Schreibt geänderte Parameter in das die aktuelle Konfiguration
    /// @param patch Geänderte Parameter
    /// @return true wenn es geschrieben wurde
    bool applyJson(JsonObjectConst patch) {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        toJson(root);
        mergeJson(root, patch);
        fromJson(root);

        return saveConfig();
    }

    /// @brief Setzt die Konfiguration auf den Auslieferungszustand zurück.
    /// @return true wenn die neue Vorgabedatei geschrieben werden konnte
    bool clearConfig() {
        Serial.println(F("[CONFIG] Setze Konfiguration zurueck..."));
        if (!mountFs()) return false;

        configFs.remove(PATH_JSON);
        configFs.remove(PATH_BAK);
        configFs.remove(PATH_TMP);

        setDefaults();
        return saveConfig();
    }

    // Beschreibung in config.h.
    size_t storageTotalBytes() { return mountFs() ? configFs.totalBytes() : 0; }
    size_t storageUsedBytes()  { return mountFs() ? configFs.usedBytes() : 0; }
}
