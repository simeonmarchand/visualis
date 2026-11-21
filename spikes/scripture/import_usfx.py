#!/usr/bin/env python3
"""Imports a USFX Bible (eBible.org format) into SQLite.

    ./import_usfx.py data/download/eng-kjv2006_usfx/eng-kjv2006_usfx.xml data/bible.db \
        --abbrev KJV --name "King James Version" --license public-domain

USFX marks verses as milestones: a verse's text runs from <v id="N"/> to <ve/>, even across
paragraph and poetry-line elements. Footnotes, cross-references, and headings are dropped.
Psalm titles (<d>) are kept as verse 0. A merged verse such as <v id="19-27"/> is one row with
verse=19, verse_end=27.

Each verse is stored twice: `text` (plain, for search and slides) and `markup`, which keeps
the formatting a template may want: <wj> words of Jesus, <i> translator-added words (KJV
italics), <sc> small caps (LORD), and <br> between poetry lines.
"""
import argparse
import re
import sqlite3
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS translation (
    id INTEGER PRIMARY KEY,
    abbrev TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    license_basis TEXT NOT NULL,   -- plan 7.1: nothing is imported without one
    attribution TEXT NOT NULL,
    source TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS book (
    translation_id INTEGER NOT NULL REFERENCES translation(id),
    code TEXT NOT NULL,            -- USFX/Paratext code, e.g. JHN
    ord INTEGER NOT NULL,          -- order in this translation
    name TEXT NOT NULL,            -- this translation's name for it
    PRIMARY KEY (translation_id, code)
);
CREATE TABLE IF NOT EXISTS verse (
    translation_id INTEGER NOT NULL REFERENCES translation(id),
    book TEXT NOT NULL,
    chapter INTEGER NOT NULL,
    verse INTEGER NOT NULL,        -- 0 = psalm title
    verse_end INTEGER NOT NULL,    -- > verse for merged verses
    paragraph INTEGER NOT NULL,    -- 1 if a new paragraph starts at this verse
    text TEXT NOT NULL,
    markup TEXT NOT NULL,
    PRIMARY KEY (translation_id, book, chapter, verse)
) WITHOUT ROWID;
"""

# Elements whose content is dropped. Their tail text still belongs to the parent.
SKIP = {"f", "fe", "x", "s", "toc", "id", "h", "ide", "rem", "fig", "languageCode", "periph", "generated"}
# Formatting kept in markup, as element -> tag.
MARKUP = {"wj": "wj", "add": "i", "nd": "sc"}
PILCROW = "¶"


class Importer:
    def __init__(self):
        self.book = None
        self.chapter = 0
        self.verse = None          # (start, end) while inside a verse
        self.title = False         # inside a <d> psalm title
        self.text, self.markup = [], []
        self.paragraph = False
        self.pending_paragraph = False  # a paragraph opened between verses; applies to the next one
        self.rows = {}             # (book, chapter, verse) -> row
        self.dropped = []          # text found outside any verse or title, for the report

    def emit(self, s):
        if not s:
            return
        if self.verse or self.title:
            if PILCROW in s:
                self.paragraph = True
                s = s.replace(PILCROW, "")
            self.text.append(s)
            self.markup.append(s.replace("&", "&amp;").replace("<", "&lt;"))
        elif s.strip():
            self.dropped.append((self.book, self.chapter, s.strip()))

    def flush(self):
        key_verse = 0 if self.title else (self.verse[0] if self.verse else None)
        if key_verse is not None:
            text = re.sub(r"\s+", " ", "".join(self.text)).strip()
            text = re.sub(r" ([,.;:!?’”)])", r"\1", text)
            markup = re.sub(r"\s+", " ", "".join(self.markup)).strip()
            markup = re.sub(r"\s*<br>\s*", "<br>", markup).removeprefix("<br>")
            markup = re.sub(r"</(wj|i|sc)>\s*<\1>", " ", markup)   # adjacent spans split by a footnote
            markup = re.sub(r"\s+</", "</", markup)
            # A verse with no text is kept: some translations leave verses out (WEB Acts 8:37 is
            # only a footnote), and the app must know the verse exists to show that.
            if text or not self.title:
                key = (self.book, self.chapter, key_verse)
                end = key_verse if self.title else self.verse[1]
                if key in self.rows:  # a verse split by a skipped element: join the parts
                    old = self.rows[key]
                    self.rows[key] = (*old[:3], max(old[3], end), old[4], old[5] + " " + text, old[6] + " " + markup)
                else:
                    self.rows[key] = (self.book, self.chapter, key_verse, end, int(self.paragraph), text, markup)
        self.text, self.markup, self.paragraph = [], [], False

    def walk(self, el):
        tag = el.tag
        if tag == "book":
            self.flush()
            self.verse, self.book, self.chapter = None, el.get("id"), 0
        elif tag == "c":
            self.flush()
            self.verse, self.chapter = None, int(el.get("id"))
        elif tag == "v":
            self.flush()
            start, _, end = el.get("id").partition("-")
            self.verse = (int(start), int(end or start))
            self.paragraph, self.pending_paragraph = self.pending_paragraph, False
        elif tag == "ve":
            self.flush()
            self.verse = None
        elif tag == "d" and not self.verse:
            self.flush()
            self.title = True
        elif tag in ("q", "qs") and self.verse and self.text:
            self.markup.append("<br>")
        elif tag == "p" and self.verse is None and el.get("style") not in ("mt", "mt1", "mt2", "ms", "mr", "cl"):
            self.pending_paragraph = True

        if tag in SKIP:
            return
        wrap = MARKUP.get(tag) if (self.verse or self.title) else None
        if wrap:
            self.markup.append(f"<{wrap}>")
        self.emit(el.text)
        for child in el:
            self.walk(child)
            self.emit(child.tail)
        if wrap:
            self.markup.append(f"</{wrap}>")
        if tag == "d" and self.title:
            self.flush()
            self.title = False


def book_names(path):
    names = {}
    if path and Path(path).exists():
        for b in ET.parse(path).getroot():
            names[b.get("code")] = b.get("short") or b.get("code")
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("usfx")
    ap.add_argument("db")
    ap.add_argument("--abbrev", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--license", required=True, help="license basis, e.g. public-domain (plan 7.1)")
    ap.add_argument("--attribution", default="")
    ap.add_argument("--booknames", help="BookNames.xml; default: next to the USFX file")
    args = ap.parse_args()

    t0 = time.perf_counter()
    root = ET.parse(args.usfx).getroot()
    imp = Importer()
    imp.walk(root)
    imp.flush()
    t_parse = time.perf_counter() - t0

    names = book_names(args.booknames or Path(args.usfx).parent / "BookNames.xml")
    books = list(dict.fromkeys(r[0] for r in imp.rows.values()))

    db = sqlite3.connect(args.db)
    db.executescript(SCHEMA)
    with db:
        old = db.execute("SELECT id FROM translation WHERE abbrev = ?", (args.abbrev,)).fetchone()
        if old:
            db.execute("DELETE FROM verse WHERE translation_id = ?", old)
            db.execute("DELETE FROM book WHERE translation_id = ?", old)
            db.execute("DELETE FROM translation WHERE id = ?", old)
        tid = db.execute(
            "INSERT INTO translation (abbrev, name, license_basis, attribution, source) VALUES (?, ?, ?, ?, ?)",
            (args.abbrev, args.name, args.license, args.attribution, Path(args.usfx).name),
        ).lastrowid
        db.executemany("INSERT INTO book VALUES (?, ?, ?, ?)",
                       [(tid, code, i + 1, names.get(code, code)) for i, code in enumerate(books)])
        db.executemany("INSERT INTO verse VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                       [(tid, *row) for row in imp.rows.values()])
    t_total = time.perf_counter() - t0

    rows = imp.rows.values()
    print(f"{args.abbrev}: {len(books)} books, {sum(1 for r in rows if r[2] > 0)} verse rows "
          f"({sum(1 for r in rows if r[3] > r[2])} merged, {sum(1 for r in rows if r[2] > 0 and not r[5])} empty), {sum(1 for r in rows if r[2] == 0)} psalm titles; "
          f"parse {t_parse:.2f} s, total {t_total:.2f} s")
    if imp.dropped:
        print(f"text outside verses, dropped: {len(imp.dropped)} pieces, e.g.", file=sys.stderr)
        for d in imp.dropped[:8]:
            print("   ", d, file=sys.stderr)


if __name__ == "__main__":
    main()
