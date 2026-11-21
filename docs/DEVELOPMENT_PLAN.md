# Visualis: Development Plan

Status: draft 3, 2026-11-21. Planning only. No application code has been written.

Changes in draft 2: incorporated Simeon's answers to the open product decisions (section 8). macOS first with Windows and Linux as a plus, GPLv3 chosen, the church's translations recorded (KJV, NLT, CSB, NKJV, maybe ESV), PowerPoint conversion no longer assumes PowerPoint is installed, animations dropped, and lower thirds made configurable for the main screen, the livestream, or both.

Changes in draft 3: other churches may use the app (D8). Added section 7.5 on what that changes: Planning Center sign-in for many churches, per-church content licensing, signed downloads and updates, settings per church, and support. The first version still targets one church; the changes mostly add rules now so nothing has to be undone later.

Repository state when this plan was written: `~/Developer/visualis` is an empty git repository (branch `main`, no commits, no remote). There are no existing conventions, so this is treated as a new project. This file lives at `docs/DEVELOPMENT_PLAN.md`, and decisions should move into `docs/adr/` (architecture decision records) as they are made.

Labels used in this document:

- **[Verified]**: checked against the cited official documentation on 2026-11-21.
- **[Third-party]**: from a non-official source; treat as likely but confirm.
- **[Unverified]**: an assumption or recollection that needs a prototype or doc check before relying on it.
- **[Assumption]**: a product assumption made so planning can proceed; listed in section 9 so it can be overturned.

---

## 1. Product goal and target users

**Goal.** A free, open-source desktop presentation app that runs a church's real Sunday workflow reliably, built first for one church and designed so other churches can install and use it: import the service order from Planning Center, show songs, scripture, sermon slides, and announcements on a main screen with lyric backgrounds, show a separate stage display, and follow MIDI cues from the band's playback rig. It does not try to match ProPresenter feature for feature. It runs alongside ProPresenter until it has earned trust.

**Target users.**

| User | What they need |
|---|---|
| Presentation operator (volunteer, often non-technical) | A clear service list, one obvious "next" action, no surprises during the service. |
| Service planner / worship leader | Songs and arrangements prepared in Planning Center show up correctly without retyping. |
| Developer/maintainer (solo, part-time) | A codebase that one person can understand, test, and fix on a Saturday night. |
| Musicians and pastor (indirect) | A stage display that shows current/next slide, clock, and notes. |
| Other churches' volunteers (after v1) | Install from a download, connect their own Planning Center and licenses, and get going from written docs without the developer's help. |

**Success measure for version 1:** the app runs three consecutive real Sunday services as the primary presentation system with ProPresenter on standby, and no failure is visible to the congregation.

**Success measure for a public release (after v1):** a second church installs it from the download page, connects its own Planning Center account, and runs a service using only the written documentation.

---

## 2. Workflows the application must support

Each workflow has an ID used later in the feature list and roadmap.

- **W1. Prepare service from Planning Center.** Operator picks a service type and a plan date. The app imports the ordered items (songs, headers, media, other items), their titles, and for songs the Planning Center song and arrangement identity. It shows the result as a service list.
- **W2. Match songs to the library.** Each imported song is matched to a library song, first by stored Planning Center song ID, then by CCLI number, then by fuzzy title. Unmatched items become visible **placeholders** that can be filled later (create a new song from pasted lyrics, link to an existing song, or leave as a blank slide). Re-importing the same plan updates order without destroying manual work.
- **W3. Edit songs and slides.** Edit lyrics by section (Verse 1, Chorus, Bridge), set the arrangement order (V1 C V2 C B C), split sections into slides, and preview the result in the assigned template.
- **W4. Edit templates (themes).** Define text style, safe area, and layout for audience lyrics, scripture, lower thirds, and stage display. Change a template once and every song using it updates.
- **W5. Show scripture.** Look up a passage (e.g. "John 3:16-18"), choose one or more translations, auto-split into slides by length, and show with reference and attribution.
- **W6. Import and show a sermon PowerPoint.** Import a `.pptx` received during the week (often late Saturday). It shows faithfully as slides on the audience screen and appears in the stage display with notes if available.
- **W7. Backgrounds.** Assign a still image or looping video background to a song, a section, or the whole service. Lyrics change without restarting or glitching the background video.
- **W8. Live output.** Send the audience presentation to the main projector or TV and a different stage layout to the stage monitor, each fullscreen on its assigned display. Optionally show a lower third, routed per template to the main screen, the livestream output, or both.
- **W9. Operate the service.** Click slides, use the keyboard (arrows, space, number keys, clear shortcuts) and a presentation clicker (which usually sends keystrokes). Clear text, clear background, go to black, and go to logo with one action each.
- **W10. MIDI automation.** Playback (MultiTracks) or Ableton Live sends MIDI cues over a virtual or hardware MIDI port. The app maps them to actions such as next slide, go to slide N of the current song, jump to a song section, select a service item, or clear. The operator can always override.
- **W11. Recover.** If the app crashes or a display disconnects, restart and return to the same service and slide within seconds, with outputs restored to the right screens.

---

## 3. Prioritized features

### 3.1 First technical prototypes (throwaway or near-throwaway spikes)

These exist to answer the risks in section 6. They are not features.

| ID | Prototype | Answers |
|---|---|---|
| P1 | **Output spike:** two fullscreen windows on two physical displays (audience and stage), a looping 1080p video background, lyrics text crossfading over it, operator window on the laptop screen. | Multi-display, video, compositing, GPU load, hot-plug behavior. Also the main input to the framework decision. |
| P2 | **MIDI spike:** list MIDI inputs, open a CoreMIDI virtual destination and the IAC bus, log Note On, Program Change, and CC messages from Ableton and Playback with timestamps. | MIDI reliability, latency, message types Playback actually sends. |
| P3 | **Planning Center spike:** CLI that authenticates with a Personal Access Token, lists service types and upcoming plans, and dumps one plan's items, songs, and arrangements to JSON. | What data is available, auth flow, rate limits. |
| P4 | **PowerPoint spike:** convert five real sermon decks to PNG per slide through LibreOffice headless on a Mac without PowerPoint, and compare against PDFs exported from PowerPoint. | PowerPoint fidelity and conversion time. |
| P5 | **Scripture data spike:** load one public-domain translation (WEB in USFM or USFX) into SQLite and resolve references, including ranges across chapters. | Scripture data model. |

### 3.2 Usable first version (v1: can run a real service)

- Song library with sections, arrangements, CCLI number, copyright line, and Planning Center link ID (W3).
- Planning Center plan import with song matching and placeholders; re-import that merges changes (W1, W2).
- Service list with headers, songs, scripture, PowerPoint, media, and blank/placeholder items.
- Template system with a small set of templates: lyrics, scripture, title, lower third, and stage (W4).
- Text slide editor: text boxes in a template, without free-form design tools.
- Scripture: KJV stored locally; NLT, CSB, NKJV, and ESV through pasted passages or a licensed API source (see 7.1); multi-translation display, either side by side or alternating slides (W5).
- PowerPoint import as **non-editable rendered slides** through LibreOffice, plus direct PDF import, with speaker notes extracted for the stage display (W6). See 5.7.
- Still and video backgrounds with loop, mute, and fade; backgrounds survive slide changes (W7).
- Audience output, one stage output, and a livestream output window (W8).
- Lower thirds that can be routed to the main screen, the livestream output, or both, chosen per template and overridable per item. The livestream output can show lower thirds over a solid key color or a transparent-style black background for the encoder (W8).
- Keyboard, mouse, and clicker control; clear layers; go to black and go to logo (W9).
- MIDI input with a mapping table and a learn mode; mappings stored per profile (W10).
- Autosave, crash recovery to the last live item, and display reassignment by saved display identity (W11).
- Manual and scheduled library backup to a folder; library export and import.
- Operator preview (current, next) and a "panic" clear-all.

