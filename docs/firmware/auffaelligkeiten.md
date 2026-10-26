# Auffälligkeiten im Quelltext

Beim Dokumentieren sind mir Stellen aufgefallen, die vermutlich nicht so gewollt sind, die sich widersprechen oder bei denen Verhalten und
Beschreibung auseinanderlaufen. **Der Quelltext wurde dafür nicht verändert.** Die Liste ist eine Sammlung zum Abarbeiten, keine Fehlerdiagnose:
Alles wurde im Code nachgelesen, aber nichts auf dem Gerät ausprobiert.

Einstufung: **A** = wahrscheinlicher Fehler oder spürbare Wirkung · **B** = Inkonsistenz oder Risiko · **C** = Aufräumen / Hinweis

## Verhalten stimmt vermutlich nicht mit der Absicht überein

| # | Stufe | Wo | Beobachtung |
|---|---|---|---|
| 1 | A | `main.cpp`, `WLANManager` | **Der Hostname wird nie als WLAN-/DHCP-Name gesetzt.** `main` belegt `Config::system.hostname` (`HyphaHome-Sens-<MAC>`), aber `WLANManager::setHostName()` wird nirgends aufgerufen. Ein Kommentar in `webinterface.cpp` spricht von „mDNS“, im Code gibt es keinen `MDNS.begin()`. Der Name taucht nur in Web-UI, MQTT-Gerätename und `/api/info` auf. Im Netz meldet sich das Gerät vermutlich mit dem Standardnamen des Arduino-Cores (nicht geprüft). |
| 2 | A | `main.cpp` / `WLANManager` / `webinterface.cpp` | **WLAN-Zugangsdaten werden nur in `setup()` übernommen.** `wlanManager.setSSID/setPassword` werden einmal aufgerufen. `POST /api/config` speichert geänderte Daten, reicht sie aber nicht weiter; `doReconnect()` wird nirgends aufgerufen. Auch der automatische 30-s-Versuch im Hotspot nutzt die alten Daten. Neue Zugangsdaten wirken erst nach einem Neustart. |
| 3 | A | `main.cpp` (LED-Rangfolge) | **Der blaue „Hotspot“-Zweig ist wahrscheinlich nicht erreichbar.** Davor prüft `WiFi.status() != WL_CONNECTED` (Cyan). Im Hotspot (`WIFI_AP_STA`) ist die Station-Seite typischerweise nicht verbunden, also zeigt die LED Cyan statt Blau. Wer „Blau = Hotspot“ erwartet, sieht es nie. |
| 4 | B | `main.cpp` | Der automatische Hostname wird nur im **Speicher** gesetzt. Gespeichert wird er erst, wenn irgendetwas `saveConfig()` aufruft. Danach steht er fest in der Datei. Das ist wohl in Ordnung, sollte aber bewusst sein (deshalb die Reihenfolge `applyIfArmed()` vor Hostname). |
| 5 | B | `LED.cpp` / `main.cpp` | `LED.cpp` nennt 64 als Standardhelligkeit, `setup()` setzt `setBrightness(255)` und ändert sie nie wieder. Die LED läuft in der Praxis mit voller Helligkeit. |
| 6 | B | `FireDetect.cpp` | Während der 300 s langen **Lernphase** meldet die Brandfrüherkennung `OK`, auch wenn schon etwas brennt. Das ist beabsichtigt (der Normalwert muss sich bilden), steht aber nirgends in der Oberfläche. Das Gleiche gilt nach jedem Neustart. |
| 7 | B | `PasswordReset.cpp` | Fällt der Strom innerhalb der 5-min-Freigabe aus, bleibt das Flag im NVS stehen und das Passwort wird **beim nächsten Start** gelöscht, egal wie viel später (die Frist läuft nur im RAM). |
| 8 | C | `MyLD2410.cpp` | `thresholdsDirty` ist vorgesehen („Schwellen im Sensor haben sich geändert“), wird aber nirgends auf `true` gesetzt. Der Zweig, der davon abhängt, läuft nie. |
| 9 | C | `Runtime.cpp` | `Runtime::reset()` existiert, wird aber von keiner Route und keinem anderen Code aufgerufen. |
| 10 | C | `WLANManager` | `setReconnectTrys()`, `getReconnectTrys()`, `getCurrentReconnects()` und `currentRetrys` werden nirgends ausgewertet. Der Verbindungsversuch ist fest auf 10 × 500 ms gesetzt. |
| 11 | C | `WLANManager` | NTP wird eingerichtet (`configTzTime`), die Uhrzeit aber nirgends ausgewertet (`getLocalTime`/`time()` kommen im Code nicht vor). Sessions und Reset-Frist rechnen mit `millis()`. |
| 12 | C | `Config` / `config.h` | **Licht und Lüftung** (`controls.light`, `controls.ventilation`) sind in Konfiguration und Standardwerten da und werden gespeichert, aber **nichts regelt sie**. Die Kommentare in `config.h` sagen „(noch nicht fertig!)“. Nur die Heizung hat eine Regelung. |

