# P1: Output spike

Throwaway prototype that answers plan section 6.1 (multiple displays and video) and feeds ADR 0001 (UI framework). This file is the spec. Any framework built for comparison (Qt 6 first, Avalonia only if Qt shows a blocker) implements the same spec, so the numbers compare.

## Spec

1. **Operator window** on the operator's screen with Previous, Next, Clear text, Clear background, and Clear all, on buttons and keys:
   - Next: Right, Down, Space, Page Down (clickers usually send Page Up/Down)
   - Previous: Left, Up, Page Up
   - Clear text: `C`. Clear background: `B`. Clear all: `Esc`
2. **Two output windows:** `audience` and `stage`. Each is fullscreen on a display chosen in the operator window. The choice is saved by stable display identity, not by index, and comes back after a restart. An output is never placed on the operator's screen. If its display is missing, the output hides and waits, and returns when that same display is plugged back in.
3. **Audience:** a looping video background under centered lyric text that crossfades between ten hard-coded slides. Changing slides never touches the video. **Stage:** current and next slide text, a clock, no video (video can be turned on for the decoder test).
4. **Measurements:** an on-screen frame counter in each output (film it with a phone camera to spot drops), and a per-second log of frames, late frames, longest frame interval, CPU, and memory. Plus the time from an operator command to the first frame swapped with the change, and the loop-point gap of the video.
5. **Decoder test:** one decoder feeding both outputs (`shared`) versus one decoder per output (`per-window`). Turn on stage video for this.

## Acceptance (plan Phase 0)

- Runs 2 hours on the church presentation Mac with a 1080p30 H.264 loop and with a 4K HEVC loop, with 0 visible dropped frames.
- Survives unplugging and replugging the stage display, and the laptop sleeping.
- Results go in `docs/benchmarks/` and ADR 0001.

## Build and run (Qt 6, macOS)

Requires Qt 6.8 or newer, CMake, Ninja (`brew install qt cmake ninja ffmpeg`).

```sh
cd spikes/output
./scripts/make-test-media.sh            # writes media/*.mp4, *.mov (git-ignored)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike --video media/4k30-hevc.mp4 --video-mode per-window
./scripts/summarize.py spike-logs/<timestamp>     # after quitting
```

If every C++ file fails with `'type_traits' file not found`, the Command Line Tools have a stale partial `/Library/Developer/CommandLineTools/usr/include/c++/v1` folder left from an older install. Clang searches it before the SDK's complete copy. Moving that folder aside or reinstalling the Command Line Tools fixes it. To build without changing the system, add `-DCMAKE_CXX_FLAGS="-nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1"` to the first `cmake` command.

Options:

| Option | Meaning |
|---|---|
| `--video <file>` | Background video. Can also be chosen in the operator window. |
| `--video-mode shared\|per-window` | One decoder for all outputs, or one per output. Default `shared`. |
| `--stage-video` | Also show the video behind the stage text (needed for the decoder test). |
| `--backend ffmpeg\|darwin` | Qt Multimedia backend. Sets `QT_MEDIA_BACKEND`. Default: Qt's default (FFmpeg). |
| `--fullscreen borderless\|native` | A borderless window sized to the display and raised above the menu bar, or macOS native fullscreen (own Space). Default `borderless`: native fails the hot-plug test (an output that enters fullscreen after another one stops the other from drawing). |
| `--windowed` | Outputs as ordinary windows, allowed on the operator screen. Turned on automatically with only one display. |
| `--settings-dir <dir>` | Keep settings in an INI file in this directory instead of the user's preferences. The test scripts use it. |
| `--log-dir <dir>` | Where logs go. Default `spike-logs/<timestamp>/` in the current directory. |
| `--script <file>` | Run a test script (below), then quit. Exit code 1 if any check failed. |
| `--auto-advance <seconds>` | Go to the next slide every N seconds, wrapping at the end. For soak runs without an operator. |

## Logs

