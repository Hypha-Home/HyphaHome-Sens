"""Versieht die festen Texte der Seiten in data/*.html mit data-t="schluessel".

Aufruf (aus der Projektwurzel):
    python scripts/i18n_extract.py            Trockenlauf: zeigt, was getan wuerde
    python scripts/i18n_extract.py --apply    schreibt die Seiten um

Was erkannt wird
  - Ein Element, dessen Inhalt nur Text und einfache Auszeichnungen (strong, em, br, ...) ist,
    bekommt data-t. Der deutsche Text bleibt im Element stehen.
  - Steht Text neben anderen Elementen (z.B. <label><input> Text</label>), wird der Text in
    <span data-t> gepackt.
  - placeholder und title-Attribute bekommen data-t-attr.
Schluessel gleicher Texte werden wiederverwendet (auch aus bereits umgestellten Seiten). Neue
Schluessel entstehen aus dem Text (ae/oe/ue/ss, Kleinbuchstaben, hoechstens sechs Woerter).

Ausgabe nach <scratch>/new_keys.json bzw. dem mit --out angegebenen Pfad: {schluessel: deutscher
Text} der neu vergebenen Schluessel - die Vorlage fuer die Uebersetzung.

Nicht angefasst werden Elemente mit data-field, data-status oder data-points (ihren Inhalt setzt
das Skript), Skripte, Stile, SVG und Texte ohne mindestens zwei Buchstaben oder nur aus
Grossbuchstaben (Produktnamen wie SHT41, MQTT).
"""

import glob
import html
import json
import os
import re
import sys
from html.parser import HTMLParser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")

VOID = {"meta", "link", "input", "br", "hr", "img", "area", "base", "col", "embed", "source", "track", "wbr"}
FORMAT = {"strong", "b", "em", "i", "br", "code", "sup", "sub", "small", "u"}
SKIP_TAGS = {"script", "style", "svg", "textarea", "pre"}
SKIP_ATTRS = {"data-field", "data-status", "data-points", "data-t", "data-bool", "data-good"}
ATTR_TARGETS = ("placeholder", "title", "aria-label")


class Node:
    def __init__(self, tag, attrs, start, open_end):
        self.tag = tag
        self.attrs = dict(attrs)
        self.start = start          # Anfang des Starttags
        self.open_end = open_end    # Ende des Starttags
        self.close_start = open_end # Anfang des Endtags
        self.close_end = open_end   # Ende des Endtags
        self.children = []
        self.parent = None


class Tree(HTMLParser):
    def __init__(self, source):
        super().__init__(convert_charrefs=False)
        self.source = source
        self.lines = [0]
        for match in re.finditer("\n", source):
            self.lines.append(match.end())
        self.root = Node("#root", [], 0, 0)
        self.stack = [self.root]

    def where(self):
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def handle_starttag(self, tag, attrs):
        start = self.where()
        open_end = start + len(self.get_starttag_text())
        node = Node(tag, attrs, start, open_end)
        node.parent = self.stack[-1]
        node.parent.children.append(node)
        if tag in VOID:
            node.close_start = node.close_end = open_end
        else:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID:
            node = self.stack.pop()
            node.close_start = node.close_end = node.open_end

    def handle_endtag(self, tag):
        start = self.where()
        end = self.source.index(">", start) + 1
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                node = self.stack[i]
                node.close_start = start
                node.close_end = end
                del self.stack[i:]
                return


def letters(text):
    return len(re.findall(r"[^\W\d_]", text))


def normalize(inner):
    """Inhalt wie im Woerterbuch: Leerraum gekuerzt, Zeichenverweise aufgeloest (ausser <, >, &)."""
    parts = re.split(r"(<[^>]+>)", inner)
    out = []
    for part in parts:
        if part.startswith("<") and part.endswith(">"):
            out.append(part)
        else:
            text = html.unescape(part)
            out.append(text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))
    return " ".join("".join(out).split())


def plain_text(inner):
    return html.unescape(re.sub(r"<[^>]+>", "", inner))


BRAND = {"Hypha Home - Sens", "Hypha Home Sens", "by EideardVMR"}


def skippable(text):
    text = " ".join(html.unescape(re.sub(r"<[^>]+>", "", text)).split())
    bare = " ".join(re.sub(r"[^\w\s]|_", "", text).split())
    if text in BRAND or bare in BRAND or re.fullmatch(r"[A-Za-z]+\d+", text):
        return True
    if letters(text) < 2:
        return True
    return text == text.upper()      # nur Grossbuchstaben/Ziffern: Produktname


def slug(text):
    text = html.unescape(re.sub(r"<[^>]+>", "", text)).lower()
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss")):
        text = text.replace(a, b)
    words = re.findall(r"[a-z0-9]+", text)
    return "_".join(words[:6]) or "text"