## Nebenläufigkeit

Die Firmware hat mehrere FreeRTOS-Tasks auf **zwei Kernen**, nutzt aber überwiegend **gemeinsame globale Variablen ohne Sperre**.

| # | Stufe | Wo | Beobachtung |
|---|---|---|---|
| 13 | B | `Config` | `Config::sensors`, `Config::mqtt`, `Config::controls` usw. werden vom Webserver-Task (`POST /api/config`), von `loopTask` (MQTT, Sensoren) und vom `heating`-Task gelesen und geschrieben, ohne Mutex. Das kann bei Strings (`String` ist nicht threadsicher) zu halb gelesenen Werten oder, im ungünstigen Fall, Abstürzen führen. `HeatingControl::syncOutputs()` kopiert deshalb die Ausgänge vorab, das ist der einzige Schutz. |
| 14 | B | Sensormodule / API | Messwerte (`MySHT41::temperature…`) schreibt `loopTask`, die API liest sie im WLAN-Task. Bei 32-Bit-Werten ist das auf dem ESP32 praktisch atomar; bei `String`-Feldern (z. B. in Konfigurationsobjekten) nicht. |
| 15 | A (klein) | `WLANManager::loop()` | `Action tmpAction;` bleibt **uninitialisiert**, wenn der Mutex nicht innerhalb von 100 ms zu bekommen ist. Danach wird `tmpAction` trotzdem verglichen. Mit Pech führt ein Zufallswert eine Aktion aus (z. B. `connect()`). Die Wahrscheinlichkeit ist gering, der Fix ein Zeichen: `Action tmpAction = Action::Nothing;`. |
| 16 | C | `WLANManager::connect()` / `maintainConnection()` | Die Variable `timeInit` aus dem Header wird in beiden Funktionen von einer lokalen `static bool timeInit` verdeckt. Funktional egal, aber die NTP-Einrichtung läuft dadurch bis zu zweimal statt einmal. |
| 17 | B | `WLANManager` | Der Webserver und `connect()` laufen im selben Task. Beim stillen Reconnect im Hotspot (alle 30 s) ist der Task bis zu 5 s blockiert; in der Zeit antwortet auch der Hotspot nicht. |

## Sicherheit

Das Gerät ist für ein vertrauenswürdiges Heimnetz gebaut. Für den Betrieb in einem fremden Netz fehlen folgende Punkte:

| # | Stufe | Wo | Beobachtung |
|---|---|---|---|
| 18 | B | `Session`, `Config` | **Passwörter** (Gerät, WLAN, MQTT, Shelly) stehen **im Klartext** in `config.json`. |
| 19 | B | Webserver | Die gesamte Kommunikation läuft über **unverschlüsseltes HTTP**; das Token und das Passwort gehen im Klartext über das Netz. |
| 20 | B | `Session` | Keine Begrenzung von Fehlversuchen beim Anmelden. |
| 21 | B | `WLANManager::createHotspot()` | Der Hotspot ist **offen** (`softAP(ssid)` ohne Passwort). Ist kein Geräte-Passwort gesetzt, kann jeder in Funkreichweite alles ändern, auch Firmware hochladen. |
| 22 | B | `Session::isValid()` | Ist **kein** Passwort gesetzt, ist jede API-Route (auch Firmware-Update, Reset, Konfiguration) offen. Das ist der Auslieferungszustand. |
| 23 | C | `/api/pwreset` | Die Freigabe des Passwort-Resets braucht keine Anmeldung (gewollt, siehe [PasswordReset](module/password-reset.md)); jeder im Netz kann sie auslösen, wirksam wird sie aber erst durch einen Neustart. |
| 24 | C | `FirmwareUpdate` | Geprüft wird nur das erste Byte (`0xE9`) und anschließend durch `Update.end()` die Prüfsumme. Es gibt keine Signatur und keinen Rückfall (`esp_ota_mark_app_valid_cancel_rollback` kommt nicht vor): Eine startende, aber kaputte Firmware bleibt der aktive Slot. |

