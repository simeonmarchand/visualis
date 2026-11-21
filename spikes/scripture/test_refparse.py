"""Run: python3 -m unittest -v   (resolve tests need data/bible.db; see README)"""
import sqlite3
import unittest
from pathlib import Path

from refparse import Range, RefError, find_book, parse, resolve

DB = Path(__file__).parent / "data" / "bible.db"


def R(book, sc, sv, ec, ev):
    return Range(book, sc, sv, ec, ev)


class BookNames(unittest.TestCase):
    def test_names_people_type(self):
        cases = {
            "Romans": "ROM", "Rom": "ROM", "rom.": "ROM", "Ro": "ROM",
            "1 Cor": "1CO", "1Cor": "1CO", "1 Co": "1CO", "I Corinthians": "1CO", "First Corinthians": "1CO",
            "1st Corinthians": "1CO", "II Tim": "2TI", "3 John": "3JN", "iii jn": "3JN",
            "Song of Songs": "SNG", "Song of Solomon": "SNG", "Psalm": "PSA", "Ps": "PSA", "Psalms": "PSA",
            "Phil": "PHP", "Philem": "PHM", "Jude": "JUD", "Judg": "JDG", "Jn": "JHN", "Jon": "JON",
            "Ezek": "EZK", "Rev": "REV", "Revelations": "REV", "Matt": "MAT", "Mk": "MRK", "JHN": "JHN",
            "Phili": "PHP", "Eccl": "ECC", "Deut": "DEU",
        }
        for text, code in cases.items():
            with self.subTest(text=text):
                self.assertEqual(find_book(text), code)

    def test_ambiguous_and_unknown(self):
        with self.assertRaisesRegex(RefError, "Judges or Jude"):
            find_book("Ju")
        with self.assertRaisesRegex(RefError, "unknown book"):
            find_book("Hezekiah")


class Parsing(unittest.TestCase):
    def test_single_ranges(self):
        cases = {
            "John 3:16": [R("JHN", 3, 16, 3, 16)],
            "Rom 8:28-39": [R("ROM", 8, 28, 8, 39)],
            "Rom 8:28–39": [R("ROM", 8, 28, 8, 39)],            # en dash
            "Rom 8:28 to 39": [R("ROM", 8, 28, 8, 39)],
            "John 3:16-4:2": [R("JHN", 3, 16, 4, 2)],           # across chapters
            "1 John 1:5–2:2": [R("1JN", 1, 5, 2, 2)],
            "Ps 23": [R("PSA", 23, None, 23, None)],
            "Gen 1-2": [R("GEN", 1, None, 2, None)],
            "John 3.16": [R("JHN", 3, 16, 3, 16)],              # period as separator
            "John 3:16a": [R("JHN", 3, 16, 3, 16)],             # verse letter ignored
            "1 Cor. 13:4-7": [R("1CO", 13, 4, 13, 7)],
            "Psalm 119:105": [R("PSA", 119, 105, 119, 105)],
            "  rom  8 : 28 - 39 ": [R("ROM", 8, 28, 8, 39)],
        }
        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(parse(text), expected)

    def test_lists(self):
        self.assertEqual(parse("John 3:16, 18"), [R("JHN", 3, 16, 3, 16), R("JHN", 3, 18, 3, 18)])
        self.assertEqual(parse("John 3:16, 18-20"), [R("JHN", 3, 16, 3, 16), R("JHN", 3, 18, 3, 20)])
        self.assertEqual(parse("John 3:16; 4"), [R("JHN", 3, 16, 3, 16), R("JHN", 4, None, 4, None)])
        self.assertEqual(parse("John 3:16; 4:1-3"), [R("JHN", 3, 16, 3, 16), R("JHN", 4, 1, 4, 3)])
        self.assertEqual(parse("Ps 23, 24"), [R("PSA", 23, None, 23, None), R("PSA", 24, None, 24, None)])
        self.assertEqual(parse("Rom 8:28; 1 Cor 13:4-7"), [R("ROM", 8, 28, 8, 28), R("1CO", 13, 4, 13, 7)])
        self.assertEqual(parse("Rom 8:28, 1 Cor 13:4"), [R("ROM", 8, 28, 8, 28), R("1CO", 13, 4, 13, 4)])

    def test_one_chapter_books(self):
        self.assertEqual(parse("Jude 3"), [R("JUD", 1, 3, 1, 3)])
        self.assertEqual(parse("Jude 3-5"), [R("JUD", 1, 3, 1, 5)])
        self.assertEqual(parse("Jude 1"), [R("JUD", 1, None, 1, None)])
        self.assertEqual(parse("Jude 1:3"), [R("JUD", 1, 3, 1, 3)])
        self.assertEqual(parse("Jude"), [R("JUD", 1, None, 1, None)])
        self.assertEqual(parse("Philemon 4, 6"), [R("PHM", 1, 4, 1, 4), R("PHM", 1, 6, 1, 6)])

    def test_errors(self):
        for text, message in {
            "Romans": "needs a chapter",
            "3:16": "needs a book",
            "John 3:18-16": "ends before it starts",
            "John 4-3": "ends before it starts",
            "John 3:16,": "empty part",
            "Hezekiah 3": "unknown book",
            "John three": "can't read",
        }.items():
            with self.subTest(text=text), self.assertRaisesRegex(RefError, message):
                parse(text)

    def test_display(self):
        self.assertEqual(str(parse("rom 8:28-39")[0]), "Romans 8:28–39")
        self.assertEqual(str(parse("jn 3:16-4:2")[0]), "John 3:16–4:2")
        self.assertEqual(str(parse("ps 23")[0]), "Psalm 23")
        self.assertEqual(str(parse("ps 23:1-3")[0]), "Psalm 23:1\u20133")
        self.assertEqual(str(parse("ps 1-2")[0]), "Psalms 1\u20132")
        self.assertEqual(str(parse("jude 3-5")[0]), "Jude 3–5")
        self.assertEqual(str(parse("jude")[0]), "Jude")


