// ===================================================================
// File:            main.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================

#include "main.h"

// Watchdog-Timeout
// 30sek. weil das MQTT Timeout 15sek. den loop blockieren würde. 
static const uint32_t WATCHDOG_TIMEOUT_S = 30;

// Zeitstempel für den Entsperrversuch des I2C-Busses (siehe loop())
static SensorHealth::State i2cBusHealth;

// Mess-Intervalle in Millisekunden. Die Sensoren werden in drei Gruppen gelesen, die unterschiedliche Intervalle haben.
static const uint32_t READ_INTERVAL_MS_LOW = 5000;
static const uint32_t READ_INTERVAL_MS_MID = 1000;
static const uint32_t READ_INTERVAL_MS_HIGH = 200;
static uint32_t lastRead_low = 0;
static uint32_t lastRead_mid = 0;
static uint32_t lastRead_high = 0;
static uint8_t lastprint = 10;

/// @brief prüft ob die Sensoren angeschlossen sind und aktiviert sie in der Konfiguration. (Wird nur beim ersten Start ausgeführt.)
static void detectSensors() {
    Serial.println(F("=== Erstinbetriebnahme: suche Sensoren ==="));

    Config::sensors.bh1750.enabled = MyBH1750::probe();
    Config::sensors.sht41.enabled  = MySHT41::probe();
    Config::sensors.scd40.enabled  = MySCD40::probe();
    Config::sensors.aht21.enabled  = MyAHT21::probe();
    Config::sensors.ens160.enabled = MyENS160::probe();
    Config::sensors.ld2410.enabled = MyLD2410::probe();   // blockiert bis zu 1 s

    Serial.printf("  BH1750: %s\n", Config::sensors.bh1750.enabled ? "gefunden" : "fehlt");
    Serial.printf("  SHT41:  %s\n", Config::sensors.sht41.enabled  ? "gefunden" : "fehlt");
    Serial.printf("  SCD40:  %s\n", Config::sensors.scd40.enabled  ? "gefunden" : "fehlt");
    Serial.printf("  AHT21:  %s\n", Config::sensors.aht21.enabled  ? "gefunden" : "fehlt");
    Serial.printf("  ENS160: %s\n", Config::sensors.ens160.enabled ? "gefunden" : "fehlt");
    Serial.printf("  LD2410: %s\n", Config::sensors.ld2410.enabled ? "gefunden" : "fehlt");

    Config::saveConfig();

    // Zählerstand sofort festschreiben, damit die Suche nicht erneut läuft, falls das Gerät gleich wieder ausgeschaltet wird.
    Runtime::flush();
}

void setup() {
    Beeper::begin();   // Pin sofort auf LOW, damit der Buzzer beim Start nicht anspricht
    Serial.begin(115200);
    delay(1000); // Zeit für Serial-Monitor-Verbindung

    Serial.println(F("=== Hypha Home - Sens ==="));
    LED::begin();

    Beeper::beep(300);   // Startton
    LED::setBrightness(255);
    LED::solid(LED::White);   // Start laeuft

    Config::loadConfig();
    PasswordReset::applyIfArmed();
    if(Config::system.hostname.length() == 0) {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        mac = mac.substring(6);
        Config::system.hostname = "HyphaHome-Sens-" + mac;
    }

    // I2C-Bus initialisieren. SDA/SCL-Pins sind in pinmapping.h definiert.
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);

    // Laufzeitzähler lesen. Steht dort nichts, lief das Gerät noch nie -> einmalig nach Sensoren suchen, bevor sie initialisiert werden.
    Runtime::begin();
    if (Runtime::isFirstBoot()) {
        Serial.println(F("Firmware läuft zum ersten Mal. Sensoren werden gesucht. Zum abbrechen, Gerät ausschalten."));
        sleep(30);
        detectSensors();
        Serial.println(F("Erster Start abgeschlossen. Sensoren erkannt"));
    }

    // Sensoren initialisieren.
    MyBH1750::init();
    MySHT41::init();
    MySCD40::init();
    MyAHT21::init();
    MyENS160::init();
    MyLD2410::init();
    MyLD2410::printConfiguration();

    Serial.println(F("=== Init abgeschlossen ==="));

    // WLAN initialisieren und verbinden
    wlanManager.setSSID(Config::wlan.ssid);
    wlanManager.setPassword(Config::wlan.password);
    wlanManager.setHotspotPrefix("HyphaHome-Sens");
    Webfiles::setupRoutes();
    Webinterface::setupWebserverRoutes();
    wlanManager.begin(32768);
    wlanManager.doConnect();

    // MQTT initialisieren
    MyMQTT::init();

    // Heizungssteuerung initialisieren
    HeatingControl::begin();

    // Ermitteln, welche Build-Informationen das andere Image enthält.
    BuildInfo::scanOtherApp();

    // WDT Starten
    if (esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true) != ESP_OK || esp_task_wdt_add(NULL) != ESP_OK) {
        Serial.println(F("[WDT] Watchdog konnte nicht eingerichtet werden"));
    }

    // Build-Informationen ausgeben
    const BuildInfo::Info& build = BuildInfo::own();
    Serial.printf("[BUILD] Version %s, Build %lu, %s, Git %s\n", build.version, (unsigned long)build.build, build.time, build.git);

    // Startmeldung
    Serial.println(F("------------------- SETUP ENDE -------------------"));
    LED::solid(LED::Green);
}

