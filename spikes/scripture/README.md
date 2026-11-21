# P5: Scripture data spike

Throwaway prototype for plan 3.1 (P5): load public-domain Bibles from an open format into SQLite and resolve references, including ranges across chapters. Python with only the standard library; the findings carry over to the C++ app in Phase 4.

## Run

```sh
cd spikes/scripture
./scripts/build-db.sh                     # downloads KJV and WEB (USFX) from eBible.org, builds data/bible.db
python3 -m unittest -v                    # parser tests; lookup tests use data/bible.db
./passage.py "Rom 8:28-39" --tr KJV,WEB   # add --markup to see the formatting markup
```

`data/` is git-ignored. Both translations are public domain.

## Files

- `books.py`: the 66 books, their codes, chapter counts, and the names people type.
- `import_usfx.py`: USFX to SQLite (`translation`, `book`, `verse`). The schema is in the file.
- `refparse.py`: `parse("1 Cor 13:4-7; 14:1")` returns ranges, and `resolve(db, "KJV", ranges)` returns verses. It raises `RefError` with a message fit to show the operator.
- `passage.py`: looks up a passage and prints it side by side.

## Findings

Measured on the development Mac (M1), 2026-11-21.

1. **USFX imports cleanly.**
   - KJV: 66 books, 31,102 verses, 116 psalm titles, about 1.0 s.
   - WEB: 81 books (including the deuterocanon), 38,058 verses, 118 psalm titles, about 1.4 s.
   - The database is 22 MB for both, with each verse stored as plain text and as markup.
2. **Lookups are instant.** Any passage resolves in about 1 ms, including all 176 verses of Psalm 119 in two translations. Phase 4's target is 2 s.
3. **Verses can be missing from a translation.** WEB leaves out 29 verses that older manuscripts don't have (Acts 8:37, Luke 17:36, and others), giving them only as footnotes. NLT, CSB, and ESV do the same; KJV keeps them. The importer stores these as empty rows so the app knows the verse exists. Side-by-side display has to show the gap instead of misaligning, which `passage.py` does.
4. **Some verses are merged.** WEB merges a few verses into one (Sirach 26:19–27). They're stored as one row with `verse_end`, and a lookup for any verse inside the range finds it.
5. **Verse numbering otherwise matches across English translations.** KJV and WEB differ in one place in the 66 books: Romans 16:25–27 (the closing doxology), which WEB places at 14:24–26. Multi-translation display can pair verses by number, as long as it handles gaps and this kind of move.
6. **Psalm titles are outside any verse** in English Bibles. They're stored as verse 0, so templates can choose whether to show them.
7. **Formatting worth keeping is in the data.**
   - words of Jesus (`<wj>`, for red-letter display)
   - KJV words added by the translators (`<i>`, printed in italics)
   - small caps for LORD (`<sc>`)
   - poetry line breaks (`<br>`)
   - KJV paragraph marks (`¶`), stored as a paragraph flag on the verse

   The `markup` column keeps them, and templates decide what to use.
8. **Reference parsing follows common usage.**
   - After a comma a bare number is a verse ("John 3:16, 18"); after a semicolon it's a chapter ("John 3:16; 4").
   - In a one-chapter book, "Jude 3" is verse 3 and "Jude 1" is the whole book, as on Bible Gateway.
   - Ambiguous abbreviations ask instead of guessing ("'Ju' could be Judges or Jude").
   - A single psalm displays as "Psalm 23".

## Not covered

- Splitting a passage into slides by length (Phase 4).
- Paste-in passages for licensed translations (plan 7.1). Text copied from Bible Gateway, YouVersion, or a publisher's site comes in different shapes, and needs its own small spike on real pasted samples.
- The deuterocanonical books in the reference parser. The church uses the 66-book canon.
