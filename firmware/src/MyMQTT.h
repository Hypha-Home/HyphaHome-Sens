// ===================================================================
// File:            MyMQTT.h
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "libs/WLANManager/WLANManager.h"
#include "MyBH1750.h"
#include "MySHT41.h"
#include "MySCD40.h"
#include "MyAHT21.h"
#include "MyENS160.h"
#include "MyLD2410.h"
#include "webfiles.h"
#include "FireDetect.h"
#include "HeatingControl.h"

#ifndef MQTT_PUBLISH_INTERVAL_MS
    /// @brief festes Sendeintervall (für Sensoren die nicht alle 5sek. eine Änderung haben.)
    #define MQTT_PUBLISH_INTERVAL_MS 5000UL
#endif

#ifndef MQTT_CHANGE_CHECK_INTERVAL_MS
    /// @brief festes Intervall in dem sich aktualisierte Sensoren gesendet werden.
    #define MQTT_CHANGE_CHECK_INTERVAL_MS 200UL
#endif

#ifndef MQTT_RECONNECT_INTERVAL_MS
    /// @brief Intervall in dem MQTT neu zu verbinden wenn die Verbindung verloren gegangen ist.
    #define MQTT_RECONNECT_INTERVAL_MS 5000UL
#endif 

namespace MyMQTT {

    /// @brief Konfiguriert den MQTT-Client (Server, Puffergröße). Muss einmalig in setup() aufgerufen werden.
    void init();

    /// @brief Muss in jedem loop()-Durchlauf aufgerufen werden. Kümmert sich um den
    /// Verbindungsaufbau/-erhalt zum Broker (nur solange WLAN verbunden ist), verarbeitet
    /// eingehende MQTT-Nachrichten und veröffentlicht die Sensor-Zustände in einem
    /// festen Intervall. Läuft bewusst außerhalb des Mess-Intervalls.
    void loop();

    /// @brief Gibt zurück, ob aktuell eine Verbindung zum MQTT-Broker besteht.
    /// @return true wenn verbunden, sonst false
    bool isConnected();

    /// @brief Der Broker, mit dem gerade gearbeitet wird, als "adresse:port".
    /// @return Adresse mit Port, oder ein leerer String vor dem ersten init()
    String activeBroker();

    /// @brief Merkt vor, dass sich die MQTT-Einstellungen geändert haben.
    void requestReconnect();

    /// @brief Merkt vor, dass sich die Einstellungen der Heizung geändert haben.
    void requestHeatingSync();
}
