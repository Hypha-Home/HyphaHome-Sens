# Modulübersicht

Eine Seite pro Modul. Die Reihenfolge folgt dem Weg der Daten: erst die Basisdienste, dann die Sensoren, dann
alles, was damit etwas anfängt.

> Die Zusammenhänge stehen in der [Architektur](../architektur.md). Dort ist auch die Grafik, die zeigt, welches
> Modul welches andere aufruft.

## Start und Basisdienste

| Modul | Dateien | Aufgabe |
|---|---|---|
| [main](main.md) | `main.cpp`, `main.h`, `pinmapping.h` | `setup()` und `loop()`, Pinbelegung, Erstinbetriebnahme |
| [Config](config.md) | `config.cpp`, `config.h` | Konfiguration (`config.json`) laden, sichern, als JSON übergeben |
| [Calibration](calibration.md) | `Calibration.cpp`, `Calibration.h` | Ausgleichsgerade aus Referenzpunkten |
| [SensorHealth](sensor-health.md) | `SensorHealth.cpp`, `SensorHealth.h` | Ausfallerkennung, Wiederfinden, I²C-Bus befreien |
| [Runtime](runtime.md) | `Runtime.cpp`, `Runtime.h` | Betriebszeit über alle Starts, Erkennung „erster Start“ |
| [BuildInfo und Version](build-info.md) | `BuildInfo.*`, `version.*`, `build_marker.cpp` | Build-Kennung, Kennung der anderen OTA-Partition, `Version` |

## Sensoren

| Modul | Sensor | Messgröße | Anschluss |
|---|---|---|---|
| [MyBH1750](bh1750.md) | BH1750 | Beleuchtungsstärke (lx) | I²C `0x23` |
| [MySHT41](sht41.md) | SHT41 | Temperatur, Luftfeuchte, Taupunkt | I²C `0x44` |
| [MySCD40](scd40.md) | SCD40 | CO₂ (ppm), Temperatur, Feuchte | I²C `0x62` |
| [MyAHT21](aht21.md) | AHT21 | Temperatur, Feuchte (für ENS160-Kompensation) | I²C `0x38` |
| [MyENS160](ens160.md) | ENS160 | Luftqualität (AQI), TVOC, eCO₂ | I²C `0x53` / `0x52` |
| [MyLD2410](ld2410.md) | LD2410 | Präsenz, Bewegung, Distanz (Radar) | UART2, 256000 Baud |

## Netzwerk und Oberfläche

| Modul | Dateien | Aufgabe |
|---|---|---|
| [WLANManager und FastWebServer](wlan-manager.md) | `libs/WLANManager/*`, `FastWebServer.*` | WLAN, Hotspot-Fallback, Webserver, NTP |
| [MyMQTT](mqtt.md) | `MyMQTT.cpp`, `MyMQTT.h` | MQTT-Verbindung, Home-Assistant-Discovery, Messwerte senden |
| [Webinterface (REST-API)](webinterface.md) | `webinterface.cpp`, `webinterface.h` | Alle Routen unter `/api/…` |
| [Webfiles](webfiles.md) | `webfiles.cpp`, `webfiles.h` | HTML-Seiten und Dateien aus dem LittleFS ausliefern |
| [Session](session.md) | `Session.cpp`, `Session.h` | Anmeldung mit Sitzungstoken |
| [PasswordReset](password-reset.md) | `PasswordReset.cpp`, `PasswordReset.h` | Notfall-Zurücksetzen des Passworts |
| [UiUpdate](ui-update.md) | `UiUpdate.cpp`, `UiUpdate.h` | Notfallseite und Upload einer neuen Web-UI |
| [FirmwareUpdate](firmware-update.md) | `FirmwareUpdate.cpp`, `FirmwareUpdate.h` | Firmware-Upload (OTA) |

## Auswertung und Steuerung

| Modul | Dateien | Aufgabe |
|---|---|---|
| [FireDetect](fire-detect.md) | `FireDetect.cpp`, `FireDetect.h` | Brandfrüherkennung aus TVOC, CO₂ und Temperatur |
| [HeatingControl](heating-control.md) | `HeatingControl.cpp`, `HeatingControl.h` | Temperaturregelung über Shelly-Relais |
| [Shelly (Probe und Switch)](shelly.md) | `ShellyProbe.*`, `ShellySwitch.*` | Shelly erkennen, Relais lesen und schalten |

## Anzeige

| Modul | Dateien | Aufgabe |
|---|---|---|
| [LED](led.md) | `LED.cpp`, `LED.h` | WS2812-Statusanzeige mit Effekten |
| [Beeper](beeper.md) | `Beeper.cpp`, `Beeper.h` | Buzzer, blockierend und nicht blockierend |

## Build

| Seite | Inhalt |
|---|---|
| [Build-System](build-system.md) | `platformio.ini`, `partitions.csv`, die Skripte in `scripts/` |
| [Auffälligkeiten](../auffaelligkeiten.md) | Dinge im Code, die beim Dokumentieren aufgefallen sind |

## Aufbau der Modulseiten

Jede Seite hat dieselbe Gliederung: **Zweck → Öffentliche Schnittstelle → Funktionsweise → Konfiguration →
Besonderheiten**. Die Schnittstelle ist als Tabelle angegeben; die Beschreibung der einzelnen Funktionen steht im
Quelltext als `///`-Kommentar direkt an der Deklaration.