void loop() {

    // WDT zurücksetzen, damit das Gerät nicht neu startet, wenn die Schleife länger als 30 Sekunden blockiert.
    esp_task_wdt_reset();

    // Aktuelle Zeit merken
    uint32_t now = millis();

    // Sensoren der schnellsten Gruppe lesen
    if (now - lastRead_high >= READ_INTERVAL_MS_HIGH) {
        lastRead_high = now;
        MyLD2410::read(false);
    }

    // Sensoren der mittleren Gruppe lesen
    if (now - lastRead_mid >= READ_INTERVAL_MS_MID) {
        lastRead_mid = now;
        MyBH1750::read(false);
        MySHT41::read(false);
    }
    
    // Sensoren der langsamsten Gruppe lesen
    if (now - lastRead_low >= READ_INTERVAL_MS_LOW) {
        lastRead_low = now;
        MySCD40::read(false);
        MyAHT21::read(false);

        // ENS160 benötigt die Temperatur und Luftfeuchtigkeit von AHT21, um die Kompensation einzustellen.
        if (MyAHT21::isConnected && MyENS160::isConnected) {
            MyENS160::setCompensation(MyAHT21::temperature, MyAHT21::humidity);
        }
        MyENS160::read(false);

        // Status der Sensoren und WLAN ausgeben (Kopfzeile nur alle 10 Zeilen ausgeben, damit die Ausgabe nicht zu unübersichtlich wird)
        if(lastprint >= 9) {
            lastprint = 0;
            Serial.println(F("    Zeit   | BH1750 | SHT41  | SCD40  | AHT21  | ENS160 | LD2410 |  WLAN  |  MQTT  |        IP       |  RSSI  "));
        }
        lastprint++;

        // Status der Sensoren und WLAN ausgeben
        Serial.printf("%10lu | %6s | %6s | %6s | %6s | %6s | %6s | %6s | %6s",
                      (unsigned long)(now / 1000),
                      MyBH1750::isConnected ? "OK" : "ERR",
                      MySHT41::isConnected ? "OK" : "ERR",
                      MySCD40::isConnected ? "OK" : "ERR",
                      MyAHT21::isConnected ? "OK" : "ERR",
                      MyENS160::isConnected ? "OK" : "ERR",
                      MyLD2410::isConnected ? "OK" : "ERR",
                      wlanManager.takeConnectionState() == WLANManager::ConnectionState::Connected ? "OK" : "ERR",
                      MyMQTT::isConnected() ? "OK" : "ERR");
    
        if(wlanManager.takeConnectionState() == WLANManager::ConnectionState::Connected) {
            Serial.printf(" | %15s | %6d\n", 
                wlanManager.takeIP().c_str(), 
                wlanManager.takeRSSI()
            );
        } else {
            Serial.printf(" | %15s | %6s\n", "N/A", "N/A");
        }

    }

    // Brandfrüherkennung prüfen
    FireDetect::update();
    const FireDetect::State fire = FireDetect::state();

    // LED-Statusanzeige setzen
    if (fire == FireDetect::State::Probable) {
        LED::blink(LED::Red, 500);          // Brand wahrscheinlich: rot blinkend
    } else if (fire == FireDetect::State::Suspect) {
        LED::blink(LED::Orange);            // Brandverdacht: orange blinkend
    } else if(WiFi.status() != WL_CONNECTED) {
        LED::fadeInOut(LED::Cyan);          // Cyan fading: WLAN nicht verbunden
    } else if(MyMQTT::isConnected() == false) {
        LED::fadeInOut(LED::Yellow);        // Gelb: MQTT nicht verbunden
    } else if(wlanManager.takeConnectionState() == WLANManager::ConnectionState::Hotspot) {
        LED::solid(LED::Blue);              // Blau: Hotspot aktiv
    }  else if(
           !MyBH1750::isConnected && Config::sensors.bh1750.enabled
        || !MySHT41::isConnected && Config::sensors.sht41.enabled
        || !MySCD40::isConnected && Config::sensors.scd40.enabled
        || !MyAHT21::isConnected && Config::sensors.aht21.enabled
        || !MyENS160::isConnected && Config::sensors.ens160.enabled
        || !Webfiles::uiUsable()
    ) {
        LED::solid(LED::Red);           // Rot: Mindestens ein Sensor nicht verbunden oder UI nicht verfügbar
    } else {
        LED::off();
    }


    // Melden sich alle I2C-Sensoren gleichzeitig ab, wird warscheinlich der Bus blockiert sein. Bus-Neuinitialisierung versuchen, wenn der letzte Versuch länger als 30 Sekunden her ist. (Damit nicht bei jedem Loop die Bus-Reinitialisierung versucht wird.)
    if (!MyBH1750::isConnected && !MySHT41::isConnected
        && !MySCD40::isConnected && !MyAHT21::isConnected && !MyENS160::isConnected
        && SensorHealth::retryDue(i2cBusHealth, 30000)) {
        SensorHealth::i2cRecoverBus();
    }

    // Sub-Loops und Handler
    MyMQTT::loop();
    PasswordReset::handleTimeout();
    Session::handleTimeout();
    Runtime::handle();
}