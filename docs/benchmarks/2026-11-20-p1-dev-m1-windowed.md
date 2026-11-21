# P1 output spike: first runs on the development Mac

Date: 2026-11-20. Spike at the commit that adds this file.

**These runs are not the Phase 0 acceptance runs.** They are short smoke runs on the development laptop with one display, so outputs ran as 640x360 windows instead of fullscreen on separate displays. They check that the spike works and show early trends. The acceptance runs need the church presentation Mac, two external displays, and 2 hours per configuration.

## Setup

- MacBook (Apple M1), macOS 15.7.9, built-in display only, 1440x900 at 60 Hz
- Qt 6.12.0 (Homebrew), Release build
- `--windowed` (automatic with one display), `--auto-advance 2`, about 25 s per run
- Clips from `spikes/output/scripts/make-test-media.sh` (20 s `testsrc` loops)

## Results

"Late" counts frame intervals over 1.5 refresh periods, only in seconds when the window was exposed (not covered). Both outputs swap together, so they show the same frame count.

| Run | Video | Mode | Backend | CPU mean (% of one core) | Peak memory | Late frames per output (of ~1400) | Command → swap, median |
|---|---|---|---|---|---|---|---|
| No video | none | – | default (FFmpeg) | 17.4% | 125 MB | 2 / 2 | 1.3 ms |
| 1080p30 H.264, both outputs | 1080p30 H.264 | shared | FFmpeg | 31.7% | 184 MB | 3 / 2 | 9.2 ms |
| 1080p30 H.264, both outputs | 1080p30 H.264 | per-window | FFmpeg | 23.6% | 132 MB | 5 / 6 | 2.6 ms |
| 4K30 HEVC, both outputs | 4K30 HEVC | shared | FFmpeg | 44.0% | 321 MB | 6 / 5 | 6.7 ms |
| 4K30 HEVC, both outputs | 4K30 HEVC | per-window | FFmpeg | 24.8% | 132 MB | 2 / 1 | 3.7 ms |
| 4K30 HEVC, both outputs | 4K30 HEVC | shared | darwin (AVFoundation) | 45.5% | 338 MB | 14 / 7 | 8.1 ms |
| 4K30 HEVC, both outputs | 4K30 HEVC | per-window | darwin (AVFoundation) | 25.4% | 132 MB | 1 / 2 | 8.0 ms |

Loop point: on nearly every loop the last frame was held about 16.4 ms longer than normal, which is one extra refresh at 60 Hz. Some loops showed no gap. Whether one held frame is visible on 30 fps content needs the phone-camera check.

## Findings so far

1. **Covered outputs stop rendering.** macOS stops drawing a window that is fully covered: the window reports not exposed and its frame rate drops to 0. Uncovering it gives a stall of up to 1.6 s. On the church Mac, outputs are fullscreen on their own displays so nothing should cover them, but a notification or a dialog might. Worth testing on purpose in the acceptance runs. The spike now logs exposure changes.
2. **Shared decoding was more expensive than one decoder per output, because the hub had no GPU context.** With 4K HEVC it used about 1.8x the CPU and 2.5x the memory. Logging the first frame confirmed the cause: the hub `QVideoSink` had no RHI, so both backends handed it NV12 frames in CPU memory, while the per-window sinks got GPU textures. **Fixed 2026-11-21** (see "Shared decoding with its own GPU context" below): the engine now gives the hub its own Metal `QRhi`, and shared mode costs about the same as per-window.
3. **Both outputs advance together.** With per-window decoders, the two players started together and stayed close in these short runs. Drift over 2 hours is untested.
4. **Command-to-swap latency is far under the 50 ms target** in every run (median 1–9 ms). Display scan-out and projector lag still need adding with a camera.
5. **A few late frames even with no video** (2 in 25 s). In windowed mode on a laptop that's also running other apps, this is probably background noise. The acceptance target (0 visible drops) applies to fullscreen outputs on the church Mac.

## Shared decoding with its own GPU context (2026-11-21)

Same machine and settings as above, after `VideoEngine` gave the hub sink its own Metal `QRhi` (created with `QRhi::create`, so it doesn't depend on any output window). Frames now reach the hub as GPU textures (`handle=rhi-texture` in `events.log`), and both output windows draw them, though they render with their own contexts.

| Run | Backend | CPU mean | Peak memory | Late frames per output | Command → swap, median |
|---|---|---|---|---|---|
| 1080p30 H.264, shared | FFmpeg | 22.4% (was 31.7%) | 137 MB (was 184) | 0 / 0 | 6.2 ms |
| 4K30 HEVC, shared | FFmpeg | 24.8% (was 44.0%) | 133 MB (was 321) | 1 / 0 | 7.5 ms |
| 4K30 HEVC, shared | darwin | 24.0% (was 45.5%) | 133 MB (was 338) | 0 / 0 | 8.1 ms |
| 4K30 HEVC, per-window (rerun, unchanged code) | FFmpeg | 25.8% | 132 MB | 0 / 0 | 16.7 ms |
| 4K30 HEVC, per-window (rerun, unchanged code) | darwin | 26.4% | 132 MB | 0 / 0 | 2.5 ms |

Shared and per-window now cost about the same, with shared slightly lower on CPU. Shared has a structural advantage: both outputs always show the same frame, so there's no drift to manage over a 2-hour service. Command-to-swap medians vary between 2 and 17 ms across reruns of unchanged code, so differences under one refresh (16.7 ms) are noise in these short runs.

Hiding the audience output during playback (what the app does when its display is unplugged) leaves the stage at 60 fps, and the audience resumes at 60 fps when shown again, on both backends (`tests/hide-output.txt`). An earlier version borrowed the audience window's GPU context instead. That worked, but it would tie the decoder to a window that disappears on hot-plug, so it's now only the fallback where an own context can't be created (non-Apple platforms in this spike).

Open questions: whether drawing textures from another `QRhi` is supported by Qt or only works because Metal shares one `MTLDevice` per GPU. On a Mac with two GPUs, or on Windows and Linux, it needs checking. The RHI API is in `Qt6::GuiPrivate`, which has weaker compatibility promises than public Qt API.

## Not tested yet

- Fullscreen on separate displays (native and borderless modes), display hot-plug, and sleep. All need external displays.
- The 1080p60 and ProRes clips, and 2-hour runs.
- The display and video controls in the operator window, driven by hand. Keyboard shortcuts are covered by `scripts/run-keyboard-test.sh`.
- GPU load (use `powermetrics` or Activity Monitor).
