# Musical timeline

Task 096D held-step edits carry `PatternId`, `TrackId`, absolute step and an
unambiguous `EventId`. Empty and multi-note steps are rejected rather than
guessed. Compatible locks reuse destination validation and the existing
parameter-event scheduler; base parameter state is not rewritten. Selections
are bounded to 64 stable references and grouped same-pattern/track edits commit
as one timeline history transaction.

Task 096C adds stable controller editing without a second timeline model.
TRACK/PAGE/MUTE/ERASE chords carry stable track/pattern IDs and absolute steps;
`step.erase.v1` removes existing events transactionally and participates in
unified Undo/Redo. PAGE selection never mutates events.

Performance capture uses audio-engine activation acknowledgements and commits a
single prepared timeline transaction; requested or canceled launches are not
treated as musical activations.

Live-loop boundaries derive from authoritative musical time and become bounded
recorder frame offsets before capture.

Audio clips are polyphonic on a track. Equal-time activation is ordered by
track and stable clip ID, with stops before starts. Envelopes are compiled in
transport time rather than expanded into per-sample events.

Timeline format v8 adds stable audio clips. Compilation keeps two edges per
clip and computes loop iterations and seek phase arithmetically rather than
expanding source loops into repeated triggers.

The timeline also owns stable-ID reusable patterns and clip arrangements.
Absolute song-position parameter events captured during performance live in
`SongArrangement::automation`; they use the same stable event identity,
destination validation, scheduler ordering and DSP delivery as parameter locks.
See `ARRANGEMENT_RECORDING.md`.
Legacy `Track::pattern` data remains authoritative in legacy mode, so old files
and Lua scripts retain their behavior. See [ARRANGEMENT.md](ARRANGEMENT.md).

The editable model in `neurogenic/timeline/model.hpp` is shared musical data,
not a step-sequencer, tracker, piano-roll, or controller model. Positions and
durations use signed 64-bit ticks at 960 ticks per quarter note. Pattern length
is arbitrary and each track loops independently. Tempo is currently one integer
microseconds-per-quarter value; the transport also carries time-signature
metadata. A later tempo map can replace the single tempo without changing event
positions.

Events have stable 64-bit identifiers and support duration notes, explicit
note-on/note-off, and stable 32-bit parameter identifiers with values. Tracks
have stable 64-bit identifiers, mute state, and one initial looping pattern. A
track is an event container, not an instrument, sample slot, or MIDI channel.
Each track nevertheless has an explicit stable routing target. `sample.main`
retains sample-slot note mapping and `synth.subtractive.v1` targets the native
synth; routing does not change the shared track or event representation.
At equal frame positions playback orders by track ID, note-off before note-on
within a track, then event ID.

On the control thread, `compile_timeline` validates IDs, ranges and lengths,
expands duration notes into on/off pairs, and produces dynamically sized data
with audio-owned cursor storage. Plan allocation and reclamation remain on the control thread. The
audio thread accepts plan-slot indices through a bounded SPSC queue and performs
only integer scheduling, preallocated heap/buffer operations, voice updates,
and rendering. It does not allocate, free, lock, perform I/O, or execute Lua.
The configurable callback staging capacity defaults to 512 events. If full,
the earliest undispatched cursor remains pending and is delivered at frame zero
of a later callback; the overrun counter records the timing degradation. No
note-off is silently discarded. Plan-slot exhaustion rejects the newest plan.
Retired slots are destroyed only by the control thread or after audio shutdown.

Tick-to-frame conversion evaluates the absolute rational position:

```text
round(tick * sample_rate * microseconds_per_quarter
      / (960 * 1,000,000))
```

It never accumulates a rounded frames-per-step value, so loop and callback
boundaries do not introduce cumulative drift. Each track's pattern length is
its loop period. Note durations may cross a loop boundary; their generated
note-off remains attached to the originating track, stable note-on ID, and loop
occurrence. Overlapping same-pitch instances therefore remain independent.
Voice stealing makes a later note-off a no-op if its exact owner no longer
exists; there is no pitch-based fallback.

