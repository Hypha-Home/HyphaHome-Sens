// ===================================================================
// File:            MyMQTT.cpp
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "MyMQTT.h"

// --------------------------------------------------------------
// Gemeinsamer Zustand, Zeiten und kleine Helfer
// --------------------------------------------------------------
namespace {

    WiFiClient wifiClient;

    PubSubClient mqttClient(wifiClient);

    String deviceId;

    String baseTopic;

    uint32_t lastReconnectAttempt = 0;

    uint32_t lastPublish = 0;

    uint32_t lastChangeCheck = 0;

    // Broker, mit dem mqtt aktuell verbunden ist.
    String activeTarget = "";

    // Von requestReconnect() gesetzt, vom loop() abgearbeitet. volatile, weil die beiden in
    // verschiedenen Tasks laufen.
    volatile bool configDirty = false;

    // Von requestHeatingSync() gesetzt (aus jedem Task), vom loop() abgearbeitet.
    volatile bool heatingSyncDirty = false;

    // Zeitpunkt, zu dem ein per MQTT geänderter Wert gespeichert wird; 0 = nichts offen.
    // Ein Schieberegler in Home Assistant schickt viele Werte kurz hintereinander, und jedes
    // Speichern verbraucht Flash-Zyklen. Gespeichert wird erst, wenn drei Sekunden Ruhe sind.
    uint32_t heatingSaveAt = 0;

    const uint32_t HEATING_SAVE_DELAY_MS = 3000;

    /// @brief Leitet aus der WLAN-MAC-Adresse einen kurzen, pro Gerät eindeutigen Suffix ab.
    /// Dient als Basis für Client-ID und Topic-Präfix, damit mehrere HHS-Geräte am
    /// selben Broker nicht kollidieren, analog zur Hotspot-Namensvergabe in WLANManager.
    /// @return Die letzten 6 Hex-Zeichen der MAC-Adresse ohne Trennzeichen
    String macSuffix() {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        return mac.substring(6);
    }

}

// --------------------------------------------------------------
// MQTT-Topics
// --------------------------------------------------------------
namespace {

    /// @brief Availability-Topic (Last-Will), über das Home Assistant erkennt ob das Gerät online oder offline ist.
    String topicAvailability() { return baseTopic + "/status"; }

    /// @brief Verfügbarkeits-Topic des BH1750.
    String topicAvailBh1750() { return baseTopic + "/bh1750/available"; }

    /// @brief Verfügbarkeits-Topic des SHT41.
    String topicAvailSht41() { return baseTopic + "/sht41/available"; }

    /// @brief Verfügbarkeits-Topic des SCD40.
    String topicAvailScd40() { return baseTopic + "/scd40/available"; }

    /// @brief Verfügbarkeits-Topic des AHT21.
    String topicAvailAht21() { return baseTopic + "/aht21/available"; }

    /// @brief Verfügbarkeits-Topic des ENS160.
    String topicAvailEns160() { return baseTopic + "/ens160/available"; }

    /// @brief Verfügbarkeits-Topic des LD2410.
    String topicAvailLd2410() { return baseTopic + "/ld2410/available"; }

    /// @brief Verfügbarkeits-Topic der zusammengefassten Luftqualität. Diese Entität braucht ein
    /// eigenes, weil sie aus zwei Quellen gespeist wird und schon dann einen gültigen Wert
    /// liefert, wenn nur einer der beiden Sensoren erreichbar ist.
    String topicAvailAirQuality() { return baseTopic + "/air_quality/available"; }

    /// @brief State-Topic für die vom SHT41 gemessene Temperatur.
    String topicTemperature() { return baseTopic + "/sht41/temperature"; }

    /// @brief State-Topic für die vom SHT41 gemessene relative Luftfeuchte.
    String topicHumidity() { return baseTopic + "/sht41/humidity"; }

    /// @brief State-Topic für den aus den gemittelten SHT41-Werten berechneten Taupunkt.
    String topicDewPoint() { return baseTopic + "/sht41/dew_point"; }

    /// @brief State-Topic für die vom BH1750 gemessene Beleuchtungsstärke.
    String topicLux() { return baseTopic + "/bh1750/lux"; }

    /// @brief State-Topic für den vom SCD40 gemessenen CO2-Wert (ppm).
    String topicCo2() { return baseTopic + "/scd40/co2"; }

    /// @brief State-Topic für die Rohtemperatur des SHT41, ohne Kalibrierung und Mittelwert.
    String topicTemperatureRaw() { return baseTopic + "/sht41/temperature_raw"; }

    /// @brief State-Topic für die kalibrierte Temperatur des SHT41, ohne Mittelwert.
    String topicTemperatureCalibrated() { return baseTopic + "/sht41/temperature_calibrated"; }

    /// @brief State-Topic für die rohe relative Luftfeuchte des SHT41.
    String topicHumidityRaw() { return baseTopic + "/sht41/humidity_raw"; }

    /// @brief State-Topic für die auf die kalibrierte Temperatur umgerechnete Luftfeuchte des SHT41, ohne Mittelwert.
    String topicHumidityCalibrated() { return baseTopic + "/sht41/humidity_calibrated"; }

    /// @brief State-Topic für die unkalibrierte Beleuchtungsstärke des BH1750.
    String topicLuxRaw() { return baseTopic + "/bh1750/lux_raw"; }

    /// @brief State-Topic für die vom SCD40 gemessene Temperatur.
    String topicScdTemperature() { return baseTopic + "/scd40/temperature"; }

    /// @brief State-Topic für die vom SCD40 gemessene relative Luftfeuchte.
    String topicScdHumidity() { return baseTopic + "/scd40/humidity"; }

    /// @brief State-Topic für den Brandindikator (Deaktiviert, OK, Verdacht, Wahrscheinlich).
    String topicFireIndicator() { return baseTopic + "/fire/indicator"; }

    /// @brief State-Topic für den TVOC-Anstieg der Brandfrüherkennung (ppb/min).
    String topicFireTvocRate() { return baseTopic + "/fire/tvoc_rate"; }

    /// @brief State-Topic für den CO2-Anstieg der Brandfrüherkennung (ppm/min).
    String topicFireCo2Rate() { return baseTopic + "/fire/co2_rate"; }

    /// @brief State-Topic für den Temperaturanstieg der Brandfrüherkennung (K/min).
    String topicFireTemperatureRate() { return baseTopic + "/fire/temperature_rate"; }

    /// @brief State-Topic für die Version der installierten Web-UI.
    String topicUiVersion() { return baseTopic + "/diagnostic/ui_version"; }

    /// @brief State-Topic für den Hersteller der installierten Web-UI.
    String topicUiVendor() { return baseTopic + "/diagnostic/ui_vendor"; }

    /// @brief State-Topic für die Chiptemperatur des ESP32.
    String topicCpuTemperature() { return baseTopic + "/diagnostic/cpu_temperature"; }

    /// @brief State-Topic für die WLAN-Signalstärke (dBm).
    String topicWifiRssi() { return baseTopic + "/diagnostic/wifi_rssi"; }

    /// @brief State-Topic für die vom AHT21 gemessene Temperatur.
    String topicAhtTemperature() { return baseTopic + "/aht21/temperature"; }

