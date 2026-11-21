# P1 output spike: hot-plug and fullscreen on virtual displays

Date: 2026-11-21. Spike at the commit that adds this file.

**Not the Phase 0 acceptance runs.** These use two virtual displays on the development laptop instead of real monitors, so they test the app's display handling and macOS's window behavior, not the church Mac's GPU, HDMI, or projectors.

## Setup

- MacBook (Apple M1), macOS 15.7.9, built-in display 1440x900 at 60 Hz (operator screen)
- Two virtual 1920x1080 at 60 Hz displays from `spikes/output/tools/virtual-display.m` (undocumented `CGVirtualDisplay` API). Starting the process plugs a display in, and killing it unplugs it.
- Qt 6.12.0 (Homebrew), Release build, default backend (FFmpeg), shared decoding, stage video on, 1080p30 H.264 loop
- `scripts/run-hotplug-test.sh` drives `tests/hotplug.txt`: unplug stage, a different display appears, replug stage, replug audience at 1280x720, unplug both, replug both.

## Results

| Fullscreen mode | Runs | Checks passed | Other output frozen on each plug or unplug |
|---|---|---|---|
| native (own Space) | 2 | 22 of 23, both runs | 1.0–2.7 s |
| borderless, above the menu bar | 3 | 23 of 23, all runs | 0.1–0.3 s |

In every run, each output went back to its own display after a replug (by saved identity), stayed hidden while a different display was plugged in, and came back at the new resolution. Shared decoding kept playing through every change.

## Findings

1. **Native fullscreen stops one output drawing.** When an output enters native fullscreen after the other one is already fullscreen, macOS stops drawing the earlier one (`not exposed`, 0 fps) until some later display change. When both enter fullscreen together at launch, both draw. The order changes on every hot-plug, so native mode isn't reliable for us. Borderless is now the spike's default.
2. **Borderless windows sit under the menu bar unless raised.** A borderless window at the display's full size is at window level 0. The menu bar is at level 24 on every display, so it would show across the top 24 px of the projector. The spike now sets the `NSWindow` level to `NSStatusWindowLevel` (25), which also covers the Dock and notification banners on output displays (see finding 1 of the 2026-11-20 note). Confirmed with `CGWindowListCopyWindowInfo`: outputs at level 25, still there after a replug.
3. **Display changes freeze the other outputs briefly.** Plugging or unplugging any display froze the remaining outputs for 0.1–0.3 s in borderless mode and up to 2.7 s in native mode. This is probably macOS reconfiguring displays and is likely outside our control. It's worth confirming with real monitors. Unplugging the stage cable mid-service would cause a visible hitch of up to 0.3 s on the audience screen.
4. **Qt reports no serial, manufacturer, or model for any display here,** built-in or virtual. Display keys fall back to the display name (`Fake Stage||`), so two identical monitors would get the same key. Check what Qt reports for the church's real monitors and projector before relying on identity by serial (plan 5.5).
5. **A display choice saved in windowed mode carries into fullscreen mode.** If an output was saved on the built-in screen, in fullscreen mode it waits with "assigned display is the operator screen" even when free displays exist. The real app should fall back to a free display, or ask.
6. **Right after a display is added, Qt reports it with an empty name.** The name arrives with a geometry change a few milliseconds later. The spike places outputs again on that change, so it works, but the real app shouldn't treat the first "screen added" event as final.

## All test clips, fullscreen on virtual displays

`scripts/run-bench.sh <clip> 60`: borderless outputs on both virtual displays, shared decoding, stage video on, slides every 2 s, default backend (FFmpeg).

| Clip | CPU mean | Peak memory | Late frames (audience / stage, of ~3540) | Command → swap, median | Loop gaps beyond one frame |
|---|---|---|---|---|---|
| 1080p30 H.264 | 23.2% | 135 MB | 1 / 2 | 16.1 ms | 16.2, 16.3 ms |
| 1080p60 H.264 | 27.0% | 137 MB | 2 / 2 | 10.2 ms | 16.3, 16.3 ms |
| 4K30 HEVC | 23.1% | 135 MB | 1 / 0 | 7.8 ms | 16.4, −0.5 ms |
| 1080p30 ProRes 422 | 16.5% | 139 MB | 1 / **1472** (stage at 38.7 fps) | 23.4 ms | 18.3, 17.9 ms |

ProRes, 30 s runs:

| Backend | Mode | Late frames (audience / stage) |
|---|---|---|
| FFmpeg | shared | 1 / 2, then 227 / 1 (plus the 60 s run above) |
| FFmpeg | per-window | 0 / 0 |
| darwin (AVFoundation) | shared | 0 / 0 |
| darwin (AVFoundation) | per-window | 0 / 0 |

7. **FFmpeg with shared decoding paces ProRes badly, intermittently** (2 of 3 runs). Frames still arrive as GPU textures (`BGRA8888`, `rhi-texture`). FFmpeg decodes ProRes in software, so each frame is uploaded through the hub's own `QRhi`, which has no render loop pacing it. That's the likely cause, but not confirmed. AVFoundation uses Apple's ProRes decoder and was clean in both modes, as it was on every other clip. **This points to the darwin backend on macOS** for ADR 0001.

## Sleep

`scripts/run-sleep-test.sh display` turns the displays off with `pmset displaysleepnow` mid-playback and wakes them 15 s later. It passes: same placement, video still playing, 60 fps after wake. Both outputs stalled once for 1.1 s when the displays went off. Virtual displays don't sleep with the real ones, so the outputs kept rendering during the sleep. This only shows the app survives the event.

8. **The real app should stop the displays idle-sleeping while presenting** (an `IOPMAssertion`), so this never happens mid-service. Whether Qt Multimedia already holds one during playback is unchecked.

System sleep (`scripts/run-sleep-test.sh system`) needs someone to wake the Mac and hasn't been run yet.

## Not covered by virtual displays

- The church Mac's GPU and decode load, and 2-hour runs there.
- Real HDMI and projector behavior on hot-plug (projectors take seconds to resync, which the audience sees regardless of the app).
- Visible drops, the loop-point hold, and cue-to-photon time, which need a phone camera.
