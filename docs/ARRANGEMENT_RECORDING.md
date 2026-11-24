# Live arrangement recording

Live arrangement recording captures a pattern performance into the existing
song arrangement. It is independent of audio recording and resampling. Source
patterns remain reusable; a pass creates clip placements referencing stable
pattern IDs plus discrete parameter automation events.

## Lifecycle and timing

`arrangement.record-arm.v1` arms capture. `arrangement.record-start.v1` selects
`replace` or `overdub`, an optional count-in in bars, and automation arm. Capture
uses the authoritative musical tick after count-in, never wall-clock time.

Manual immediate and quantized launches are recorded when the prepared timeline
actually becomes active in the audio callback. The callback writes only a frame
timestamp to a bounded 64-entry SPSC acknowledgement queue. The control thread
converts it to a musical tick and associates it with the pending pattern or
chain. Chain/follow transitions use the playback chain contract and seed.

`arrangement.record-stop.v1` closes the last clip. Transport Stop also finalizes
an active pass. Restart and seek are rejected during capture. The pass remains
pending until `arrangement.record-commit.v1` or
`arrangement.record-discard.v1`.

## Clips, automation and history

Repeated activation of the same pattern is coalesced. A different activation
closes the prior clip; Stop supplies the final boundary. Replace removes clips
and automation overlapping the pass. Overdub preserves existing material and
uses normal same-track overlap validation. Ambiguous material is rejected.

Automation captures eligible numeric synth-instance, native-FX, track/group/
master mixer, aux and external-FX changes with stable instance destinations.
Thinning is expressed in musical ticks: a nearby change to the same destination
replaces the prior captured value. Modulation samples are not recorded. Base
values remain separate; playback uses automation-or-base plus modulation and
then destination clamping.

Macro gestures are recorded as `macro.<source-instance>.value` semantic
automation. Downstream mapped parameters are deliberately not expanded into
separate automation lanes, preserving the editable macro performance. Playback
delivers those events through the same ordered callback queue used by manual
macro changes and prepared scene morphs. The most recently processed semantic
macro event interrupts an active morph for that macro. Automation remains
discrete in this version; interpolated segments are a documented extension
point rather than synthesized control-rate events.

Commit is one validated `TimelineService` transaction, so a pass is one
undo/redo operation. Revision or publication failure leaves history and the
active timeline unchanged.

## Capacity, state and persistence

The default control-side pass capacity is 4096 combined launch and automation
events. The activation queue holds 64. Overflow marks the pass `overflow`; Stop
and Commit fail explicitly, so truncated capture is never reported successful.
Application snapshots expose API version 1, phase, mode, arms, boundaries,
counts, overflow, pending events, commit state and validation error.

Timeline metadata version 7 and project format 5 persist committed clips and
absolute song automation. Capture queues and pending passes are transient.
Task 045 data migrates with an empty automation lane.

Automation is initially discrete; curves are deferred. Return-to-arrangement is
represented by resulting song material, not a special persisted event. An
implicit backwards loop discontinuity must be split into explicit passes.
Modulator parameters are eligible only when they expose a callback-safe DSP
destination.

The shared Slint recording surface now arms, starts, stops, commits and discards
arrangement and performance passes through the existing semantic actions. It
shows actual captured-event and overflow counts and does not label these event
passes as audio recording.

Committed reusable-pattern and clip objects remain ordinary timeline material
after Task 089 and are editable through the same stable pattern/track and clip
identities. Capture timing semantics are unchanged. Capture does not implicitly
redirect subsequent step edits: the user-selected reusable pattern remains the
explicit editing target.