    /// @brief State-Topic für die vom AHT21 gemessene relative Luftfeuchte.
    String topicAhtHumidity() { return baseTopic + "/aht21/humidity"; }

    /// @brief State-Topic für die aus dem CO2-Wert abgeleitete Ampel-Stufe (Super..Hoch).
    String topicCo2Level() { return baseTopic + "/scd40/level"; }

    /// @brief State-Topic für die aus dem AQI abgeleitete Ampel-Stufe (Super..Hoch).
    String topicAqiLevel() { return baseTopic + "/ens160/level"; }

    /// @brief State-Topic für die zusammengefasste Luftqualität: das Maximum (die schlechtere
    /// Stufe) aus CO2-Stufe (SCD40) und AQI-Stufe (ENS160). Fasst co2_level und aqi_level zusammen,
    /// die beide standardmäßig deaktiviert sind.
    String topicAirQuality() { return baseTopic + "/air_quality"; }

    /// @brief State-Topic für die vom ENS160 gemessene TVOC-Konzentration (ppb).
    String topicTvoc() { return baseTopic + "/ens160/tvoc"; }

    /// @brief State-Topic für das vom ENS160 berechnete CO2-Äquivalent (ppm).
    String topicEco2() { return baseTopic + "/ens160/eco2"; }

    /// @brief State-Topic für den Betriebszustand des ENS160 (Aufwärmen/Erststart/Normal/Ungültig).
    /// Nur bei "Normal" sind AQI, TVOC und eCO2 belastbar.
    String topicEnsStatus() { return baseTopic + "/ens160/status"; }

    /// @brief State-Topic für die allgemeine Präsenzerkennung des LD2410.
    String topicPresence() { return baseTopic + "/ld2410/presence"; }

    /// @brief State-Topic für die Bewegungserkennung des LD2410.
    String topicMotion() { return baseTopic + "/ld2410/motion"; }

    /// @brief State-Topic für die Stillstandserkennung (stillsitzende Person) des LD2410.
    String topicStationary() { return baseTopic + "/ld2410/stationary"; }

    /// @brief State-Topic für die Entfernung des zuletzt erkannten bewegten Ziels.
    String topicMovingDistance() { return baseTopic + "/ld2410/moving_distance"; }

    /// @brief State-Topic für die Signalenergie des zuletzt erkannten bewegten Ziels.
    String topicMovingEnergy() { return baseTopic + "/ld2410/moving_energy"; }

    /// @brief State-Topic für die Entfernung des zuletzt erkannten stillstehenden Ziels.
    String topicStationaryDistance() { return baseTopic + "/ld2410/stationary_distance"; }

    /// @brief State-Topic für die Signalenergie des zuletzt erkannten stillstehenden Ziels.
    String topicStationaryEnergy() { return baseTopic + "/ld2410/stationary_energy"; }

    /// @brief State-Topic der Zieltemperatur der Heizung; Befehle gehen an <topic>/set.
    String topicHeatingTarget()  { return baseTopic + "/heating/target"; }

    /// @brief State-Topic der Einschalthysterese der Heizung (Minuten); Befehle gehen an <topic>/set.
    String topicHeatingHystOn()  { return baseTopic + "/heating/hysteresis_on"; }

    /// @brief State-Topic der Ausschalthysterese der Heizung (Minuten); Befehle gehen an <topic>/set.
    String topicHeatingHystOff() { return baseTopic + "/heating/hysteresis_off"; }

    /// @brief State-Topic für den Soll-Zustand der Heizung.
    String topicHeatingDesired() { return baseTopic + "/heating/desired"; }

    /// @brief State-Topic für den Ist-Zustand der Heizung an den Shelly-Kanälen.
    String topicHeatingActual()  { return baseTopic + "/heating/actual"; }

}

// --------------------------------------------------------------
// Home Assistant Discovery: Bausteine
// --------------------------------------------------------------
namespace {

    /// @brief Trägt den gemeinsamen "device"-Block (Identifiers, Name, Hersteller, Modell,
    /// Firmwareversion, MAC, Seriennummer, Weboberfläche) in ein Discovery-JSON-Objekt ein.
    /// Nur dadurch ordnet Home Assistant alle veröffentlichten Entitäten demselben physischen
    /// Gerät zu, statt für jede Entität ein eigenes Gerät anzulegen.
    ///
    /// Als Gerätename dient der konfigurierte Hostname (Standard "HyphaHome-Sens-<mac>", im
    /// Webinterface änderbar) und nicht die deviceId: HA bildet die Entity-IDs aus Gerätename +
    /// Entity-Name, "HyphaHome-Sens-<suffix>" ergäbe doppelt "HyphaHome-Sens". Ein geänderter Hostname
    /// erreicht HA beim nächsten (Re-)Connect, da die Discovery dann neu gesendet wird.
    /// @param device JSON-Objekt, in das der device-Block geschrieben wird
    void addDevice(JsonObject device) {
        String mac = WiFi.macAddress();
        String macLower = mac;
        macLower.toLowerCase();
        mac.replace(":", "");

        JsonArray identifiers = device["identifiers"].to<JsonArray>();
        identifiers.add(deviceId);
        JsonArray connection = device["connections"].to<JsonArray>().add<JsonArray>();
        connection.add("mac");
        connection.add(macLower);

        device["name"] = Config::system.hostname.length() > 0 ? Config::system.hostname : "HyphaHome-Sens-" + macSuffix();
        device["manufacturer"] = "EideardVMR";
        device["model"] = "Hypha Home - Sens";
        device["serial_number"] = mac;
        device["sw_version"] = Config::version.toString();

        // "Besuchen"-Button in HA. addDevice() läuft nur nach einem (Re-)Connect, also bei
        // bestehender WLAN-Verbindung; die Discovery wird dabei neu gesendet und hält die IP aktuell.
        IPAddress ip = WiFi.localIP();
        if (ip != IPAddress(0, 0, 0, 0)) {
            device["configuration_url"] = "http://" + ip.toString();
        }
    }

