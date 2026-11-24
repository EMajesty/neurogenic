# MIDI routing, clock, and external sequencing

## Linux backend availability

Production Linux builds use the ALSA sequencer backend. Absence or
inaccessibility of `/dev/snd/seq`, an empty endpoint set, or an exact-name
mismatch is an observable unavailable state and never triggers fallback to
another physical endpoint. The application can continue without MIDI.

Libremidi 5.4.2's optional udev metadata integration is disabled because its
monitor constructor asserts under restricted netlink environments. This does
not disable ALSA sequencer enumeration, connections, or sequencer announce-port
hotplug; it does mean udev-derived manufacturer/serial/path metadata is not
available. Stable matching therefore remains limited to the backend identities
documented for ALSA. Permission and physical reconnect behavior still require
target-host validation.

Task 086 exposes the existing bounded `MidiRoute` graph in Setup: logical
ports, omni/channel remapping, note/CC/clock/transport filters, enable and
removal. Controller profiles remain separate. Learn accepts eligible note/CC
traffic, captures exact port/channel/message identity, detects conflicts and
requires confirmation before changing a mapping. Configured missing devices
are never silently replaced. ALSA identity for multiple identical hotplugged
devices still requires physical validation.

NEUROGENIC's MIDI 1.0 subsystem keeps device ownership outside the musical
engine. `MidiInput` and `MidiOutput` are libremidi adapters. Logical ports
(`1..8`) remain stable in timelines and projects; a device selector binds a
logical port to a host port. Missing devices remain missing and are never
silently replaced by another port.

The Slint Setup page displays enumerated input/output endpoints and the
authoritative internal/external clock source. Clock changes use
`midi.clock-source-set.v1`; endpoint preferences are staged for restart.
Musical routing and controller mappings remain separate, and learn only
consumes an eligible event while an explicit controller learn session is active.

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
captured backend port identity reappears; it never selects a different device.
Output reconnect requests panic cleanup.

Third-party control-surface setup remains separate from projects. Create a
schema-2 `.ngc` profile, add `midi-device|EXACT PORT NAME`, then add
`midi|PORT|CHANNEL|NUMBER|MODE|CONTROL|feedback` records. Canonical mode names
are `note`, `cc-absolute`, `cc-relative-2c`, and `cc-relative-offset`;
`absolute`, `relative-twos`, and `relative-offset` remain accepted aliases.
Launch with `--midi-controller-profile profile.ngc`. Feedback mappings connect
the same exact device on the corresponding logical output port when that
device exposes MIDI output. Profile authoring exposes begin-learn, cancel, status and
atomic save through the control-side C++ API; no graphical setup page exists.

A logical port can intentionally carry both musical input and controller
messages only when both configurations name the same exact device. Explicitly
mapped messages are consumed by the controller session; unmatched notes, CCs
and other messages continue through ordinary musical routing. Conflicting
device selectors for one logical port are rejected before device startup.

## Hardware limits

Automated tests use simulated packets and deterministic timestamps. They verify
scheduling, routing, phase following, SPP, jitter, holdover/loss/recovery,
priority overflow, panic, exact-name reconnect policy, and persistence. They do
not measure physical DIN/USB MIDI jitter, backend latency, OS hotplug timing, or
device interoperability.
### Manual control-surface mapping

Setup separates MIDI source capture from the semantic target. MIDI learn or an
existing binding supplies endpoint/channel/message/mode; a descriptor-backed
chooser supplies an application action or parameter. Controller profiles
persist pickup, inversion, fine scaling, acceleration, feedback and validated
typed action arguments. Unknown targets and actions missing required arguments
are rejected transactionally. Ordinary unmatched musical MIDI remains routed
under the musical-routing policy.
