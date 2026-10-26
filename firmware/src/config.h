// ===================================================================
// File:            config.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "version.h"
#include "Calibration.h"

// Firmware Version
#define VERSION             "1.0.0"


#define LD2410_GATE_COUNT 9
#define LD2410_SENSITIVITY_UNSET 255
#define LD2410_MOVING_HOLD_MAX_S 600
#define SHT41_SMOOTHING_MAX 60

/// @brief Höchstzahl der eingetragenen Shelly-Geräte.
///
/// Keine fachliche Grenze, sondern eine Sicherung: Die Geräte liegen in einem Feld fester
/// Größe, damit die Konfiguration zur Laufzeit keinen Speicher anfordert und wieder freigibt.
/// Ohne Obergrenze könnte außerdem eine fehlerhafte Anfrage an die Schnittstelle den Speicher
/// vollschreiben.
#define SHELLY_MAX_COUNT 16

/// @brief Höchstzahl der Ausgänge, die ein Regelkreis gleichzeitig schalten kann.
///
/// Wie SHELLY_MAX_COUNT eine Sicherung für das Feld fester Größe.
#define CONTROL_MAX_OUTPUTS 8

/// @brief Höchstzahl der Kanäle (Schaltausgänge) eines einzelnen Shelly.
///
/// Ein Shelly 1 hat einen, ein Shelly 2.5 oder 2PM zwei. Kanäle werden von 0 an gezählt.
#define SHELLY_MAX_CHANNELS 2

/// @brief Minimal festzulegende Temperatur in °C
#define HEATING_TARGET_MIN_C     5.0f

/// @brief Maximal festzulegende Temperatur in °C
#define HEATING_TARGET_MAX_C     30.0f

/// @brief Standardtemperatur in °C
#define HEATING_TARGET_DEFAULT_C 21.0f

/// @brief Basisname der Konfigurationsdatei im LittleFS. config.cpp hängt die Endungen selbst
/// an: .json für die gültige Konfiguration, .bak für die vorherige Fassung und .tmp für den
/// Zwischenstand beim Speichern.
#define CONFIG_FILE_BASE    "config"

/// @brief Konfiguration des Geräts.
///
/// Gespeichert wird als JSON im LittleFS. Die Datei ist dabei reine Ablage: Beim Start wird sie
/// einmal gelesen und in die Strukturen unten kopiert, beim Speichern einmal zurückgeschrieben.
/// Dazwischen arbeitet alles mit diesen Variablen, nicht mit dem JSON.
namespace Config {

    /// @brief Firmwareversion (definiert in config.cpp, sonst hätte jede Übersetzungseinheit
    /// ihre eigene Kopie und der Linker bräche mit Mehrfachdefinition ab)
    extern Version version;

    /// @brief Geräteeinstellungen
    struct SystemCfg {
        /// @brief Gerätename. Leer bedeutet: main.cpp vergibt "HyphaHome-Sens-<MAC>".
        String hostname;
        /// @brief Zugangspasswort für das Webinterface. Leer bedeutet kein Passwortschutz —
        /// das ist auch der Zustand nach einem Passwort-Reset (siehe PasswordReset.h).
        String password;
        /// @brief Brandfrüherkennung eingeschaltet (siehe FireDetect.h). Standard: aus.
        bool fireDetection = false;
    };

    /// @brief Zugangsdaten für WLAN
    struct WlanCfg {
        String ssid;
        String password;
    };

    /// @brief Zugangsdaten für MQTT
    struct MqttCfg {
        String url;
        uint16_t port;
        String username;
        String password;
    };

    // --------------------------------------------------------------
    // ---- Sensoren ----
    // Jeder Sensor hat seine eigene Konfiguration. Vom einfachen enable bis hin zu Kalibrierungswerten
    // --------------------------------------------------------------

    /// @brief Einstellungen des Helligkeitssensors BH1750
    struct Bh1750Cfg {
        bool enabled;
        /// @brief Kalibrierpunkte für die Beleuchtungsstärke, alle Werte in lx
        Calibration::Table luxCalibration;
    };