### 3.3 Later features

- Editable PowerPoint conversion for simple text-on-background slides.
- Windows and Linux builds that are tested and packaged (CI compile checks start earlier; see D1).
- Key-and-fill pair outputs for hardware switchers, beyond the v1 livestream window.
- NDI or Syphon output to streaming software. NDI SDK licensing needs to be checked before committing [Unverified].
- Planning Center write-back, such as marking items or uploading media, and webhook-driven auto-refresh.
- CCLI SongSelect import, which depends on partner access (see 7.1).
- Planning Center OAuth sign-in for other churches (7.5).
- Public release work: signed installers, automatic update checks, a documentation site, and an issue tracker (7.5).
- Countdown timers, a stage clock with service-item timing, and a stage message ("wrap up").
- Remote control from a phone on the local network.
- Multiple simultaneous audience outputs with different looks (e.g. a lobby screen).
- Props and announcement loops, and slide timers.
- Song usage report for CCLI reporting.

### 3.4 Explicitly out of scope for now

- Reading or writing ProPresenter file formats (`.pro`, libraries, playlists) or using its network API. Migration goes through Planning Center and plain-text or other open formats instead.
- Live video input, camera switching, or acting as a video switcher.
- Audio playback beyond a video background's own audio track. Audio plays from Playback or Ableton.
- Cloud sync and multi-user editing.
- Full PowerPoint authoring, and PowerPoint animation or build playback (confirmed not needed).
- Built-in streaming or recording.
- Edge blending, multi-projector warping, LED-wall pixel mapping.
- Mobile apps.

---

## 4. Phased roadmap

Estimates assume about 6 to 10 focused hours per week. They are rough and mainly show relative size. Every phase ends with something runnable and a short written note of what was learned.

### Phase 0: Spikes and framework decision (about 4 to 6 weeks)

Deliverables:
- P1 built in the **two** front-runner frameworks (see 5.1): Qt 6 (Qt Quick plus C++) and Avalonia (.NET) with LibVLCSharp. Both build from the same spec in `spikes/output/README.md`.
- P2, P3, P4, P5 built in whatever is quickest, since they mostly exercise OS or HTTP APIs and their findings carry over.
- P1 also compiled and smoke-tested once on a Windows machine and a Linux machine (or VM) if one is available, so cross-platform blockers show up before the framework decision.
- `docs/adr/0001-ui-framework.md` with benchmark numbers from section 6.1.
- `docs/benchmarks/` with raw measurements from at least the church's presentation Mac.

Acceptance criteria:
- P1 runs for 2 hours on the church presentation machine with a 1080p30 H.264 loop and a 4K HEVC loop. It drops no more frames than the measured threshold (target: 0 visible drops, measured by a frame counter). It survives unplugging and replugging the stage display.
- P2 receives cues from Ableton and Playback over IAC or a virtual port. Measured cue-to-photon latency is under 100 ms on the church machine [Assumption about the target].
- P3 dumps a real upcoming plan, including song IDs, arrangement IDs, and CCLI numbers where present.
- P4 produces a fidelity scorecard for the five decks through LibreOffice on a Mac **without** PowerPoint installed (fonts, layout, images, and what is lost), and a note on which fonts the presentation machine must have installed.
- A framework is chosen, and the ADR records why.

### Phase 1: Library, editor skeleton, and basic output (about 6 to 8 weeks)

Deliverables: project skeleton in the chosen stack, `LICENSE` (GPLv3) and `THIRD_PARTY_NOTICES`, CI build on macOS plus compile-only CI jobs on Windows and Linux, data model (5.2) with migrations, song CRUD with sections and arrangements, one lyrics template, audience and stage windows, keyboard navigation.

Acceptance criteria:
- Create a song by pasting lyrics with section labels. It auto-splits into slides and displays on a real projector and stage screen.
- Arrow keys and space step through slides. Output updates in under 50 ms from the key press, measured with a high-speed phone camera or frame log.
- Kill the process mid-song, relaunch, and it returns to the same slide within 10 seconds with outputs on the right displays.
- 50 automated tests cover parsing, arrangement expansion, and slide splitting.

### Phase 2: Services and Planning Center import (about 4 to 6 weeks)

Deliverables: service list, Planning Center authentication (Personal Access Token first), plan picker, import, the song matching UI, placeholders, re-import merge, and offline cache of the last import.

Acceptance criteria:
- Importing next Sunday's real plan produces the right order. At least 90% of songs auto-match once the library is seeded, and unmatched songs show as placeholders that can be resolved in under 30 seconds each.
- Re-importing after a planner reorders items in Planning Center updates order and keeps local edits (custom slide splits, backgrounds).
- With the network disconnected, the last imported service still opens and runs.
- The token is stored in the macOS Keychain, never in the library file.

### Phase 3: Templates and backgrounds (about 5 to 7 weeks)

Deliverables: template editor (text styles, safe areas, layouts), per-item and per-service background assignment, video loop engine from the winning P1 approach, media library with thumbnails, and fade transitions.

Acceptance criteria:
- Changing the lyrics template's font updates every song in the open service.
- A 2-hour run of a real service, with lyrics changing every 5 to 15 seconds over looping video, shows no background restart and no visible stutter.
- Missing media shows a clear warning before the service, not a black screen during it.

### Phase 4: Scripture (about 3 to 4 weeks)

Deliverables: translation importer for one open format (USFM or USFX) used for KJV, a paste-in passage tool for licensed translations (paste text from a licensed source and the app splits it into verses and slides), reference parser, passage-to-slides splitter, multi-translation layouts, attribution line per translation, and a translation licensing record in the library. An API.Bible source for NLT, CSB, NKJV, and ESV follows if the church gets an API key that covers them (see 7.1).

Acceptance criteria:
- Enter "Rom 8:28-39" and get correctly split slides in KJV plus one pasted or API-sourced translation in under 2 seconds.
- Each slide shows the translation's required attribution.
- Only translations with a recorded license basis can be imported (see 7.1).

### Phase 5: PowerPoint import (about 3 to 5 weeks)

Deliverables: LibreOffice-based import pipeline (PowerPoint for Mac used instead when it happens to be installed), direct PDF import, LibreOffice detection with a clear install prompt, rendered-slide presentation item, speaker notes on stage, re-import when the deck changes, and a conversion progress and error UI.