Queued pattern publication uses an absolute frame computed from this same
rational mapping. A boundary inside a variable-sized callback divides its
scheduling range: outgoing events are end-exclusive at the boundary and the
incoming plan begins there. At equal positions the established deterministic
event order still applies. See [ARRANGEMENT.md](ARRANGEMENT.md) for chain,
follow-action, voice-transition, and song-end semantics.

## Version 1 interchange

`NEUROGENIC_TIMELINE` is a narrow versioned text interchange format, not the final
project format. Parsing and validation happen before playback. Blank lines and
lines beginning with `#` are ignored. Records are:

```text
NEUROGENIC_TIMELINE 1
transport MICROSECONDS_PER_QUARTER NUMERATOR DENOMINATOR
track TRACK_ID LENGTH_TICKS MUTED_0_OR_1 "NAME"
note TRACK_ID EVENT_ID TICK PITCH VELOCITY DURATION_TICKS
note_on TRACK_ID EVENT_ID TICK PITCH VELOCITY
note_off TRACK_ID EVENT_ID TICK PITCH VELOCITY TARGET_NOTE_ON_ID
parameter TRACK_ID EVENT_ID TICK PARAMETER_ID VALUE
```

Tracks precede their events. IDs are positive and unique in their scope, and
event positions fall within the owning pattern. Sample assignments remain
separate application configuration. `examples/multitrack.ngt` is a complete
three-track example selecting slots through notes 36, 37, and 38.

An explicit `note_off` has its own stable event ID and must name the stable ID
of its corresponding `note_on`. Duration notes generate this relationship
automatically. For compatibility, a version 1 file may omit the final target
only when exactly one preceding, not-yet-targeted note-on has the same pitch;
the loader resolves it before compilation. Ambiguous or unknown note-offs are
rejected, and saved files always include the explicit target.

## Transport behavior

The audio engine owns one frame position and play state. Timeline and legacy
Lua patterns share that authority. Internal mode advances by audio frames;
external MIDI mode maps filtered 24-PPQN Q32 phase into the prepared timeline
frame domain and scales event offsets into the physical callback. Stop freezes position and
cleans active voices. Play continues from that position. Restart cleans voices
and starts at tick/frame zero. Each track loops independently. Replacing a
timeline keeps the current position, adopts new tempo metadata at the callback
boundary, and cleans old timeline voices. This deterministic segment-boundary
policy leaves room for a tempo map without moving clock ownership into either
scheduler. SPP seeks in sixteenth-note units; Start resets to zero while
Continue preserves the current or SPP-selected position.

The existing Lua `neurogenic.v1` API remains a 16-step compatibility surface.
Its next-bar replacement scheduler is retained, and `adapt_step_pattern`
converts that data to the general timeline for future callers. MIDI controller
actions remain in the mapping/control layer: demo note 36 can launch Lua, but
that mapping is not part of the musical event model.

Current limitations: one pattern plays per track, tempo is constant within a
plan, parameter automation is discrete rather than interpolated, and timeline
replacement takes effect at the next callback rather than a musical boundary.
Callback staging is finite and observable. Arrangement, recording, editing
interfaces, tempo maps, and asynchronous sample replacement are future work.

## Editing and generation service

`TimelineService` exposes hardware-independent snapshots and working-copy
transactions. A transaction contains the authoritative `Timeline` value, its
base revision, and a reproducible generation seed. Track/event creation,
deletion, naming, mute, loop length, notes, tempo, and time signature are C++
operations. The same service supplies deterministic RNG, Euclidean generation,
and timing/pitch/velocity transformations to Lua and future native clients.

Commit performs limit checks and `compile_timeline` validation before calling
the publication callback. It rejects stale base revisions, so concurrent
controllers must refresh and intentionally reapply their edit. Successful
commits replace the editable snapshot only after the complete playback plan is
accepted. Rapid commits are bounded by the existing playback-plan pool; a full
pool rejects the newest commit without changing editable state.

## Step editing adapter