    /// @brief Einstellungen des Klimasensors SHT41
    struct Sht41Cfg {
        bool enabled;
        /// @brief Kalibrierpunkte für die Temperatur, alle Werte in °C
        Calibration::Table temperatureCalibration;
        /// @brief Anzahl der Messungen, aus denen der gemeldete Mittelwert gebildet wird
        ///        (1 = keine Glättung, höchstens SHT41_SMOOTHING_MAX)
        uint8_t smoothing;
    };

    /// @brief Einstellungen des CO2-Sensors SCD40
    struct Scd40Cfg { bool enabled; };

    /// @brief Einstellungen des Klimasensors AHT21 (auf dem ENS160-Modul)
    struct Aht21Cfg { bool enabled; };

    /// @brief Einstellungen des Luftqualitätssensors ENS160
    struct Ens160Cfg { bool enabled; };

    /// @brief Empfindlichkeitsschwellen eines einzelnen LD2410-Gates.
    struct Ld2410GateCfg {
        uint8_t moving;      ///< Schwelle für Bewegung
        uint8_t stationary;  ///< Schwelle für Stillstand
    };

    /// @brief Einstellungen des Radarsensors LD2410
    struct Ld2410Cfg {
        bool enabled;
        /// @brief Empfindlichkeiten je Entfernungszone, Index 0 ist die nächstgelegene
        Ld2410GateCfg gates[LD2410_GATE_COUNT];
        /// @brief Haltezeiten in Sekunden: Wie lange eine Meldung nach der letzten Erkennung stehen bleibt.
        ///
        /// Der Sensor meldet Bewegung, Stillstand und Präsenz über UART nur im Augenblick der
        /// Erkennung und flackert dadurch; die Haltezeit glättet das in der Firmware. Jede der
        /// drei Meldungen hat ihre eigene. 0 schaltet das Halten ab.
        uint16_t movingHoldSeconds;
        uint16_t stationaryHoldSeconds;
        uint16_t presenceHoldSeconds;
    };

    /// @brief Ein eingetragenes Shelly-Gerät im Netz.
    struct ShellyCfg {
        String name;      ///< Frei wählbarer Name, nur zur Unterscheidung in der Oberfläche
        String address;   ///< IP-Adresse oder Hostname im Netz
        String password;  ///< Gerätepasswort, leer wenn keines gesetzt ist
        String mac;       ///< Kennung des Geräts, erkennt eine vertauschte Adresse
        uint8_t gen;      ///< Gerätegeneration 1 bis 4, 0 wenn noch nicht ermittelt
        /// @brief Wofür das Gerät eingesetzt wird: "switch1", "switch2", "cover" oder "dimmer".
        /// Leer, solange es weder erkannt noch gewählt wurde. Ein Shelly 2.5 kann zwei
        /// Schalter oder ein Rollladen sein, die Hardware ist dieselbe.
        String kind;
    };

    /// @brief Liste der eingetragenen Shelly-Geräte.
    struct ShellysCfg {
        uint8_t count;                      /// Belegte Einträge in items
        ShellyCfg items[SHELLY_MAX_COUNT];
    };

    /// @brief Ein Schaltausgang: ein Kanal eines eingetragenen Shelly.
    struct ControlOutput {
        String  shelly;    /// MAC des Shelly, normalisiert
        uint8_t channel;   /// Kanal am Shelly, von 0 an
    };

    /// @brief Die Ausgänge, die ein Regelkreis schaltet.
    struct ControlOutputs {
        uint8_t count;
        ControlOutput items[CONTROL_MAX_OUTPUTS];
    };

    /// @brief Heizung. Braucht den SHT41.
    struct HeatingCfg {
        bool enabled;
        ControlOutputs outputs;
        float targetTemp;           ///< Zieltemperatur, °C (HEATING_TARGET_MIN_C bis _MAX_C), auf 0,1 gerundet
        uint16_t hysteresisOnMin;   ///< Sperre nach dem Ausschalten: so lange darf nicht wieder eingeschaltet werden, Minuten (0 bis 1440)
        uint16_t hysteresisOffMin;  ///< Sperre nach dem Einschalten: so lange darf nicht wieder ausgeschaltet werden, Minuten (0 bis 1440)
    };

