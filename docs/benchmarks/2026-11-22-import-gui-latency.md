# GUI-thread latency during a song import

Date: 2026-11-22. Issues #69 and #77. The tests in `tests/tst_operator.cpp` assert only loose wall-clock bounds (1 s) so they don't flake on a slow CI runner. The numbers below are what those tests measured on the development machine.

## Setup

- MacBook (Apple M1), macOS 15.7.9, Debug build, `QT_QPA_PLATFORM=offscreen`
- Synthetic Visualis song exports (two groups, three slides each) in a temporary folder, imported into a new library on the internal SSD
- A 10 ms `QTimer` records the longest gap between ticks. For the save numbers, the test pastes a new song about every 5 ms while the import runs.
- Other agents' builds were running at the same time (load average 7 to 41), so the worst cases include that noise.

## Results

| Measurement | Before #77 (worker writes on its own connection) | After #77 (GUI connection is the only writer) |
|---|---|---|
| GUI save while idle | 5.3 ms | 5.3 ms |
| GUI save during a 2000-file import, median | 5.6 to 6.4 ms | about 5 ms |
| GUI save during a 2000-file import, worst | **67 to 205 ms** | 7 to 22 ms (91 ms once, at load average 41) |
| Longest timer gap, 2000-file import with saves | not measured | 22 to 42 ms (119 ms once, at load average 41) |
| Longest timer gap, 1000-file import | 11 to 12 ms (#69) | 11 to 16 ms |
| 1000-file import, total time | about 300 ms (#69) | about 190 ms |

Before #77, the worst-case waits came from SQLite's busy handler, which sleeps in growing steps while another connection holds the write lock. Each importer transaction was only one song, but that didn't help. After #77, a save never waits on a lock. What's left is the import's own batches: 5 ms of saves, then one commit, every 10 ms, plus a reread of the library's titles at the start of each batch.