Acceptance criteria:
- The five Phase 0 reference decks plus five new decks import with no layout differences a pastor would notice when compared to PowerPoint's own slideshow.
- A 40-slide deck imports in under 60 seconds on the church machine.
- On a Mac without PowerPoint, importing works end to end with only LibreOffice installed.
- A PDF exported from PowerPoint, Keynote, or Google Slides imports with no conversion step.
- Notes appear on the stage display.

### Phase 6: MIDI automation and lower thirds (about 3 to 4 weeks)

Deliverables: MIDI device selection, mapping table, learn mode, activity monitor, a cue log, lower-third templates and layer, the livestream output window, and lower-third routing (main screen, livestream, or both).

Acceptance criteria:
- A full song with Playback cues advances every slide correctly in 10 consecutive rehearsal runs.
- Disconnecting and reconnecting the MIDI device or restarting Playback recovers without restarting the app.
- The operator can disable MIDI with one click during a service.
- A lower third set to "both" appears on the main screen and the livestream output at the same moment; one set to "livestream" never appears on the main screen.

### Phase 7: Hardening and shadow services (about 4 to 8 weeks, calendar-bound)

Deliverables: backup and restore UI, first-run setup that asks for nothing church-specific until needed, pre-service check ("all media present, displays assigned, MIDI connected, plan up to date"), logging, installer or signed build, an operator quick-start guide.

Acceptance criteria:
- **Shadow mode:** run the app in parallel with ProPresenter for 4 services (outputs to a spare screen or a recording) with no blocking defect.
- Then run 3 services as primary with ProPresenter on standby and no audience-visible failure. This is the v1 definition of done.
- A restore from a backup made the previous week succeeds on a second Mac.

---

## 5. High-level architecture

### 5.1 Desktop application and UI framework

#### Requirements that drive the choice

1. Two or more fullscreen windows on specific physical displays, kept in sync, surviving display hot-plug.
2. Hardware-decoded, looping video composited **under** animated text, ideally decoded once and shown on more than one output.
3. A rich operator UI (lists, drag and drop, thumbnails, editors).
4. Native MIDI input (CoreMIDI first).
5. Packaging for macOS first, with other platforms as an option.
6. Licensing compatible with an open-source project.
7. Maintainable by one part-time developer for years.

#### Comparison

| Criterion | Qt 6 (C++, Qt Quick/QML) | Avalonia (C#/.NET) | Tauri 2 (Rust plus web UI) | wxWidgets (C++) |
|---|---|---|---|---|
| macOS / Windows / Linux | All three, first-class. | All three [Verified: avaloniaui.net FAQ]. | All three. Each OS uses a different system webview: WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux [Verified: Tauri webview versions]. | All three, using native widgets. |
| Multi-display output | `Window.screen` puts a window on a chosen screen; `visibility: FullScreen` [Verified: Qt Window QML type]. | `Screens` API with bounds, scaling, and change events. Fullscreen on a chosen screen is not explicitly documented [Verified: Avalonia window management]. | `availableMonitors()`, `setPosition`, `setFullscreen` [Verified: @tauri-apps/api window.d.ts]. | `wxDisplay` enumerates displays [Unverified detail]. |
| Video playback | Qt Multimedia with an FFmpeg backend as the default on desktop, plus native backends (AVFoundation, WMF) [Verified: Qt Multimedia 6.8]. Hardware decode backends include VideoToolbox, D3D11VA, and VAAPI [Verified: Advanced FFmpeg Configuration]. One `QMediaPlayer` has one video output at a time [Verified: QMediaPlayer docs], so sharing frames across outputs needs a custom sink (prototype). | No built-in player in the open-source framework. Avalonia's Media Player is a paid Pro component [Verified: FAQ]. LibVLCSharp.Avalonia (LGPL-2.1+) works on Win/macOS/Linux [Verified: NuGet], but native video views sit in a separate layer and can't be overlapped by Avalonia content [Verified: Avalonia macOS guide on NativeControlHost], so lyrics-over-video needs LibVLC's frame-callback path, which copies frames through the CPU [Unverified: performance]. | The HTML `<video>` element in each window. Codec support varies by webview, and each window decodes its own copy [Unverified]. Linux WebKitGTK media depends on GStreamer packages [Unverified]. | `wxMediaCtrl` backends are old (DirectShow; QuickTime/AVFoundation; GStreamer) [Verified: wxPython MediaCtrl docs, which may lag wxWidgets 3.2]. Realistically needs libmpv or FFmpeg plus custom OpenGL/Metal rendering. |
| GPU acceleration of UI | Qt Quick scene graph on Metal, D3D, Vulkan, or OpenGL through the RHI [Unverified: needs a doc check]. | Skia with Metal as default on macOS, then OpenGL, then software [Verified: Avalonia macOS guide]. | Webview compositor (GPU in practice; not controlled by the app). | Native widgets; custom drawing needs your own GL or Metal code. |
| MIDI | No core Qt MIDI module. Use RtMidi (MIT-style; CoreMIDI, WinMM/UWP, ALSA, JACK; virtual ports on CoreMIDI, ALSA, and JACK) [Verified: RtMidi docs]. | DryWetMIDI device API covers Windows and macOS only, not Linux [Verified: DryWetMIDI supported OS]. Linux would need another library. | `midir` in Rust: CoreMIDI, WinMM, WinRT, ALSA, JACK; virtual ports except on Windows [Verified: docs.rs midir]. Web MIDI in WKWebView should not be relied on [Unverified]. | RtMidi, same as Qt. |
| Packaging | `macdeployqt`/`windeployqt`; signing and notarization are your own job. | `dotnet publish` plus app-bundle tooling; native macOS views require the `net10.0-macos` target and building on a Mac [Verified: Avalonia macOS guide]. | Built-in bundler for .app/.dmg, .msi/NSIS, .deb/AppImage [Unverified detail]. | Manual. |
| Licensing | Qt Multimedia is LGPLv3/GPLv2 or commercial [Verified: Qt Multimedia page]. Qt Quick is not on the GPL-only list, and is believed to be LGPLv3 [Unverified: confirm per module]. Some modules are **GPL-only** in the open-source edition, including Qt Quick 3D, Qt Graphs, Qt Network Authorization, Qt Virtual Keyboard, and Qt Quick Timeline [Verified: Qt Licensing]. FFmpeg in Qt's binaries is LGPL-compatible and dynamically linked [Verified]. Codec patent royalties (e.g. H.264) are not covered by Qt's license [Verified]. | MIT [Verified]. LibVLC is LGPL-2.1+ [Verified]. | MIT/Apache-2.0 [Unverified: check repo LICENSE]. | wxWindows Library Licence: LGPL plus an exception allowing binary distribution on your own terms [Verified]. |
| Long-term maintenance | Large, stable, commercially backed. Open-source users don't get later LTS patch releases. For example, Qt 6.8.8 is commercial-only, so open-source users must follow newer minor versions [Verified: Qt Releases, Qt LTS page]. C++ costs more developer time. | Active, MIT core, commercially backed company. The video stack depends on a third party. | Active. Three different webviews means three sets of rendering and media bugs. Two languages (Rust plus TypeScript). | Mature but smaller momentum. You would write most of the hard parts (rendering, video) yourself. |

#### Assessment

- **Tauri** gives the best operator-UI productivity, and React experience transfers directly. But the hardest requirement here is synchronized, glitch-free video under text on several fullscreen outputs. In Tauri that depends on three different system webviews' media pipelines, which the app can't control. Moving video out of the webview (native video layer from Rust) puts you in the same overlap problem as Avalonia's native views, and the Rust video ecosystem would need its own evaluation. **Not a front-runner**, unless P1 surprisingly shows the webview handling it well.
- **wxWidgets** is a capable native-widget toolkit, but it offers nothing for the output compositor and video. Those would be written from scratch on top of it. **Not a front-runner.**
- **Qt 6** is the only option where an animated GPU scene graph, video-as-a-texture, multi-screen windows, and the operator UI all live in one officially supported framework. Costs: C++ (and QML), compile times, packaging by hand, and the LTS gap for open-source users. **Front-runner.**
- **Avalonia** matches the C# experience and is MIT-licensed. The open question is video: lyrics have to render over video, and native video views can't be overlapped. The workable path is LibVLC's software frame callbacks into an Avalonia bitmap or GPU texture. Its performance at 1080p60 and 4K on the church Mac is unknown. **Second front-runner, conditional on P1.**

Windows and Linux being a plus, not a requirement, favors Qt slightly more: it has first-party multimedia and windowing on all three platforms, while Avalonia's MIDI library (DryWetMIDI) has no Linux device support and Tauri's video quality would differ by OS webview.

**Recommendation:** build P1 in Qt 6 first. Build the Avalonia version of P1 only if Qt shows a blocker or if C++ productivity feels unworkable after two weeks. Commit after the benchmarks in 6.1, and record it in ADR 0001.

#### Qt licensing and LTS, specifically

- An open-source, personal-use project can use Qt under LGPLv3 with no fee. Because the project is licensed GPLv3 (decision D3), the GPL-only modules (e.g. Qt Network Authorization for OAuth) are also usable.
- LTS: a new LTS every two years, five years of maintenance starting with Qt 6.8 (standard support until 2029-10-08) [Verified: Qt Releases]. Open-source users get early LTS patch releases but not the later commercial-only ones [Verified]. **Practical policy:** target the newest Qt minor release (6.11 or 6.12 at the time of writing; check current), upgrade once or twice a year in a quiet season (not before Easter or Christmas), and pin the exact version in CI.
- FFmpeg ships as LGPL-compatible dynamic libraries [Verified]. Don't enable GPL-only FFmpeg components if the project is to stay LGPL-compatible.

### 5.2 Service and song data model

Core entities. IDs are UUIDs, and timestamps are UTC.

```
Library
 ├─ Song { id, title, aliases[], ccli_number?, authors, copyright, pco_song_id?,
 │         default_template_id?, default_background_id? }
 │    ├─ Section { id, label (Verse 1, Chorus...), color, lyrics_text, slide_breaks[] }
 │    └─ Arrangement { id, name, pco_arrangement_id?, sequence: [section_id...] }
 ├─ ScriptureTranslation { id, abbrev, name, license_basis, attribution_text, source }
 ├─ Presentation { id, kind: rendered_pptx | native, source_file_hash, slides[] }
 ├─ Template { id, kind: lyrics|scripture|title|lower_third|stage, layout json,
 │             lower_third_route?: main|livestream|both }
 ├─ MediaAsset { id, content_hash, original_name, kind: image|video, duration?, probe info }
 └─ MidiProfile { id, name, mappings[] }

Service { id, title, date, pco_plan_id?, pco_service_type_id?, last_imported_at }
 └─ ServiceItem { id, position, kind: header|song|scripture|presentation|media|text|placeholder,
                  ref_id?, pco_item_id?, arrangement_id?, overrides { template, background, slide edits },
                  import_snapshot (raw PCO fields for diffing) }
```

**Groups.** A `Section` is what ProPresenter calls a group: a labeled, colored block of slides (Verse 1, Chorus, Bridge), and an arrangement is an ordered list of groups that may repeat one ("V1 C V2 C B C C"). Operators work in groups: the slide list shows each group's label and color, a group can be jumped to directly by key or MIDI cue ("go to Chorus", even when the arrangement doesn't have it next), and editing a group's lyrics changes every place the arrangement uses it. Imports map onto groups: ProPresenter groups and arrangements directly (7.4), Planning Center arrangement sections and sequences (5.3), and pasted lyrics by their section labels. Song credits (CCLI number, copyright) are optional; a church that doesn't use CCLI leaves them empty, and the credit line is a template setting.

