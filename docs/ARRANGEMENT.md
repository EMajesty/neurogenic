# Patterns, clips, and arrangements

Session performances are committed as the same pattern and audio clip objects;
actual activation ticks determine clip boundaries. See `PERFORMANCE_CAPTURE.md`.

Patterns and audio clips may be referenced by stable performance launch slots.
Hybrid session ownership suppresses arrangement material per track; unaffected
lanes continue and explicit return resumes at the current song position.
Session launches use the same compiler and transport; arrangement/session/hybrid
intent is explicit. See `PERFORMANCE_LAUNCHING.md`.

Completed live-loop passes become ordinary looping audio clips through a
timeline transaction. No separate looper lane or mixer path exists.

Audio lanes allow overlap. Overlap sums normally unless a validated stable-ID
crossfade explicitly connects the clips.

Song arrangements also contain first-class audio clips referencing immutable
sample assets. They use a non-overlapping lane per track and the same transport,
publication and Undo/Redo model as pattern clips. See
[AUDIO_CLIPS.md](AUDIO_CLIPS.md).

NEUROGENIC stores patterns and arrangements inside the authoritative
revisioned `Timeline`. They are edited through `TimelineTransaction`, use the
same bounded history as step edits, and compile into the existing immutable
`PlaybackPlan`; there is no second sequencer or callback-side graph.

## Model and identity

- `PatternId` and `ClipId` are stable 64-bit identifiers, never collection
  indices or UI positions.
- A pattern is reusable multi-track musical content. Each `PatternTrack` has
  its own loop length, so polyrhythms remain possible. Pattern metadata carries
  a name, nominal length, and time signature.
- A clip references a pattern and supplies start, duration, loop enable,
  transpose, velocity scaling, and enable state. Clips do not duplicate their
  source event vectors.
- A song owns a start/end range and optional loop region. The initial engine
  loops that compiled range through the existing scheduler.

Collections are discoverable and have no fixed bank size. Validation budgets
are 256 patterns, 4,096 clips, and 4,096 pattern-track records, in addition to
the existing 65,536-event timeline budget.

## Playback and launch

`legacy` plays original per-track patterns exactly as older releases did.
`live_pattern` plays one selected reusable pattern. `song` expands enabled
clips into the selected song or loop range before publication.

Pattern launches can be immediate or queued to quarter-note, bar, two-bar, or
current-pattern-end boundaries. The control thread compiles the replacement
plan in advance and publishes its absolute transport-frame boundary. If the
boundary falls inside a callback, the callback schedules the old and new plans
on opposite sides of that frame. It never activates early, compiles, or
allocates. Fractional tempo conversion uses the same absolute rational timing
as timeline scheduling; external-clock launches use the follower's musical
frame domain. Diagnostics expose the requested frame, actual activation frame,
and bounded lateness (normally zero; a late control submission activates at the
next callback start).

Chains contain stable pattern IDs and per-entry repeat counts. They can loop or
run once, start at a selected entry, stop, reset, and be replaced transactionally
while playing. Follow actions run after the configured repetitions: continue,
next, previous, a specific pattern, seeded-random eligible pattern, or stop.
Probability is evaluated from the project generation seed and chain position,
so recompiling the same snapshot gives the same result. The bounded compiler
materializes one chain traversal; looped chains repeat that prepared traversal.

Transition policy is explicit: `preserve` leaves outgoing owned voices active,
`release` sends note-off/releases, and `stop` terminates them immediately.
Cleanup targets timeline-owned sample/synth voices and tracked external-MIDI
notes only; live pad notes are not killed.

## Compilation and precedence

Enabled clip events repeat up to clip duration and are then transformed.
Transpose applies to notes and velocity scaling is clamped to 0..1.
Probability, conditions, ratchets, parameter destinations, MIDI routing,
instrument identity, and deterministic seeds are retained. Placement IDs are
deterministic functions of clip ID, source event ID, and repetition.

At an equal tick the established scheduler order remains note-off,
parameter/MIDI control, note-on, then remaining notes, with stable track/event
tie-breakers. Parameter locks therefore precede notes. Base values remain
separate from automation and modulation.

The initial serial-lane arrangement rejects overlapping enabled clips that
address the same track. This avoids ambiguous note and parameter precedence.
Clips on disjoint tracks may overlap.

## Editing, persistence, and migration

Pattern, chain, follow-action, clip, and live-recording edits commit complete validated snapshots and participate in
undo/redo, revision conflict detection, publication failure handling, and safe
playback-plan reclamation. Timeline format version 7 stores definitions,
per-track content, clips, launch settings, chain entries, follow actions, voice
policy, song-end behavior, and recorded song automation. Version 6 loads with
an empty automation lane. Version 5 loads its legacy pattern-ID chain with
the prior defaults; versions 1–4 load in `legacy` mode without changing sound.

Live recording captures callback-acknowledged pattern activations,
deterministic chain/follow transitions, and eligible semantic parameter changes
as clips and song automation. See `ARRANGEMENT_RECORDING.md`.

Task 089 makes reusable `PatternDefinition` track content the edit authority
for any nonzero selected pattern. The production Slint Arrangement view places
pattern and audio clips and provides discrete move, resize, duplicate, delete,
loop-boundary and seek operations. Every operation addresses a stable clip ID
and publishes through the existing transaction/history path.

## Limitations

- Song end can loop, stop exactly at the prepared end frame, or stop and return
  the application to a configured live pattern. Return-to-live plan publication
  occurs on the control thread after the exact callback stop.
- Seeking uses the bounded `arrangement.seek.v1` transport command and shares
  the existing transport frame and MIDI SPP timing domain.
- Arrangement authoring is intentionally a compact structured editor; it does
  not yet provide arbitrary drag gestures, piano-roll editing or bulk transforms.
- Runtime chain edits take effect after a newly validated plan is published;
  they do not rewrite the already-active plan in place.