def is_format_only(node):
    for child in node.children:
        if child.tag not in FORMAT or set(child.attrs) - {"class"}:
            return False
        if child.children and not is_format_only(child):
            return False
    return True


def main():
    apply = "--apply" in sys.argv
    out = os.path.join(os.environ.get("TEMP", "."), "new_keys.json")
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]

    pages = {}
    trees = {}
    for path in sorted(glob.glob(os.path.join(DATA, "*.html"))):
        source = open(path, "rb").read().decode("utf-8")
        crlf = "\r\n" in source
        source = source.replace("\r\n", "\n")
        tree = Tree(source)
        tree.feed(source)
        pages[path] = (source, crlf)
        trees[path] = tree

    # Bekannte Schluessel aus schon umgestellten Elementen
    known = {}      # normalisierter Text -> Schluessel
    used = set()

    def harvest(tree, node):
        key = node.attrs.get("data-t")
        if key:
            used.add(key)
            inner = tree.source[node.open_end:node.close_start]
            if is_format_only(node):
                known.setdefault(normalize(inner), key)
        for child in node.children:
            harvest(tree, child)

    for tree in trees.values():
        harvest(tree, tree.root)

    new_keys = {}

    def key_for(text):
        if text in known:
            return known[text]
        base = "t." + slug(text)
        key, n = base, 2
        while key in used:
            key = "%s_%d" % (base, n)
            n += 1
        used.add(key)
        known[text] = key
        new_keys[key] = text
        return key

    total_edits = 0
    for path, (source, crlf) in pages.items():
        tree = trees[path]
        edits = []      # (position, replace_length, text)

        def visit(node, in_body):
            if node.tag in SKIP_TAGS:
                return
            if node.tag == "body":
                in_body = True
            if node.tag == "title" or in_body:
                handle(node)
            if getattr(node, "whole", False):
                return      # Inhalt ging als Ganzes ins Woerterbuch, die Auszeichnungen darin nicht einzeln
            for child in node.children:
                visit(child, in_body)

        def handle(node):
            if node.tag in ("#root", "html", "head", "body"):
                return
            if "data-t" in node.attrs and is_format_only(node):
                node.whole = True       # schon umgestellt; die Auszeichnungen darin gehoeren dazu
            # Attribute
            if not SKIP_ATTRS & set(node.attrs):
                pairs = []
                for attr in ATTR_TARGETS:
                    value = node.attrs.get(attr)
                    if value and not skippable(plain_text(value)) and "data-t-attr" not in node.attrs:
                        pairs.append((attr, key_for(normalize(value))))
                if pairs:
                    tag_end = node.start + 1 + len(node.tag)
                    edits.append((tag_end, 0, ' data-t-attr="%s"' % ";".join("%s:%s" % p for p in pairs)))

            if SKIP_ATTRS & set(node.attrs) or node.tag in VOID:
                return
            inner = tree.source[node.open_end:node.close_start]
            if not node.children or is_format_only(node):
                if node.children == [] and skippable(inner):
                    return
                if node.children and skippable(plain_text(inner)):
                    return
                if node.children == [] and not inner.strip():
                    return
                key = key_for(normalize(inner))
                edits.append((node.start + 1 + len(node.tag), 0, ' data-t="%s"' % key))
                node.attrs["data-t"] = key
                node.whole = True
                return
            # Gemischt: Text neben anderen Elementen einzeln einpacken
            position = node.open_end
            spans = []
            for child in node.children:
                spans.append((position, child.start))
                position = child.close_end if child.tag not in VOID else child.open_end
            spans.append((position, node.close_start))
            for a, b in spans:
                segment = tree.source[a:b]
                if "<!--" in segment or skippable(segment):
                    continue
                lead = len(segment) - len(segment.lstrip())
                trail = len(segment) - len(segment.rstrip())
                text = normalize(segment.strip())
                key = key_for(text)
                edits.append((a + lead, len(segment) - lead - trail,
                              '<span data-t="%s">%s</span>' % (key, segment.strip())))

        visit(tree.root, False)

        if edits:
            print("%s: %d Aenderungen" % (os.path.basename(path), len(edits)))
            total_edits += len(edits)
        if apply and edits:
            result = source
            for position, length, text in sorted(edits, key=lambda e: e[0], reverse=True):
                result = result[:position] + text + result[position + length:]
            if crlf:
                result = result.replace("\n", "\r\n")
            with open(path, "wb") as handle:
                handle.write(result.encode("utf-8"))

    with open(out, "w", encoding="utf-8") as handle:
        json.dump(new_keys, handle, ensure_ascii=False, indent=1)
    print("%d Aenderungen, %d neue Schluessel -> %s%s" % (total_edits, len(new_keys), out,
                                                         "" if apply else "   (Trockenlauf)"))


if __name__ == "__main__":
    main()