Rendering pipeline (shared by preview and live output):
`ServiceItem → expand (arrangement or passage or deck) → Slide[] → apply Template → Layers {background, content, lower_third} → Output(role)`.

The key rule is that **outputs are views of one live state**. The live state holds the current item, slide index, and layer contents. The audience, stage, and lower-third outputs each render that state through their own layout. Nothing in an output window owns state, so a crashed or reopened window just re-renders.

### 5.3 Planning Center import

- **Auth, phase 2:** Personal Access Token (application ID and secret, HTTP Basic). Planning Center documents this for apps that only access your own organization's data [Verified: planningcenter/developers README]. Store it in the OS keychain. This keeps working for other churches too: each church creates its own token in its own Planning Center account, so it's supported permanently as the "advanced" option.
- **Auth, for other churches:** OAuth 2 with a registered application is Planning Center's documented method for apps used by many organizations [Verified: planningcenter/developers README]. See 7.5 for the design question it raises in an open-source desktop app (keeping the client secret secret). Build the auth layer behind an interface from Phase 2 so OAuth can be added without touching import code.
- **API:** JSON:API 1.0 at `https://api.planningcenteronline.com/services/v2/` [Verified]. Path: service types → plans (filter future) → items with `include=song,arrangement,key` [Item relationships listed in a third-party generated client, so treat as Third-party until checked against the official docs]. Item fields include `title`, `item_type`, `sequence`, `length`, `description`, `html_details`, and `key_name` [Third-party].
- **Rate limits:** reported as 100 requests per 20 seconds, with 429 and `Retry-After` on excess and `X-PCO-API-Request-Rate-*` headers [Third-party: usecarly.com]. Implement header-driven backoff instead of hardcoding numbers. One plan import is only a handful of requests, so this mainly matters for bulk song-library sync.
- **Lyrics:** whether arrangement lyrics or chord charts come through the API in a usable form is **Unverified**, and P3 must check it. If they do, matching can seed new songs from Planning Center automatically.
- **Matching order:** stored `pco_song_id` → CCLI number → normalized title plus author fuzzy match (operator confirms) → placeholder.
- **Merge on re-import:** match items by `pco_item_id`. Update order and titles, keep local overrides, flag removed items instead of deleting them silently, and show a diff before applying.
- **Never** scrape the Planning Center web UI or reuse ProPresenter's Planning Center integration.

### 5.4 Slide editor and template system

- Templates are declarative JSON: named text regions (frame, font, size range, alignment, shadow/outline, line-height, auto-shrink), plus a background slot and safe-area margins. Stored in the library and versioned.
- The slide editor in v1 edits **text and region assignment only**. Free-form design (shapes, arbitrary images) is later. This keeps the editor small and makes every song consistent.
- Lyrics editing is text-first: a plain-text editor with `[Verse 1]` style headers and blank-line slide breaks, with a live preview. This is fast for volunteers and easy to import from Planning Center, SongSelect exports, or ProPresenter text exports.
- Text rendering must handle auto-fit, explicit line breaks, and non-Latin scripts. Qt Quick Text and Skia both do shaping via HarfBuzz [Unverified for Avalonia].

