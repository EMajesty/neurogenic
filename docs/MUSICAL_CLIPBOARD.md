# Typed musical clipboard

The musical clipboard is one transient, presentation-neutral authority owned by
`ApplicationService`. It captures bounded values rather than pointers into a
project, engine publication, controller session or frontend component. Copy and
clear do not dirty musical content or create history. Project replacement clears
the clipboard; project files never serialize it.

## Payloads and limits

The initial payload discriminator supports:

- one populated step or an explicitly targeted step;
- a sparse selection of up to 64 steps in one stable pattern-track context;
- complete legacy or reusable pattern-track content;
- a complete reusable pattern, pasted as a new independent pattern.

At most 1,024 events and 64 pattern tracks are captured. Overflow is rejected,
never truncated. Payloads retain source pattern/track metadata, source anchor,
musical extent, relative event positions and the source instrument identity.
Raw sample PCM, instrument/effect settings and independent arrangement
automation are not clipboard payloads.

Every `MusicalEvent` field is copied: event type, relative tick (including
microtiming), pitch, velocity, duration, parameter identifier/destination and
value, target-event relationship, probability, condition operands and ratchet
count. Event-owned parameter locks therefore move with the selected event
window. A capture that omits an event referenced by `target_event_id` is rejected.

## Paste and compatibility

Paste captures explicit destination `PatternId`, `TrackId` and anchor tick in
the semantic command. A later selection change cannot redirect that command.
All pasted events receive fresh stable `EventId` values and internal event links
are remapped.

`merge` is the safe default. Existing unrelated events are retained. A matching
event type at the same tick conflicts when it is the same note pitch or the same
parameter destination; the complete candidate is rejected atomically. `replace`
removes content only in the destination extent and requires `confirmed=true`.
Copied empty positions are meaningful to replacement extent but never delete
anything during merge.

Initial cross-track compatibility is deliberately strict: destination and
source track instrument identifiers must match. Sample, synth and external-MIDI
content is not reinterpreted, and parameter destinations are never remapped.
Pattern bounds, event capacity, stable destination existence, target references
and timeline validation are checked before publication.

Complete reusable-pattern paste creates a new `PatternId`, preserves its length
and track content, assigns fresh event identities and leaves arrangement clips
that reference the source unchanged. The copy is independently editable.

## Publication, history and clients

Mutation uses a normal `TimelineTransaction`, full validation, prepared
publication and the unified application history. One successful paste is one
Undo/Redo entry. Failed paste, copy and clear create no musical history entry;
Undo does not modify clipboard contents.

The semantic registry exposes selection, pattern-track and reusable-pattern
copy; merge/replace paste; independent-pattern paste; and clear. Slint publishes
payload type/context, event count and destination compatibility. Alternative
clients use the same actions.

Reference Controller V2 uses COPY tap for the selected compatible content,
COPY+pad for an explicit sequencer step, and SHIFT+COPY for merge paste at the
captured selected destination. Chords consume the ordinary pad action. Destructive
replacement remains an explicit confirmed application operation.

## Exclusions and next milestone

Independent automation lanes are not included because they are arrangement
owned rather than event owned. Task 098C established that there is no
authoritative live sequencer-event recording window. Task 098E must add bounded
sample-trigger, synth-note and external-MIDI event capture through recording arm,
count-in/quantized start, transport timestamps, note-on/off duration, overdub,
loop wrapping, prepared publication and unified history. Task 098D does not
approximate that missing authority.
