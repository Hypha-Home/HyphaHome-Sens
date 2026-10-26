"""Prueft die Woerterbuecher der Web-UI gegen die Schluessel, die in den Seiten benutzt werden.

Aufruf (aus der Projektwurzel):   python scripts/i18n_check.py

Gesucht wird in data/*.html und data/js/*.js:
  data-t="schluessel"            statischer Text; das deutsche Original steht im Element
  data-t-attr="attr:schluessel"  dasselbe fuer Attribute
  I18n.t('schluessel' ...)       Text aus dem Skript

Gemeldet wird je Sprachdatei (data/js/lang-xx.js ausser lang-de.js), was fehlt, samt dem
deutschen Original, damit man nicht in den Seiten suchen muss. Skripttexte muessen zusaetzlich
in lang-de.js stehen. Der Rueckgabewert ist 1, wenn etwas fehlt.

Mit --reference entsteht zusaetzlich i18n/de.json: alle deutschen Texte (Seiten und Skripte)
in einer Datei, als Vorlage fuer Uebersetzungen. Sie wird nicht auf das Geraet geladen.
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def dictionary_keys(path):
    """Alle 'schluessel': in einer Sprachdatei."""
    return set(re.findall(r"^\s*'([\w.]+)'\s*:", read(path), re.M))


def write_reference(static, german):
    """Vollstaendige deutsche Vorlage fuer Uebersetzer: statische Texte aus den Seiten plus die
    Skripttexte aus lang-de.js. Wird nicht auf das Geraet geladen."""
    import json
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from i18n_extract import normalize

    texts = {}
    for key, (page, text) in static.items():
        texts[key] = normalize(text) if text != "(Attribut)" else text
    source = read(os.path.join(DATA, "js", "lang-de.js"))
    for match in re.finditer(r"^\s*'([\w.]+)'\s*:\s*'((?:[^'\
]|\.)*)'", source, re.M):
        texts[match.group(1)] = match.group(2).replace("\'", "'").replace("\\\\", "\\")
    os.makedirs(os.path.join(ROOT, "i18n"), exist_ok=True)
    target = os.path.join(ROOT, "i18n", "de.json")
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(dict(sorted(texts.items())), handle, ensure_ascii=False, indent=1)
    print("Vorlage: %d Texte -> %s" % (len(texts), target))


def main():
    static = {}     # Schluessel -> deutsches Original
    script = set()  # Schluessel aus I18n.t(...) und I18n.error(...)

    for path in sorted(glob.glob(os.path.join(DATA, "*.html"))):
        html = read(path)
        name = os.path.basename(path)
        for match in re.finditer(r'<(\w+)[^>]*\sdata-t="([\w.]+)"[^>]*>(.*?)</\1>', html, re.S):
            static.setdefault(match.group(2), (name, " ".join(match.group(3).split())))
        for match in re.finditer(r'data-t-attr="([^"]+)"', html):
            for pair in match.group(1).split(";"):
                if ":" in pair:
                    static.setdefault(pair.split(":", 1)[1].strip(), (name, "(Attribut)"))
        for match in re.finditer(r"I18n\.t\(\s*'([\w.]+)'", html):
            script.add(match.group(1))
        for match in re.finditer(r"I18n\.plural\(\s*'([\w.]+)'", html):
            script.update((match.group(1) + ".one", match.group(1) + ".other"))
        for match in re.finditer(r"I18n\.error\(\s*'(\w+)'", html):
            script.add("error." + match.group(1))

    for path in sorted(glob.glob(os.path.join(DATA, "js", "*.js"))):
        for match in re.finditer(r"I18n\.t\(\s*'([\w.]+)'", read(path)):
            script.add(match.group(1))
        for match in re.finditer(r"I18n\.plural\(\s*'([\w.]+)'", read(path)):
            script.update((match.group(1) + ".one", match.group(1) + ".other"))

    # Vorsaetze dynamisch zusammengesetzter Schluessel ('wlan.state.' + wert) und Beispiele aus
    # Kommentaren sind keine echten Schluessel.
    script = {k for k in script if not k.endswith(".") and k != "schluessel"}

    # Wo ein Schluessel als Zeichenkette vorkommt (auch in "cond ? 'a' : 'b'"), gilt er als benutzt.
    code = ""
    for path in glob.glob(os.path.join(DATA, "*.html")) + [
            os.path.join(DATA, "js", n) for n in ("ui.js", "api.js")]:
        code += read(path)

    german = dictionary_keys(os.path.join(DATA, "js", "lang-de.js"))
    missing = False

    # Dynamisch zusammengesetzte Schluessel ('wlan.state.' + wert) lassen sich nicht erfassen;
    # fuer sie gilt: alle Eintraege mit dem Vorsatz muessen in jeder Sprache vorhanden sein.
    prefixes = ("error.", "wlan.state.", "reset.", "level.", "validity.", "bool.", "unit.", "fire.state.", "webui.state.", "shelly.kind.")

    lacking = sorted(k for k in script if k not in german)
    if lacking:
        missing = True
        print("lang-de.js: es fehlen Skripttexte:")
        for key in lacking:
            print("  " + key)

    for path in sorted(glob.glob(os.path.join(DATA, "js", "lang-*.js"))):
        if os.path.basename(path) == "lang-de.js":
            continue
        have = dictionary_keys(path)
        wanted = set(static) | script | {k for k in german if k.startswith(prefixes)}
        lacking = sorted(k for k in wanted if k not in have)
        print("%s: %d Schluessel, %d fehlen" % (os.path.basename(path), len(have), len(lacking)))
        for key in lacking:
            origin = static.get(key)
            print("  %s%s" % (key, ("   <- %s: %s" % origin) if origin else ""))
        missing = missing or bool(lacking)

    unused = sorted(k for k in german
                    if not k.startswith(prefixes) and k not in script and ("'" + k + "'") not in code)
    if unused:
        print("lang-de.js: ungenutzt (oder nur dynamisch zusammengesetzt):")
        for key in unused:
            print("  " + key)

    if "--reference" in sys.argv:
        write_reference(static, german)

    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