### 5.5 Audience, stage, and lower-third outputs

- **Output roles:** `audience`, `stage`, `livestream`. Each lower-third template has a route setting: `main`, `livestream`, or `both`, overridable per service item. The `livestream` output renders the lower-third layer over a configurable background (solid key color for chroma keying, or black), optionally with the lyrics layer too, so it can feed a capture card or streaming encoder. Later: key/fill output pairs and NDI/Syphon.
- **Display assignment:** saved by stable display identity (name, resolution, and position, plus OS display ID where available). The operator window must never be covered by an output window. If an assigned display is missing, its output waits or goes to a preview window and is never placed on the operator screen.
- **Stage layout:** current slide text, next slide text, item title, clock, timer, speaker notes, and a message line.
- **Frame pacing:** outputs render on vsync. Transitions are GPU animations. Measure dropped frames in every benchmark.
- **Mac specifics to verify:** fullscreen windows vs. borderless windows sized to the display, macOS Spaces/Mission Control behavior, and "Displays have separate Spaces" settings [Unverified; P1 tests both modes].

### 5.6 Still-image and video backgrounds

- The background layer is independent of the content layer, so changing slides never touches the video.
- **Decode once, show many:** where possible, one decoder feeds every output that shows the same background. In Qt, `QMediaPlayer` has a single output [Verified], so P1 must test a `QVideoSink` that fans frames out to several scene-graph textures, versus one player per window kept roughly in sync.
- **Seamless looping:** `QMediaPlayer.loops = Infinite` exists [Verified], but whether the loop point is gapless is not documented. Test it in P1. A fallback is two players crossfading.
- **Media pipeline:** on import, probe the codec, resolution, frame rate, and duration. Generate thumbnails. Warn about codecs that won't hardware-decode on the target machine. Optionally offer to transcode to a known-good profile (e.g. H.264 or HEVC 1080p) with an external FFmpeg, as a later feature.
- Separate **framework overhead** from **decode and render cost**. Video cost comes from codec, resolution, bitrate, hardware decoder availability, color conversion, and the number of composited outputs, regardless of framework. Framework choice mainly affects how frames get to the GPU (zero-copy texture vs. CPU copy). See 6.1.

### 5.7 PowerPoint import

Two strategies:

| | A. Faithful rendered import (non-editable) | B. Editable conversion |
|---|---|---|
| How | Render each slide to an image (or PDF page) at output resolution and show it as a slide. Extract notes and slide text from the `.pptx` XML for the stage display and search. | Parse PresentationML (slides, layouts, masters, text runs, images) [Verified: Open XML SDK docs describe the structure] and map it into native slides and templates. |
| Fidelity | As good as the renderer. With PowerPoint itself, very high. | Low to medium. Themes, placeholders inheritance, SmartArt, charts, WordArt, and effects are hard to reproduce faithfully. |
| Editable in app | No. Edit in PowerPoint and re-import. | Yes, for text. |
| Animations and builds | Lost (each slide shows its final state). Acceptable: confirmed not needed. | Possible in theory, expensive in practice. |
| Embedded video | Lost; needs separate handling. | Could be extracted. |
| Effort | Small. | Large and open-ended. |

**Renderers available:**
1. **PowerPoint for Mac** (optional, only when installed) exports every slide as PNG/JPEG at a chosen width and height, or as PDF [Verified: Microsoft support]. Scripting it (AppleScript) needs PowerPoint installed and licensed on that Mac, and exact automation commands are **Unverified**. Highest fidelity.
2. **LibreOffice headless** (`soffice --headless --convert-to pdf`) [Verified: soffice man page], then rasterize PDF pages. Free and runs on macOS, Windows, and Linux, which matches D1. Fidelity depends on fonts installed and LibreOffice's PPTX import, so expect differences with custom fonts.
3. **Keynote** can open `.pptx` and export PDF, but it's Mac-only and unattended automation is unverified, so it's a manual fallback rather than a pipeline [Unverified].

**Recommendation (updated for draft 2):** PowerPoint is often not installed on the presentation computer, so v1 uses A with **LibreOffice headless as the primary renderer** and PDF as the intermediate (vector, scales to any output). There are three entry paths:

1. **`.pptx` through LibreOffice** (primary). The app finds an installed LibreOffice and runs it as a separate process. It doesn't bundle or link it, which keeps the app small and sidesteps license mixing (LibreOffice is MPL-2.0 [Unverified: confirm]). If LibreOffice is missing, the import screen says so and links to the download.
2. **PDF import** (always available, no converter). The pastor or operator exports a PDF from PowerPoint, Keynote, or Google Slides on any computer. This gives exact fidelity whenever the deck's author has PowerPoint, and is the recommended fallback when LibreOffice renders a deck badly.
3. **PowerPoint for Mac** (optional). Used automatically when present, because its fidelity is highest.
4. **Folder of slide images** (always available, no converter). PowerPoint, Keynote, and Google Slides can all export every slide as PNG or JPEG. Exact fidelity like PDF, and simpler to show. Costs: the images are fixed at the exported size, so they must be exported at the output's resolution (a PDF scales), and they carry no slide text or notes, so the stage display and search need the `.pptx` imported alongside. P4 tests one deck this way.

The main fidelity risk with LibreOffice is fonts: a deck using a font the presentation machine doesn't have will reflow. Mitigations: show a list of missing fonts after import (read from the `.pptx` theme and runs), keep the church's common sermon fonts installed on the presentation machine, and fall back to PDF import. Animations and builds are out of scope (confirmed not needed). B starts later and only for simple text slides (title plus body on a plain background), where a pastor would benefit from editing in the app.

Rendering PDF pages needs a PDF rasterizer in the app. Qt PDF is a candidate, but its license isn't shown on Qt's licensing page and must be checked [Unverified]. Poppler (GPL) and PDFium (BSD-style) are alternatives [Unverified: confirm licenses]. GPLv3 for the project keeps all three options open.

### 5.8 Keyboard, mouse, and MIDI input

