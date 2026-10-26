"""Baut aus data/ das Verzeichnis, das tatsaechlich auf das Geraet geht.

Warum ueberhaupt ein Bauschritt
-------------------------------
Der Webserver des Geraets bedient genau eine TCP-Verbindung gleichzeitig, und LWIP hat nur
16 Kontrollbloecke (CONFIG_LWIP_MAX_ACTIVE_TCP), die nach dem Schliessen noch 60 Sekunden
belegt bleiben. Jede Datei einer Seite ist eine eigene Verbindung — die Startseite mit ihren
sieben Symbolen brauchte zehn und raeumte den Pool mit einem einzigen Aufruf leer. Danach
blieben SYN-Pakete unbeantwortet und der Browser ging in die Rueckname: 1 s, 2 s, 4 s, 8 s.
Gemessen am 10.10.2026: Seitenaufbau bis zu 1,2 Minuten, einzelne Anfragen abgebrochen.

Dieser Schritt setzt CSS, JavaScript und die Symbole in die jeweilige Seite ein. Aus zehn
Verbindungen wird eine. Die Quelldateien in data/ bleiben einzeln und werden normal
bearbeitet; erzeugt wird nach $BUILD_DIR/fsdata, also unter .pio/ und damit ausserhalb der
Versionsverwaltung.

Was passiert
------------
  <link rel="stylesheet" href="css/style.css">   ->  <style>...</style>
  <script src="js/api.js"></script>              ->  <script>...</script>
  <img class="x" src="images/fan.svg" alt="L">   ->  <svg class="x" aria-label="L">...</svg>
  <link rel="icon" href="images/logo.svg">       ->  <link rel="icon" href="data:image/svg+xml;base64,...">

Pfade in den Seiten sind relativ zu data/ (die Seiten liegen dort flach), damit sich eine Seite
auch direkt als Datei im Browser ansehen laesst. Absolute Pfade ("/css/...") und "./css/..."
werden genauso aufgeloest. Eine Datei, die nicht eingebettet werden kann, wird einzeln
ausgeliefert — dann aber nur unter ihrem Pfad ab Wurzel; von einer Seite mit Unterpfad
(/sensors/aht21) aus liefe ein relativer Verweis ins Leere. Das Script meldet jede solche Datei.

Anschliessend wird jede ueber HTTP ausgelieferte Datei mit gzip gepackt. Die Auslieferung
kann das bereits: sendFileFromFs() sucht nach einer .gz-Fassung und setzt
Content-Encoding (ebenso serveStatic() der Bibliothek).

config.json gehoert nicht mehr in dieses Abbild: Die Konfiguration liegt in einem eigenen
Dateisystem (Partition "config", siehe partitions.csv), damit "uploadfs" sie nicht ueberschreibt.
Eine config.json in data/ wird deshalb uebergangen.
"""

import base64
import gzip
import os
import posixpath
import re
import shutil

Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
SRC = os.path.join(PROJECT, "data")
OUT = os.path.join(env.subst("$BUILD_DIR"), "fsdata")  # noqa: F821

# Nur diese werden gepackt. Alles andere wird unveraendert kopiert.
GZIP_EXTENSIONS = (".html", ".css", ".js", ".svg")

# Diese Ordner enthalten nur Zutaten fuer die Seiten. Nach dem Einbetten werden sie nicht mehr
# gebraucht — ausser eine Seite verweist doch noch auf etwas daraus, das wird geprueft.
INGREDIENT_DIRS = ("css/", "js/", "images/")


def read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def collect():
    """Liest data/ ein. Rueckgabe: {relativer Pfad mit /: Inhalt als bytes}."""
    files = {}
    for root, _dirs, names in os.walk(SRC):
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, SRC).replace(os.sep, "/")
            if rel == "config.json":
                continue
            with open(full, "rb") as handle:
                files[rel] = handle.read()
    return files


def resolve(ref, page):
    """Pfad aus einer Seite -> Schluessel in files. Absolut ("/css/a.css") gilt ab data/,
    relativ ("css/a.css", "./css/a.css") ab dem Ordner der Seite."""
    if ref.startswith("/"):
        return posixpath.normpath(ref.lstrip("/"))
    return posixpath.normpath(posixpath.join(posixpath.dirname(page), ref))


def svg_body(raw, label, css_class):
    """Macht aus einer SVG-Datei ein einbettbares <svg>-Element.

    Alles vor dem oeffnenden <svg> faellt weg: XML-Deklaration und Kommentare duerfen mitten
    im HTML nicht stehen. Das alt-Attribut des ersetzten <img> wandert als aria-label mit,
    sonst verliert eine Vorlesesoftware die Beschriftung; ein leeres alt (reine Dekoration)
    wird zu aria-hidden. Die class des <img> bleibt erhalten, weil CSS und Skripte sie nutzen.
    """
    text = raw.decode("utf-8")
    start = text.find("<svg")
    if start < 0:
        return None
    text = text[start:]

    extra = ""
    if css_class:
        extra += ' class="%s"' % css_class
    if label:
        extra += ' role="img" aria-label="%s"' % label
    elif label == "":
        extra += ' aria-hidden="true"'
    if extra:
        text = text.replace("<svg", "<svg" + extra, 1)
    return text


