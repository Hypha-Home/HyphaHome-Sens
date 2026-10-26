// ===================================================================
// File:            WLANManager.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "WLANManager.h"

#pragma region Getter/Setter

    /// @brief Legt die SSID fest
    /// @param ssid 
    void WLANManager::setSSID(String ssid) {
        
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            this->ssid = ssid;
            xSemaphoreGive(Mutex);
        }

    }

    /// @brief Gibt die aktuell eingestellte SSID für den Connect zurück
    /// @return SSID
    String WLANManager::getSSID() {
        return ssid;
    }

    /// @brief Setzt das WLAN Passwort
    /// @param password 
    void WLANManager::setPassword(String password) {
        
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            this->password = password;
            xSemaphoreGive(Mutex);
        }

    }

    /// @brief Setzt den Hotspotprefix
    /// @param prefix 
    void WLANManager::setHotspotPrefix(String prefix) {
        
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            this->hotspotprefix = prefix;
            xSemaphoreGive(Mutex);
        }

    }

    /// @brief Gibt den Hotspotprefix zurück
    /// @return Prefix
    String WLANManager::getHotspotPrefix() {
        return hotspotprefix;
    }

    /// @brief Setzt die Anzahl der Verbindungsversuche
    /// @param trys 
    void WLANManager::setReconnectTrys(byte trys) {
        
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            this->reconnectTrys = trys;
            xSemaphoreGive(Mutex);
        }

    }

    /// @brief Setzt den Hostnamen
    /// @param hostname
    void WLANManager::setHostName(String hostname) {
        WiFi.setHostname(hostname.c_str());
        
    }

    /// @brief Gibt den Hostnamen zurück
    /// @return Hostname
    String WLANManager::getHostName() {
        return WiFi.getHostname();
    }

    /// @brief Gibt die Max Anzahl der Verbindungsversuche zurück
    /// @return Max Verbindungsversuche
    byte WLANManager::getReconnectTrys() {
        return reconnectTrys;
    }

    /// @brief Gibt die aktuelle Anzahl der Verbindungsversuche zurück
    /// @return Aktuelle Verbindungsversuche
    byte WLANManager::getCurrentReconnects() {
        return currentRetrys;
    }

#pragma endregion

#pragma region Öffentliche Methoden

    /// @brief Startet den Task für WLAN verbindungen
    void WLANManager::begin(uint32_t stacksize)
    {  
        //TODO: Prüfen ob Task schon läuft und ggf. nicht starten
        Serial.printf("[WLANManager] begin with stacksize %d\n", stacksize);

        xTaskCreatePinnedToCore(
            taskWrapper,   // Funktion
            "WLANMGT_loop", // Name
            stacksize,        // Stack-Größe (HTTP braucht etwas mehr)
            this,        // Parameter
            2,           // Priorität
            NULL,        // Task-Handle
            1            // Core 0 → LVGL läuft auf Core 1
        );
    }

    /// @brief Verbinde zum WLAN
    /// @return Befehl gesendet.
    bool WLANManager::doConnect() {

        Serial.println("[WLANManager] Action: doConnect");

        bool success = false;
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(nextAction == Action::Nothing) {
                nextAction = Action::Connect;
                success = true;
            }
            xSemaphoreGive(Mutex);
        }
        return success;
    }

    /// @brief Trenne die Verbindung
    /// @return Befehl gesendet
    bool WLANManager::doDisconnect() {

        Serial.println("[WLANManager] Action: doDisconnect");

        bool success = false;
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(nextAction == Action::Nothing) {
                nextAction = Action::Disconnect;
                success = true;
            }
            xSemaphoreGive(Mutex);
        }
        return success;
    }

    /// @brief Verbinde neu
    /// @return Befehl gesendet
    bool WLANManager::doReconnect() {

        Serial.println("[WLANManager] Action: doReconnect");

        bool success = false;
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(nextAction == Action::Nothing) {
                nextAction = Action::Reconnect;
                success = true;
            }
            xSemaphoreGive(Mutex);
        }
        return success;
    }

    /// @brief Erzeuge Hotspot
    /// @return Befehl gesendet
    bool WLANManager::doCreateHotspot() {

        Serial.println("[WLANManager] Action: doCreateHotspot");

        bool success = false;
        if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(nextAction == Action::Nothing) {
                nextAction = Action::CreateHotspot;
                success = true;
            }
            xSemaphoreGive(Mutex);
        }
        return success;
    }

    /// @brief Ließt die aktuelle IP aus
    /// @return Aktuelle IP
    String WLANManager::takeIP()
    {
        ConnectionState state = takeConnectionState();

        if (state == ConnectionState::Connected)        return WiFi.localIP().toString();
        else if (state == ConnectionState::Hotspot)     return WiFi.softAPIP().toString();
        
        return "0.0.0.0";
    }

    /// @brief Gibt die SSID zurück. Wenn im Hotspotmode dann die Hotspot-SSID
    /// @return SSID
    String WLANManager::takeSSID() {
        ConnectionState state = takeConnectionState();
        
        if (state == ConnectionState::Hotspot) {
            // Hotspot-SSID aus Prefix + MAC zusammenbauen
            String mac = WiFi.macAddress();
            mac.replace(":", "");
            return hotspotprefix + "-" + mac.substring(6);
        } else if (state == ConnectionState::Connected) {
            // Verbundene SSID direkt von WiFi
            return WiFi.SSID();
        }
        
        // Nicht verbunden — konfigurierte SSID zurückgeben
        return ssid;
    }

    /// @brief Fügt eine Route zum Webserver hinzu (GET)
    /// @param uri URL die aufgerufen werden soll (z.B. "/status")
    /// @param handler Handler-Funktion die aufgerufen werden soll
    void WLANManager::addRoute(const char *uri, WebServer::THandlerFunction handler) {
        webserver.on(uri, handler);
    }

    /// @brief Fügt eine Route zum Webserver hinzu
    /// @param uri URL die aufgerufen werden soll (z.B. "/status")
    /// @param method HTTP-Methode (z.B. HTTP_GET)
    /// @param handler Handler-Funktion die aufgerufen werden soll
    void WLANManager::addRoute(const char *uri, HTTPMethod method, WebServer::THandlerFunction handler) {
        webserver.on(uri, method, handler);
    }

    WebServer& WLANManager::getWebserver() { return webserver; }
    
