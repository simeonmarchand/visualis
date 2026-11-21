"""Run: python3 -m unittest -v   (synthetic data only; no church files needed)"""
import unittest

from match import Label, match_sequence, parse_label
from pro7 import decode, rtf_to_text


class Wire(unittest.TestCase):
    def test_decode_nested(self):
        # field 3 (string "Hi"), field 12 (message {field 2: varint 1}), field 4 (varint 300)
        buf = b"\x1a\x02Hi" + b"\x62\x02\x10\x01" + b"\x20\xac\x02"
        m = decode(buf)
        self.assertEqual(m[3], [b"Hi"])
        self.assertEqual(decode(m[12][0]), {2: [1]})
        self.assertEqual(m[4], [300])


class Rtf(unittest.TestCase):
    def test_cocoa_rtf(self):
        # Backslashes doubled on purpose: this is RTF source, with a \\uN escape for the apostrophe.
        rtf = ("{\\rtf1\\ansi\\ansicpg1252\\cocoartf2761 {\\fonttbl\\f0\\fswiss Helvetica;}"
               "{\\colortbl;\\red255\\green255\\blue255;}\\pard\\qc\\f0\\fs120 \\cf1 "
               "I can\\uc0\\u8217 t win\\par Caf\\'e9 \\{x\\}}")
        self.assertEqual(rtf_to_text(rtf), "I can\u2019t win\nCaf\u00e9 {x}")

    def test_line_break_escape(self):
        self.assertEqual(rtf_to_text("{\\rtf1 one\\\ntwo}"), "one\ntwo")


class Labels(unittest.TestCase):
    def test_spellings(self):
        self.assertEqual(parse_label("Pre Chorus 1"), Label("prechorus", 1, ""))
        self.assertEqual(parse_label("PreChorus"), Label("prechorus", None, ""))
        self.assertEqual(parse_label("Pre-Chorus 2"), Label("prechorus", 2, ""))
        self.assertEqual(parse_label("Refain 3"), Label("refrain", 3, ""))   # typo in a real library
        self.assertEqual(parse_label("VAMP"), Label("vamp", None, ""))
        self.assertEqual(parse_label("Spanish - Chorus 1"), Label("chorus", 1, "spanish"))
        self.assertEqual(parse_label("Bridge 1 Tamil"), Label("bridge", 1, "tamil"))

    def test_sequence(self):
        groups = ["Intro", "Verse 1", "PreChorus", "Chorus", "Pre-Chorus 2", "Bridge 1", "Spanish - Chorus"]
        steps, unmatched = match_sequence(
            ["Intro", "Verse 1", "Pre Chorus 1", "Chorus", "Pre Chorus 2", "Bridge 1", "Bridge 2", "Chorus"], groups)
        self.assertEqual([h for _, h in steps],
                         ["Intro", "Verse 1", "PreChorus", "Chorus", "Pre-Chorus 2", "Bridge 1", None, "Chorus"])
        self.assertEqual(unmatched, ["Bridge 2"])

    def test_numbering_differs(self):
        # "Chorus" matches the only chorus even when it's numbered, and "Chorus 1" the unnumbered one.
        self.assertEqual(match_sequence(["Chorus"], ["Chorus 1"])[0], [("Chorus", "Chorus 1")])
        self.assertEqual(match_sequence(["Chorus 1"], ["Chorus"])[0], [("Chorus 1", "Chorus")])
        # With two numbered choruses, a bare "Chorus" is ambiguous.
        self.assertEqual(match_sequence(["Chorus"], ["Chorus 2", "Chorus 3"])[1], ["Chorus"])

    def test_language_groups_kept_apart(self):
        steps, _ = match_sequence(["Chorus"], ["Chorus", "Spanish - Chorus"], language="spanish")
        self.assertEqual(steps, [("Chorus", "Spanish - Chorus")])


if __name__ == "__main__":
    unittest.main()
