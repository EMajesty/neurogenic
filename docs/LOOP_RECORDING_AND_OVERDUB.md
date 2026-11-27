# Loop recording and overdub

Task 098F extends the Task 098E musical-event recorder without adding another
clock, recorder, timeline, playback path, or history stack. The
`RecordingWorkflowCoordinator` configures the operation; `ApplicationService`
owns the captured stable destination and session; `AudioEngine` contributes
bounded accepted-live-event records; `TimelineService` validates and publishes
ordinary immutable timelines.

## State and configuration

Transport playback and event-recording activation are independent. REC may
start a stopped transport, may begin during playback, and may end while playback
continues. Transport STOP finalizes the take and stops playback. Cancellation
rolls an active take back to its captured baseline. PANIC finalizes valid event
recording before performing its existing voice/MIDI cleanup.

The presentation-neutral configuration contains mode (`audio`,
`musical-events`, or `live-loop`), source, stable pattern and track destination,
range, start quantization/count-in, monitoring, and capture policy (`overdub` or
confirmed `replace`). Musical-event ownership captures project generation,
PatternId, TrackId, instrument instance, MIDI route, range, and session
generation before waiting or count-in. Later selection changes cannot redirect
it.

`live-input` is the compatible combined performer-input source retained for
pad, synth, repeat, and routed MIDI capture. `external-midi` narrows capture to
the captured MIDI endpoint and channel. Audio and live-loop sources retain their
existing validation and lifecycle.

## LOOP, RANGE, overdub, and replacement

LOOP toggles the existing arrangement loop; it does not create a live-loop
audio session. SHIFT+LOOP enters the shared `recording.range` interaction.
Ranges are half-open integer-tick intervals `[start,end)` inside a stable
reusable pattern. They must be nonempty and in bounds. Captured time wraps into
that range.

Overdub is non-destructive. Complete captured notes are drained in bounded
batches on the application/control thread, validated, assigned fresh EventIds,
committed through `TimelineService`, and queued as the ordinary prepared
timeline. A batch becomes visible after its original live occurrence, so it
cannot double-trigger that occurrence and is heard by the ordinary scheduler on
the next loop pass. Transport position is neither rewound nor restarted.

Replace is explicit and requires `confirmed=true`. Its scope is only the
captured pattern-track and recording range. Existing content is removed at the
first meaningful successfully prepared batch, not at arm or start; an empty or
failed take preserves the old content. Other tracks and events outside the
range are untouched. Independent automation is not rewritten.

Same-tick/same-pitch events use the existing deterministic collision policy:
the existing event wins. Other pitches at the same tick remain valid. Event
locks, conditions, and ratchets already on unrelated events are preserved.

## Passes, pending notes, and history

The public state exposes current pass, pending event count, cumulatively
published events, source, policy, range, and loop state. Only completed notes
are published. Bounded pending ownership therefore correlates note-off across a
wrap before creating an event; the existing duration representation retains the
performed duration. Pending notes are finalized by the established normal-stop
policy and discarded on explicit rollback.

All incremental batches share one session baseline. Normal completion creates
one unified application-history entry from that baseline to the final timeline.
Undo removes the whole multi-pass take; redo restores it. Copying and unrelated
application history remain independent. Cancellation after intermediate
publication republishes the baseline and creates no take history entry.

Simultaneous Task 098C live erase remains explicitly unavailable: its own open
edit group cannot yet be safely merged with the recording session transaction.
The application rejects the combination rather than risk resurrecting erased
events or splitting history.

## Capacity and real-time behavior

- audio-to-control capture queue: 2,048 records;
- application batch: 2,048 complete events, drained after each poll;
- unfinished note ownership: 128 notes;
- no indefinitely growing session event buffer;
- overflow ends capture with an explicit failed state; it is never silently
  truncated.

Audio callbacks allocate no memory, acquire no application lock, mutate no
timeline, access no files, perform no discovery, and call no toolkit code.
Timeline validation, identity generation, history, and prepared publication are
control-thread work. Publication never blocks the callback and a late batch is
heard at the next scheduler opportunity without changing transport position.

## Reference controls and presentation

- LOOP: arrangement loop toggle.
- SHIFT+LOOP: shared RANGE configuration.
- REC: start/stop the selected recording workflow; event REC stop keeps PLAY.
- SHIFT+REC: shared OVERDUB/replace configuration.
- REC MODE: mode selection.
- SHIFT+REC MODE: source configuration.
- STOP: finalize and stop transport.
- PANIC: remains available through the global safety exception.

Slint exposes mode, source, destination, range/loop, policy, waiting/count-in,
active/finalizing/failure state, current pass, and pending/published counts at
both supported resolutions. Text and physical interaction invoke the same
semantic actions. No protocol wire identifier was added.

## Current limitations

Application polling is the safe publication service point; a batch that is not
polled before a desired pass becomes audible on a later pass. Simultaneous live
erase and event recording remains unavailable. Event automation recording and
simultaneous multi-destination musical capture are not implemented. Audio
capture and audio live-loop overdub retain their distinct content and history
semantics. Physical controller, MIDI hardware, display, and listening evidence
remain NOT RUN.
