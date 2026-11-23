# MIDI routing, clock, and external sequencing

NEUROGENIC's MIDI 1.0 subsystem keeps device ownership outside the musical
engine. `MidiInput` and `MidiOutput` are libremidi adapters. Logical ports
(`1..8`) remain stable in timelines and projects; a device selector binds a
logical port to a host port. Missing devices remain missing and are never
silently replaced by another port.

Task 069 keeps musical input and control-surface interpretation independent.
`MidiControlSurfaceAdapter` only consumes explicitly configured port/channel
note or CC bindings and forwards normalized controls to a `ControllerSession`.
Unmatched messages continue through `MidiMapper` note and raw handlers. It
supports absolute CC, two common relative-encoder encodings and momentary
notes; soft takeover is profile-driven. Task 070 adds explicit learn with
cancel/timeout, atomic MIDI-profile persistence, exact selector startup through
`--midi-controller-profile`, and optional outgoing note/CC feedback. Learn only
captures an eligible event while armed; unmatched events remain ordinary
musical MIDI. See
[controller integration](CONTROLLER_INTEGRATION.md).

The input callback copies messages into the bounded controller queue. The
control dispatcher preserves controller mappings and passes raw messages into a
second bounded queue owned by `AudioEngine`. At the next audio block the engine
evaluates validated thru routes, received transport, and clock state. Outbound
messages enter bounded normal and priority SPSC queues. Clock, transport,
note-off, and panic cleanup use reserved priority capacity. A non-real-time dispatch thread alone
calls libremidi. Queue exhaustion drops the newest message and increments the
observable counter; Stop and Panic enqueue CC 123 (All Notes Off) and CC 120
(All Sound Off) for every channel.

## Clock

Internal clock uses the authoritative transport tempo and an integer rational
phase accumulator to emit 24 PPQN without cumulative fractional-BPM drift.
Start resets phase; Stop halts it; Continue preserves phase. Tempo changes
rescale the fractional phase rather than restarting it. Physical transmission
occurs after the audio callback, so frame timestamps express intended engine
time, not a claim of sample-accurate cable timing.

External reception recognizes Clock, Start, Continue, Stop, and 14-bit Song
Position Pointer (SPP, in MIDI sixteenth notes). A bounded seven-pulse median
filter estimates pulse frequency; a 1/8 low-pass update follows gradual drift.
Each pulse applies at most one eighth-pulse phase correction unless acquiring,
recovering, or more than two pulses out of phase. The resulting Q32 pulse phase
drives the authoritative timeline range, and event offsets are proportionally
mapped into each audio callback. This preserves musical position across
callback partitions without blocking for a pulse.

The sync states are `acquiring`, `locked`, `holdover`, `lost`, and `stopped`.
After approximately 125 ms without a pulse the follower enters holdover and
continues at its filtered rate. At 500 ms it enters lost, freezes position,
cleans active notes, and stops playback. The next valid pulse snaps to its
deterministic clock position and resumes if external transport is running.
Start cleans notes and resets to zero; Continue retains the current or
SPP-selected position; Stop freezes and cleans notes.

External clock/transport forwarding is explicit. The originating logical port
is skipped to prevent immediate feedback; validated routes also reject direct
same-port clock loops. Backend timestamp latency compensation remains future.

## Tracks and routes

Set a track's instrument to `midi.external.v1`, then set its logical output
port and zero-based channel. Note on/off, CC, program change, and 14-bit pitch
bend use the ordinary compiled timeline, including probability and ratchets.
Routing rules explicitly name input/output logical ports, optional input/output
channels, and enabled message classes. There is no implicit broadcast. Direct
same-port/channel feedback configurations are rejected.

Native launch examples:

```sh
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --midi "Keyboard" --midi-out "Synth" sample.wav
./build/dev/neurogenic --midi 0 --midi 1 --midi-out 0 sample.wav
```

Multiple `--midi` and `--midi-out` options bind logical ports in command-line
order. Projects persist logical selectors, routes, track assignments, clock
source/output/forwarding settings, and latency-offset metadata. A non-real-time
monitor checks once per second. A disconnected port reconnects only when the
exact configured display name reappears; it never selects a different device.
Output reconnect requests panic cleanup.

Third-party control-surface setup remains separate from projects. Create a
schema-2 `.ngc` profile, add `midi-device|EXACT PORT NAME`, then add
`midi|PORT|CHANNEL|NUMBER|MODE|CONTROL|feedback` records. Supported mode names
are `note`, `absolute`, `relative-twos`, and `relative-offset`. Launch with
`--midi-controller-profile profile.ngc`; configure a matching `--midi-out` when
feedback is enabled. Profile authoring exposes begin-learn, cancel, status and
atomic save through the control-side C++ API; no graphical setup page exists.

## Hardware limits

Automated tests use simulated packets and deterministic timestamps. They verify
scheduling, routing, phase following, SPP, jitter, holdover/loss/recovery,
priority overflow, panic, exact-name reconnect policy, and persistence. They do
not measure physical DIN/USB MIDI jitter, backend latency, OS hotplug timing, or
device interoperability.
