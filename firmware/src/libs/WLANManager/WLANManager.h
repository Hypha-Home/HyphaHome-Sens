// ===================================================================
// File:            WLANManager.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once

#include <WiFi.h>
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "FastWebServer.h"

class WLANManager {

    public:
        
        enum class ConnectionState { Unknown, Connecting, Connected, Disconnected, ConnectionLost, ConnectionFailed, SSIDNotFound, Hotspot };    

        // Konstruktor
        WLANManager() : webserver(80) {
            Mutex = xSemaphoreCreateMutex();
        }

        // --------
        // Methoden
        // --------

        // getter/setter
        void setSSID(String ssid);
        void setPassword(String password);
        void setHotspotPrefix(String prefix);
        void setReconnectTrys(byte trys);
        void setHostName(String hostname);
        String getHostName();
        String getSSID();
        String getHotspotPrefix();
        byte getReconnectTrys();
        byte getCurrentReconnects();

        // Actions
        void begin(uint32_t stacksize = 16384);             // WLAN verbinden
        bool doConnect();
        bool doDisconnect();
        bool doReconnect();
        bool doCreateHotspot();

        // Status
        ConnectionState takeConnectionState();     // Prüfen ob verbunden
        String takeSSID();
        int takeRSSI();                            // Signalstärke auslesen
        int takeRSSIBars();                        // Signalstärke auslesen
        String takeIP();                           // IP-Adresse auslesen

        // Webserver
        void addRoute(const char *uri, WebServer::THandlerFunction handler);
        void addRoute(const char *uri, HTTPMethod method, WebServer::THandlerFunction handler);
        WebServer& getWebserver();

    private:

        enum class Action { Nothing, Connect, Disconnect, Reconnect, CreateHotspot };
        
        // Semaphoren
        SemaphoreHandle_t Mutex;

        // Settings
        String ssid;
        String password;
        String hotspotprefix;
        byte reconnectTrys = 3;
        byte currentRetrys = 0;

        // internal Parameters
        Action nextAction = Action::Nothing;
        ConnectionState currentState = ConnectionState::Unknown;

        /// @brief Intervall zwischen automatischen Reconnect-Versuchen im Hotspot-Modus (ms)
        uint32_t autoReconnectIntervalMs = 30000;
        /// @brief Zeitpunkt (millis()) des letzten automatischen Reconnect-Versuchs
        uint32_t lastAutoReconnectAttempt = 0;

        //internal Objects
        // Abgeleitet vom WebServer, nur mit kurzer Wartezeit auf die Anfragedaten.
        // getWebserver() liefert weiterhin eine WebServer&, fuer Aufrufer aendert sich nichts.
        FastWebServer webserver;
        DNSServer dnsServer;

        /// @brief Wurde die Uhrzeit initialisiert?
        bool timeInit = false;
        
        /// @brief Zeitzone = Europe/Berlin inkl. Sommerzeit-Umschaltung
        String timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
        String ntp_server1 = "de.pool.ntp.org";
        String ntp_server2 = "pool.ntp.org";

        // Methoden
        String getRandomString(int length);
        void connect();
        void disconnect();
        void reconnect();
        void createHotspot();
        void stopWebserver();
        void startWebserver();
        void maintainConnection();
        void loop();

        // Statischer Wrapper für FreeRTOS
        static void taskWrapper(void *parameter) {
            Serial.println("[WLANManager] Task gestartet");
            WLANManager *self = (WLANManager*)parameter;
            self->loop();
        }
};

extern WLANManager wlanManager;
