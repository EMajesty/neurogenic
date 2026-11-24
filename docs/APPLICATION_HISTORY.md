# Application history and content transactions

## Authority

`ApplicationService` owns the single user-facing chronological Undo/Redo
sequence. `TimelineService` retains its bounded timeline snapshots as the
timeline transaction implementation, but Slint, SDL and controller clients use
`edit.undo.v1` / `edit.redo.v1` and the application sequence. There is no
frontend, preset, modulation or macro history stack.

An application history entry contains only the affected bounded configuration:
an optional `Timeline` before/after pair and/or mixer, modulation, macro and
synth-instance configuration. Preset entries also contain `SliceProject`
metadata and paths. They do not retain PCM: restoration prepares immutable
samples again from their authoritative assets. The application sequence is
bounded to 32 entries; the oldest entry is evicted and any new committed edit
clears Redo. Opening, creating or closing a project clears both directions.

## Commit and acknowledgement

Timeline-only edits enter history after validation and successful prepared
timeline publication. Modulation, macro and acknowledged preset restorations
use `PresetActivationRequest` as the existing atomic audio handoff. Their
history state moves between Undo and Redo only after the audio callback reports
the matching activation sequence. A failed preparation, rejected publication,
cancelled activation or invalid restoration does not consume the entry or add a
successful edit. While a restore is pending, both directions are unavailable;
the copied application state exposes `history_pending`, depth and operation
labels.

Implemented reversible authoring includes timeline/step/pattern/arrangement
transactions, modulation source and route authoring, macro assignments and
scene authoring, synth/track-sound presets, and track FX-chain preset
activation. Sample-instrument preset activation is reversible when its before
and after configurations use the same stable sample-slot set. An activation
that changes that set remains valid but is deliberately not advertised as
undoable because the current engine activation contract has no atomic slot
removal operation.

High-rate macro value performance gestures and scene recall are not recorded as
authoring edits. Existing grouped timeline edits provide coalescing for step and
arrangement gestures. A future presentation-neutral gesture contract should
capture a stable target at begin, replace the pending after-value while moving,
and commit one entry at end; cancellation restores the before-value. Task 094
does not implement Reference Controller V2 gesture arbitration.

## Dependency recovery

`relink_project_assets` builds and validates a complete candidate project. Exact
replacement and old-prefix/new-prefix mappings use stable expected paths;
filename-only substitution is not performed. Every WAV is decoded and checked
at the fixed engine rate before the candidate is committed. The semantic
`project.relink-prefix.v1` additionally validates slice/region bounds, prepares
all sample versions and pad mappings, and publishes one bounded activation.
Only its matching acknowledgement replaces project references and marks the
project dirty. A failed proposal leaves every project reference unchanged.

`PresetLibrary::repair_assets` applies the same full validation to one preset
copy and persists it using the library's generation-atomic save. The
`preset.repair-asset.v1` and `preset.repair-prefix.v1` semantic actions never
rewrite unrelated entries. The previous valid generation remains selected if
validation or persistence fails.

## Browser bounds and consistency

Filesystem browsing no longer has an 8192-entry invisibility ceiling. The
worker counts matching entries while retaining a fixed 1024-entry window (eight
128-row pages), then sorts that window and publishes only the requested page.
Memory and queued requests remain bounded, request generations reject stale
results, and a newly requested location supersedes older publication.

Ordering is deterministic inside an unchanged enumeration window. It is not a
claim of globally sorted cursor pagination: provider order and concurrent
directory mutation can move entries between windows, so refresh starts a new
listing generation. The single `jthread` owns no application references and is
joined safely. Stop checks bound cancellation between directory iterator
increments and preset records; standard C++ cannot interrupt an arbitrary
uninterruptible kernel or network-filesystem operation, so a strict shutdown
latency bound is not claimed for such providers.

## Temporary performance-state restoration

Reference Controller V2's future temporary restore is a named, single bounded
checkpoint owned by `ApplicationService`, not another chronological stack. The
initial scope should contain a selected reusable pattern plus instrument/FX
base configuration, modulation, macro values and project-owned performance
assignments; it should exclude PCM, recording buffers, controller profiles and
project persistence metadata. Capture does not dirty the project. Restore is a
validated prepared transaction and, once acknowledged, may create one normal
history entry so it can itself be undone. Saving does not silently replace the
checkpoint, project replacement invalidates it, and failed restoration keeps
both checkpoint and active state. Implementation and V2 gestures remain Task
096-or-later work.