#pragma endregion

#pragma region Status-Methoden

    /// @brief Gibt den aktuellen Verbindungsstatus zurück
    /// @return den aktuellen Verbindungsstatus
    WLANManager::ConnectionState WLANManager::takeConnectionState()
    {
        ConnectionState tmp;
        if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            tmp = currentState;

            // Verifizieren wenn verbunden oder getrennt
            if (tmp == ConnectionState::Connected && WiFi.status() != WL_CONNECTED) {
                // War connected aber ist es nicht mehr
                
                if(WiFi.status() == WL_NO_SSID_AVAIL)         currentState = tmp = ConnectionState::SSIDNotFound;
                else if(WiFi.status() == WL_CONNECT_FAILED)   currentState = tmp = ConnectionState::ConnectionFailed;
                else if(WiFi.status() == WL_CONNECTION_LOST)  currentState = tmp = ConnectionState::ConnectionLost;
                else                                          currentState = tmp = ConnectionState::Disconnected;
            } else if (tmp == ConnectionState::Disconnected && WiFi.status() == WL_CONNECTED) {
                // War disconnected aber ist jetzt verbunden
                currentState = tmp = ConnectionState::Connected;
            } else if (tmp == ConnectionState::Hotspot && WiFi.softAPgetStationNum() == 0 && WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
                // Hotspot war aktiv aber AP Mode ist weg
                currentState = tmp = ConnectionState::Disconnected;
            }

            xSemaphoreGive(Mutex);
        }
        return tmp;
    }

    /// @brief Gibt die aktuelle Signalstärke zurück
    /// @return -200 wenn nicht verbunden, sonst die Signalstärke in dBm
    int WLANManager::takeRSSI()
    {
        if (takeConnectionState() == WLANManager::ConnectionState::Connected)
            return WiFi.RSSI();
        return -200;
    }

    /// @brief Gibt die aktuelle Signalstärke in Stufen zurück
    /// @return 0-5 Stufen, 0 = kein Signal, 5 = sehr starkes Signal
    int WLANManager::takeRSSIBars()
    {
        int rssi = takeRSSI();
        int bars = 0;
        if (rssi > -50)         bars = 5;
        else if (rssi > -60)    bars = 4;
        else if (rssi > -70)    bars = 3;
        else if (rssi > -80)    bars = 2;
        else if (rssi == -200)  bars = 0;
        else                    bars = 1;
        return bars;
    }

#pragma endregion