`ApplicationService` maps step actions onto timeline transactions instead of
maintaining another pattern. Resolutions are 1/4, 1/8, 1/16 (default), 1/32
and their triplet forms, represented as exact 960-PPQ tick counts. Step `n`
starts at `n * resolution`. A non-divisible track length exposes the final
start tick inside the pattern and rejects the following step.

Each track has an application-level MIDI note target, defaulting to notes 36,
37, and so on by track order. Toggling removes the matching duration note at
that tick and pitch, or inserts one with a new stable ID, velocity and duration.
Unrelated events and tracks retain their identifiers. Commits become audible
at the next callback; validation, conflicts and publication failures leave the
prior timeline intact. Note-target project persistence and musical-boundary
publication remain future work.

## Undo, grouping, and step properties

`TimelineService` owns one bounded snapshot history shared by native actions
and Lua timeline commits. The default capacity is 64 committed states and is
configurable at construction. Memory scales with history capacity times the
editable timeline size. Successful new commits record the previous timeline
and generation seed and clear redo. Validation, revision-conflict, and
publication failures change neither current state nor history. Undo and redo
publish a complete candidate through the same next-callback path before the
control snapshot changes.

`edit.group-begin.v1`, `edit.group-commit.v1`, and
`edit.group-cancel.v1` collect several mutations into one undo entry. Playback
never sees the working copy before commit. `step.event-select.v1` selects a
stable duration-note event ID; an explicit ID disambiguates multiple notes in
one cell. Velocity, duration, pitch, and tick-relative timing actions preserve
that ID and leave other notes, parameter events, and explicit note-on/off pairs
untouched. Durations may exceed one cell. Timing remains within one resolution
of the nominal step and inside the pattern. Toggle remains exact-grid and
target-note-specific.

## Parameter locks and automation

Parameter events retain stable event IDs and now carry an optional stable
destination such as `synth.cutoff` or `effect.delay.mix`. Compilation resolves
that string to a bounded numeric target and native parameter index; unknown or
track-incompatible destinations reject the entire plan before publication.
Legacy numeric parameter events remain readable and schedulable for format
compatibility, but new edits use stable destinations.

`step.lock-set.v1`, `step.lock-remove.v1`, and `step.locks-clear.v1` edit
parameter events at the selected step's exact grid tick. Locks do not require a
note and multiple distinct destinations may share a step. Editing a lock keeps
its event ID. Transactions, conflicts, failed publication, Undo and Redo use
the same timeline history as notes.

At a shared tick the deterministic order is note-off, parameter changes,
note-on, then other events; IDs break ties within a type. Values persist in the
target until another event or live parameter command changes them. They are not
reset per buffer, reverted after a step, or interpolated. Loops reapply events
on each occurrence. Stop preserves current parameter values; restart returns to
tick zero, where tick-zero automation is applied on the next callback. Seeking
is not yet a public transport operation and there is no automation chase.

Application adapters use stable semantic action IDs
`timeline.generate.v1`, `timeline.mutate-track.v1`,
`timeline.regenerate.v1`, `timeline.variation.v1`, `timeline.commit.v1`,
`timeline.discard.v1`, and `timeline.query.v1`. These identify controller/UI
intent; parameters and selection context belong to the calling adapter and the
shared C++ service, not to a physical control layout or Lua script.
## Expressive trigger behavior

Each duration note stores a probability (`0.0..1.0`), condition, cycle values,
and ratchet count (`1..8`). Defaults are 100%, `always`, `1/1`, and one hit.

Control-side length edits are nondestructive by default. `track.pattern-length-set.v1`
and `pattern.length-set.v1` reject a shorter boundary when any existing event
would fall outside it. Clients must explicitly clear or move those events
before retrying. Pattern deletion likewise fails while arrangement clips,
chains, active playback or queued playback reference the pattern. These
operations publish one validated timeline revision and participate in existing
Undo/Redo history.

## Pattern edit authority

Step semantic actions resolve a stable selected pattern and selected track.
For reusable patterns, notes, advanced trigger fields, microtiming and parameter
locks live in `PatternDefinition::tracks`. A missing track entry means empty
content and is created only by an explicit mutation. Selected, active and queued
patterns remain separate state.

