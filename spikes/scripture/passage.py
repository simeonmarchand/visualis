#!/usr/bin/env python3
"""Looks up a passage and prints it, verse by verse, in one or more translations.

    ./passage.py "Rom 8:28-39" --tr KJV,WEB [--markup]
"""
import argparse
import sqlite3
import sys
import time
from pathlib import Path

from refparse import RefError, parse, resolve

DB = Path(__file__).parent / "data" / "bible.db"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("--tr", default="KJV", help="comma-separated translations")
    ap.add_argument("--markup", action="store_true", help="show the formatting markup instead of plain text")
    args = ap.parse_args()

    t0 = time.perf_counter()
    db = sqlite3.connect(DB)
    try:
        ranges = parse(args.reference)
        by_tr = {tr: resolve(db, tr, ranges) for tr in args.tr.split(",")}
    except RefError as e:
        sys.exit(f"error: {e}")
    elapsed = (time.perf_counter() - t0) * 1000

    print("; ".join(str(r) for r in ranges))
    # Pair verses across translations by number, so a verse one translation leaves out shows.
    keys = sorted({(v.chapter, v.verse) for verses in by_tr.values() for v in verses})
    for c, n in keys:
        for tr, verses in by_tr.items():
            v = next((v for v in verses if (v.chapter, v.verse) == (c, n)), None)
            body = "(not in this translation)" if v is None or not v.text else (v.markup if args.markup else v.text)
            label = f"{c}:{n}" + (f"-{v.verse_end}" if v and v.verse_end > n else "")
            print(f"  {label:>7} {tr:<4} {body}")
    print(f"({elapsed:.1f} ms, {sum(len(v) for v in by_tr.values())} verses)")


if __name__ == "__main__":
    main()