    /// @brief Serialisiert ein fertiges Discovery-JSON-Dokument und veröffentlicht es retained
    /// unter dem von Home Assistant erwarteten Discovery-Topic. Retained ist hier Pflicht,
    /// da HA den Discovery-Config nur beim (Neu-)Verbinden des eigenen MQTT-Brokers abruft.
    /// @param component HA-Komponententyp (z.B. "sensor", "binary_sensor", "number")
    /// @param objectId  Eindeutige Objekt-ID der Entität innerhalb dieses Geräts
    /// @param doc       Fertig befülltes Discovery-JSON-Dokument
    void publishDiscoveryDoc(const char* component, const String& objectId, JsonDocument& doc) {
        String topic = "homeassistant/" + String(component) + "/" + deviceId + "/" + objectId + "/config";
        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    /// @brief Trägt die Verfügbarkeitsangaben in ein Discovery-Dokument ein.
    ///
    /// Ohne sensorAvailTopic hängt die Entität nur am geräteweiten Availability-Topic. Mit
    /// sensorAvailTopic werden beide Topics als Liste eingetragen und über availability_mode
    /// "all" verknüpft: Die Entität gilt nur dann als verfügbar, wenn das Gerät online ist und
    /// der Sensor antwortet. "availability_topic" und "availability" schließen sich in Home
    /// Assistant gegenseitig aus, deshalb entweder das eine oder das andere.
    ///
    /// Die Payloads müssen nicht angegeben werden: HA erwartet standardmäßig "online" und
    /// "offline", und genau die werden hier gesendet.
    /// @param doc               Discovery-Dokument
    /// @param sensorAvailTopic  Verfügbarkeits-Topic des Sensors, leer für geräteweit
    void addAvailability(JsonDocument& doc, const String& sensorAvailTopic) {
        if (sensorAvailTopic.isEmpty()) {
            doc["availability_topic"] = topicAvailability();
            return;
        }
        JsonArray availability = doc["availability"].to<JsonArray>();
        availability.add<JsonObject>()["topic"] = topicAvailability();
        availability.add<JsonObject>()["topic"] = sensorAvailTopic;
        doc["availability_mode"] = "all";
    }

    /// @brief Baut und veröffentlicht die Discovery-Config für eine schreibgeschützte
    /// "sensor"-Entität in Home Assistant.
    /// @param objectId      Eindeutige Objekt-ID der Entität
    /// @param name          Anzeigename in Home Assistant
    /// @param stateTopic    Topic, unter dem der aktuelle Wert veröffentlicht wird
    /// @param deviceClass   HA device_class (z.B. "temperature"), optional
    /// @param unit          Einheit (z.B. "°C"), optional
    /// @param entityCategory HA entity_category (z.B. "diagnostic"), optional
    /// @param enabledByDefault false = HA legt die Entität deaktiviert an (nur beim ersten Anlegen
    ///                         wirksam, im HA-Frontend jederzeit aktivierbar)
    /// @param sensorAvailTopic Verfügbarkeits-Topic des zugehörigen Sensors (siehe addAvailability)
    void publishSensorDiscovery(const String& objectId, const String& name, const String& stateTopic,
                                        const char* deviceClass = nullptr, const char* unit = nullptr,
                                        const char* entityCategory = nullptr, bool enabledByDefault = true,
                                        const String& sensorAvailTopic = "") {
        JsonDocument doc;
        doc["name"] = name;
        doc["unique_id"] = deviceId + "_" + objectId;
        doc["state_topic"] = stateTopic;
        addAvailability(doc, sensorAvailTopic);
        if (deviceClass)    doc["device_class"] = deviceClass;
        if (unit)           doc["unit_of_measurement"] = unit;
        if (entityCategory) doc["entity_category"] = entityCategory;
        if (!enabledByDefault) doc["enabled_by_default"] = false;
        addDevice(doc["device"].to<JsonObject>());
        publishDiscoveryDoc("sensor", objectId, doc);
    }

    /// @brief Baut und veröffentlicht die Discovery-Config für eine "binary_sensor"-Entität
    /// (ON/OFF-Zustände wie Präsenz/Bewegung) in Home Assistant.
    /// @param objectId    Eindeutige Objekt-ID der Entität
    /// @param name        Anzeigename in Home Assistant
    /// @param stateTopic  Topic, unter dem "ON"/"OFF" veröffentlicht wird
    /// @param deviceClass HA device_class (z.B. "motion"), optional
    /// @param enabledByDefault false = HA legt die Entität deaktiviert an (siehe publishSensorDiscovery)
    /// @param sensorAvailTopic Verfügbarkeits-Topic des zugehörigen Sensors (siehe addAvailability)
    void publishBinarySensorDiscovery(const String& objectId, const String& name, const String& stateTopic,
                                              const char* deviceClass = nullptr, bool enabledByDefault = true,
                                              const String& sensorAvailTopic = "") {
        JsonDocument doc;
        doc["name"] = name;
        doc["unique_id"] = deviceId + "_" + objectId;
        doc["state_topic"] = stateTopic;
        doc["payload_on"] = "ON";
        doc["payload_off"] = "OFF";
        addAvailability(doc, sensorAvailTopic);
        if (deviceClass) doc["device_class"] = deviceClass;
        if (!enabledByDefault) doc["enabled_by_default"] = false;
        addDevice(doc["device"].to<JsonObject>());
        publishDiscoveryDoc("binary_sensor", objectId, doc);
    }

    /// @brief Baut und veröffentlicht die Discovery-Config für eine einstellbare "number"-Entität.
    void publishNumberDiscovery(const String& objectId, const String& name, const String& stateTopic,
                                        float minValue, float maxValue, float step, const char* unit,
                                        const char* deviceClass, const char* entityCategory, const char* mode) {
        JsonDocument doc;
        doc["name"] = name;
        doc["unique_id"] = deviceId + "_" + objectId;
        doc["state_topic"] = stateTopic;
        doc["command_topic"] = stateTopic + "/set";
        doc["min"] = minValue;
        doc["max"] = maxValue;
        doc["step"] = step;
        doc["mode"] = mode;
        if (unit)           doc["unit_of_measurement"] = unit;
        if (deviceClass)    doc["device_class"] = deviceClass;
        if (entityCategory) doc["entity_category"] = entityCategory;
        addAvailability(doc, "");
        addDevice(doc["device"].to<JsonObject>());
        publishDiscoveryDoc("number", objectId, doc);
    }

    /// @brief Löscht eine Entität in Home Assistant: leere retained Nachricht auf ihre Discovery
    /// und auf ihr Wert-Topic.
    void removeEntity(const char* component, const String& objectId, const String& stateTopic) {
        const String config = "homeassistant/" + String(component) + "/" + deviceId + "/" + objectId + "/config";
        mqttClient.publish(config.c_str(), (const uint8_t*)"", 0, true);
        mqttClient.publish(stateTopic.c_str(), (const uint8_t*)"", 0, true);
    }

}

// --------------------------------------------------------------
// Heizung: einstellbare Werte und Rückmeldung
// --------------------------------------------------------------
namespace {

    //
    // Drei "number"-Entitäten in Home Assistant, die es nur gibt, solange die Heizungssteuerung
    // eingeschaltet ist: Schaltet man sie aus, wird ihre Discovery-Meldung gelöscht, und die
    // Entitäten verschwinden. Geschrieben wird auf <topic>/set, der Wert kommt retained unter
    // <topic> zurück, damit Home Assistant ihn nach einem Neustart sofort kennt.

    /// @brief Legt die Heizungs-Entitäten an, wenn die Steuerung an ist, sonst entfernt es sie.
    void publishHeatingDiscovery() {
        if (!Config::controls.heating.enabled) {
            removeEntity("number", "heating_target", topicHeatingTarget());
            removeEntity("number", "heating_hysteresis_on", topicHeatingHystOn());
            removeEntity("number", "heating_hysteresis_off", topicHeatingHystOff());
            removeEntity("sensor", "heating_desired", topicHeatingDesired());
            removeEntity("sensor", "heating_actual", topicHeatingActual());
            return;
        }
        publishSensorDiscovery("heating_desired", "Heizung Soll-Zustand", topicHeatingDesired());
        publishSensorDiscovery("heating_actual", "Heizung Ist-Zustand", topicHeatingActual());
        publishNumberDiscovery("heating_target", "Heizung Zieltemperatur", topicHeatingTarget(),
                               HEATING_TARGET_MIN_C, HEATING_TARGET_MAX_C, 0.5f, "°C", "temperature", nullptr, "box");
        publishNumberDiscovery("heating_hysteresis_on", "Heizung Einschalthysterese", topicHeatingHystOn(),
                               0, 1440, 1, "min", "duration", "config", "box");
        publishNumberDiscovery("heating_hysteresis_off", "Heizung Ausschalthysterese", topicHeatingHystOff(),
                               0, 1440, 1, "min", "duration", "config", "box");
    }

