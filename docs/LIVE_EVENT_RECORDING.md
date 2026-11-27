# Live sequencer-event recording

NEUROGENIC has one musical event-recording authority composed from the existing
`RecordingWorkflowCoordinator`, `ApplicationService`, `AudioEngine`, and
`TimelineService`. It is explicitly distinct from audio capture, live-loop
capture, arrangement capture, and ordinary sequencer playback.

## Ownership and lifecycle

`event-recording.mode-set.v1` selects `audio` or `musical-events`. Arming
captures a reusable or legacy pattern identity, track identity, instrument
identity/instance, pattern extent, MIDI endpoint/channel where applicable, and
a monotonically increasing session generation. Selection changes cannot
redirect that session.

The observable phases are idle, armed, waiting, count-in, recording,
finalizing, completed, cancelled, and failed. An optional explicit start tick
implements quantized waiting. The existing count-in setting establishes a
future start tick and uses the existing metronome count-in; capture before the
boundary is rejected in the audio callback.

Normal REC stop and transport STOP finalize. Explicit cancel, PANIC, project
replacement, or an overflow discard the uncommitted buffer. Active audio
capture and musical-event capture cannot be armed simultaneously. One stable
event destination is supported per session.

## Authoritative timestamps and eligibility

Accepted live pad/synth commands, external MIDI note input, and scheduled note
repeat converge in `AudioEngine`. At that point the engine stamps the event
against the authoritative integer PPQN transport tick. GUI time and controller
arrival time are not musical timestamps. The rounding is the existing rational
frame-to-tick floor conversion; repeat uses its exact grid tick.

Eligible origins are:

- accepted sample-pad triggers;
- accepted synth note-on/off pairs;
- filtered external MIDI note-on/off (including note-on velocity zero);
- sample, synth, and MIDI repeat occurrences.

Sequencer/arrangement playback, metronome, previews, auditions, modal input,
and modifier-consumed pads never enter the capture queue. MIDI CC, pitch bend,
aftertouch, and automation recording remain unsupported.

## Notes, loops, and overdub

Synth and MIDI note ownership is bounded and correlated FIFO by origin,
instrument/endpoint/channel, and pitch. This permits overlapping pitches and a
defined repeated-same-pitch policy. A note held at normal stop is finalized at
the stop boundary. Duration is at least one tick and at most one pattern
extent. Sample triggers use the established sixteenth-note default event
duration (`ticks_per_quarter / 4`).
Repeat occurrences retain the existing 50% division gate.

Ticks wrap relative to the captured pattern extent. Existing unrelated events,
conditions, ratchets, automation, and parameter locks are preserved. Exact
same-tick/same-pitch collisions retain the existing event and omit the duplicate
overdub event. The initial implementation publishes once at session completion;
new events therefore become sequencer-audible after stop, not on the next loop
pass during the active take. Task 098F owns richer loop-pass publication and
replace/overdub presentation.

## Publication, history, and persistence

The callback only pushes value-owned capture records into a fixed SPSC queue.
The application thread drains them, validates the complete candidate, creates
fresh `EventId` values, and commits one ordinary timeline transaction through
prepared immutable publication. A successful non-empty take is one unified
history entry. Empty/cancelled/failed takes create no musical history entry.

Recorded events use the ordinary project format and typed clipboard. Active
sessions, pending notes, queues, and controller ownership are transient.

## Bounds and failure policy

- callback capture queue: 2,048 records;
- application capture buffer: 2,048 completed events;
- pending polyphonic notes: 128;
- one destination per recording session.

Any queue, event, or pending-note overflow marks the session failed, disables
further capture, and prevents partial publication. No callback allocation,
locking, filesystem access, discovery, logging, timeline mutation, or toolkit
work is performed.

## Current limitations

External MIDI timing is callback-aligned because the current input adapter does
not expose a calibrated device-to-audio timestamp conversion. Simultaneous
recording and live erase is not supported as one transaction and should be
treated as unavailable. Multi-track capture, per-pass loop publication,
destructive replace recording, automation/CC recording, and the complete
REC/OVERDUB and LOOP/RANGE workflow remain Task 098F scope. Quantization and
swing remain Task 098G scope.
