# Fade frame timing on the development Mac

Date: 2026-11-24. Issue #96. This checks that the text and background fades drop no frames, per plan 5.5.

## Setup

- MacBook with Apple M1, macOS 15.7.9, built-in display only, 1440x900 at 60 Hz
- Qt 6.12.0 from Homebrew, Release build, `QT_QPA_PLATFORM=cocoa`
- One audience output window at 1920x1080, not fullscreen. The window is larger than the laptop's screen, so this isn't the church Mac's setup.
- The built-in Lyrics template's look, with the shadow on and a 500 ms text fade. The background fade is the output's 600 ms clear and show, which #98 will replace with the template's background transition.
- Video runs use a 1080p30 H.264 `testsrc` loop from `spikes/output/scripts/make-test-media.sh`, playing through `VideoEngine`.
- Each run is 20 text fades, alternating Next and Previous, and then 10 background fades, alternating clear and show, 250 ms apart.

```sh
cmake --preset release && cmake --build --preset release --target tst_outputs
VISUALIS_FRAME_BENCH=1 QT_QPA_PLATFORM=cocoa build/release/tests/tst_outputs fadesDropNoFrames
# over video:
VISUALIS_BENCH_VIDEO=spikes/output/media/1080p30-h264.mp4 VISUALIS_FRAME_BENCH=1 QT_QPA_PLATFORM=cocoa \
    build/release/tests/tst_outputs fadesDropNoFrames
```

The method is the one the P1 benchmarks use in `spikes/output/src/framestats.cpp`. It records `frameSwapped` timestamps and counts an interval over 1.5 refresh periods as late. Only intervals inside a fade count. The first interval of each fade is skipped because it includes the command's latency after an idle window.

## Results

| Run | Text fade intervals | Late | Longest | Background fade intervals | Late | Longest |
|---|---|---|---|---|---|---|
| No video, 1 | 576 | 0 | 21.2 ms | 347 | 0 | 23.2 ms |
| No video, 2 | 575 | 1 | 27.1 ms | 350 | 0 | 18.9 ms |
| No video, 3 | 575 | 0 | 19.1 ms | 349 | 0 | 21.1 ms |
| Video, 1 | 578 | 0 | 24.4 ms | 350 | 1 | 42.1 ms |
| Video, 2 | 579 | 0 | 19.6 ms | 349 | 0 | 19.6 ms |
| Video, 3 | 580 | 0 | 19.9 ms | 349 | 0 | 19.8 ms |

A seventh run, after the idle layer was hidden so it no longer costs a second shadow pass, had 0 late in 576 and 349.

The first six runs had 2 late intervals in about 5,560, each in a different run. The P1 windowed runs saw the same rate with no video at all, 2 in 25 s. The fades don't add dropped frames. A 500 ms fade had at least 28 counted intervals out of a possible 29 at 60 Hz.

## Findings

1. **`QTest::qWait` makes animations look like they drop frames.** The first runs waited with `QTest::qWait` and had about 11% late intervals. Each was 25 to 31 ms, so one missed refresh. The rate held at every window size from 640x360 up to 1920x1080, in both Debug and Release builds. `qWait` sleeps between batches of events, so the GUI thread misses animation ticks. Waiting with a `QEventLoop`, as the app does, removed the late intervals. Any frame-timing test in Qt Test has to wait this way.
2. **The shadow isn't a cost.** The text fades re-render the shadow effect every frame, and they timed the same as the background fades, where the text didn't change.
3. **Not tested yet.** Fullscreen on the church Mac's displays and projector, a 4K or ProRes background under a fade, and the stage output fading at the same time.