    /// @brief Meldet die aktuellen Werte zurück (retained). Nur bei eingeschalteter Steuerung.
    void publishHeatingState() {
        if (!Config::controls.heating.enabled) return;
        const Config::HeatingCfg& heat = Config::controls.heating;
        mqttClient.publish(topicHeatingTarget().c_str(), String((double)heat.targetTemp, 1U).c_str(), true);
        mqttClient.publish(topicHeatingHystOn().c_str(), String((unsigned)heat.hysteresisOnMin).c_str(), true);
        mqttClient.publish(topicHeatingHystOff().c_str(), String((unsigned)heat.hysteresisOffMin).c_str(), true);
    }

    void subscribeHeating() {
        mqttClient.subscribe((topicHeatingTarget() + "/set").c_str());
        mqttClient.subscribe((topicHeatingHystOn() + "/set").c_str());
        mqttClient.subscribe((topicHeatingHystOff() + "/set").c_str());
    }

    /// @brief Nimmt Befehle für die Heizung an. Ungültige oder unpassende Werte werden
    /// verworfen und der gültige Wert zurückgemeldet, damit Home Assistant nicht auf einem
    /// Wert stehen bleibt, den das Gerät nie übernommen hat.
    void onMessage(char* topic, uint8_t* payload, unsigned int length) {
        if (!Config::controls.heating.enabled) return;

        char text[16];
        if (length == 0 || length >= sizeof(text)) return;
        memcpy(text, payload, length);
        text[length] = 0;
        char* end = nullptr;
        const float value = strtof(text, &end);
        const bool numeric = end != text && *end == 0 && !isnan(value);

        Config::HeatingCfg& heat = Config::controls.heating;
        const String t(topic);
        bool changed = false;

        if (t == topicHeatingTarget() + "/set") {
            const float rounded = roundf(value * 10.0f) / 10.0f;
            if (numeric && rounded >= HEATING_TARGET_MIN_C && rounded <= HEATING_TARGET_MAX_C) {
                changed = rounded != heat.targetTemp;
                heat.targetTemp = rounded;
            }
        } else if (t == topicHeatingHystOn() + "/set" || t == topicHeatingHystOff() + "/set") {
            const float rounded = roundf(value);
            if (numeric && rounded >= 0 && rounded <= 1440) {
                uint16_t& field = (t == topicHeatingHystOn() + "/set") ? heat.hysteresisOnMin : heat.hysteresisOffMin;
                changed = (uint16_t)rounded != field;
                field = (uint16_t)rounded;
            }
        } else {
            return;
        }

        if (changed) {
            heatingSaveAt = millis() + HEATING_SAVE_DELAY_MS;
            if (heatingSaveAt == 0) heatingSaveAt = 1;
        }
        publishHeatingState();
    }

}

// --------------------------------------------------------------
// Home Assistant Discovery: Gesamtkonfiguration
// --------------------------------------------------------------
namespace {

