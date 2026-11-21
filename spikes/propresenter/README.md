# ProPresenter 7 import spike

Throwaway prototype: can Visualis read the church's ProPresenter 7 library (songs with groups, arrangements, and slides), and can a Planning Center arrangement sequence be matched to those groups? Added after P3 showed Planning Center usually has no lyrics as text (plan 7.4). Python, standard library only.

## Run

```sh
cd spikes/propresenter
python3 -m unittest -v      # synthetic data only
./survey.py songs/          # counts and names only, never lyrics
```

Put `.pro` files in `songs/`, which is git-ignored (church content and copyrighted lyrics).

## Files

- `pro7.py`: a protobuf reader with no schema compiler, a minimal RTF-to-text converter, and `read(path)`, which returns groups (name, color, slides), arrangements, and credits. Field numbers come from the MIT-licensed reverse-engineered schema at github.com/greyshirtguy/ProPresenter7-Proto.
- `match.py`: matches Planning Center sequence labels to group names.
- `survey.py`: library report.

## Findings

From the church's library: 151 files, 4.2 MB, read 2026-11-21.

1. **Everything reads.** All 151 files read in 0.25 s with no failures.
   - Every arrangement step points at a real group.
   - My RTF converter matched macOS's `textutil` on all 300 sampled slides, out of 3,414 text elements.
2. **Songs come in exactly as Visualis models them.** 132 songs have 1,453 groups and 2,488 slides.
   - Each group has a name and a color; 126 groups have no color.
   - Arrangements are named lists of groups: 107 songs have one, 17 have two, and 8 have none (play groups in order).
   - Lyric-less sections (Intro, Turnaround, Interlude, Ending) are groups with one blank slide; 485 slides are blank.
3. **Slides are short**: 2 lines on 52% of slides, 3 lines on 19%, rarely more than 4.
4. **Group names are inconsistent**: 70 distinct names in 132 songs.
   - Spelling variants: PreChorus, Pre-Chorus 2, Pre Chorus; VAMP and Vamp; "tag 2".
   - A typo: "Refain 3".
   - Language-tagged groups: "Spanish - Chorus 1", "Tamil - Verse 1", "Bridge 1 Tamil". Some songs are sung in several languages, with each language's sections as separate groups.
   - Three unnamed groups.
5. **The ProPresenter files hold no credits or metadata**: no CCLI number, author, key, or category in any of the 151. Credits will come from Planning Center, which had them.
6. **Use the file name as the title.** The name stored inside a file is often wrong: 26 say "Untitled", one says "Verse 1", and one "Glorious Day-1.pro".
7. **Scripture and sermons are plain text presentations.** "Acts 16 22-26 NLT", "Isaiah 53_4_5", and "Acts 21721" are one unnamed group of pasted text with no scripture metadata. The reference is only in the file name, written around characters file names can't hold. Sermon presentations ("Step Out of Fear into Faith") are the same.
8. **Matching Planning Center's sequence to groups works, and flags what it can't match.** For "I Thank God", 34 of 36 sequence steps matched.
   - "Pre Chorus 1" matched "PreChorus", and "Pre Chorus 2" matched "Pre-Chorus 2".
   - "Bridge 2" and "Bridge 3" are left unmatched. The ProPresenter song has only "Bridge 1" (four slides, repeated), so the operator has to decide.
   - The two ProPresenter arrangements of that song are identical, which is worth a look in the library itself.

## Not covered

- Slide formatting (fonts, sizes, positions), backgrounds, and media cues: songs will use Visualis templates instead.
- ProPresenter 6 (`.pro6`, XML) files. None are in this library.
- Writing `.pro` files. Import only.
