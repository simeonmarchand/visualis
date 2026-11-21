#!/usr/bin/env python3
"""P4 pipeline for one deck, as the app's import would run it:
fonts and text from the deck, a missing-font warning, LibreOffice to PDF, PDF to 1920x1080 PNGs,
and a report of fonts LibreOffice substituted.

    ./deck.py decks/sermon.pptx [--fonts-conf fonts.conf]

Writes out/<deck name>/: deck.pdf, slides/slide-NN.png, slides.json (text and notes per slide).
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).parent
SOFFICE = "/Applications/LibreOffice.app/Contents/MacOS/soffice"
PDFTOOL = HERE / "build" / "pdftool"


def soffice(fmt, src, outdir, fonts_conf):
    env = {"FONTCONFIG_FILE": str(fonts_conf.resolve()), "HOME": str(Path.home()), "PATH": "/usr/bin:/bin"}
    t0 = time.perf_counter()
    r = subprocess.run([SOFFICE, "--headless", "--convert-to", fmt, "--outdir", str(outdir), str(src)],
                       capture_output=True, text=True, env=env, timeout=300)
    if r.returncode:
        sys.exit(f"LibreOffice failed: {r.stderr.strip() or r.stdout.strip()}")
    return time.perf_counter() - t0


def slide_number(name):
    return int(re.findall(r"\d+", name)[-1])


def read_pptx(path):
    """Fonts the deck asks for, and each slide's text and notes, from the PresentationML XML."""
    fonts, slides = {}, []
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        for n in names:
            if re.match(r"ppt/(slides|slideLayouts|slideMasters|theme)/[^/]+\.xml$", n):
                for face in re.findall(r'<a:(?:latin|ea|cs) typeface="([^"]+)"', z.read(n).decode("utf-8")):
                    if not face.startswith("+"):  # "+mj-lt" etc. point at the theme's fonts
                        fonts[face] = fonts.get(face, 0) + 1
        for n in sorted((n for n in names if re.match(r"ppt/slides/slide\d+\.xml$", n)), key=slide_number):
            xml = z.read(n).decode("utf-8")
            text = [t for t in re.findall(r"<a:t>([^<]*)</a:t>", xml) if t.strip()]
            notes_xml = f"ppt/notesSlides/notesSlide{slide_number(n)}.xml"
            notes = []
            if notes_xml in names:
                notes = [t for t in re.findall(r"<a:t>([^<]*)</a:t>", z.read(notes_xml).decode("utf-8")) if t.strip()]
            slides.append({"slide": slide_number(n), "text": text, "notes": notes,
                           "pictures": xml.count("<p:pic"), "videos": xml.count("<p:videoFile") + xml.count("<a:videoFile")})
    return fonts, slides


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deck")
    ap.add_argument("--fonts-conf", default=str(HERE / "fonts.conf"))
    args = ap.parse_args()
    deck, fonts_conf = Path(args.deck), Path(args.fonts_conf)
    out = HERE / "out" / deck.stem
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)

    # .ppt (binary) goes through LibreOffice to .pptx first so its fonts and text can be read.
    with tempfile.TemporaryDirectory() as tmp:
        pptx = deck
        t_pptx = 0.0
        if deck.suffix.lower() != ".pptx":
            t_pptx = soffice("pptx", deck, tmp, fonts_conf)
            pptx = Path(tmp) / (deck.stem + ".pptx")
        fonts, slides = read_pptx(pptx)
    (out / "slides.json").write_text(json.dumps(slides, indent=1, ensure_ascii=False))

    check = subprocess.run([str(PDFTOOL), "fonts", *fonts], capture_output=True, text=True).stdout
    t_pdf = soffice("pdf", deck, out, fonts_conf)
    pdf = out / (deck.stem + ".pdf")
    used = sorted({re.sub(r"^[A-Z]{6}\+", "", f) for f in
                   re.findall(r"/BaseFont\s*/([A-Za-z0-9+_-]+)", pdf.read_bytes().decode("latin-1"))})
    render = subprocess.run([str(PDFTOOL), "render", str(pdf), str(out / "slides")], capture_output=True, text=True).stdout

    print(f"{deck.name}: {len(slides)} slides, {sum(s['pictures'] for s in slides)} pictures, "
          f"{sum(s['videos'] for s in slides)} videos, notes on {sum(1 for s in slides if s['notes'])} slides")
    print("fonts the deck asks for (uses):")
    print(check.rstrip())
    print("fonts in LibreOffice's PDF:", ", ".join(used) or "(none)")
    print(f"timing: to pptx {t_pptx:.1f} s, to PDF {t_pdf:.1f} s; {render.strip().splitlines()[-1]}")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