    /// @brief Veröffentlicht die komplette Home-Assistant-Discovery-Konfiguration für alle
    /// Entitäten dieses Geräts (Umweltsensoren und LD2410-Zustände). Wird nach jedem (Re-)Connect zum Broker
    /// aufgerufen, damit HA das Gerät auch nach einem Broker-Neustart ohne manuelles Anlegen
    /// in der configuration.yaml wiedererkennt.
    void publishDiscovery() {
        publishSensorDiscovery("temperature", "Temperatur", topicTemperature(), "temperature", "°C", nullptr, true, topicAvailSht41());
        publishSensorDiscovery("humidity", "Rel. Luftfeuchte", topicHumidity(), "humidity", "%", nullptr, true, topicAvailSht41());
        publishSensorDiscovery("dew_point", "Taupunkt", topicDewPoint(), "temperature", "°C", nullptr, true, topicAvailSht41());
        publishSensorDiscovery("illuminance", "Helligkeit", topicLux(), "illuminance", "lx", nullptr, true, topicAvailBh1750());
        publishSensorDiscovery("co2", "CO2", topicCo2(), "carbon_dioxide", "ppm", nullptr, true, topicAvailScd40());

        // Rohwerte, kalibrierte Einzelwerte und die Werte des SCD40: braucht kaum jemand, deshalb
        // standardmäßig deaktiviert (nur beim ersten Anlegen wirksam, in HA jederzeit aktivierbar).
        publishSensorDiscovery("temperature_raw", "Temperatur (roh)", topicTemperatureRaw(), "temperature", "°C", nullptr, false, topicAvailSht41());
        publishSensorDiscovery("temperature_calibrated", "Temperatur (kalibriert)", topicTemperatureCalibrated(), "temperature", "°C", nullptr, false, topicAvailSht41());
        publishSensorDiscovery("humidity_raw", "Rel. Luftfeuchte (roh)", topicHumidityRaw(), "humidity", "%", nullptr, false, topicAvailSht41());
        publishSensorDiscovery("humidity_calibrated", "Rel. Luftfeuchte (kalibriert)", topicHumidityCalibrated(), "humidity", "%", nullptr, false, topicAvailSht41());
        publishSensorDiscovery("illuminance_raw", "Helligkeit (roh)", topicLuxRaw(), "illuminance", "lx", nullptr, false, topicAvailBh1750());
        publishSensorDiscovery("scd40_temperature", "Temperatur (SCD40)", topicScdTemperature(), "temperature", "°C", nullptr, false, topicAvailScd40());
        publishSensorDiscovery("scd40_humidity", "Rel. Luftfeuchte (SCD40)", topicScdHumidity(), "humidity", "%", nullptr, false, topicAvailScd40());

        // Brandindikator: "Deaktiviert", "OK", "Verdacht", "Wahrscheinlich". Die Anstiege sind
        // Hilfswerte zum Abstimmen der Schwellen und standardmäßig deaktiviert.
        publishSensorDiscovery("fire_indicator", "Brandindikator", topicFireIndicator(), nullptr, nullptr, nullptr, true);
        publishSensorDiscovery("fire_tvoc_rate", "Brand TVOC-Anstieg", topicFireTvocRate(), nullptr, "ppb/min", "diagnostic", false);
        publishSensorDiscovery("fire_co2_rate", "Brand CO2-Anstieg", topicFireCo2Rate(), nullptr, "ppm/min", "diagnostic", false);
        publishSensorDiscovery("fire_temperature_rate", "Brand Temperaturanstieg", topicFireTemperatureRate(), nullptr, "K/min", "diagnostic", false);

        // Systeminfos unter "Diagnose". Hängen nur am Gerät, nicht an einem Sensor.
        publishSensorDiscovery("ui_version", "UI Version", topicUiVersion(), nullptr, nullptr, "diagnostic", true);
        publishSensorDiscovery("ui_vendor", "UI Hersteller", topicUiVendor(), nullptr, nullptr, "diagnostic", true);
        publishSensorDiscovery("cpu_temperature", "CPU Temperatur", topicCpuTemperature(), "temperature", "°C", "diagnostic", true);
        publishSensorDiscovery("wifi_rssi", "WLAN Signalstärke", topicWifiRssi(), "signal_strength", "dBm", "diagnostic", true);

        // AHT21 doppelt den SHT41 und liegt neben der ENS160-Heizung -> standardmäßig deaktiviert
        publishSensorDiscovery("aht21_temperature", "Temperatur (AHT21)", topicAhtTemperature(), "temperature", "°C", nullptr, false, topicAvailAht21());
        publishSensorDiscovery("aht21_humidity", "Rel. Luftfeuchte (AHT21)", topicAhtHumidity(), "humidity", "%", nullptr, false, topicAvailAht21());
        // Die beiden Einzelstufen (CO2 Stufe, AQI Stufe) haben dieselbe Schreibweise und sind
        // standardmäßig deaktiviert, weil "Luftqualität" sie schon zusammenfasst. Den
        // numerischen Index (1-5) gibt es nicht mehr: Die Stufe enthält dieselbe Information.
        publishSensorDiscovery("co2_level", "CO2 Stufe", topicCo2Level(), nullptr, nullptr, nullptr, false, topicAvailScd40());
        publishSensorDiscovery("aqi_level", "AQI Stufe", topicAqiLevel(), nullptr, nullptr, nullptr, false, topicAvailEns160());
        // Fasst co2_level und aqi_level zusammen: die schlechtere (höhere) der beiden Ampel-Stufen.
        // Eigenes Verfügbarkeits-Topic, weil schon eine der beiden Quellen für einen gültigen
        // Wert reicht.
        publishSensorDiscovery("air_quality", "Luftqualität", topicAirQuality(), nullptr, nullptr, nullptr, true, topicAvailAirQuality());
        publishSensorDiscovery("tvoc", "TVOC", topicTvoc(), nullptr, "ppb", nullptr, true, topicAvailEns160());
        publishSensorDiscovery("eco2", "eCO2", topicEco2(), "carbon_dioxide", "ppm", nullptr, false, topicAvailEns160());
        publishSensorDiscovery("ens160_status", "ENS160 Status", topicEnsStatus(), nullptr, nullptr, "diagnostic", true, topicAvailEns160());

        // Allgemeine Präsenz ist nur Bewegung ODER Stillsitzen und damit redundant
        // device_class "presence" zeigt in HA "Zuhause/Abwesend" an, "occupancy" dagegen "Erkannt/Frei".
        publishBinarySensorDiscovery("presence", "Präsenz", topicPresence(), "occupancy", false, topicAvailLd2410());
        publishBinarySensorDiscovery("motion", "Bewegung erkannt", topicMotion(), "motion", false, topicAvailLd2410());
        publishBinarySensorDiscovery("stationary", "Stillsitzen erkannt", topicStationary(), "occupancy", false, topicAvailLd2410());

        publishSensorDiscovery("moving_distance", "Bewegung Entfernung", topicMovingDistance(), nullptr, "cm", nullptr, false, topicAvailLd2410());
        publishSensorDiscovery("moving_energy", "Bewegung Energie", topicMovingEnergy(), nullptr, "%", nullptr, false, topicAvailLd2410());
        publishSensorDiscovery("stationary_distance", "Stillstand Entfernung", topicStationaryDistance(), nullptr, "cm", nullptr, false, topicAvailLd2410());
        publishSensorDiscovery("stationary_energy", "Stillstand Energie", topicStationaryEnergy(), nullptr, "%", nullptr, false, topicAvailLd2410());

        publishHeatingDiscovery();

    }

    /// @brief Entfernt die früheren Konfigurations-Entitäten des LD2410 aus Home Assistant.
    ///
    /// Gates, Nachlaufzeit und Max-Gates waren einmal über MQTT einstellbar. Die Discovery-
    /// Nachrichten dazu liegen retained im Broker und würden als tote Entitäten in Home
    /// Assistant stehen bleiben. Eine leere retained Nachricht auf dasselbe Topic löscht sie.
    /// Läuft einmal pro Start; bei einem Gerät, das nie die alte Firmware hatte, ist es wirkungslos.
    void removeLegacyLd2410Entities() {
        static bool done = false;
        if (done) return;
        done = true;

        auto clear = [](const String& topic) {
            mqttClient.publish(topic.c_str(), (const uint8_t*)"", 0, true);
        };
        auto clearEntity = [&](const char* component, const String& objectId) {
            clear("homeassistant/" + String(component) + "/" + deviceId + "/" + objectId + "/config");
        };

        const String cfg = baseTopic + "/ld2410/config/";
        clearEntity("sensor", "max_gate");
        clearEntity("sensor", "aqi");            // der numerische Luftqualitätsindex entfällt
        clear(cfg + "max_gate");
        const char* single[] = { "max_moving_gate", "max_stationary_gate", "idle_time" };
        for (const char* name : single) {
            clearEntity("number", name);
            clear(cfg + name);
        }
        for (uint8_t gate = 0; gate <= 8; gate++) {
            const String g = String(gate);
            clearEntity("number", "gate" + g + "_moving_sensitivity");
            clearEntity("number", "gate" + g + "_stationary_sensitivity");
            clear(cfg + "gate/" + g + "/moving_sensitivity");
            clear(cfg + "gate/" + g + "/stationary_sensitivity");
        }
    }

}

// --------------------------------------------------------------
// Verfügbarkeit der Sensoren
// --------------------------------------------------------------
namespace {

    /// @brief Veröffentlicht "online"/"offline" für ein einzelnes Verfügbarkeits-Topic, aber nur
    /// wenn sich der Zustand geändert hat. Retained, damit Home Assistant nach einem eigenen
    /// Neustart sofort den richtigen Zustand kennt und nicht bis zur nächsten Änderung wartet.
    /// @param topic     Verfügbarkeits-Topic
    /// @param available Aktueller Zustand des Sensors
    /// @param lastState Zuletzt gesendeter Zustand (wird aktualisiert)
    /// @param force     true sendet unabhängig von einer Änderung (nach einem Reconnect nötig)
    void publishAvail(const String& topic, bool available, bool& lastState, bool force) {
        if (!force && lastState == available) return;
        lastState = available;
        mqttClient.publish(topic.c_str(), available ? "online" : "offline", true);
    }

