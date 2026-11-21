# P2: MIDI spike

Throwaway prototype for plan 3.1 (P2) and 6.3: what MIDI arrives, how fast, in what order, and what happens when the sending app restarts. Objective-C on CoreMIDI, using the MIDI 1.0 protocol through Universal MIDI Packets (macOS 11+). The real app may use RtMidi (plan 5.1); the findings carry over.

## Build and run

```sh
cd spikes/midi
clang -fobjc-arc -O2 -framework Foundation -framework CoreMIDI -o build/midi-spike midi-spike.m
./build/midi-spike list                                   # MIDI sources and destinations
./build/midi-spike listen > out/rehearsal.csv             # log everything until Ctrl-C
./build/midi-spike loopback --count 2000 --interval-ms 1 [--via "IAC Driver Bus 1"]
./build/midi-spike send "Visualis Cues" --count 10        # or --virtual, or a bus name
```

`listen` creates a virtual MIDI destination called **Visualis Cues** that Ableton or Playback can send to directly. It also connects to every MIDI source, including ones that appear later, and writes one CSV line per message: time, source, type, channel, data, and `age_ms` (delivery time, when the sender stamped the message).

## Findings (development Mac, M1, macOS 15.7.9, 2026-11-21)

1. **MIDI delivery on the Mac is effectively instant and reliable.** In 2,000-message runs, no message was lost or arrived out of order, even with no gap between messages.

   | Path | Spacing | Median | p99 | Max |
   |---|---|---|---|---|
   | in-process virtual source | 1 ms | 0.04 ms | 0.06 ms | 0.08 ms |
   | in-process virtual source | none | 0.59 ms | 0.85 ms | 0.88 ms |
   | IAC Driver Bus 1 | 1 ms | 0.04 ms | 0.07 ms | 0.15 ms |
   | IAC Driver Bus 1 | none | 0.89 ms | 1.49 ms | 1.50 ms |

   The 100 ms cue-to-screen target (plan Phase 0) is spent almost entirely on rendering and the display, not on MIDI.
2. **A sending app restarting mid-service is handled.** Watching CoreMIDI's setup-changed notification and reconnecting every source picked up a sender that quit and restarted three times. All messages after each restart arrived. The listener logs each connect and disconnect, so the operator UI can show "Playback disconnected".
3. **A virtual destination works.** Messages sent straight to "Visualis Cues" arrived, so Ableton or Playback can target Visualis by name without the IAC bus.
4. **This Mac already has a "Network Ableton" network MIDI session** and the IAC bus turned on. If the church's Ableton sends cues over the network, Wi-Fi or Ethernet latency and dropouts are the real risk, not CoreMIDI. That needs measuring on the church's network.

## Still needs the church's rig

- **What Ableton and Playback actually send:** message types, channels, and note or program numbers. Run `listen` through a rehearsal and keep the CSV.
- **Network MIDI latency and dropouts** between the Ableton machine and the presentation Mac.
- **Cue-to-photon time** with the P1 outputs and a phone camera (plan 6.1).