- **Command layer:** every operator action is a named command (`next_slide`, `prev_slide`, `goto_slide(n)`, `goto_section(label)`, `goto_item(n)`, `clear_text`, `clear_background`, `clear_all`, `black`, `logo`). Keyboard shortcuts, mouse clicks, clicker keys, and MIDI all dispatch the same commands. This gives one place for logging, testing, and an "ignore external input" safety switch.
- **Keyboard:** shortcuts work when the operator window is focused. Global hotkeys when other apps are focused are a later item and need macOS Accessibility permission [Unverified].
- **MIDI:** CoreMIDI through RtMidi (C++) or the chosen stack's equivalent. The app listens on chosen sources and optionally creates its own virtual destination (supported on CoreMIDI) [Verified: RtMidi docs]. On macOS the IAC Driver lets apps on the same Mac exchange MIDI over named buses [Verified: Apple Audio MIDI Setup guide]. Ableton can send Program Change messages [Verified: Ableton help article title; read the article during P2].
- **What Playback sends:** cues have an editable MIDI bus, message type, value, and velocity [Verified: MultiTracks help center], but the exact message types available must be confirmed in P2. Their default cues are designed for ProPresenter playlists; the app must define its **own** mapping scheme rather than copying ProPresenter's.
- **Proposed default mapping (to confirm):** Note On on a chosen channel, note number = slide index within the current item; Program Change = service item index; specific CCs for clear/black/next. Fully remappable in a learn mode.
- **Safety:** ignore duplicates within a short window, show a MIDI activity light, log every cue with timestamp and resulting action, and offer one-click "MIDI off."
- Windows later: Windows MIDI Services adds built-in loopback endpoints and multi-client ports on Windows 11 [Verified: Windows Experience blog, Feb 2026]. RtMidi's Windows backends would need testing against it [Unverified].

### 5.9 Persistence, media storage, and backup and recovery

- **Library:** a single folder containing `library.sqlite` (songs, services, templates, translations metadata, settings) and a `media/` directory of content-addressed files (`media/ab/abcdef….mp4`). Content addressing stops duplicates and broken links when files are renamed.
- SQLite in WAL mode with schema migrations, plus integrity check on start.
- **Autosave:** every edit commits immediately. There's no "Save" button to forget.
- **Live state journal:** current service, item, slide, and layer state written on every change to a small file, read on crash restart.
- **Backup:** scheduled zip of the database (consistent snapshot through SQLite's backup API) plus a media manifest, to a user-chosen folder (e.g. a synced cloud folder or external drive). Keep the last N. Media is copied incrementally.
- **Export format:** human-readable JSON/plain-text export of songs and services, so data never depends on this app's database. Also doubles as the test fixture format.
- **Recovery test** is part of Phase 7 acceptance.

---

## 6. Highest-risk technical questions and how to answer them

### 6.1 Multiple displays and video playback (highest risk; P1)

Questions:
1. Can the chosen framework put fullscreen windows on two specific external displays reliably on macOS, including after sleep, display hot-plug, and resolution changes?
2. Can it composite text over hardware-decoded video with zero dropped frames at 1080p60 and 4K30 on the church's actual Mac?
3. Can one decode feed two outputs, or do two decoders stay in sync well enough?
4. Is looping gapless?

What to measure, on each candidate framework, on the church machine and the dev machine:
- Idle CPU, GPU, and memory with operator UI plus two output windows and **no video** (framework overhead).
- The same with one video background on one output, then on two outputs (decode and render cost).
- Dropped or late frames over 30 minutes (frame callback counter plus a visible frame counter filmed with a phone camera).
- Time from key press to the new slide being visible.
- Loop-point gap in milliseconds.
- Behavior when the stage display is unplugged and replugged, and when the laptop sleeps.

Test media: 1080p30 H.264, 1080p60 H.264, 4K30 HEVC, and one ProRes file, since many motion background packs ship in these codecs [Assumption].

### 6.2 PowerPoint fidelity (P4)

- Collect five real recent sermon decks, including any with custom fonts or embedded video.
- Render each through LibreOffice on a Mac without PowerPoint (the primary path), and through a PDF exported from PowerPoint as the reference. Compare against PowerPoint slideshow screenshots.
- Record which fonts each deck uses and which are missing on the presentation machine.
- Run the same conversion on Windows and Linux LibreOffice once, to see whether output differs by platform.
- Record conversion time, failures, and what is lost. - Low priority: confirm whether PowerPoint for Mac can be automated unattended (AppleScript dictionary) for the optional path [Unverified].

### 6.3 MIDI input (P2)

- Confirm the message types and channels Playback and the church's Ableton set actually send.
- Measure delivery latency through IAC versus a hardware interface versus network MIDI [Verified that network MIDI exists in Audio MIDI Setup].
- Confirm behavior when the source app restarts mid-service (port disappears and reappears).
- Confirm that timing jitter with cues placed close together (fast lyrics) doesn't reorder actions.

### 6.4 Planning Center integration (P3)

- Confirm on the official docs (developer.planning.center) the auth options, PKCE status for a future OAuth flow, rate limits, and the item, song, and arrangement attributes.
- Check whether arrangement lyrics and sequence data come through the API, and what "item_type" values a real plan uses.
- Check how the operator's Planning Center permissions affect what the token can read (a low-permission token may return less data [Third-party]).

### 6.5 Smaller risks

- Text auto-fit quality and font rendering parity between preview and output.
- Color management: whether video and images look the same on projector outputs as in preview [Unverified].
- macOS permissions needed (Accessibility for global hotkeys, local network for future remote control) [Unverified].
- Code signing and notarization for distribution to the church machine. Ad-hoc signing may be enough for personal use [Unverified].

---

## 7. Data, licensing, privacy, reliability, and migration

### 7.1 Content licensing

- **Bible translations.** The church normally uses KJV, NLT, CSB, NKJV, and possibly ESV.
  - **The key distinction:** publishers' free-quotation rules cover *showing passages* in a service, but a presentation app normally wants the *whole translation* stored locally, which those rules don't cover. NKJV's gratis use explicitly excludes "an entire translation" and requires a permission request for use in apps or digital products [Verified: HarperCollins Christian permissions].
  - **KJV:** public domain in most countries, but under Crown patent in the UK [see eBible.org KJV copyright page]. The app can store and ship the full text. This is the development and default translation.
  - **NLT (Tyndale):** up to 500 verses without permission if under 25% of the work and no complete book. Screen projections and church bulletins need only "(NLT)" after each quotation, without a full notice [Verified: Tyndale permissions]. Tyndale directs Bible app requests to API.Bible [Verified].
  - **NKJV (Thomas Nelson / HarperCollins):** up to 500 verses, under 25% of the work, not a whole book. Churches may show quotations on screens without permission if accurately cited. Full notice text is specified [Verified: HarperCollins Christian permissions]. App use needs a permission request [Verified].
  - **CSB (Holman / Lifeway):** permission terms not yet checked against the publisher's own page [Unverified]. Treat it like NLT and NKJV until confirmed.
  - **ESV (Crossway):** up to 500 verses with conditions; non-saleable church media need "(ESV)" after each quotation [Verified: Crossway permissions]. The ESV API allows free non-commercial use in websites and apps with registration [Verified]. The guide doesn't specifically address projection software [Unverified].
  - API.Bible requires cached content to be refreshed at least every 30 days and forbids modifying the text [Verified: API.Bible help center]. Which of NLT, CSB, NKJV, and ESV a free API.Bible key covers is **Unverified** and is a Phase 4 check.
  - **Plan for the app, in order:**
    1. KJV stored locally (Phase 4).
    2. **Paste-in passages** for NLT, CSB, NKJV, and ESV. The operator pastes the week's passages from a licensed source (a purchased Bible app, the publisher's site, or Planning Center notes); the app splits them into verses and slides and applies the right attribution. This stays within each publisher's quotation limits and works offline once prepared.
    3. **API source** (API.Bible, and the ESV API for ESV) fetching only the passages a service needs, cached under 30 days. Added when a key covering the needed translations is in hand.
    4. **Full local text** of a copyrighted translation only with written permission from that publisher. Requests can go out in parallel with development; the plan doesn't depend on them.
  - The app never ships copyrighted translation text, records each translation's license basis and attribution, and shows the attribution on screen automatically.
  - The World English Bible (public domain, USFM/USFX) [Verified: eBible.org] remains useful as test data even though the church doesn't use it.
