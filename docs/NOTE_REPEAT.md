# Musical note repeat

Task 098C adds a bounded live note-repeat authority to `AudioEngine`. It shares
the transport frame, project tempo and 960-PPQN integer musical grid used by
prepared timeline playback; it is not a timer, second sequencer, or persistent
pattern representation.

## Timing and ownership

Repeat starts on the next grid boundary (including the current boundary), only
while the internal transport is running. Supported divisions are 1/4, 1/8,
1/16, 1/32, 1/8T, 1/16T and 1/32T. Triplets are exact 320/160/80-tick
intervals. Frame positions are derived from absolute musical ticks on every
event, so fractional samples per division do not accumulate clock drift.

Up to eight simultaneous targets are held in fixed callback-owned storage.
Each captures owner and connection generation, target kind, note, velocity,
instrument or MIDI endpoint/channel, and division. Overflow and invalid targets
are rejected. The default synth/MIDI gate is half of the division. Each synth
gate uses a distinct note identity; MIDI emits explicit note-off packets.
Samples use the ordinary pad mapping, choke, envelope and voice-stealing path.

Repeat is cancelled by pad release, REPEAT release, STOP, PANIC, modal/context
replacement, reconnect, and project/session cancellation. Releases address the
captured owner, never the current selection. External-clock repeat is currently
unavailable because this task does not invent a free-running fallback phase.
Stopped transport repeat is likewise unavailable.

## Controller and semantics

- Hold REPEAT + pad: begin a compatible captured repeat.
- Release either owner: terminate the repeat safely.
- REPEAT + DATA: cycle the divisions within the current straight/triplet family.
- SHIFT + REPEAT: toggle the triplet family.

The semantic actions are `performance.note-repeat-begin.v1`, `-end.v1`,
`-cancel.v1`, `-division-next.v1`, and `-triplet-toggle.v1`. Alternative clients
can use them without the reference controller. The 67-control protocol inventory
is unchanged.

## Recording limitation

The production recording coordinator currently captures audio and arrangement
operations, but has no authoritative live MIDI/sequencer-event recording
window. Repeated sound is therefore not claimed to create persistent sequencer
events in Task 098C. Adding a typed event-recording authority, quantized
destination capture and transactional insertion is a separate prerequisite;
audio generation alone is not treated as recorded-event evidence.

Live ownership is transient and is never serialized. Ordinary project
persistence applies to future explicitly recorded events.
