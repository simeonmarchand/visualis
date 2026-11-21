# Visualis

A free, open-source desktop presentation app for church services: Planning Center import, lyrics over video backgrounds, scripture, sermon slides, a stage display, and MIDI cues.

**Status:** Phase 1 (song library, editor, basic output) has started. Not usable yet. See [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) and the issues on GitHub.

## Build

Requires macOS, Qt 6.8 or newer, CMake 3.21+, and Ninja (`brew install qt cmake ninja`).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
open build/debug/app/Visualis.app
```

## Layout

- `app/`: the application and its QML UI.
- `core/`: data model, parsing, and storage, with no UI.
- `tests/`: automated tests.
- `docs/DEVELOPMENT_PLAN.md`: product goal, roadmap, architecture.
- `docs/adr/`: architecture decision records.
- `spikes/`: throwaway prototypes that answer the risks in plan section 6.

## License

GPLv3. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
