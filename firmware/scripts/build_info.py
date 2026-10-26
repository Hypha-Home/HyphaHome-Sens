"""Zaehlt bei jedem Bauen die Build-Nummer hoch und erzeugt die Kennung im Firmware-Image.

Warum
-----
Die Versionsnummer (VERSION in config.h) wird von Hand hochgezaehlt, zwischen zwei Versionen
entstehen aber viele Staende. Die Build-Nummer unterscheidet sie. Ausserdem liest die Firmware
aus der jeweils anderen OTA-Partition die Kennung dieses Builds aus, denn die App-Beschreibung
des ESP-IDF stammt aus der vorkompilierten Arduino-Bibliothek und ist in jedem Build gleich.

Was passiert
------------
  build_number.txt        Zaehlerstand, wird bei jedem Bauen um 1 erhoeht (liegt in der
                          Versionsverwaltung, damit der Stand ueber Rechner hinweg weiterlaeuft).
  src/build_marker.cpp    erzeugt, nicht in der Versionsverwaltung. Enthaelt die Kennung
                          "MSBUILD|v=<VERSION>|b=<Nummer>|t=<Zeit>|g=<Git>" als Zeichenkette im
                          Image. VERSION wird vom Compiler aus config.h eingesetzt, hier steht
                          sie nicht zweimal.

Hochgezaehlt wird nur beim Bauen der Firmware (pio run, pio run -t upload). Dateisystem-Abbild,
Aufraeumen, IntelliSense-Abfragen der IDE und dergleichen zaehlen nicht.
Scheitert die Uebersetzung, ist die Nummer trotzdem vergeben.
"""

import os
import subprocess
import time

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: F401  (von PlatformIO bereitgestellt)

Import("env")  # noqa: F821

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
COUNTER = os.path.join(PROJECT, "build_number.txt")
MARKER = os.path.join(PROJECT, "src", "build_marker.cpp")

# Ohne Ziel (Strg+Alt+B in der IDE, "pio run") oder mit Hochladen wird die Firmware gebaut.
FIRMWARE_TARGETS = {"", "upload", "buildprog"}


def building_firmware():
    targets = set(COMMAND_LINE_TARGETS) or {""}
    return targets <= FIRMWARE_TARGETS


def read_counter():
    try:
        with open(COUNTER, "r", encoding="ascii") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return 0


def git_state():
    """Kurzer Commit-Hash, mit angehaengtem '+' bei nicht eingecheckten Aenderungen."""
    def run(*args):
        return subprocess.run(["git", *args], cwd=PROJECT, capture_output=True,
                              text=True, timeout=10, check=True).stdout.strip()
    try:
        short = run("rev-parse", "--short", "HEAD")
        # Der Zaehler aendert sich bei jedem Bauen und zaehlt deshalb nicht als Aenderung.
        dirty = run("status", "--porcelain", "--untracked-files=no", "--",
                    ".", ":(exclude)build_number.txt")
        return short + ("+" if dirty else "")
    except Exception:
        return "unbekannt"


def write_if_changed(path, text):
    try:
        with open(path, "r", encoding="utf-8", newline="") as handle:
            if handle.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)


number = read_counter()
if building_firmware():
    number += 1
    write_if_changed(COUNTER, "%d\n" % number)

stamp = time.strftime("%Y-%m-%d %H:%M")

# "|" trennt die Felder und darf in keinem Wert vorkommen.
write_if_changed(MARKER, (
    "// build_marker.cpp - ERZEUGT von scripts/build_info.py bei jedem Bauen, nicht von Hand aendern.\n"
    "#include \"config.h\"\n"
    "\n"
    "extern const char BUILD_MARKER[];\n"
    "const char BUILD_MARKER[] __attribute__((used)) =\n"
    "    \"MSBUILD|v=\" VERSION \"|b=%d|t=%s|g=%s\";\n"
) % (number, stamp, git_state().replace("|", "")))
