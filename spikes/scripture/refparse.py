"""Parses scripture references ("Rom 8:28-39", "1 Cor 13:4-7; 14:1", "John 3:16-4:2") and
resolves them to verses in a translation.

Conventions follow common usage (and Bible Gateway's behaviour):
- After a comma, a bare number is a verse in the current chapter ("John 3:16, 18"),
  unless the previous part named only chapters ("Ps 23, 24").
- After a semicolon, a bare number is a chapter ("John 3:16; 4").
- In a one-chapter book, "Jude 3" is verse 3, and "Jude 1" is the whole book.
- Verse letters ("16a") are accepted and ignored: slides show whole verses.
"""
import re
import sqlite3
from dataclasses import dataclass

from books import BOOKS, NAME, SINGLE_CHAPTER


class RefError(ValueError):
    pass


@dataclass(frozen=True)
class Range:
    book: str
    start_chapter: int
    start_verse: int | None  # None: from the start of the chapter
    end_chapter: int
    end_verse: int | None    # None: to the end of the chapter

    def __str__(self):
        sc, sv, ec, ev = self.start_chapter, self.start_verse, self.end_chapter, self.end_verse
        name = NAME.get(self.book, self.book)
        if self.book == "PSA" and sc == ec:
            name = "Psalm"  # "Psalm 23", "Psalms 1–2"
        if self.book in SINGLE_CHAPTER:  # "Jude 3-5", not "Jude 1:3-5"
            if sv is None and ev is None:
                return name
            return f"{name} {sv}" + (f"–{ev}" if ev != sv else "")
        if sv is None and ev is None:
            return f"{name} {sc}" + (f"–{ec}" if ec != sc else "")
        start = f"{sc}:{sv or 1}"
        if ec == sc:
            return f"{name} {start}" + (f"–{ev}" if ev not in (sv, None) else "")
        return f"{name} {start}–{ec}:{ev}" if ev else f"{name} {start}–{ec}"


# --- book names ---------------------------------------------------------------------------

_ORDINALS = {"i": "1", "ii": "2", "iii": "3", "first": "1", "second": "2", "third": "3",
             "1st": "1", "2nd": "2", "3rd": "3"}


def _fold(name):
    """'I Cor.' -> '1 cor', '1Co' -> '1 co', 'First  John' -> '1 john'."""
    s = re.sub(r"[.\s]+", " ", name.lower()).strip()
    s = re.sub(r"^(iii|ii|i|first|second|third|1st|2nd|3rd)\s+", lambda m: _ORDINALS[m.group(1)] + " ", s)
    s = re.sub(r"^([123])\s*", r"\1 ", s)
    return s


_ALIASES = {}
for _code, _name, _chapters, _extra in BOOKS:
    for _alias in [_name, _code, *_extra]:
        _ALIASES[_fold(_alias)] = _code
_FULL = [(_fold(name), code) for code, name, _c, _e in BOOKS]


def find_book(name):
    key = _fold(name)
    if key in _ALIASES:
        return _ALIASES[key]
    # Unique prefix of a full name: "Phili" -> Philippians, but "Ju" is Judges or Jude.
    hits = {code for full, code in _FULL if full.startswith(key)} if len(key.replace(" ", "")) >= 2 else set()
    if len(hits) == 1:
        return hits.pop()
    if hits:
        raise RefError(f"'{name}' could be {' or '.join(NAME[c] for c in sorted(hits, key=list(NAME).index))}")
    raise RefError(f"unknown book '{name}'")


def _known(name):
    try:
        find_book(name)
        return True
    except RefError:
        return False


# --- parsing ------------------------------------------------------------------------------

_BOOK = r"(?P<book>(?:[123]|iii|ii|i|first|second|third|1st|2nd|3rd)?\s*[a-z][a-z .]*?)"
_NUM = r"(?P<{}>\d+)[a-z]?"
_SPEC = (r"(?P<c1>\d+)[a-z]?(?:\s*[:.]\s*" + _NUM.format("v1") + r")?"
         r"(?:\s*-\s*(?:(?P<c2>\d+)\s*[:.]\s*)?" + _NUM.format("n2") + r")?")
_PART = re.compile(r"^\s*(?:" + _BOOK + r"\s*(?=\d))?" + _SPEC + r"\s*$")
_WHOLE_BOOK = re.compile(r"^\s*" + _BOOK + r"\s*$")


def _normalize(text):
    s = text.lower()
    s = re.sub(r"[‐-―−]", "-", s)                  # hyphens and dashes
    s = re.sub(r"\s+(?:to|through|thru)\s+", "-", s)
    s = re.sub(r"\b(?:verses?|vv?)\b\.?", " ", s)
    return s