#pragma region Private Methoden

    /// @brief Gibt einen zufälligen String (0-9A-zA-Z) zurück
    /// @param length Länge des Strings
    /// @return zufälliger String
    String WLANManager::getRandomString(int length) {
        const String charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        String result = "";
        
        for (int i = 0; i < length; i++) {
            // esp_random() nutzt den Hardware-RNG des ESP32
            int randomIndex = esp_random() % charset.length();
            result += charset[randomIndex];
        }
        
        return result;
    }

    /// @brief Verbindet mit dem WLAN
    void WLANManager::connect() {

        Serial.println("[WLANManager] Connecting...");

        // Status setzen
        if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentState = ConnectionState::Connecting;
            xSemaphoreGive(Mutex);
        }

        // WiFi Mode setzen
        stopWebserver();
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.disconnect(true);
        WiFi.setAutoReconnect(false);
        WiFi.mode(WIFI_STA);
        // Modem-Schlaf aus (Standard waere WIFI_PS_MIN_MODEM): Im Schlafmodus liefert der
        // Router Pakete an das Geraet nur zum Beacon aus. Beim Senden einer Seite bleiben die
        // ACKs des Browsers dadurch haengen, WiFiClient::write() gibt nach 10 x 1 s auf und
        // der Browser sieht eine abgebrochene Antwort. Muss nach jedem WiFi.mode() stehen,
        // weil der Treiber beim Neustart des Funkteils auf den Standard zurueckfaellt.
        WiFi.setSleep(false);
        WiFi.begin(ssid.c_str(), password.c_str());

        // Auf Verbindung warten
        int attempt = 0;
        while (WiFi.status() != WL_CONNECTED && attempt < 10) {
            vTaskDelay(pdMS_TO_TICKS(500));
            attempt++;
            Serial.printf("[WLANManager] Versuch %d/%d...\n", attempt, 10);
        }

        // Ergebnis auswerten
        if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (WiFi.status() == WL_CONNECTED) {
                currentState = ConnectionState::Connected;
                Serial.println("[WLANManager] Verbunden! IP: " + WiFi.localIP().toString());
                xSemaphoreGive(Mutex);

                static bool timeInit = false;
                if (!timeInit && WiFi.status() == WL_CONNECTED) {
                    configTzTime(timezone.c_str(), ntp_server1.c_str(), ntp_server2.c_str());
                    timeInit = true;
                }

                startWebserver();
            } else {

                if(WiFi.status() == WL_NO_SSID_AVAIL)         currentState = ConnectionState::SSIDNotFound;
                else if(WiFi.status() == WL_CONNECT_FAILED)   currentState = ConnectionState::ConnectionFailed;
                else if(WiFi.status() == WL_CONNECTION_LOST)  currentState = ConnectionState::ConnectionLost;
                else                                          currentState = ConnectionState::Disconnected;

                Serial.println("[WLANManager] Verbindung fehlgeschlagen!");
                xSemaphoreGive(Mutex);
                
                // Hotspot als Fallback
                createHotspot();
            }
        }
    }

    /// @brief Beendet die Verbindung
    void WLANManager::disconnect() {
    
        Serial.println("[WLANManager] Disconnected");
        
        stopWebserver();
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.disconnect(true);

        // Status setzen
        if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentState = ConnectionState::Disconnected;
            xSemaphoreGive(Mutex);
        }
    }

    /// @brief Erzeugt einen Hotspot
    void WLANManager::createHotspot()
    {
        Serial.println("[WLANManager] Creating Hotspot...");
        // SSID aus Prefix + letzten 6 Zeichen der MAC zusammenbauen
        String mac = WiFi.macAddress(); // Format: "AA:BB:CC:DD:EE:FF"
        mac.replace(":", "");           // → "AABBCCDDEEFF"
        String ssid = hotspotprefix + "-" + mac.substring(6);

        if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentState = ConnectionState::Connecting;
            xSemaphoreGive(Mutex);
        }

        stopWebserver();
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.disconnect(true);
        // AP_STA statt reinem AP: das STA-Radio bleibt nutzbar, damit im Hintergrund
        // weiter nach dem konfigurierten WLAN gesucht/verbunden werden kann, während
        // der Hotspot für den Nutzer erreichbar bleibt.
        WiFi.mode(WIFI_AP_STA);
        // Modem-Schlaf aus (Standard waere WIFI_PS_MIN_MODEM): Im Schlafmodus liefert der
        // Router Pakete an das Geraet nur zum Beacon aus. Beim Senden einer Seite bleiben die
        // ACKs des Browsers dadurch haengen, WiFiClient::write() gibt nach 10 x 1 s auf und
        // der Browser sieht eine abgebrochene Antwort. Muss nach jedem WiFi.mode() stehen,
        // weil der Treiber beim Neustart des Funkteils auf den Standard zurueckfaellt.
        WiFi.setSleep(false);

        IPAddress apIP(192, 168, 4, 1);
        IPAddress netMsk(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apIP, netMsk);
        if (WiFi.softAP(ssid.c_str())) {
            Serial.println("[WLANManager] Hotspot aktiv: " + ssid);
            Serial.println("[WLANManager] IP: " + WiFi.softAPIP().toString());
            dnsServer.start(53, "*", apIP);
            startWebserver();
            

            if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                currentState = ConnectionState::Hotspot;
                xSemaphoreGive(Mutex);
            }
        } else {
            Serial.println("[WLANManager] Hotspot fehlgeschlagen!");
            if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                currentState = ConnectionState::Disconnected;
                xSemaphoreGive(Mutex);
            }
        }

    }

    /// @brief Überwacht die WLAN-Verbindung und stellt den autarken Betrieb sicher:
    /// Geht eine bestehende Verbindung verloren, wird sofort wieder der Hotspot aktiviert,
    /// damit der Nutzer nie ausgesperrt ist. Ist keine Verbindung aktiv, wird in festen
    /// Abständen automatisch ein Reconnect-Versuch unternommen (z.B. falls der Nutzer die
    /// WLAN-Zugangsdaten über das Webinterface geändert hat), ohne dass der Hotspot dafür
    /// beendet werden muss (AP_STA-Modus). Wird bei jedem Durchlauf von loop() aufgerufen.
    void WLANManager::maintainConnection() {

        // Bestehende Verbindung überwachen: Verbindung verloren -> Hotspot sofort wieder hochfahren
        if (currentState != ConnectionState::Connected && currentState != ConnectionState::Hotspot) {
            Serial.println("[WLANManager] Verbindung verloren, aktiviere Hotspot...");
            createHotspot();
            return;
        }

        // Kein WLAN aktiv -> in festen Abständen automatisch neu versuchen, ohne den Hotspot zu stören
        if (currentState != ConnectionState::Connected && ssid.length() > 0) {
            uint32_t now = millis();
            if (now - lastAutoReconnectAttempt < autoReconnectIntervalMs) return;
            lastAutoReconnectAttempt = now;

            Serial.println("[WLANManager] Automatischer Reconnect-Versuch...");
            WiFi.begin(ssid.c_str(), password.c_str());

            // Kurz auf Verbindung warten, ohne den Task dauerhaft zu blockieren
            int attempt = 0;
            while (WiFi.status() != WL_CONNECTED && attempt < 10) {
                vTaskDelay(pdMS_TO_TICKS(500));
                attempt++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[WLANManager] Automatischer Reconnect erfolgreich! IP: " + WiFi.localIP().toString());

                // Hotspot wird nicht mehr benötigt -> beenden, damit nicht unnötig viele SSIDs aktiv sind
                dnsServer.stop();
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);

                if (xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    currentState = ConnectionState::Connected;
                    xSemaphoreGive(Mutex);
                }

                static bool timeInit = false;
                if (!timeInit) {
                    configTzTime(timezone.c_str(), ntp_server1.c_str(), ntp_server2.c_str());
                    timeInit = true;
                }
            } else {
                Serial.println("[WLANManager] Automatischer Reconnect fehlgeschlagen, Hotspot bleibt aktiv");
            }
        }
    }

    void WLANManager::stopWebserver() {
        webserver.stop();
        dnsServer.stop();
        Serial.println("[WLANManager] Webserver gestoppt");
    }

    void WLANManager::startWebserver(){
        // Der WebServer merkt sich nur die Kopfzeilen, die hier stehen — alle anderen wirft er
        // beim Einlesen weg. X-Auth-Token braucht die Schnittstelle für das Sitzungstoken.
        const char * headerkeys[] = {"User-Agent", "X-Auth-Token"};
        size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
        webserver.collectHeaders(headerkeys, headerkeyssize);
        Serial.println("[WLANManager] Start Webserver");
        Serial.printf("[WLANManager] Freier Stack: %d bytes\n", 
        uxTaskGetStackHighWaterMark(NULL));
        webserver.begin();
    };

    /// @brief Task der ständig läuft
    void WLANManager::loop() {

        while (true) {
           
            webserver.handleClient();

            if (currentState == ConnectionState::Hotspot) {
                dnsServer.processNextRequest();
            }

            // Daten zwischenspeichern und Semaphore freigeben
            Action tmpAction;
            if(xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                tmpAction = nextAction;
                nextAction = Action::Nothing;
                xSemaphoreGive(Mutex);
            }

            // Actionen abarbeiten
            if(tmpAction == WLANManager::Action::Connect || tmpAction == WLANManager::Action::Reconnect) {
                connect();
            } else if(tmpAction == WLANManager::Action::Disconnect) {
                disconnect();
            } else if(tmpAction == WLANManager::Action::CreateHotspot) {
                createHotspot();
            }

            // Autarker Betrieb: Verbindung überwachen und bei Bedarf automatisch wiederherstellen
            maintainConnection();

            // Delay um den Ressourcen freizugeben.
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

#pragma endregion

WLANManager wlanManager;