def attribute(tag, name):
    """Wert eines Attributs im Tag, None wenn es fehlt."""
    found = re.search(r'\s%s="([^"]*)"' % name, tag)
    return found.group(1) if found else None


def inline_page(html, page, files, still_needed):
    """Setzt CSS, JS, SVG und Favicon in eine Seite ein. page = Pfad der Seite unter data/."""

    def css(match):
        ref = resolve(match.group(1), page)
        body = files.get(ref)
        if body is None:
            still_needed.add(ref)
            return match.group(0)
        # </style> im CSS gaebe es nur in einem String; der Vollstaendigkeit halber entschaerft.
        return "<style>%s</style>" % body.decode("utf-8").replace("</style", "<\\/style")

    def script(match):
        ref = resolve(match.group(1), page)
        body = files.get(ref)
        if body is None:
            still_needed.add(ref)
            return match.group(0)
        # Ein </script> in einem JS-String wuerde das Element vorzeitig beenden.
        return "<script>%s</script>" % body.decode("utf-8").replace("</script", "<\\/script")

    def image(match):
        tag = match.group(0)
        src = attribute(tag, "src")
        ref = resolve(src, page)
        body = files.get(ref)
        if body is None:
            still_needed.add(ref)
            return tag
        inlined = svg_body(body, attribute(tag, "alt"), attribute(tag, "class"))
        if inlined is None:
            still_needed.add(ref)
            return tag
        return inlined

    def icon(match):
        tag = match.group(0)
        ref = resolve(attribute(tag, "href"), page)
        body = files.get(ref)
        if body is None:
            still_needed.add(ref)
            return tag
        encoded = base64.b64encode(body).decode("ascii")
        return tag.replace('href="%s"' % attribute(tag, "href"),
                           'href="data:image/svg+xml;base64,%s"' % encoded, 1)

    html = re.sub(r'<link[^>]*href="([^"]+\.css)"[^>]*>', css, html)
    html = re.sub(r'<script src="([^"]+\.js)"></script>', script, html)
    html = re.sub(r'<img\s[^>]*src="[^"]+\.svg"[^>]*>', image, html)
    html = re.sub(r'<link\s[^>]*rel="icon"[^>]*href="[^"]+\.svg"[^>]*>', icon, html)
    return html


def build():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    files = collect()
    still_needed = set()
    result = {}

    for rel, raw in files.items():
        if rel.endswith(".html"):
            result[rel] = inline_page(raw.decode("utf-8"), rel, files, still_needed).encode("utf-8")
        elif rel.startswith("js/lang-") and rel != "js/lang-de.js":
            # Fremdsprachige Woerterbuecher laedt der Seitenkopf nach (siehe I18N_LOADER); sie
            # stehen in keinem script-Tag und muessen deshalb ausdruecklich mit aufs Geraet.
            result[rel] = raw
        elif any(rel.startswith(d) for d in INGREDIENT_DIRS):
            continue  # weiter unten, falls doch noch gebraucht
        else:
            result[rel] = raw

    # Was eine Seite nicht einbetten konnte, muss weiterhin einzeln ausgeliefert werden.
    for rel in still_needed:
        if rel in files:
            result[rel] = files[rel]
            print("  [frontend] nicht eingebettet, wird einzeln ausgeliefert: %s" % rel)

    raw_total = 0
    out_total = 0
    for rel, data in sorted(result.items()):
        packed = rel.endswith(GZIP_EXTENSIONS)
        payload = gzip.compress(data, 9) if packed else data
        target = os.path.join(OUT, rel + (".gz" if packed else ""))

        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as handle:
            handle.write(payload)

        raw_total += len(data)
        out_total += len(payload)

    source_total = sum(len(v) for v in files.values())
    print("  [frontend] %d Quelldateien (%.1f KB) -> %d Dateien (%.1f KB gepackt)"
          % (len(files), source_total / 1024.0, len(result), out_total / 1024.0))
    print("  [frontend] eingebettet waeren es %.1f KB ungepackt" % (raw_total / 1024.0))


# Das Dateisystemabbild wird aus diesem Verzeichnis gebaut, nicht aus data/.
env.Replace(PROJECT_DATA_DIR=OUT)  # noqa: F821

# Nur bauen, wenn tatsaechlich ein Dateisystem gefragt ist — ein reiner Firmware-Build soll
# nicht jedes Mal das Frontend durchrechnen.
if any(t in ("buildfs", "uploadfs", "uploadfsota") for t in COMMAND_LINE_TARGETS):  # noqa: F821
    build()