- **Song lyrics.** Projecting lyrics needs the church's own CCLI (or equivalent) license; the app does not grant rights. Store CCLI numbers and copyright lines and show them on the first or last slide as the license requires. SongSelect import depends on CCLI's integration program and is not assumed to be open [Unverified].
- **Media backgrounds.** Purchased motion backgrounds have their own licenses. The app stores but never redistributes them. Never commit media to the source repository.
- **Project license: GPLv3.** Reasons: (1) every likely dependency is compatible with it: Qt (LGPLv3, and its GPL-only modules), FFmpeg (LGPL), LibVLC (LGPL), RtMidi (MIT-style), SQLite (public domain); (2) it unlocks Qt's GPL-only modules, such as Network Authorization for a future Planning Center OAuth flow, without a commercial Qt license; (3) it keeps improvements open if other churches fork it; (4) there's no plan to sell the app or ship it closed, which is the main reason to prefer a permissive license. Tradeoff: a later switch to a permissive license would need agreement from every contributor, and app stores that restrict GPL software (such as Apple's Mac App Store) are effectively off the table [Unverified: App Store terms]. Neither matters for a church tool distributed as a download.
- **Third-party libraries.** Keep a `THIRD_PARTY_NOTICES` file from day one (Qt LGPL, FFmpeg LGPL, RtMidi MIT, etc.), and keep FFmpeg dynamically linked as Qt's binaries do.
- **Codec patents.** Qt's docs note that codec patent royalties are separate from Qt's license [Verified]. For personal church use relying on the OS's licensed decoders (VideoToolbox) this is low practical risk, but it is not legal advice.

### 7.2 Planning Center authorization and privacy

- Use only the documented API with a Personal Access Token or OAuth. Don't scrape and don't reuse other apps' credentials.
- Request and store the minimum: plans, items, songs, arrangements. Don't import people, schedules, or contact data, even though the API offers them.
- Tokens live in the OS keychain, never in the library, backups, logs, or the repository.
- Logs never contain tokens or personal data. Planning Center responses saved for debugging must be scrubbed.

### 7.3 Reliability

- Outputs never show OS dialogs, notifications, or the desktop. Disable macOS notifications on the presentation Mac during services (operator checklist item).
- A crash in any non-output component (import, editor) must not affect the outputs. In v1 this means careful exception boundaries; a separate output process is a later option if crashes happen.
- The pre-service check (Phase 7) catches missing media, unassigned displays, and stale plans before the service starts.
- Keep ProPresenter installed and ready during the evaluation period. Write down a 30-second fallback procedure.

### 7.4 Migration from ProPresenter

- No ProPresenter file parsing (out of scope). Migration paths:
  1. Songs from Planning Center arrangements, if P3 confirms lyrics are available.
  2. Plain-text lyrics import with section headers (covers copy-paste from any source, including ProPresenter's own text export, if it has one [Unverified]).
  3. Manual re-entry for the remaining songs, done gradually by usage frequency. The 50 most used songs probably cover most Sundays [Assumption].
- Media files are reused from their original folders by importing them into the library.
- Templates are rebuilt by hand. There are usually only a few.

### 7.5 Use by other churches

Simeon said other churches may use the app (D8). Version 1 still targets one church, but these rules apply from the start so nothing has to be torn out later.

**Planning Center sign-in**
- Personal Access Tokens are tied to the organization that creates them [Verified: planningcenter/developers README]. Any church can create its own and paste it in, so this works from day one and stays as the fallback.
- OAuth 2 gives a "Sign in with Planning Center" button. The catch: the standard authorization-code flow uses a client secret, and anything compiled into an open-source desktop app is public. Options, to settle with a spike once OAuth is needed:
  1. **PKCE without a secret**, if Planning Center supports public clients. Not confirmed in the docs I could read [Unverified]; ask in the planningcenter/developers GitHub issues if the docs don't say.
  2. **A tiny token-exchange service** run by the project that holds the secret and swaps codes for tokens. Works with any OAuth provider, but adds a server to host, secure, and pay for.
  3. **Each church registers its own OAuth app** and enters its own client ID and secret. No server, but no better than a token for volunteers.
- Default: ship with tokens only, add option 1 if Planning Center supports it, otherwise decide between 2 and staying with tokens based on how many churches are actually using it.
- Rate limits are per user [Third-party], so many churches don't share one limit. A project-run exchange service (option 2) would only handle sign-in, not API traffic.
- Planning Center may have rules for apps used by many organizations (app listing, branding, review) [Unverified]; check before advertising the integration.

**Content licensing is always per church**
- The app ships **no** copyrighted song lyrics or Bible text, and no shared song library between churches. Each church brings its own CCLI license, Bible permissions, and API keys.
- Bible API keys (API.Bible, ESV API) are entered by each church in settings, never built into the app. A shared project key would make the project the licensee for every church's usage, which the providers' terms may not allow [Unverified], and ESV's API is limited to non-commercial use [Verified: Crossway permissions].
- Settings hold the church's CCLI license number so the song credit slide can show it [Assumption about what CCLI's license requires on screen; confirm].
- Translation records carry their license basis per library, so a library moved to another church doesn't carry permissions with it. The export screen warns when a library contains copyrighted translation text.

**Distribution and updates**
- macOS: other churches need a Developer ID-signed and notarized build, or Gatekeeper will block or warn on launch. That needs an Apple Developer Program membership (annual fee) [Unverified: current price and terms]. For the one church, an unsigned local build is fine.
- Windows: unsigned installers trigger SmartScreen warnings; a code-signing certificate costs money each year [Unverified: current options]. Only matters once Windows is supported.
- Updates: an "update available" check against GitHub Releases, with release notes, and never auto-installing during a service window. Automatic install can come later.
- Version the library format and migrate it on open; never let a newer library silently open in an older app.

**Settings and data per church**
- Everything church-specific (name, logo, CCLI number, Planning Center connection, API keys, default templates, MIDI mappings, display assignments) lives in the church's library or the OS keychain, never in the app bundle or source code.
- Display assignments and MIDI mappings are per computer, since churches (and campuses) have different hardware.
- No telemetry. If crash reports are added, they're opt-in, show what will be sent, and contain no service content or tokens.

**Support and project health**
- Written docs become a deliverable: install, connect Planning Center, add translations, set up outputs, MIDI setup with Playback and Ableton, and backup and restore.
- Public issue tracker with templates for bug reports, plus a support policy that says plainly this is volunteer-maintained with no guaranteed response time.
- Contributions: GPLv3 stays (7.1). Add a `CONTRIBUTING.md` and decide whether to require a Developer Certificate of Origin sign-off, which keeps relicensing questions clear [Assumption: DCO rather than a CLA, which is lighter for volunteers].
- Hardware variety matters more: Windows support (D1) moves up in priority if interested churches run Windows, and the Phase 0 benchmarks should be repeated on an older or lower-end machine.
- Localization: the UI isn't translated in v1, but strings are kept translatable from Phase 1 so another language can be added later.

---

## 8. Product decisions

Decided by Simeon on 2026-11-21 unless marked open.

| ID | Decision | Answer |
|---|---|---|
| D1 | Platforms | **macOS first.** Windows and Linux are a plus. Keep the code portable, run compile-only CI on Windows and Linux from Phase 1, and test and package those platforms after v1. |
| D2 | Final UI framework | **Open.** Decided at the end of Phase 0 using the P1 benchmarks. Qt 6 is the front-runner. |
| D3 | Project license | **GPLv3** ("whatever makes the most sense"; reasoning in 7.1). |
| D4 | Bible translations | **KJV, NLT, CSB, NKJV, maybe ESV.** The church will work on getting access. KJV ships locally; the others use paste-in passages or a licensed API until permission for local text exists (7.1). |
| D5 | Sermon slides | **Non-editable import is fine and animations don't matter.** |
| D6 | PowerPoint availability | **Often not installed** on the presentation computer. LibreOffice is the primary converter and PDF import is always available (5.7). |
| D7 | Lower thirds | **Configurable: main screen, livestream, or both** (5.5). |
| D8 | Use by other churches | **Possibly.** v1 targets your church; the rules in 7.5 apply from the start (no bundled content, per-church keys and licenses, auth behind an interface). Public release work comes after v1. |
| D9 | Which presentation computer and displays the church uses | **Open.** Record the model, macOS version, and display models during Phase 0. |
| D10 | Planning Center sign-in for other churches | **Open, later.** Tokens first; OAuth approach (PKCE, exchange service, or per-church app) decided after checking Planning Center's support (7.5). |
| D11 | Paying for code signing (Apple Developer Program, later a Windows certificate) | **Open, later.** Only needed when other churches start downloading builds. |

---

## 9. Explicit assumptions

- One presentation Mac, Apple Silicon, with at least two external displays (projector or TV plus stage monitor) [Assumption; record actual models and macOS version in Phase 0].
- Playback or Ableton run on a separate machine or the same Mac and connect via IAC, a USB MIDI interface, or network MIDI.
- Typical service: 4 to 7 songs, 1 to 3 scripture readings, 1 sermon deck, a few announcements.
- One operator per service, often a volunteer.
- ProPresenter remains available as a fallback throughout development.
- Target cue-to-screen latency under 100 ms and slide-change latency under 50 ms; these are design targets, not measured values.

---

## 10. Recommended first prototype

**P1, the output spike, in Qt 6 (Qt Quick plus C++).** It is the riskiest and most framework-dependent part of the whole system, and it decides the stack. Scope it to about two weekends:

1. An operator window with buttons and keyboard shortcuts for next, previous, and clear.
2. Two output windows, each assigned to a chosen display and fullscreen, with saved display assignment.
3. A looping video background (test files from 6.1) under centered lyric text that crossfades between ten hard-coded slides. The stage window shows current and next text without video.
4. An on-screen frame counter and a log of dropped frames, CPU, and memory.
5. A test of one decoder feeding both windows versus one decoder per window.

Run it for two hours on the church's presentation Mac, then unplug and replug the stage display. If it passes the criteria in Phase 0, commit to Qt. If it fails in a way that Qt can't work around, build the same spike in Avalonia with LibVLCSharp before considering anything else.

---

## Sources

Official documentation unless noted.

- Qt Releases and LTS: https://doc.qt.io/qt-6/qt-releases.html, https://www.qt.io/development/qt-framework/qt-lts
- Qt Licensing (GPL-only modules): https://doc.qt.io/qt-6/licensing.html
- Qt Multimedia (backends, FFmpeg licensing, codec patents): https://doc.qt.io/qt-6.8/qtmultimedia-index.html
- Qt FFmpeg hardware decoding configuration: https://doc.qt.io/qt-6/advanced-ffmpeg-configuration.html
- QMediaPlayer (single output, loops): https://doc.qt.io/qt-6/qmediaplayer.html
- Qt Quick Window (screen, visibility): https://doc.qt.io/qt-6/qml-qtquick-window.html
- Avalonia FAQ (license, Pro Media Player): https://avaloniaui.net/FAQ
- Avalonia window management (Screens API): https://docs.avaloniaui.net/docs/app-development/window-management
- Avalonia macOS guide (Metal rendering, NativeControlHost layering): https://docs.avaloniaui.net/docs/platform-specific-guides/macos
- LibVLCSharp.Avalonia: https://www.nuget.org/packages/LibVLCSharp.Avalonia/
- Tauri webview versions: https://v2.tauri.app/reference/webview-versions/
- Tauri window API typings: https://cdn.jsdelivr.net/npm/@tauri-apps/api@2.10.1/window.d.ts
- wxWidgets licence: https://wxwidgets.org/about/licence/
- wxMediaCtrl backends (wxPython docs, may lag wxWidgets): https://docs.wxpython.org/wx.media.MediaCtrl.html
- RtMidi: https://caml.music.mcgill.ca/~gary/rtmidi/index.html
- midir: https://docs.rs/crate/midir/
- DryWetMIDI supported OS: https://melanchall.github.io/drywetmidi/articles/dev/Supported-OS.html
- Windows MIDI Services announcement: https://blogs.windows.com/windowsexperience/2026/02/17/making-music-with-midi-just-got-a-real-boost-in-windows-11/
- Apple IAC Driver: https://support.apple.com/guide/audio-midi-setup/transfer-midi-information-between-apps-ams1013/mac
- Ableton Program Change: https://help.ableton.com/hc/en-us/articles/209070489-Sending-Program-Change-messages-from-Ableton-Live
- MultiTracks Playback MIDI cues: https://helpcenter.multitracks.com/en/articles/5354415-midi-out-series-5-build-midi-cues-in-playback
- Planning Center developers (auth): https://github.com/planningcenter/developers ; API docs: https://developer.planning.center/docs/
- Planning Center rate limits (third-party): https://www.usecarly.com/blog/planning-center-api/
- Planning Center item fields (third-party generated client): https://pub.dev/documentation/planningcenter_api/latest/planningcenter_api/PcoServicesItem-class.html
- PowerPoint for Mac export: https://support.microsoft.com/en-us/powerpoint/export-your-powerpoint-for-mac-presentation-as-a-different-file-format
- PowerPoint Slide.Export (Windows VBA): https://learn.microsoft.com/office/vba/api/powerpoint.slide.export
- PresentationML structure: https://learn.microsoft.com/en-us/office/open-xml/presentation/structure-of-a-presentationml-document
- LibreOffice soffice options: https://dyn.manpages.debian.org/testing/libreoffice-common/soffice.1
- ESV permissions: https://www.crossway.org/permissions/
- API.Bible caching: https://care.api.bible/article/401-caching-content
- World English Bible: https://ebible.org/details.php?all=1&id=engwebp
- KJV copyright notes: https://ebible.org/KJV/copr.htm
- NLT permissions (Tyndale): https://www.tyndale.com/permissions
- NKJV permissions (HarperCollins Christian): https://www.harpercollinschristian.com/permissions/