    /// @brief Veröffentlicht die Verfügbarkeit aller Sensoren. Wird nach jedem (Re-)Connect
    /// einmal mit force=true aufgerufen und danach bei jedem State-Publish, wo nur noch
    /// tatsächliche Änderungen gesendet werden.
    /// @param force true sendet alle Zustände unabhängig von Änderungen
    void publishSensorAvailability(bool force) {
        static bool lastBh1750 = false;
        static bool lastSht41 = false;
        static bool lastScd40 = false;
        static bool lastAht21 = false;
        static bool lastEns160 = false;
        static bool lastLd2410 = false;
        static bool lastAirQuality = false;

        publishAvail(topicAvailBh1750(), MyBH1750::isConnected, lastBh1750, force);
        publishAvail(topicAvailSht41(),  MySHT41::isConnected,  lastSht41,  force);
        publishAvail(topicAvailScd40(),  MySCD40::isConnected,  lastScd40,  force);
        publishAvail(topicAvailAht21(),  MyAHT21::isConnected, lastAht21, force);
        publishAvail(topicAvailEns160(), MyENS160::isConnected, lastEns160, force);
        publishAvail(topicAvailLd2410(), MyLD2410::isConnected, lastLd2410, force);

        // Die zusammengefasste Luftqualität braucht nur eine der beiden Quellen.
        bool airQualityOk = MySCD40::isConnected || MyENS160::isConnected;
        publishAvail(topicAvailAirQuality(), airQualityOk, lastAirQuality, force);
    }

}

// --------------------------------------------------------------
// Messwerte veröffentlichen
// --------------------------------------------------------------
namespace {

    /// @brief Ordnet eine SCD40-Ampel-Stufe in eine über beide Sensoren vergleichbare Rangzahl
    /// ein (0 = Super ... 4 = Hoch), damit sich das Maximum aus CO2- und AQI-Stufe bilden lässt.
    int8_t levelRank(MySCD40::Level l) {
        switch (l) {
            case MySCD40::Level::Super:    return 0;
            case MySCD40::Level::Gut:      return 1;
            case MySCD40::Level::Mittel:   return 2;
            case MySCD40::Level::Schlecht: return 3;
            case MySCD40::Level::Hoch:     return 4;
        }
        return -1;
    }

    /// @brief Ordnet eine ENS160-Ampel-Stufe in dieselbe Rangzahl wie levelRank(MySCD40::Level)
    /// ein. "Unbekannt" (noch kein belastbarer Wert) liefert -1 und fließt damit nicht ein.
    int8_t levelRank(MyENS160::Level l) {
        switch (l) {
            case MyENS160::Level::Super:     return 0;
            case MyENS160::Level::Gut:       return 1;
            case MyENS160::Level::Mittel:    return 2;
            case MyENS160::Level::Schlecht:  return 3;
            case MyENS160::Level::Hoch:      return 4;
            case MyENS160::Level::Unbekannt: return -1;
        }
        return -1;
    }

    //
    // Jeder Messwert hat einen Platz im Zwischenspeicher. Der volle Durchlauf (alle 5 s)
    // sendet alles und merkt sich, was er gesendet hat. Der schnelle Durchlauf dazwischen
    // vergleicht mit diesem Stand und sendet nur, was abweicht.
    //
    // Verglichen wird mit dem zuletzt GESENDETEN Wert, nicht mit der letzten Messung. Sonst
    // würde ein Wert, der in vielen kleinen Schritten driftet, nie die Schwelle überschreiten.

    enum Slot : uint8_t {
        SLOT_TEMPERATURE, SLOT_HUMIDITY, SLOT_DEW_POINT, SLOT_LUX,
        SLOT_TEMP_RAW, SLOT_TEMP_CAL, SLOT_HUM_RAW, SLOT_HUM_CAL, SLOT_LUX_RAW,
        SLOT_SCD_TEMPERATURE, SLOT_SCD_HUMIDITY,
        SLOT_UI_VERSION, SLOT_UI_VENDOR, SLOT_CPU_TEMP, SLOT_RSSI,
        SLOT_FIRE, SLOT_FIRE_TVOC_RATE, SLOT_FIRE_CO2_RATE, SLOT_FIRE_TEMP_RATE,
        SLOT_CO2, SLOT_CO2_LEVEL, SLOT_AIR_QUALITY,
        SLOT_AHT_TEMPERATURE, SLOT_AHT_HUMIDITY,
        SLOT_ENS_STATUS, SLOT_AQI_LEVEL, SLOT_TVOC, SLOT_ECO2,
        SLOT_PRESENCE, SLOT_MOTION, SLOT_STATIONARY,
        SLOT_MOVING_DISTANCE, SLOT_MOVING_ENERGY, SLOT_STATIONARY_DISTANCE, SLOT_STATIONARY_ENERGY,
        SLOT_HEATING_DESIRED, SLOT_HEATING_ACTUAL,
        SLOT_COUNT
    };

    struct SentValue {
        bool  valid  = false;   ///< false, solange noch nichts gesendet wurde
        float number = 0;       ///< zuletzt gesendeter Zahlenwert
        char  text[48] = "";    ///< zuletzt gesendeter Text (lang genug für den UI-Hersteller)
    };

    SentValue sent[SLOT_COUNT];

    /// @brief Sendet eine Zahl, wenn sie sich seit dem letzten Senden genug geändert hat.
    ///
    /// Was als "genug" gilt, ist die größere der beiden Schwellen: absolut (`minDelta`) oder
    /// relativ zum zuletzt gesendeten Wert (`minRelative`, 0,05 = 5 %). Die absolute Schwelle
    /// hält das Messrauschen zurück, die relative sorgt dafür, dass große Werte (Lux) nicht
    /// wegen jeder Kleinigkeit gesendet werden.
    ///
    /// @param full true sendet in jedem Fall (voller Durchlauf)
    void sendNumber(Slot slot, const String& topic, float value, uint8_t decimals,
                           bool full, float minDelta, float minRelative = 0) {
        SentValue& last = sent[slot];
        if (!full && last.valid) {
            const float threshold = max(minDelta, minRelative * fabsf(last.number));
            if (fabsf(value - last.number) < threshold) return;
        }
        mqttClient.publish(topic.c_str(), String((double)value, (unsigned int)decimals).c_str());
        last.valid  = true;
        last.number = value;
    }

    /// @brief Sendet einen Text, wenn er sich seit dem letzten Senden geändert hat.
    /// @param full true sendet in jedem Fall (voller Durchlauf)
    void sendText(Slot slot, const String& topic, const char* text, bool full) {
        SentValue& last = sent[slot];
        if (!full && last.valid && strcmp(last.text, text) == 0) return;
        mqttClient.publish(topic.c_str(), text);
        last.valid = true;
        strncpy(last.text, text, sizeof(last.text) - 1);
        last.text[sizeof(last.text) - 1] = '\0';
    }

    /// @brief Veröffentlicht die zusammengefasste Luftqualität: die schlechtere (höhere) der
    /// beiden Ampel-Stufen aus CO2-Wert (SCD40) und Luftqualitätsindex (ENS160). Liegt nur eine
    /// der beiden Quellen vor (z.B. ENS160 noch in der Aufwärmphase), wird nur diese verwendet;
    /// liegt keine vor, wird nichts veröffentlicht.
    void publishAirQuality(bool full) {
        static const char* LEVEL_NAMES[5] = { "Super", "Gut", "Mittel", "Schlecht", "Hoch" };

        int8_t co2Rank = MySCD40::isConnected ? levelRank(MySCD40::level) : (int8_t)-1;
        int8_t aqiRank = (MyENS160::isConnected && MyENS160::aqi > 0)
                             ? levelRank(MyENS160::level)
                             : (int8_t)-1;
        int8_t worst = max(co2Rank, aqiRank);
        if (worst >= 0) {
            sendText(SLOT_AIR_QUALITY, topicAirQuality(), LEVEL_NAMES[worst], full);
        }
    }