## Inkonsistenzen in Kommentaren und Namen

| # | Stufe | Wo | Beobachtung |
|---|---|---|---|
| 25 | B | `webfiles.h` | `#include "Version.h"`, die Datei heißt aber `version.h`. Unter Windows (case-insensitiv) kein Problem; unter Linux/macOS (je nach Dateisystem) bricht der Build ab. In `config.h` und `version.cpp` steht richtig `version.h`. |
| 26 | C | `main.h` | `#include "webfiles.h"` steht doppelt (Zeile 25 und 40). Durch `#pragma once` harmlos. |
| 27 | C | `PasswordReset.h` | Makro `PWRESET_VALUD_MS` (Tippfehler, gemeint: `VALID`) und **ohne Klammern** definiert (`5UL * 60UL * 1000UL`). In einem Ausdruck wie `a / PWRESET_VALUD_MS` ergäbe das ein falsches Ergebnis. Die aktuellen Verwendungen sind unkritisch. |
| 28 | C | `ShellyProbe.h` | Der Kommentar zu `State::Pending` sagt „wartet auf den nächsten `loop()`“. Tatsächlich wird sofort ein eigener Task gestartet (`probeTask`). |
| 29 | C | `platformio.ini` | Der Kommentar zu `upload_config.py` beschreibt, das Skript lese „die Konfiguration aus der anderen OTA-Partition“ und schreibe sie in die aktuelle. Das Skript schreibt jedoch `config/config.json` in die Partition `config`. |
| 30 | C | `WLANManager.cpp` | Der Task-Kommentar sagt „Core 0 → LVGL läuft auf Core 1“, der Code pinnt auf Kern **1**; LVGL gibt es in diesem Projekt nicht (Rest aus einem anderen Projekt). |
| 31 | C | `WLANManager.h` | Der Kommentar zu `addRoute(uri, handler)` sagt „(GET)“, der Aufruf `webserver.on(uri, handler)` gilt für **alle** Methoden (`HTTP_ANY`). |
| 32 | C | `WLANManager.cpp` | `begin()` hat ein `TODO` („Prüfen ob Task schon läuft“); ein zweiter Aufruf würde einen zweiten Task starten. |
| 33 | C | `LED.h`, `Beeper.h` | Rückfallwerte `PIN_LED 1` bzw. `PIN_BEEP 1` (`#ifndef`) sind wirkungslos, weil `pinmapping.h` vorher eingebunden ist; `LED_COUNT` wird nicht benutzt. Wirksam: GPIO 26 und 27. |
| 34 | C | `Webfiles` | `sendFileFromFs()` liefert immer `Content-Type: text/html`, auch wenn eine einzeln ausgelieferte Datei etwas anderes ist; `serveStatic` (CSS/JS/Bilder) setzt den Typ selbst. Gilt nur für Seiten und ist deshalb folgenlos. |

## Hinweise zur Dokumentation selbst

- Der Ordner `data/` im vorliegenden Stand enthält nur `version.json`. HTML, CSS und JavaScript der Web-Oberfläche sowie die Sprachdateien
  wurden **nicht** dokumentiert.
- `NOTIZEN.md` und die Hardware-Unterlagen (`hardware/`) wurden nicht ausgewertet.
- Alle Aussagen zu Zeiten, Schwellen und Größen stammen aus dem Quelltext (Stand der Dateien vom 24./25.10.2026), nicht aus Messungen am Gerät.

[← zurück zur Architektur](architektur.md) · [Modulübersicht](module/README.md)
