# Prioritized backlog after Task 080

No P0 defect was confirmed. IDs are stable references for follow-up work.

## P1 — standalone workflow blockers

### AUD-001 — Unified recording workflow is inaccessible in Slint

**Task 081 status: resolved for the initial workflow surface.** Record requires
explicit mode/source/destination configuration and composes all five existing
recording families. Detailed live-loop overdub/replace UX remains limited.

- **Impact:** users cannot record external audio, resample, arrange-record,
  performance-capture or live-loop without another client.
- **Evidence:** `SlintFrontendAdapter::transport_record()` always rejects;
  backend action families exist in `control.cpp`.
- **Subsystem:** Slint/application workflow adapter.
- **Resolution:** add a presentation-neutral recording mode/state coordinator
  over existing services and a compact Slint recording surface.
- **Dependencies:** none in DSP.
- **Acceptance:** select source/destination/mode, arm/start/stop, observe actual
  boundary/overflow, publish result, and recover failure in deterministic UI-to-
  audio tests.

### AUD-002 — Sequencer cannot edit beyond 16 steps or core step properties

**Task 083 status: resolved for the essential standalone workflow.** The
production Sequencer has absolute 16-step pages, partial-page validation,
optional playhead following, descriptor-backed parameter locks, an advanced
step inspector, and transactional track/pattern lifecycle controls. Deterministic
adapter tests exercise a 17-step pattern, authoritative trigger-property and
lock round trips, safe shortening rejection, stable-ID duplication/deletion,
and stale-selection recovery. Generated Slint states render the populated
second page at both supported resolutions. Pattern event editing remains based
on the selected timeline track; detailed reusable-pattern-track authoring and
DAW-style Arrangement editing are intentionally deferred.

- **Impact:** common patterns are impossible touch-only.
- **Evidence:** `sequencer_page.slint` has no page controls; registry has
  `step.page-*`, probability, condition, ratchet and lock actions.
- **Subsystem:** Sequencer presentation/context.
- **Resolution:** descriptor-backed paging and advanced-step panel.
- **Dependencies:** existing semantic actions.
- **Acceptance:** edit and play a >16-step pattern; verify page, playhead,
  probability, ratchet, condition and lock round trip.

### AUD-003 — Offline processing cannot complete from Slint

**Task 081 status: resolved.** Slint exposes progress, cancellation, explicit
save/publication, and waits for audio-active acknowledgement.

- **Impact:** processing starts but result cannot be saved/published/used.
- **Evidence:** Sample page calls create/add/start only; backend exposes result,
  save and publish actions.
- **Subsystem:** Sample presentation model.
- **Resolution:** expose job progress/result/cancel/save/publish with
  acknowledgement and stale-source handling.
- **Dependencies:** existing processing service.
- **Acceptance:** process → acknowledge → publish new immutable version → play;
  failure leaves active sample unchanged.

### AUD-004 — Reusable preset creation and management is inaccessible

**Resolved by Task 085 for the existing version-1 preset scopes.** Stable-source
semantic capture covers synth, sample-instrument, track-owned FX-chain and
track-sound presets. Sound/FX/Browser provide touch-only save, validate,
transactional apply, rename, duplicate, explicit update and confirmed delete.
Generation-atomic writes and prepared activation preserve prior valid content;
tests cover repeated identity remapping, cross-project activation, missing
dependencies and rollback. Group/aux/master FX capture and performance macros
remain outside the format and are documented rather than fabricated.

- **Impact:** users cannot build a reusable sound library on the appliance.
- **Evidence:** Sound “Save Current” explicitly rejects; Browser only validates
  and applies despite `PresetLibrary` save/delete support.
- **Subsystem:** presets/semantic API/Browser/Sound/FX.
- **Resolution:** thin transactional capture-current actions plus enumerate,
  save, rename, duplicate and delete workflows.
- **Dependencies:** target compatibility descriptors.
- **Acceptance:** synth, sampler and FX preset save/apply twice with fresh IDs,
  confirmation, rollback and project independence.

### AUD-005 — MIDI routing and controller mapping are not appliance-configurable

- **Impact:** external instruments and third-party surfaces still require CLI,
  profile files or typed internal IDs.
- **Evidence:** Setup exposes device preferences, clock source and learn/save,
  not routes, semantic target selection or mapping removal.
- **Subsystem:** Setup/controller integration/MIDI.
- **Resolution:** descriptor-driven routing and mapping inspector using existing
  discovery, coordinator and `.ngc` format.
