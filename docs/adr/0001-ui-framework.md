# 0001: UI framework: Qt 6 (Qt Quick and C++)

**Status:** Accepted, 2026-11-21, pending a confirmation pass on the church presentation Mac (see "Still to confirm").

## Context

The hardest requirement is lyrics and other text animating over a looping, hardware-decoded video on two or more fullscreen outputs, on chosen displays, surviving hot-plug, with no visible dropped frames (plan 5.1, 6.1). Plan 5.1 narrowed the choice to Qt 6 and Avalonia, recommending Qt first and building the Avalonia version of the P1 spike only if Qt showed a blocker.

P1 was built in Qt 6.12 (`spikes/output/`) and tested on the development Mac (M1, macOS 15.7.9). The built-in display ran the operator window. External displays were simulated with virtual displays that macOS treats as plugged-in monitors. Results:

- [2026-11-20, windowed](../benchmarks/2026-11-20-p1-dev-m1-windowed.md)
- [2026-11-21, virtual displays](../benchmarks/2026-11-21-p1-dev-m1-virtual-displays.md)

What P1 showed:

| Requirement | Result in Qt |
|---|---|
| Outputs fullscreen on chosen displays, kept by saved identity | Works. Unplug, replug, a different display appearing, a resolution change, and losing both outputs all pass (23 of 23 checks, 3 runs). |
| Text over hardware-decoded video | Works. Frames stay on the GPU as textures, and slide changes never touch the video. |
| One decoder feeding every output | Works, once the shared decoder has its own GPU context. It costs about the same as one decoder per output (about 24% of one core for 4K HEVC on two outputs), and both outputs always show the same frame. |
| All test clips (1080p30 and 1080p60 H.264, 4K30 HEVC, ProRes 422) | 0–2 late frames per output in 60 s with the AVFoundation backend, in both decoder modes. |
| Command to new frame | Median 2–17 ms, against a 50 ms target (before display and projector lag). |
| Looping | Usually one extra refresh (about 16 ms) at the loop point; sometimes none. |
| 2-hour run | 1080p30 H.264 was clean for 1 h 43 min, with video freezes of 3–11.5 s in the first 15 min. The batch was cut short by the Mac running out of memory (the terminal app had grown to about 109 GB), which most likely caused the freezes. 4K HEVC wasn't run on a quiet machine. |
| Memory | No leak over 2 hours. |

Problems found, each with a fix that stays inside Qt:

1. macOS native fullscreen stops one output drawing when another enters fullscreen after it. Borderless windows sized to the display don't have this problem, and they recover from display changes in 0.1–0.3 s instead of up to 2.7 s.
2. Borderless windows sit under the menu bar. Raising them to `NSStatusWindowLevel` fixes that, and also keeps the Dock and notification banners off the outputs. This needs a few lines of Objective-C++.
3. The shared decoder delivered frames in CPU memory (about 2x CPU, 2.5x memory) until it was given its own `QRhi`.
4. The FFmpeg backend with shared decoding paced ProRes badly in 2 of 3 runs. The AVFoundation backend was clean on every clip.

No blocker was found, so the Avalonia spike isn't needed.

## Decision

Build Visualis in **Qt 6, with Qt Quick (QML) for the UI and C++ for the core**, targeting the newest Qt minor release and upgrading once or twice a year (plan 5.1).

Output and video choices carried over from P1:

- **Outputs are borderless windows** sized to their display and raised above the menu bar on macOS, not native fullscreen.
- **One decoder per background** feeds every output (a hub `QVideoSink` with its own `QRhi`).
- **On macOS, use the AVFoundation (`darwin`) Qt Multimedia backend**, not Qt's default FFmpeg backend.
- **Displays are identified by saved key**, but not by serial number alone. Qt reported no serial, manufacturer, or model for any display tested, so keys fell back to the display name (plan 5.5 needs revisiting with real monitors).

## Consequences

- C++ and QML, compile times, and packaging and signing by hand (plan 5.1).
- **The shared decoder uses the `QRhi` API, which is in `Qt6::GuiPrivate`.** Qt promises less stability for it than for public API, so expect small fixes on Qt upgrades. Keep that code in one place.
- **The shared decoder's own GPU context is Metal-only for now.** On Windows and Linux the spike falls back to borrowing an output window's context, which dies with that window on hot-plug. Before shipping there, give it its own D3D or Vulkan context, or use one decoder per output.
- **The macOS window level needs native code** (`spikes/output/src/macwindow.mm`). Each platform will need its own way to keep outputs above system UI.
- **Requirements for the real app** that came out of P1:
  - a watchdog that notices when video frames stop arriving, and recovers or alerts the operator
  - a power assertion that stops displays idle-sleeping while presenting
  - treating the first "display added" event as provisional, because the display's name arrives a moment later
  - falling back to a free display when an output's saved display is the operator screen
- The AVFoundation backend is macOS-only, so Windows and Linux will use FFmpeg and need their own P1-style runs before support is claimed. The spike hasn't been compiled on Windows or Linux yet (a Phase 0 deliverable still open).

## Still to confirm

On the church presentation Mac, with its real displays and projector, using the spike's scripts (`run-bench.sh`, `run-hotplug-test.sh`, `run-sleep-test.sh`):

1. **2-hour runs with 1080p30 H.264 and 4K HEVC** on a quiet machine, with no video freezes.
2. **Real cable unplug and replug** of the stage display, and what the projector shows meanwhile.
3. **System sleep and wake** (`run-sleep-test.sh system`).
4. **What Qt reports as each real display's identity.**
5. **The phone-camera checks:** visible drops, the loop-point hold, and cue-to-photon time.

If any of these fails in a way Qt can't work around, revisit this decision.