    /// @brief Veröffentlicht die Messwerte aller Sensoren (SHT41, BH1750, SCD40, ENS160/AHT21,
    /// LD2410) auf ihre State-Topics.
    ///
    /// @param full true sendet jeden Wert, auch unveränderte. Das geschieht alle
    /// MQTT_PUBLISH_INTERVAL_MS und direkt nach jedem (Re-)Connect, damit Home Assistant nicht auf den
    /// ersten Wert warten muss. false sendet nur, was sich seit dem letzten Senden merklich
    /// geändert hat; Aufruf alle MQTT_CHANGE_CHECK_INTERVAL_MS.
    ///
    /// Die Schwellen liegen über dem Messrauschen der Sensoren. Distanz und Energie des LD2410
    /// schwanken mit jedem Frame und gehen deshalb nur im vollen Durchlauf hinaus. Präsenz,
    /// Bewegung und Stillstand dagegen sofort.
    void publishState(bool full) {
        // Zuerst die Verfügbarkeit: Fällt ein Sensor aus, soll Home Assistant seine Entitäten
        // als "nicht verfügbar" anzeigen, statt den letzten Wert weiter als gültig zu führen.
        // Sendet ohnehin nur bei Änderung.
        publishSensorAvailability(false);

        // Heizung: Soll- und Ist-Zustand, solange die Steuerung läuft. Wörter wie bei den
        // übrigen Stufen (Luftqualität, Brandindikator) auf Deutsch.
        if (Config::controls.heating.enabled) {
            const HeatingControl::Status heat = HeatingControl::status();
            const char* desired = "Unbekannt";
            if (heat.desired == HeatingControl::Desired::On)  desired = "Ein";
            if (heat.desired == HeatingControl::Desired::Off) desired = "Aus";
            const char* actual = "Unbekannt";
            switch (heat.actual) {
                case HeatingControl::Actual::On:          actual = "Ein"; break;
                case HeatingControl::Actual::Off:         actual = "Aus"; break;
                case HeatingControl::Actual::Mixed:       actual = "Gemischt"; break;
                case HeatingControl::Actual::Unreachable: actual = "Nicht erreichbar"; break;
                default: break;
            }
            sendText(SLOT_HEATING_DESIRED, topicHeatingDesired(), desired, full);
            sendText(SLOT_HEATING_ACTUAL, topicHeatingActual(), actual, full);
        }

        // Werte nur von aktuell erreichbaren Sensoren senden. Der BH1750 liefert bei einem
        // Lesefehler z.B. -1, das darf nicht als Messwert durchgehen.
        // Der SHT41 meldet den Mittelwert der kalibrierten Werte (Einstellung "smoothing").
        // Temperatur und Helligkeit gehen kalibriert heraus: Home Assistant soll den Wert
        // bekommen, auf den das Gerät eingemessen wurde. Ohne hinterlegten Kalibrierpunkt ist
        // er mit dem Rohwert identisch.
        if (MySHT41::isConnected) {
            sendNumber(SLOT_TEMPERATURE, topicTemperature(), MySHT41::temperatureSmoothed, 2, full, 0.1f);
            sendNumber(SLOT_HUMIDITY, topicHumidity(), MySHT41::humiditySmoothed, 2, full, 0.5f);
            sendNumber(SLOT_DEW_POINT, topicDewPoint(), MySHT41::dewPoint, 2, full, 0.1f);
            sendNumber(SLOT_TEMP_RAW, topicTemperatureRaw(), MySHT41::temperature, 2, full, 0.1f);
            sendNumber(SLOT_TEMP_CAL, topicTemperatureCalibrated(), MySHT41::temperatureCalibrated, 2, full, 0.1f);
            sendNumber(SLOT_HUM_RAW, topicHumidityRaw(), MySHT41::humidity, 2, full, 0.5f);
            sendNumber(SLOT_HUM_CAL, topicHumidityCalibrated(), MySHT41::humidityCalibrated, 2, full, 0.5f);
        }
        if (MyBH1750::isConnected) {
            sendNumber(SLOT_LUX, topicLux(), MyBH1750::luxCalibrated, 1, full, 1.0f, 0.05f);
            sendNumber(SLOT_LUX_RAW, topicLuxRaw(), MyBH1750::lux, 1, full, 1.0f, 0.05f);
        }
        if (MySCD40::isConnected) {
            sendNumber(SLOT_CO2, topicCo2(), MySCD40::co2, 0, full, 10.0f);
            sendText(SLOT_CO2_LEVEL, topicCo2Level(), MySCD40::levelToString(MySCD40::level), full);
            sendNumber(SLOT_SCD_TEMPERATURE, topicScdTemperature(), MySCD40::temperature, 2, full, 0.1f);
            sendNumber(SLOT_SCD_HUMIDITY, topicScdHumidity(), MySCD40::humidity, 2, full, 0.5f);
        }
        publishAirQuality(full);

        // Nur veröffentlichen, was der Sensor tatsächlich geliefert hat: Ein nicht erkannter
        // Sensor oder ein noch fehlender erster Messwert soll in HA "unbekannt" zeigen statt 0.
        if (MyAHT21::isConnected) {
            sendNumber(SLOT_AHT_TEMPERATURE, topicAhtTemperature(), MyAHT21::temperature, 2, full, 0.1f);
            sendNumber(SLOT_AHT_HUMIDITY, topicAhtHumidity(), MyAHT21::humidity, 2, full, 0.5f);
        }
        if (MyENS160::isConnected) {
            sendText(SLOT_ENS_STATUS, topicEnsStatus(), MyENS160::validityToString(MyENS160::validity), full);
            if (MyENS160::aqi > 0) {
                sendText(SLOT_AQI_LEVEL, topicAqiLevel(), MyENS160::levelToString(MyENS160::level), full);
                sendNumber(SLOT_TVOC, topicTvoc(), MyENS160::tvoc, 0, full, 10.0f);
                sendNumber(SLOT_ECO2, topicEco2(), MyENS160::eco2, 0, full, 10.0f);
            }
        }

        // Brandindikator (gesendet wird immer, auch "Deaktiviert") und die Anstiege dazu.
        sendText(SLOT_FIRE, topicFireIndicator(), FireDetect::mqttText(FireDetect::state()), full);
        if (FireDetect::tvoc().available)        sendNumber(SLOT_FIRE_TVOC_RATE, topicFireTvocRate(), FireDetect::tvoc().rate, 1, full, 5.0f);
        if (FireDetect::co2().available)         sendNumber(SLOT_FIRE_CO2_RATE, topicFireCo2Rate(), FireDetect::co2().rate, 1, full, 2.0f);
        if (FireDetect::temperature().available) sendNumber(SLOT_FIRE_TEMP_RATE, topicFireTemperatureRate(), FireDetect::temperature().rate, 2, full, 0.05f);

        // Systeminfos (Diagnose). Die UI-Angaben stehen erst fest, wenn eine Web-UI gelesen wurde.
        const Webfiles::UiInfo& ui = Webfiles::ui();
        if (ui.version[0] != '\0') sendText(SLOT_UI_VERSION, topicUiVersion(), ui.version, full);
        if (ui.vendor[0] != '\0')  sendText(SLOT_UI_VENDOR, topicUiVendor(), ui.vendor, full);
        const float chipTemp = temperatureRead();
        if (!isnan(chipTemp) && chipTemp > -40.0f && chipTemp < 150.0f) {
            sendNumber(SLOT_CPU_TEMP, topicCpuTemperature(), chipTemp, 1, full, 1.0f);
        }
        if (WiFi.status() == WL_CONNECTED) {
            sendNumber(SLOT_RSSI, topicWifiRssi(), WiFi.RSSI(), 0, full, 3.0f);
        }

        // Beim LD2410 ist das besonders wichtig: Ohne Verbindung liefern seine Getter die
        // zuletzt empfangenen Werte weiter. Ein abgezogener Sensor hätte sonst dauerhaft
        // "Bewegung erkannt" gemeldet.
        if (MyLD2410::isConnected) {
            sendText(SLOT_PRESENCE, topicPresence(), MyLD2410::presence ? "ON" : "OFF", full);
            sendText(SLOT_MOTION, topicMotion(), MyLD2410::movingTarget ? "ON" : "OFF", full);
            sendText(SLOT_STATIONARY, topicStationary(), MyLD2410::stationaryTarget ? "ON" : "OFF", full);
            if (full) {
                sendNumber(SLOT_MOVING_DISTANCE, topicMovingDistance(), MyLD2410::movingDistance, 0, true, 0);
                sendNumber(SLOT_MOVING_ENERGY, topicMovingEnergy(), MyLD2410::movingEnergy, 0, true, 0);
                sendNumber(SLOT_STATIONARY_DISTANCE, topicStationaryDistance(), MyLD2410::stationaryDistance, 0, true, 0);
                sendNumber(SLOT_STATIONARY_ENERGY, topicStationaryEnergy(), MyLD2410::stationaryEnergy, 0, true, 0);
            }
        }
    }

}

// --------------------------------------------------------------
// Verbindung zum Broker
// --------------------------------------------------------------
namespace {

