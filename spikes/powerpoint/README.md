# P4: PowerPoint spike

Throwaway prototype for plan 3.1 (P4) and 6.2: convert sermon decks to one image per slide with LibreOffice on a Mac without PowerPoint, and find what's lost. Python for orchestration plus a small Objective-C helper (`pdftool.m`) that renders PDF pages and checks fonts through Apple's frameworks.

## Run

```sh
brew install --cask libreoffice
cd spikes/powerpoint
clang -fobjc-arc -O2 -framework Foundation -framework CoreGraphics -framework CoreText -framework ImageIO -o build/pdftool pdftool.m
./deck.py decks/<deck>.pptx                                      # or .ppt
./deck.py decks/<deck>.pptx --fonts-conf fonts-with-deck-fonts.conf
```

`decks/`, `out/`, and `fonts-download/` are git-ignored. Decks are church content.

## Findings

So far from one deck: `Sermon_Slide_Template-PowerPoint.ppt`, a 2013 template in the old binary `.ppt` format. It has 7 slides, 16:9, 8 pictures, and no notes or video. Real recent sermon decks are still needed.

1. **Headless LibreOffice on macOS can't see the Mac's fonts.** Without help, every deck renders in LibreOffice's bundled fonts: Arial became Liberation Sans, and Gill Sans became Linux Libertine (a serif), even though both are installed. In headless mode LibreOffice finds fonts through fontconfig, which on macOS only knows LibreOffice's own folder. **Fix:** run it with `FONTCONFIG_FILE` pointing at `fonts.conf`, which lists the macOS font folders. Confirmed: Arial, Gill Sans, and Helvetica then render as themselves. The app must always pass this.
2. **A missing font falls back to whatever fontconfig picks**, which here was a Japanese rounded font (Hiragino Maru Gothic). `fonts.conf` adds a rule so missing fonts fall back to Helvetica Neue.
3. **Missing fonts break layouts, not just looks.** The template uses League Gothic (tall and narrow) for titles. With Helvetica Neue in its place, "SERMON TITLE HERE" overflowed off the top of the slide. **The import must warn about missing fonts before the service**, which `deck.py` does by checking each font the deck asks for.
4. **With the right fonts, the template renders as designed**, judged by eye since there's no PowerPoint reference yet. League Gothic and Open Sans are free (SIL OFL). Pointed at them, LibreOffice used both, and titles, body text, images, and shadows came out as designed with no overflow.
5. **Decks name fonts as "Family Style"**: "Open Sans Semibold", "League Gothic Regular". Font files name the family ("Open Sans") and the weight separately, so the lookup has to translate. `fonts-with-deck-fonts.conf` shows the rules. Apple's CoreText lookup in `pdftool fonts` doesn't translate either, so it reports "Open Sans Semibold" as missing even when Open Sans is installed. The real app needs one font resolver used both for the warning and for rendering.
6. **Old `.ppt` files work.** LibreOffice converts them to PDF directly, and to `.pptx` for reading text and fonts.
7. **Speed:** about 1–2 s to convert a 7-slide deck to PDF, and 0.7 s to render 7 slides at 1920x1080. That's fine for an import done before the service.
8. **Slide text and notes can be read from `.pptx`** (`slides.json`), for the stage display and search, even though slides are shown as images.

## Still needed

- **Real recent sermon decks** (plan 6.2 asks for five), ideally each with a PDF exported from PowerPoint as the reference for comparison.
- **The fonts the pastor uses**, so the presentation Mac can have them installed.
- **Embedded video:** whether LibreOffice keeps a frame and how the app should play the video itself.
- **Exported slide images as an import path** (plan 5.7, path 4).
