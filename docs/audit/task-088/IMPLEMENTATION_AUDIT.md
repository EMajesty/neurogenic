# Task 088 implementation audit

Audit baseline: `d8172a7` (2026-11-24). This directory supplements rather than
replaces the Task 080 audit in `docs/audit/`. “Complete” below means a traced
production path and automated evidence; it never means physical validation.

## Executive summary

NEUROGENIC has a broad, real engine and application layer: deterministic sample
and synth playback, mixer/FX/modulation, recording, audio clips, live looping,
session launching, hybrid ownership, persistence and semantic discovery all
have production implementations. The eight Slint pages are generated production
components, not static mockups. A musician can build and save a multi-track
step-sequenced sample/synth project, process samples, route MIDI, use presets and
perform session launches in simulation.

It is not yet a coherent standalone song-production instrument. The principal
blocker is not DSP: step editing targets `Track::pattern`, while reusable
patterns, launches and arrangement clips target `SongArrangement::patterns`.
The Sequencer displays/selects the latter but edits the former. Arrangement is
read-only in Slint. Consequently a reusable multi-pattern song cannot be
authored end-to-end from the appliance UI.

No P0 data-loss or audio-callback defect was confirmed. A new host-runtime crash
risk was observed: the direct Slint CTest run aborted in libremidi's udev helper
when a monitor could not be created. This is an assertion in a dependency path,
not a graceful missing-device result; see `AUD2-004`.

## Evidence standard

The audit traced Slint callbacks in `src/ui/slint_frontend.cpp`, adapter commands
in `src/ui/slint_adapter.cpp`, descriptors/execution in
`src/application/control.cpp`, transactions in `src/timeline/edit.cpp`, engine
paths, persistence, and actual test assertions. The classification dimensions
are recorded in [CAPABILITY_MATRIX.md](CAPABILITY_MATRIX.md).

## Reconciliation of AUD-001–AUD-017

| ID | Task 088 status | Evidence and remaining acceptance |
|---|---|---|
| AUD-001 | Partial | `RecordingWorkflowCoordinator`, recording modal and numerical live-loop/internal-resample tests exist. Detailed live-loop replace/overdub/result management is not a complete Slint workflow; external physical capture is NOT RUN. |
| AUD-002 | Reopened / partial | Long pages and advanced `Track::pattern` editing work, but reusable `PatternDefinition::tracks` are not the edit target. Slint arrangement remains selection-only (`sequencer_page.slint`, `select_arrangement_clip`). |
| AUD-003 | Complete in software | Processing jobs verify PCM and acknowledged publication in `tests/sample_processing_tests.cpp`; Sample presents lifecycle. Physical audition is NOT RUN. |
| AUD-004 | Substantially complete | Synth/sample/track/FX-chain preset authoring and atomic library mutations are tested. Group/aux/master capture, activation Undo/Redo and persistent multi-dependency repair remain open. |
| AUD-005 | Partial | Setup exposes routes, removal, learn and profile save. Manual mapping creation/target selection/scaling/inversion is absent from `setup_page.slint`. |
| AUD-006 | Complete in software | Shared UTF-8 keyboard and picker remove absolute-path typing for supported workflows. Physical touch is NOT RUN. |
| AUD-007 | Partial | Slint compiles in CI and generated callbacks run. Direct host Slint tests can abort in udev; coordinate input and visual assertions remain absent. |
| AUD-008 | Partial | Task 082 limits model replacement, but `SlintFrontendAdapter::refresh()` still begins from full `ApplicationService::snapshot()` copies and encoder turns rediscover descriptors. |
| AUD-009 | Complete in software | Coordinator copies `shared_ptr` endpoint sets before polling/feedback and shutdown swaps ownership before external work. Deterministic reentrancy tests exist; physical blocked transports remain NOT RUN. |
| AUD-010 | Partial | Browser is asynchronous, generation-safe and paged, but scans/sorts at most 8192 entries and `shutdown()` joins a worker that may be inside a blocking filesystem call. |
| AUD-011 | Partial | Generated callbacks and software rendering cover eight pages at both sizes. Tests assert dimensions/artifact size, not clipping, touch target reachability or screenshot diffs. |
| AUD-012 | Partial | Individual project relink and preset/package relink exist. Project prefix repair and persistent atomic multi-asset preset repair are incomplete. |
| AUD-013 | Open | Performance pads still map the first 16 launch slots after sorting, not an explicit persisted pad assignment (`slint_adapter.cpp` pad loop). |
| AUD-014 | Open | FX add choices remain fixed in `fx_page.slint`; discovery is not the source of the visible type list. |
| AUD-015 | Open | Waveform overview exists, but polarity/detail and physical drag/navigation remain insufficiently verified. |
| AUD-016 | Partial | Controller profile actions preserve and validate bound arguments (`ControlBinding::arguments`). Shared Slint `SlintContextItem` discards them and `invoke_function()` sends only an action ID; current built-in contexts happen to use zero-argument actions. |
| AUD-017 | Intentionally deferred | A shared granular core is feasible but unimplemented; see future direction below. |

## Standalone readiness

The first usable release can reasonably include step sequencing, sampling,
synthesis, mixing, native FX, basic modulation, session performance, recording,
presets and project persistence. It should not claim complete song authoring
until reusable pattern editing and arrangement mutations share one coherent
model. Physical appliance readiness is separately blocked by every row in
`docs/HARDWARE_VALIDATION_REPORT.md` remaining **NOT RUN**.

## Granular feasibility (AUD-017)

Existing immutable `SampleVersion` ownership, bounded voices, prepared
publication, modulation descriptors and rolling recording buffers are suitable
building blocks for one reusable granular kernel. It would need an immutable
source handle, bounded preallocated grain pool, deterministic grain scheduler,
portable interpolation, explicit live-buffer generation ownership and bounded
modulation inputs. Instrument, insert and rolling-capture consumers should wrap
that core rather than fork algorithms. This is creative expansion, not a first
release blocker, and no DSP was implemented in this audit.
