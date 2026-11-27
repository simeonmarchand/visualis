# Glossary

Terms used across the code, issues, and plan. Plan sections are in `docs/DEVELOPMENT_PLAN.md`.

**LiveSlide**: a slide as it will show: its place in the song plus its resolved layers. Built by `liveItem()` and `liveSong()` (`core/live/liveitem.h`) for the outputs, the previews, and recovery alike.

**Content layer**: a slide's text layer: the template's regions with the item's and the slide's overrides applied, and the transition it changes with. `Content` in `core/live/liveitem.h`.

**Override**: a stored partial set of template properties, on a service item or a slide. An overridden property sticks and everything else follows the template. Written only through `core/template/overrides.h`, which documents the format.

**Media snapshot**: the library's assets and media folder as a value, safe to read on any thread. `MediaSnapshot` in `core/media/mediaresolve.h`.

**Media check**: the quick, show-time check that an asset's file is there (`checkMedia`).

**Media verification**: the full preflight check: the media check plus hash and decode (`verifyMedia`).

**Background layer**: the live asset id, whether it's cleared, and the transition it arrived with. `BackgroundLayer` in `core/live/liveitem.h`; outputs are views of it.

**Background player**: the app module that plays the background layer through the video or still adapter, in two slots so a fade can overlap them. `BackgroundPlayer` in `app/background/`.
