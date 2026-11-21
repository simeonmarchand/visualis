#!/usr/bin/env python3
"""Reads every .pro file in a folder and reports what the library contains: parse failures,
kinds of presentation, group names, arrangements, credits. Prints counts and names only,
never lyrics.

    ./survey.py songs/
"""
import collections
import sys
import time
from pathlib import Path

import pro7


def kind(song):
    if song.bible_reference:
        return "scripture"
    names = {g.name.lower() for g in song.groups}
    if any(n.startswith(("verse", "chorus", "bridge", "pre", "tag", "refrain", "intro", "ending")) for n in names):
        return "song"
    return "other"


def main(folder):
    files = sorted(Path(folder).glob("*.pro"))
    t0 = time.perf_counter()
    songs, failures = [], []
    for f in files:
        try:
            songs.append((f, pro7.read(f)))
        except Exception as e:  # report every file that fails, not just the first
            failures.append((f.name, f"{type(e).__name__}: {e}"))
    elapsed = time.perf_counter() - t0

    print(f"{len(files)} files read in {elapsed:.2f} s; {len(failures)} failed")
    for name, err in failures:
        print(f"  FAILED {name}: {err}")

    kinds = collections.Counter(kind(s) for _, s in songs)
    print("kinds:", dict(kinds))
    lyric = [s for _, s in songs if kind(s) == "song"]

    group_names = collections.Counter(g.name for s in lyric for g in s.groups)
    print(f"\nsongs: {len(lyric)}; distinct group names: {len(group_names)}")
    print("  group names:", ", ".join(f"{n!r}×{c}" for n, c in group_names.most_common()))

    arr_counts = collections.Counter(len(s.arrangements) for s in lyric)
    print("\narrangements per song:", dict(sorted(arr_counts.items())))
    identical = sum(1 for s in lyric if len(s.arrangements) > 1 and len({tuple(v) for v in s.arrangements.values()}) == 1)
    print(f"songs whose arrangements are all identical: {identical}")
    unknown = sum(1 for s in lyric for seq in s.arrangements.values() for g in seq if g == "?")
    print(f"arrangement steps pointing at a missing group: {unknown}")
    print(f"songs with slides outside any group: {sum(1 for s in lyric if s.loose_slides)}")
    no_color = sum(1 for s in lyric for g in s.groups if g.color[3] == 0)
    print(f"groups with no color: {no_color} of {sum(len(s.groups) for s in lyric)}")

    slides = [sl for s in lyric for g in s.groups for sl in g.slides]
    print(f"\nslides: {len(slides)}; blank: {sum(1 for sl in slides if not sl.text.strip())}; "
          f"disabled: {sum(1 for sl in slides if not sl.enabled)}; with notes: {sum(1 for sl in slides if sl.notes.strip())}; "
          f"with a cue label: {sum(1 for sl in slides if sl.label)}")
    lines = collections.Counter(min(sl.text.count('\n') + 1, 6) for sl in slides if sl.text.strip())
    print("lines per slide (6 = 6+):", dict(sorted(lines.items())))

    print(f"\ncredits: CCLI song number in {sum(1 for s in lyric if s.ccli['song_number'])} songs, "
          f"author in {sum(1 for s in lyric if s.ccli['author'])}, key in {sum(1 for s in lyric if s.music_key)}")
    print("categories:", dict(collections.Counter(s.category or '(none)' for _, s in songs)))

    scripture = [s for _, s in songs if kind(s) == "scripture"]
    print(f"\nscripture presentations: {len(scripture)}; translations: "
          f"{dict(collections.Counter(s.bible_reference.get('translation') or '?' for s in scripture))}")
    others = [s.name for _, s in songs if kind(s) == "other"]
    print(f"other presentations ({len(others)}): {', '.join(others[:25])}{' …' if len(others) > 25 else ''}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "songs")