Pattern ID zero explicitly selects the legacy `Track::pattern` path. Loading is
nondestructive and does not rewrite legacy data. Timeline version 10 already
stores both forms, so no schema change or eager migration is required. Deletion
rejects reusable clip, launcher, chain, follow, audio-clip and scene references.

Arrangement authoring uses stable clip IDs. Pattern clips retain source pattern,
start, independent playback duration and loop state; audio clips retain track
and immutable sample-region state. Move, resize, duplicate, delete and loop
edits use existing timeline transactions and prepared publication.
Decisions are a pure hash of the serialized timeline seed, track ID, event ID,
and zero-based loop occurrence. They are therefore repeatable across callback
sizes and x86-64/AArch64, and unrelated tracks do not consume shared RNG state.
Restart begins again at loop zero; publishing a replacement uses that plan's
seed at the current transport position.

Conditions have these precise meanings:

- `first`: loop occurrence zero; `not-first`: every later occurrence.
- `every-n`: occurrences N, 2N, ... when counted musically from one.
- `n-of-m`: the first N occurrences in each repeating M-loop cycle.
- `previous-fired` / `previous-not-fired`: the decision for the preceding note
  encountered on the same track. At restart or seek the initial value is false.
- `fill` / `not-fill`: the Fill state sampled when the note-on is scheduled.

Ratchets divide the note duration evenly using integer tick boundaries. Each hit
has its own note ownership token and matching note-off. Parameter automation is
independent and is not skipped when a conditional note does not fire. This first
model persists lock values until another parameter event changes them; it does
not interpolate or restore values automatically.

Slice sequencing adds no second musical event type. Pad assignments resolve
notes 36–51 to immutable sample-version/frame ranges before voice creation, so
One-shot ignores the scheduled note-off, Gate enters its release at the note
duration, and Toggle responds to successive semantic note-ons. Probability,
conditions and ratchets decide/schedule those note-ons before the sample voice
applies its copied pad settings. Parameter automation remains independent.
Tempo-synchronized stretch resolves source beats/BPM against the authoritative
timeline tempo. Active voices rebase source time and smoothly follow later
internal or external-clock tempo changes. Stretched clip seeks reconstruct
source phase from the compiled output offset. Song-loop reactivation uses that
same deterministic mapping. Timeline event placement and note ownership are
unchanged.
duration notes preserve scheduler ordering, velocity, probability, conditions,
ratchets and ownership. Without an assignment, existing slot mapping applies.

Trigger properties currently apply to duration-note events produced by the
step/timeline editing services. Legacy explicit note-on/note-off pairs retain
unconditional edge semantics. Parameter events are independent; attaching a
lock to a conditional note as one atomic conditional group is not yet supported.
# Unified projects

The timeline remains authoritative when embedded in a version-1 project. Its
transport, generation seed, routing, stable IDs, automation, trigger conditions,
and ratchets are written unchanged to the committed generation. Project load
stops transport and publishes one validated timeline; it does not auto-play.

Recording beat/bar quantization derives its boundary from this same transport;
there is no recording-only clock. Fixed bars are converted to frames at arm
time. A stopped transport accepts immediate recording but rejects quantized
start, and project replacement cancels an active capture.

# Modulation interaction

Discrete automation and step locks remain authoritative timeline events. Their
latest value becomes the matrix's automation/base layer; enabled routes are
then summed and clamped without modifying or disabling the lock.
Tempo-synchronized sources follow the same authoritative transport tempo.

Tracks now persist `instrument_instance_id`. Synth tracks may share an instance
explicitly or select independent instances; compiled note and parameter events
carry that numeric identity sample-accurately. Version-1/2 timelines migrate to
shared synth instance 1. Changing a track's instrument/instance publishes a
complete plan and existing timeline voices are cleaned up at activation.
# Hybrid activation

Arrangement and session playback share this scheduler. Prepared swaps carry a
bounded affected-track set so cleanup does not interrupt unrelated tracks.