- **Dependencies:** AUD-006 for touch-only text where unavoidable.
- **Acceptance:** connect simulated device, create/edit/remove mapping and MIDI
  route, persist/reload, preserve unmatched musical MIDI.

### AUD-006 — No touchscreen text-entry/path-selection workflow

**Resolved for core standalone naming and path selection by Task 084.** The
shared shell owns a UTF-8 editing buffer and reusable touch keyboard; Browser
selection now supplies project/sample locations and export/profile filenames
without absolute-path entry. Track/pattern renames and new-project naming
commit only through semantic actions. Preset authoring names remain Task 085,
and the general controller mapping editor remains Task 086.

- **Impact:** project paths, samples, preset IDs and controller configuration
  require a keyboard or terminal.
- **Evidence:** multiple `LineEdit` fields; no on-screen keyboard component or
  native picker service.
- **Subsystem:** shared Slint shell/platform adapter.
- **Resolution:** reusable on-screen text entry plus validated directory/file
  selection, without a new content database.
- **Dependencies:** none.
- **Acceptance:** complete open/save-as/sample load/profile save on both target
  resolutions with touch/mouse only.

## P2 — correctness, coverage and responsiveness

### AUD-007 — Production CI does not compile Slint

**Resolved by Tasks 081–082.** CI configures pinned Slint, compiles generated UI
and the production executable, runs generated-component/rendered CTest at both
target resolutions, and retains PPM diagnostics on failure. Native AArch64
frontend build instructions are documented; physical rendering remains hardware
validation.

- **Impact:** generated UI/link regressions can reach main.
- **Evidence:** `flake.nix` forces `NEUROGENIC_ENABLE_SLINT=OFF`; CI has no Slint
  job.
- **Subsystem:** build/CI.
- **Resolution:** cached bounded Slint compile/test job and optional Nix output.
- **Acceptance:** x86_64 production frontend and renderer-model tests run in CI;
  AArch64 compile path documented.

### AUD-008 — Full UI models rebuild on high-frequency revisions

**Task 082 substantially resolved.** Structural fingerprints split full rebuilds
from telemetry, stable models use row publication, and inactive pages are skipped
for high-frequency updates. The workload measured a 13.9% median refresh
reduction and asserts no structural rebuild during playback. Complete
`ApplicationService::snapshot()` copying remains a measured bottleneck.

- **Impact:** possible UI stalls/allocation pressure during playback/metering.
- **Evidence:** full `SlintUiState` rebuild and replacement of all
  `VectorModel`s in `refresh()`/`publish()`.
- **Subsystem:** UI runtime.
- **Resolution:** measure first, then split shell/page/telemetry revisions and
  update active/changed models incrementally.
- **Acceptance:** benchmark at medium/heavy workload with bounded UI latency and
  materially reduced allocations; no stale state regression.

### AUD-009 — Controller coordinator executes external work under global lock

- **Impact:** blocked feedback can delay polling, learn, diagnostics or shutdown;
  reentrancy risk is unclear.
- **Evidence:** `poll()` and `dispatch_midi_feedback()` retain `mutex_` across
  transport/sender calls; raw pointers escape locked getters.
- **Subsystem:** controller integration.
- **Resolution:** define endpoint lifetime handles and two-phase queue/send
  ownership; add lock-order tests before changing behavior.
- **Acceptance:** blocking/failing/reentrant simulated sender cannot deadlock;
  concurrent shutdown has no stale access and feedback still converges.

### AUD-010 — Browser performs synchronous bounded filesystem work

**Resolved by Task 084.** A dedicated control-side worker performs bounded,
non-recursive enumeration and preset parsing. Generation IDs reject stale
results, shutdown joins the worker, and deterministic 128-row pages expose up
to the documented 8192-entry scan bound. Tests cover 270 entries and rapid
superseding navigation. Removable-media timing still requires hardware QA.

- **Impact:** slow/removable media can freeze the UI; truncation has no next
  page.
- **Evidence:** `navigate_browser()` enumerates and parses presets directly in
  callback, capped at 512.
- **Subsystem:** Browser/control worker.
- **Resolution:** cancellable asynchronous paged listing with stable selection.
- **Acceptance:** slow/disappearing directory does not block UI; cancellation,
  refresh and next-page behavior tested.

### AUD-011 — Slint rendered interaction/responsiveness is unverified