    /// @brief Baut die Verbindung zum MQTT-Broker auf (inkl. Last-Will auf dem
    /// Availability-Topic) und stößt bei Erfolg die Erstveröffentlichung an: Availability
    /// "online", Discovery-Configs, aktuelle Konfigurationswerte sowie die aktuellen
    /// Messwerte. So sieht Home Assistant das Gerät nach jedem Reconnect sofort vollständig,
    /// statt erst auf das nächste Publish-Intervall warten zu müssen.
    /// @return true bei erfolgreichem Verbindungsaufbau, sonst false
    bool connect() {
        if (deviceId.isEmpty()) {
            deviceId = "HyphaHome-Sens-" + macSuffix();
            baseTopic = "HyphaHome-Sens/" + macSuffix();
        }

        String availTopic = topicAvailability();
        bool ok = mqttClient.connect(deviceId.c_str(),
                                      Config::mqtt.username.c_str(), Config::mqtt.password.c_str(),
                                      availTopic.c_str(), 1, true, "offline");
        if (ok) {
            Serial.println(F("[MQTT] Verbunden"));
            mqttClient.publish(availTopic.c_str(), "online", true);
            publishDiscovery();
            subscribeHeating();
            publishHeatingState();
            removeLegacyLd2410Entities();
            // Nach dem Discovery einmal alle Verfügbarkeiten senden, auch die unveränderten:
            // Der Broker hat die retained Topics zwar noch, aber nach einem Neustart des
            // Geräts stimmt der lokale Merker nicht mehr mit dem überein, was dort liegt.
            publishSensorAvailability(true);
            publishState(true);
            lastPublish = millis();
        } else {
            Serial.print(F("[MQTT] Verbindung fehlgeschlagen, rc="));
            Serial.println(mqttClient.state());
        }
        return ok;
    }

    /// @brief Gibt zurück, ob aktuell eine Verbindung zum MQTT-Broker besteht.
    /// @return true wenn verbunden, sonst false
    /// @brief Übernimmt Adresse und Port aus der Konfiguration.
    ///
    /// Hat sich der Broker geändert, wird eine bestehende Verbindung getrennt — sonst bliebe
    /// sie zum alten Broker stehen und die Oberfläche meldete "verbunden", während in der
    /// Konfiguration längst eine andere Adresse steht.
    ///
    /// Darf nur aus dem Task laufen, dem der Client gehört (siehe requestReconnect()).
    void applyServer() {
        const String target = Config::mqtt.url + ":" + String(Config::mqtt.port);
        if (target == activeTarget) return;

        if (mqttClient.connected()) {
            Serial.printf("[MQTT] Broker gewechselt: %s -> %s, trenne Verbindung\n",
                          activeTarget.c_str(), target.c_str());
            mqttClient.disconnect();
        }

        mqttClient.setServer(Config::mqtt.url.c_str(), Config::mqtt.port);
        activeTarget = target;
        lastReconnectAttempt = 0;   // nicht erst das Intervall abwarten
    }

}

// --------------------------------------------------------------
// Public functions
// --------------------------------------------------------------
namespace MyMQTT {

    // Public functions

    void init() {
        applyServer();
        mqttClient.setBufferSize(1024);
        mqttClient.setCallback(onMessage);
    }

    void loop() {
        // Geänderte Einstellungen hier übernehmen, nicht dort, wo sie gespeichert wurden:
        // Der Client gehört diesem Task.
        if (configDirty) {
            configDirty = false;
            applyServer();
        }

        // Per MQTT geänderte Heizungswerte nach einer Ruhezeit speichern (siehe HEATING_SAVE_DELAY_MS).
        if (heatingSaveAt != 0 && (int32_t)(millis() - heatingSaveAt) >= 0) {
            heatingSaveAt = 0;
            Config::saveConfig();
        }

        if (wlanManager.takeConnectionState() != WLANManager::ConnectionState::Connected) return;

        if (!mqttClient.connected()) {
            uint32_t now = millis();
            if (now - lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
                lastReconnectAttempt = now;
                connect();
            }
            return;
        }

        mqttClient.loop();

        // Die Heizungssteuerung wurde ein-/ausgeschaltet oder über die Oberfläche geändert.
        if (heatingSyncDirty) {
            heatingSyncDirty = false;
            publishHeatingDiscovery();
            publishHeatingState();
        }

        uint32_t now = millis();
        if (now - lastPublish >= MQTT_PUBLISH_INTERVAL_MS) {
            lastPublish = now;
            lastChangeCheck = now;
            publishState(true);
        } else if (now - lastChangeCheck >= MQTT_CHANGE_CHECK_INTERVAL_MS) {
            lastChangeCheck = now;
            publishState(false);
        }
    }

    bool isConnected() {
        return mqttClient.connected();
    }

    String activeBroker() {
        return activeTarget;
    }

    void requestReconnect() {
        configDirty = true;
    }

    void requestHeatingSync() {
        heatingSyncDirty = true;
    }
}