- `frames.csv`: one row per output per second: `time, window, exposed, frames, fps, late, max_interval_ms, refresh_hz, cpu_percent, rss_mb, video_mode, backend`. A frame is late when its interval is over 1.5 refresh periods. Outputs render continuously (the frame counter changes every frame), so a late frame is a dropped frame. `exposed` is 0 when the OS isn't letting the window draw: macOS stops drawing fully covered windows, and those seconds aren't renderer drops.
- `events.log`: commands, command-to-swap latency, loop gaps, display changes, exposure changes, and window placement.
- `scripts/summarize.py <log-dir>` turns both into the numbers listed under "What to record".

In `--windowed` mode the outputs are 640x360, kept above other windows, in the right-hand corners of the screen, so they aren't covered while measuring.

Command-to-swap latency is measured from the command to the first frame that the render thread synchronized after it. It does not include display scan-out or projector lag; film the screen to get cue-to-photon time.

GPU load isn't logged by the app. Watch it with `sudo powermetrics --samplers gpu_power -i 1000` or Activity Monitor's GPU History.

## What to record

For each run: machine, macOS version, displays, backend, fullscreen mode, video file, video mode, run length, and the summary from `frames.csv` (total frames, total late, worst interval, mean CPU, peak memory). Measure no-video first (framework overhead), then video on one output, then two (decode and render cost).

First results: [docs/benchmarks/2026-11-20-p1-dev-m1-windowed.md](../../docs/benchmarks/2026-11-20-p1-dev-m1-windowed.md). Hot-plug and fullscreen on virtual displays: [docs/benchmarks/2026-11-21-p1-dev-m1-virtual-displays.md](../../docs/benchmarks/2026-11-21-p1-dev-m1-virtual-displays.md).

## Test scripts

`./scripts/run-smoke.sh` runs `tests/operator-smoke.txt` (about 40 s, windows open on screen, nothing to press) and prints each check. Snapshots of every window after each step land in `spike-logs/smoke-<timestamp>/snapshots/`.

A script is one step per line. The steps call the same commands as the operator window's keys and buttons, so the operator's real key presses and clicks aren't tested this way. Steps: `next`, `prev`, `goto <slide>`, `clear-text`, `background on|off`, `clear-all`, `mode shared|per-window`, `video <file>`, `assign <role> <screen key>`, `wait <ms>`, `mark <text>` (logs a marker for an outside script to wait for), `snapshot <name>`, `expect <key> <op> <value>`, and `wait-for <key> <op> <value> [timeout ms]`. Operators are `=`, `!=`, `>=`, `<=`, `~` (starts with), and `has` (contains). `src/scriptrunner.h` lists the state keys.

`tests/hide-output.txt` hides the audience output mid-playback, as an unplugged display would, and checks that shared decoding keeps feeding the stage and resumes the audience when it comes back. Run it with `--video media/4k30-hevc.mp4 --video-mode shared --stage-video --script tests/hide-output.txt`. It ends by assigning the audience back to `Built-in Retina Display||`, so it only fits a Mac with that display.

### Hot-plug without external displays

`./scripts/run-hotplug-test.sh [borderless|native]` (about 35 s) puts the outputs fullscreen on two virtual displays and plugs and unplugs them while `tests/hotplug.txt` checks placement and frame rate: unplug stage, a different display appears, replug stage, replug audience at 720p, unplug and replug both.

The virtual displays come from `build/virtual-display` (`tools/virtual-display.m`), which uses `CGVirtualDisplay`, an undocumented macOS API. macOS treats each one like a plugged-in monitor until the process is killed, so plugging is starting it and unplugging is killing it. It's test tooling only and must never ship in the app. It can't stand in for the church Mac's GPU, real HDMI or projector behavior, or the phone-camera checks.

### Benchmarks and sleep without external displays

`./scripts/run-bench.sh <clip> <seconds> [app options]` runs the outputs fullscreen on two virtual displays, advancing slides every 2 s, keeps the Mac awake, and prints the summary. For the 2-hour acceptance runs use 7200 seconds.

`./scripts/run-sleep-test.sh display` sleeps and wakes the displays mid-playback, unattended. `./scripts/run-sleep-test.sh system` puts the Mac to sleep; wake it by hand after about 15 s.

`assign` saves the display choice to the app's settings, the same as the operator window does. The test scripts pass `--settings-dir` so tests never change the display choices of manual runs. A snapshot stalls rendering for about 230 ms while Qt renders the window again to capture it, so keep snapshots out of measurement runs.

