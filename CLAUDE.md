# Visualis

Free, open-source (GPLv3) church presentation app for macOS first: Planning Center import, song lyrics over video backgrounds, scripture, sermon slides, a stage display, and MIDI cues. Built first for one church; other churches may use it later (plan 7.5).

## Where things are

- `docs/DEVELOPMENT_PLAN.md`: product goals, roadmap, architecture. Section numbers (e.g. "plan 5.2") are referenced everywhere.
- `docs/adr/`: decisions. ADR 0001: C++ and QML on Qt 6; outputs are borderless windows raised above the menu bar; one video decoder per background with its own `QRhi`; AVFoundation (`darwin`) multimedia backend on macOS.
- `docs/benchmarks/`: measured results. Cite numbers from here, not from memory.
- `core/`: data model, parsing, storage. Qt Core only, no UI.
- `app/`: the executable and the `Visualis.Ui` QML module (a library, so tests can load it).
- `tests/`: Qt Test, one `tst_*.cpp` per area, registered in `tests/CMakeLists.txt`.
- `spikes/`: Phase 0 throwaway prototypes. Read them for findings and edge cases (each README has a Findings section); don't import from them or extend them. Port what's needed into `core/` or `app/` with tests.

## Build and test

```sh
cmake --preset debug          # Qt from Homebrew (/opt/homebrew/opt/qt)
cmake --build --preset debug
ctest --preset debug          # runs offscreen; no display needed
```

Run the tests before calling any work done, and say plainly if any fail.

## Workflow

Work is tracked as GitHub issues on `simeonmarchand/visualis`, grouped by phase milestone.

1. Read the issue and the plan sections and spike findings it links to.
2. Branch from `main`: `<issue number>-<short-slug>`, e.g. `12-song-data-model`.
3. Commit in Conventional Commits style (`feat:`, `fix:`, `test:`, `docs:`, `chore:`, `refactor:`), explaining why.
4. Open a pull request whose body says `Closes #<n>`, summarizes what changed and how it was tested, and lists anything left for later. Simeon reviews and merges; never merge or push to `main` directly.
5. If an issue turns out bigger or different than written, say so on the issue before going further.

Labels: `type:` and `area:` describe the work; `needs-church`, `needs-hardware`, and `needs-decision` mean it's blocked on Simeon, the church, or hardware. Don't start those without asking.

## Rules specific to this project

- **Never commit church content or secrets.** Planning Center tokens live in `~/.config/visualis/pco.env` (and later the Keychain), never in the repo. Song files, decks, plan dumps, and Bible downloads stay in the git-ignored folders under `spikes/` or outside the repo. Tests use synthetic data.
- **Copyrighted text:** the app ships no song lyrics and no copyrighted Bible text. KJV and WEB are public domain (plan 7.1).
- **Outputs are views of one live state** (plan 5.2). Nothing in an output window owns state.
- **Reliability over features:** this runs live on Sunday mornings. Fail soft on bad input files, report what was skipped, and never block the output on an import or a network call.
