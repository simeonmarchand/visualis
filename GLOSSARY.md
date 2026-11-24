# Glossary

Terms used across the code, issues, and plan. Plan sections are in `docs/DEVELOPMENT_PLAN.md`.

**LiveSlide**: a slide as it will show: its place in the song plus its resolved layers. Built by `liveItem()` and `liveSong()` (`core/live/liveitem.h`) for the outputs, the previews, and recovery alike.

**Content layer**: a slide's text layer: the template's regions with the item's and the slide's overrides applied, and the transition it changes with. `Content` in `core/live/liveitem.h`.