@unittest.skipUnless(DB.exists(), "data/bible.db not built")
class Resolving(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = sqlite3.connect(DB)

    def refs(self, text, tr="KJV"):
        return [(v.chapter, v.verse) for v in resolve(self.db, tr, parse(text))]

    def test_ranges(self):
        self.assertEqual(len(self.refs("Rom 8:28-39")), 12)
        self.assertEqual(self.refs("John 3:35-4:2"), [(3, 35), (3, 36), (4, 1), (4, 2)])
        self.assertEqual(len(self.refs("Ps 119")), 176)
        self.assertEqual(len(self.refs("Gen 1-2")), 31 + 25)
        self.assertEqual(self.refs("Jude 3-4"), [(1, 3), (1, 4)])
        self.assertEqual(len(self.refs("Jude")), 25)

    def test_text(self):
        [v] = resolve(self.db, "KJV", parse("John 11:35"))
        self.assertEqual(v.text, "Jesus wept.")
        [v] = resolve(self.db, "KJV", parse("Ps 23:1"))
        self.assertEqual(v.text, "The LORD is my shepherd; I shall not want.")
        self.assertEqual(v.markup, "The <sc>LORD</sc> <i>is</i> my shepherd; I shall not want.")

    def test_verse_left_out_of_a_translation(self):
        [v] = resolve(self.db, "WEB", parse("Acts 8:37"))
        self.assertEqual(v.text, "")
        [v] = resolve(self.db, "KJV", parse("Acts 8:37"))
        self.assertTrue(v.text.startswith("And Philip said"))

    def test_merged_verse_found_from_any_of_its_verses(self):
        # WEB merges Sirach 26:19-27 into one verse.
        verses = resolve(self.db, "WEB", [Range("SIR", 26, 20, 26, 20)])
        self.assertEqual([(v.verse, v.verse_end) for v in verses], [(19, 27)])

    def test_errors(self):
        for text, message in {
            "John 22": "John has 21 chapters in KJV, not 22",
            "John 3:37": "John 3 has 36 verses in KJV, not 37",
            "Jude 26": "Jude has 25 verses in KJV, not 26",
        }.items():
            with self.subTest(text=text), self.assertRaisesRegex(RefError, message):
                resolve(self.db, "KJV", parse(text))
        with self.assertRaisesRegex(RefError, "no translation"):
            resolve(self.db, "NIV", parse("John 1"))


if __name__ == "__main__":
    unittest.main()
