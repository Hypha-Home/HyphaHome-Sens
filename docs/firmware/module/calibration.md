# Calibration – Ausgleichsgerade aus Referenzpunkten

**Dateien:** `src/Calibration.cpp`, `src/Calibration.h` · **Namespace:** `Calibration`

## Zweck

Ein Sensor zeigt nie ganz genau das, was ein Referenzgerät zeigt. `Calibration` rechnet einen Rohwert über eine
Gerade um:

```
korrigiert = slope × roh + intercept
```

Die Gerade wird aus Wertepaaren bestimmt, die man im selben Raum aufgenommen hat: *Was zeigt mein Sensor* (`measured`) und
*was zeigt das Referenzgerät* (`reference`). Eine Gerade kann einen **Verstärkungsfehler** (`slope` ≠ 1) und eine
**Verschiebung** (`intercept` ≠ 0) gleichzeitig ausgleichen.

Verwendet wird sie von [MyBH1750](bh1750.md) (Lux) und [MySHT41](sht41.md) (Temperatur). Die Punkte stehen in
[Config](config.md).

## Öffentliche Schnittstelle

| Typ / Funktion | Beschreibung |
|---|---|
| `struct Point { float measured; float reference; }` | Ein Messpunkt |
| `struct Table { uint8_t count; Point points[8]; }` | Alle Punkte einer Messgröße (`CALIBRATION_MAX_POINTS = 8`) |
| `bool Table::add(measured, reference)` | Punkt anhängen. `false` und verworfen, wenn die Tabelle voll ist |
| `void Table::clear()` | Alle Punkte löschen → die Messgröße gilt als unkalibriert |
| `enum class Fallback { Offset, Factor }` | Annahme bei **genau einem** Punkt |
| `struct Line { float slope; float intercept; uint8_t pointsUsed; }` | Die berechnete Gerade; `pointsUsed = 0` heißt unkalibriert |
| `Line computeLine(const Table&, Fallback)` | Bestimmt die Gerade |
| `float apply(const Table&, float raw, Fallback)` | Rechnet einen Rohwert um |

## Funktionsweise

`computeLine()` liefert **immer** eine brauchbare Gerade, auch in den Randfällen:

| Anzahl Punkte | Ergebnis |
|---|---|
| 0 | Identität (`slope = 1`, `intercept = 0`): Rohwert bleibt unverändert |
| 1 | Je nach `Fallback`: **Offset** verschiebt (`intercept = reference − measured`, `slope = 1`); **Factor** kippt (`slope = reference / measured`, `intercept = 0`) |
| ≥ 2 | Lineare Regression nach der Methode der kleinsten Quadrate |
| ≥ 2, alle mit demselben Rohwert | Eine Steigung ist nicht bestimmbar, es gibt nur die mittlere Verschiebung (`intercept = Ø reference − Ø measured`) |

### Warum zwei Fallbacks?

Mit einem einzigen Punkt lassen sich Verschiebung und Verstärkung nicht trennen. Welche Annahme sinnvoll ist, hängt
von der Messgröße ab:

| Fallback | Passt zu | Begründung | Verwendet von |
|---|---|---|---|
| `Offset` | Größen mit willkürlichem Nullpunkt (°C); konstante Fehler wie Eigenerwärmung | Gerade parallel verschieben | SHT41 (Temperatur) |
| `Factor` | Größen mit echtem Nullpunkt (lx); anteilige Fehler wie eine getönte Gehäusescheibe | Null bleibt null | BH1750 (Lux) |

Beim Faktor ist ein Punkt mit `measured ≤ 0` oder `reference ≤ 0` unbrauchbar (Division durch null, bzw. ein negativer
Faktor würde das Vorzeichen drehen) – dann gilt die Identität.

### Numerik

Die Regression rechnet mit den **um die Mittelwerte zentrierten** Summen in `double`:

```
slope     = Σ (x − x̄)(y − ȳ) / Σ (x − x̄)²
intercept = ȳ − slope · x̄
```

Die Lehrbuchform mit rohen Summen (`Sxy − n·x̄·ȳ`) löscht bei dicht beieinanderliegenden Messwerten die signifikanten
Stellen gegeneinander aus. Genau diesen Fall gibt es, wenn jemand im selben Raum mehrmals nachkalibriert.

## Beispiel

Der SHT41 zeigt 22,4 °C, das Referenzthermometer 21,9 °C. Ein einzelner Punkt `{22.4, 21.9}` mit `Offset` ergibt
`intercept = −0,5`: Jeder Rohwert wird um 0,5 K gesenkt. Mit einem zweiten Punkt (`{26.0, 25.3}`) wird daraus eine
Regressionsgerade mit `slope ≈ 0,94` und `intercept ≈ 0,7`.

## Besonderheiten

- `apply()` berechnet die Gerade bei **jedem Aufruf neu**. Bei höchstens acht Punkten und einem Aufruf pro Messung ist
  das unkritisch.
- Die Kalibrierung ändert nur den Anzeigewert. Die Rohwerte stehen weiterhin in `MySHT41::temperature` und
  `MyBH1750::lux` und gehen auch per MQTT hinaus (`…_raw`).

## Siehe auch

[Config](config.md) · [MyBH1750](bh1750.md) · [MySHT41](sht41.md)