**Software portion resolved by Task 082; physical validation remains.** The
production generated component and callbacks render all eight pages plus modal
state at both resolutions using Slint's software renderer. Tests cover semantic
round trips and stable model identity. The pinned C++ API does not provide
reliable coordinate-level pointer/touch simulation with pixels, so compositor
input, physical touch, focus ergonomics and readability remain hardware QA.

- **Impact:** controls may clip, focus poorly, or fail touch use despite model
  tests.
- **Evidence:** adapter-only tests and missing-display smoke; no generated-
  component interaction or screenshots.
- **Subsystem:** UI testing.
- **Resolution:** Slint testing backend plus manual touchscreen checklist.
- **Acceptance:** callback/property tests at 1920x1200 and 1024x600; physical QA
  remains separately labeled.

### AUD-012 — Missing-asset recovery is backend-only

**Partially resolved by Task 084.** Browser now presents project missing-sample
paths and invokes the existing transactional `project.relink-sample.v1` action
for an explicitly selected replacement. Folder-assisted lookup only proposes
the same filename for the selected dependency; an atomic multi-dependency
prefix transaction and preset-package recovery remain follow-up work.

**Preset portion advanced by Task 085.** Preset enumeration reports missing
dependencies and Browser can select an explicit replacement or directory
prefix through the existing prepared-transaction relink actions. Validation is
performed before activation and cancellation preserves the stored preset.
Self-contained portable export is accessible through Browser and copies assets
using the existing collision-safe package service.
Persisting repaired paths back into the library as one atomic multi-dependency
mutation is still missing, so AUD-012 remains partial.

- **Impact:** relocated projects/presets cannot be repaired standalone.
- **Evidence:** relink actions/services exist; Browser exposes no missing
  dependency or prefix/file mapping UI.
- **Subsystem:** Browser/projects/presets.
- **Resolution:** dependency inspector and transactional relink workflow.
- **Acceptance:** reopen relocated project, map prefix, validate, save and
  restore audio without silent substitution.

### AUD-013 — Launch-slot-to-pad placement is implicit

- **Impact:** adding a lower stable slot ID reorders visible pad assignments;
  users cannot author spatial placement.
- **Evidence:** adapter sorts slots by stable ID and fills 0..15.
- **Subsystem:** performance launcher/presentation metadata.
- **Resolution:** decide whether explicit presentation-neutral slot position is
  required; preserve stable IDs and controller independence.
- **Acceptance:** documented deterministic migration and no unexpected pad
  movement after unrelated slot creation.

## P3 — refinement

### AUD-014 — Descriptor-driven FX type chooser

Replace four hard-coded add buttons with registry data; accept when newly
registered compatible native types appear without `.slint` edits.

### AUD-015 — Waveform fidelity and editing gestures

Preserve min/max polarity, add horizontal navigation and marker/boundary drag
with precise semantic validation; accept with deterministic coordinate tests.

### AUD-016 — Shared contextual action arguments

Extend contextual function assignments to carry validated bound arguments or
explicitly limit them to zero-argument actions; accept when required-argument
actions cannot be presented as invokable without their arguments.

### AUD-017 — Granular DSP investigation (deferred roadmap)

Investigate one shared future core for sample instrument, insert effect,
rolling/live capture and sequenced/modulated use. Reuse discovery, automation,
modulation, macros and bindings. No design or implementation is authorized by
Task 080.

## Recommended task sequence

1. **Task 081 — Complete standalone recording and sample-result workflows**
   (`AUD-001`, `AUD-003`) with end-to-end acknowledgement tests.
2. **Task 082 — Slint measurement, incremental publication and rendered tests**
   (`AUD-007`, `AUD-008`, `AUD-011`) before broader UI growth.
3. **Task 083 — Complete Sequencer editing and track lifecycle** (`AUD-002`).
4. **Task 084 — Touch-only text, paths and missing-asset recovery**
   (`AUD-006`, `AUD-010`, `AUD-012`).
5. **Task 085 — Preset authoring and library management** (`AUD-004`).
6. **Task 086 — MIDI routing and controller mapping UI** (`AUD-005`) plus the
   bounded coordinator lifecycle correction (`AUD-009`).
7. **Task 087 — Reference-appliance deployment and physical validation** after
   the above workflows are usable: supervised startup, N150/RPi5 audio/MIDI,
   touchscreen, controller and storage testing.

This order prioritizes playable standalone workflows and evidence. It does not
advance granular DSP before core appliance gaps are closed.
