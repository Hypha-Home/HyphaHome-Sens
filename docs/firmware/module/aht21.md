# MyAHT21 – Temperatur und Feuchte (Zusatzsensor)

**Dateien:** `src/MyAHT21.cpp`, `src/MyAHT21.h` · **Namespace:** `MyAHT21`
**Sensor:** AHT21 · **Anschluss:** I²C, Adresse `0x38` (`AHTX0_I2CADDR_DEFAULT`)
**Bibliothek:** `adafruit/Adafruit AHTX0`

## Zweck

Der AHT21 sitzt **auf demselben Modul wie der ENS160**. Seine Aufgabe in dieser Firmware: Er liefert Temperatur und
Feuchte als **Kompensationswerte** für den Gassensor ([MyENS160](ens160.md)). Als Raumklimasensor taugt er weniger, weil
er neben der Heizung des ENS160 sitzt und etwas zu warm misst; der [SHT41](sht41.md) ist dafür maßgeblich. In Home
Assistant sind seine Entitäten deshalb standardmäßig deaktiviert.

## Öffentliche Schnittstelle

| Element | Beschreibung |
|---|---|
| `aht` | `Adafruit_AHTX0`-Instanz |
| `isConnected` | Sensor antwortet |
| `temperature` | °C |
| `humidity` | % rF |
| `init()` | Startet den Sensor, wenn `Config::sensors.aht21.enabled` |
| `read(bool print = true)` | Liest Temperatur und Feuchte |
| `probe()` | Ping der Adresse |

## Funktionsweise

Das Modul ist das einfachste der Sensormodule und zeigt das Grundmuster:

1. Nicht aktiviert → `isConnected = false`, nichts weiter.
2. Nicht verbunden → alle 15 s Ping und `aht.begin(&Wire, 0, 0x38)`; bei Erfolg `reportOk()`.
3. Verbunden → `aht.getEvent(&humidityEvent, &tempEvent)`; bei `false` `reportFail()` und letzte Werte behalten, sonst
   `reportOk()` und Werte übernehmen.

Es gibt weder Kalibrierung noch Glättung.

## Zusammenspiel mit dem ENS160

In `loop()` (5-s-Takt):

```cpp
MyAHT21::read(false);
if (MyAHT21::isConnected && MyENS160::isConnected)
    MyENS160::setCompensation(MyAHT21::temperature, MyAHT21::humidity);
MyENS160::read(false);
```

Der ENS160 schreibt die Werte beim nächsten `read()` in seine Kompensationsregister. Ohne AHT21 misst der ENS160 mit
den Standardwerten des Chips.

## Konfiguration

| Einstellung | Bedeutung |
|---|---|
| `sensors.aht21.enabled` | Sensor ein/aus |

## Verwendung der Werte

| Verbraucher | Wert |
|---|---|
| [MyENS160](ens160.md) | Kompensation |
| [MyMQTT](mqtt.md) | `…/aht21/temperature`, `…/aht21/humidity` (in HA deaktiviert), Verfügbarkeit `…/aht21/available` |
| [Webinterface](webinterface.md) | `GET /api/sensors` → `aht21` |

## Siehe auch

[MyENS160](ens160.md) · [SensorHealth](sensor-health.md)