def parse(text):
    """Returns a list of Ranges. Raises RefError with a message fit to show the operator."""
    ranges = []
    book = None
    chapter = None          # current chapter, for bare verses after a comma
    verse_mode = False      # whether the previous part named verses
    for sep, part in _split(_normalize(text)):
        if not part.strip():
            raise RefError(f"empty part in '{text.strip()}'")
        m = _PART.match(part)
        if not m:
            m2 = _WHOLE_BOOK.match(part)
            if m2:  # "Philemon", "Jude": the whole book only makes sense for one-chapter books
                try:
                    code = find_book(m2.group("book"))
                except RefError:
                    words = m2.group("book").split()
                    if any(_known(" ".join(words[:n])) for n in range(1, len(words))):
                        raise RefError(f"can't read '{part.strip()}'") from None  # "John three"
                    raise
                if code not in SINGLE_CHAPTER:
                    raise RefError(f"'{part.strip()}' needs a chapter")
                book, chapter, verse_mode = code, 1, False
                ranges.append(Range(code, 1, None, 1, None))
                continue
            raise RefError(f"can't read '{part.strip()}'")
        if m.group("book"):
            book = find_book(m.group("book"))
            chapter, verse_mode = None, False
        elif book is None:
            raise RefError(f"'{part.strip()}' needs a book name")

        c1, v1 = int(m.group("c1")), m.group("v1") and int(m.group("v1"))
        c2, n2 = m.group("c2") and int(m.group("c2")), m.group("n2") and int(m.group("n2"))
        bare_verse = (not m.group("book") and v1 is None and sep == "," and verse_mode)

        if book in SINGLE_CHAPTER and v1 is None and not bare_verse:
            if c2 is not None:
                raise RefError(f"{NAME[book]} has one chapter")
            if c1 == 1 and n2 is None:          # "Jude 1": the whole book
                r = Range(book, 1, None, 1, None)
                verse_mode = False
            else:                               # "Jude 3", "Jude 3-5"
                r = Range(book, 1, c1, 1, n2 or c1)
                verse_mode = True
            chapter = 1
        elif bare_verse:                        # "John 3:16, 18-20"
            if c2 is not None:
                r = Range(book, chapter, c1, c2, n2)
            else:
                r = Range(book, chapter, c1, chapter, n2 or c1)
        elif v1 is None:                        # chapters: "Ps 23", "Gen 1-2"
            if c2 is not None:
                raise RefError(f"can't read '{part.strip()}'")
            r = Range(book, c1, None, n2 or c1, None)
            verse_mode = False
        else:                                   # "John 3:16", "3:16-18", "3:16-4:2"
            r = Range(book, c1, v1, c2 or c1, n2 or v1) if (c2 or n2) else Range(book, c1, v1, c1, v1)
            verse_mode = True
        if (r.end_chapter, r.end_verse or 10**6) < (r.start_chapter, r.start_verse or 0):
            raise RefError(f"'{part.strip()}' ends before it starts")
        chapter = r.end_chapter
        ranges.append(r)
    return ranges


def _split(s):
    """Yields (separator before the part, part). Separators: ';' and ','."""
    sep = ";"
    for piece in re.split(r"([;,])", s):
        if piece in (";", ","):
            sep = piece
        else:
            yield sep, piece


# --- resolving against the database -------------------------------------------------------

@dataclass(frozen=True)
class Verse:
    book: str
    chapter: int
    verse: int
    verse_end: int
    text: str
    markup: str


def resolve(db: sqlite3.Connection, translation: str, ranges):
    """Returns the verses for the ranges, in order. Verses a translation leaves out come back
    with empty text. Raises RefError for chapters or verses that don't exist."""
    row = db.execute("SELECT id FROM translation WHERE abbrev = ?", (translation,)).fetchone()
    if not row:
        raise RefError(f"no translation '{translation}'")
    tid = row[0]
    out = []
    for r in ranges:
        last_chapter = _last_chapter(db, tid, r.book)
        if last_chapter is None:
            raise RefError(f"{translation} doesn't have {NAME.get(r.book, r.book)}")
        for c in (r.start_chapter, r.end_chapter):
            if not 1 <= c <= last_chapter:
                raise RefError(f"{NAME[r.book]} has {last_chapter} chapter{'s' if last_chapter > 1 else ''}"
                               f" in {translation}, not {c}")
        for c, v in ((r.start_chapter, r.start_verse), (r.end_chapter, r.end_verse)):
            last = _last_verse(db, tid, r.book, c)
            if v is not None and not 1 <= v <= last:
                where = NAME[r.book] if r.book in SINGLE_CHAPTER else f"{NAME[r.book]} {c}"
                raise RefError(f"{where} has {last} verses in {translation}, not {v}")
        start = (r.start_chapter, r.start_verse or 1)
        end = (r.end_chapter, r.end_verse or 10**6)
        # A merged verse (verse 19-27) is included when any of its verses is in range.
        out += [Verse(*row) for row in db.execute(
            """SELECT book, chapter, verse, verse_end, text, markup FROM verse
               WHERE translation_id = ? AND book = ? AND verse > 0
                 AND (chapter, verse_end) >= (?, ?) AND (chapter, verse) <= (?, ?)
               ORDER BY chapter, verse""",
            (tid, r.book, *start, *end))]
    return out


def _last_chapter(db, tid, book):
    return db.execute("SELECT max(chapter) FROM verse WHERE translation_id = ? AND book = ?", (tid, book)).fetchone()[0]


def _last_verse(db, tid, book, chapter):
    return db.execute("SELECT max(verse_end) FROM verse WHERE translation_id = ? AND book = ? AND chapter = ?",
                      (tid, book, chapter)).fetchone()[0] or 0