    /// @brief Licht. Braucht BH1750 und LD2410. (noch nicht fertig!)
    struct LightCfg {
        bool enabled;
        ControlOutputs outputs;
        uint16_t luxThreshold;   ///< Unter diesem Wert gilt es als dunkel, Lux (0 bis 65535)
        uint8_t  brightnessPct;  ///< Helligkeit an Dimmern, Prozent (1 bis 100)
        uint8_t  fadeSeconds;    ///< Dauer des Einblendens an Dimmern, Sekunden (0 bis 60)
    };

    /// @brief Lüftung. Braucht SHT41 und ENS160 und/oder SCD40. (noch nicht fertig!)
    struct VentilationCfg {
        bool enabled;
        ControlOutputs outputs;
        uint8_t aqiThreshold;       ///< Ab dieser Luftqualitätsstufe wird gelüftet (1 bis 5)
        uint8_t humidityThreshold;  ///< Ab dieser Luftfeuchte wird gelüftet, Prozent (0 bis 100)
    };

    /// @brief Die drei Regelkreise, die Shelly-Kanäle nach Sensorwerten schalten.
    struct ControlsCfg {
        HeatingCfg     heating;
        LightCfg       light;
        VentilationCfg ventilation;
    };

    /// @brief Einstellungen der einzelnen Sensoren.
    struct SensorsCfg {
        Bh1750Cfg bh1750;
        Sht41Cfg  sht41;
        Scd40Cfg  scd40;
        Aht21Cfg  aht21;
        Ens160Cfg ens160;
        Ld2410Cfg ld2410;
    };

    extern SystemCfg system;
    extern WlanCfg wlan;
    extern MqttCfg mqtt;
    extern SensorsCfg sensors;
    extern ShellysCfg shellys;
    extern ControlsCfg controls;

    /// @brief Lädt die Konfiguration aus dem LittleFS.
    ///
    /// Ist die Hauptdatei beschädigt oder fehlt sie, wird die Sicherung verwendet. Ist auch die
    /// nicht brauchbar, gelten die Vorgabewerte und es wird eine neue Datei damit angelegt.
    /// @return true wenn eine Datei gelesen werden konnte, false wenn Vorgabewerte gelten
    bool loadConfig();

    /// @brief Schreibt die Konfiguration in das LittleFS.
    ///
    /// Der Schreibvorgang läuft über eine Zwischendatei, damit ein Stromausfall mittendrin
    /// nicht die bestehende Konfiguration zerstört.
    /// @return true bei Erfolg
    bool saveConfig();

    /// @brief Bringt eine MAC-Adresse in die Schreibweise der Konfiguration: Großbuchstaben, nur
    /// Hexziffern ("a4:cf:12:34:56:78" und "A4CF12345678" werden gleich).
    String normalizeMac(const String& mac);

    /// @brief Sucht den eingetragenen Shelly zu einem Schlüssel aus ControlOutput::shelly.
    ///
    /// Gesucht wird zuerst über die MAC.
    /// @return Der Eintrag (gültig bis zur nächsten Änderung der Konfiguration), oder nullptr
    const ShellyCfg* findShelly(const String& key);

    /// @brief Setzt alles auf die Vorgabewerte zurück und legt eine entsprechende Datei an.
    /// @return true bei Erfolg
    bool clearConfig();

    /// @brief Größe und Belegung des Konfigurations-Dateisystems in Bytes (0 wenn nicht eingehängt).
    size_t storageTotalBytes();
    size_t storageUsedBytes();

    /// @brief Schreibt die Laufzeitwerte als JSON in ein Objekt.    ///
    /// @param root Zielobjekt
    /// @param includeSecrets true schreibt die Passwörter mit (zum Speichern); false sagt nur, dass Passwörter vorhanden sind (für UI)
    void toJson(JsonObject root, bool includeSecrets = true);

    /// @brief Übernimmt einzelne Felder aus einem JSON-Objekt und speichert.
    ///
    /// Gedacht für die Schnittstelle des Webinterfaces: Eine Seite schickt nur, was sie
    /// tatsächlich geändert hat, nicht die ganze Konfiguration. 
    ///
    /// @param patch Objekt mit den zu ändernden Feldern, Aufbau wie `config.json`
    /// @return true wenn gespeichert wurde
    bool applyJson(JsonObjectConst patch);
}
