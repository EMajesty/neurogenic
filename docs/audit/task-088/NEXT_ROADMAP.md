# Next development roadmap

## Current handoff — 2026-11-28

Application implementation baseline: HW-006B `c163f6ea27a3bfbc6a15e19645a20305749f8688`.
Controller specification baseline: [HW-006A](https://github.com/EMajesty/neurogenic-control/commit/cc0cbc42a7b775a9de6964e2545192876437483b).

| Workstream | Next task | Dependency |
|---|---|---|
| Application | 098G — quantization and swing | 098F complete in software |
| Controller firmware | HW-006C — adopt normative protocol 1.4 | HW-006B complete in software |
| Controller integration | HW-007 — production USB endpoint | HW-006C |
| Physical acceptance | HW-008 / OS-002 | Assembled hardware |
| Instrument integration | HW-009 | Physical acceptance and USB integration |

098G and HW-006C may proceed independently in their respective repositories.
Do not run competing implementation sessions on the same branch.

### Completed software milestones and evidence

- 098A–098D: metronome/count-in/tap tempo, solo/arm/panic, repeat/live erase,
  typed musical clipboard.
- 098E: authoritative live event recording, including repeat capture.
- 098F: continuous overdub with ordinary next-pass PCM playback, REC stop
  without stopping transport, range-scoped confirmed replacement, grouped
  Undo/Redo and cancellation rollback. See [validation](TASK_098F_VALIDATION.md).
- HW-006B: normative negotiated protocol 1.4 host support with exact 1.3
  fallback, independently negotiated optional pushes and encoder RGB.
  See [validation](../../HW_006B_VALIDATION.md) and
  [firmware contract](../../HW_006C_FIRMWARE_CONTRACT.md).

### Next application task: 098G

Inventory existing integer PPQN grids, microtiming, timeline transforms and
scheduler swing before implementing one descriptor-driven quantization model.
Cover input and post-recording quantization, straight/triplet grids, strength,
retained original timing, selection/pattern/track scope, swing grid/amount,
QUANT/SWING shared focus, atomic timeline transactions and unified history.
Require deterministic event-position and scheduler/PCM evidence, Undo/Redo,
save/reopen and fresh-engine playback. Avoid irreversible default quantization.

Then: 099 temporary performance checkpoints; 100 consolidated acceptance audit;
101+ creative DSP/granular expansion.

### Next controller task: HW-006C

Pin the exact HW-006B implementation commit above, not this documentation
revision. Consume its shared wire header and normative baseline/1.4 fixtures.
Implement portable negotiated session behavior, optional push suppression in
baseline mode, extended snapshots, generation/reconciliation and encoder RGB
feedback using the existing calibrated electrical output pipeline. Preserve
standalone diagnostics. See the firmware contract for exact normative fields.
HW-007 completes the production binary USB endpoint and integration.

### Open acceptance and limitations

- Simultaneous live erase and event recording remain mutually exclusive.
- Late control-thread publication can defer recorded material to a later pass.
- One stable event recording destination per session; no CC/automation capture.
- External MIDI timestamps remain callback-aligned.
- Explicit logarithmic/perceptual parameter metadata is absent; encoder
  feedback currently uses normalized descriptor ranges.
- Optional push assignment and exhaustive historical binary interoperability
  require evidence beyond the reported simulator cases.
- Physical USB, illumination, display/touch, audio/MIDI and latency remain
  NOT RUN. No physical calibration or hardware acceptance is inferred.
- HW-006B reports dev/sanitizer 49/49 and Slint/production 52/52; leak detection
  disabled. These are committed validation results, not an independent rerun.

The following sections retain historical planning context; their prospective
wording does not supersede the current handoff above.

## Post-097D bounded workstreams

1. **Task 097F — controller profile library:** coordinator-owned stable profile
   identities; create/name/rename/duplicate/select/remove; atomic persistence;
   known-valid recovery; supported context/modifier and feedback fields;
   learned-source preview; descriptor-driven typed arguments without the
   current four-field graphical limit. This must not enter project history or
   store project Performance definitions.
2. **Task 098 — missing musical capabilities:** split metronome/count-in/tap
   tempo, note/triplet repeat, typed clipboard, per-track solo/arm, unified
   internal+MIDI panic, live erase, contextual loop, recording/overdub and
   quantization/swing into bounded semantic+engine tasks with deterministic
   musical-output acceptance.
3. **Physical workstream:** G10/display/touch bring-up, V2 PCB and board-specific
   firmware, audio hardware, latency and sustained-load measurement. Every
   physical row remains **NOT RUN** until owner-supplied hardware evidence.

After Task 096A, Task 096B should consume effective SHIFT state, logical V2
control identities, context/modal generations, and interaction feedback to
implement primary/secondary page routing, root-page return, and contextual
label publication. It must not yet absorb navigation/edit/transport binding,
held-step locks, performance banks, or firmware feedback.

## Task 089 — unify reusable pattern editing and arrangement authoring

**Objective:** remove the split between the step-editable track pattern and the
reusable pattern selected/launched/placed by the arrangement, then expose safe
clip editing. Addresses AUD2-001 and AUD2-002. Scope: explicit edit target,
legacy conversion policy, reusable per-track pattern events, Slint pattern/clip
editor and transactional reference handling. Acceptance: create two distinct
patterns for one track, edit/launch/place/move/resize/duplicate/delete them,
save/reopen and render deterministic distinct PCM/MIDI. Include migration,
Undo/Redo, capture and controller-context tests. Excludes arrangement visual
polish, new sequencing algorithms and DSP.

## Task 090 — harden missing-device runtime and production packaging

Address AUD2-004 and AUD2-010. Make audio/MIDI enumeration degrade without
assertion, produce a fully sandboxed Slint Nix package, and smoke the installed
runtime in clean x86_64 and native AArch64 environments. Excludes hardware
performance claims.

## Task 091 — complete modulation and macro authoring

Address AUD2-003 and AUD2-017 with descriptor-driven sources, routes,
destinations, depths, macro assignments/scenes and history. Require audible or
engine-observable modulation plus cross-project persistence.

## Task 092 — complete live-loop performance workflow

Address AUD2-005: overdub, replace, clear, result save and failure recovery in
the production recording surface, retaining exact handoff and immutable sample
ownership.

## Task 093 — controller and hardware-first workflow completion

Address AUD2-006, AUD2-007 and AUD2-014. Finish manual mapping fields, explicit
pad assignment and page/subview contextual banks with bound arguments. Preserve
generic devices and no-push-encoder operation.

## Task 094 — dependency recovery, history and content bounds

Address AUD2-008, AUD2-009 and AUD2-013 with atomic multi-asset repair, preset
activation history and scalable cancellable listings.

## Task 095 — frontend verification/performance closure

Address AUD2-011 and AUD2-012 using measured domain snapshots and structured
layout assertions. No broad visual redesign.

## Task 096 — Reference Controller V2 interaction integration

Implement the locked design in `docs/REFERENCE_CONTROLLER_V2.md` using the
existing hardware-independent semantic/profile architecture. Begin with gesture
ownership/input epochs, V2 stable identities and shifted page routing, then the
navigation/edit/transport defaults, descriptor-driven banks and held-step lock
path. Project-owned Performance encoder/function banks, manual mapping closure
and firmware feedback conformance follow as bounded subphases. This milestone
addresses the remaining AUD2-006/AUD2-007 acceptance; exact scope and RC2 IDs
are in `REFERENCE_CONTROLLER_V2_GAPS.md`.

RC2 gesture ownership may proceed alongside Task 094 if needed for safety.
Temporary performance-state restoration depends on Task 094's unified history
decision. Task 095 retains its independent frontend verification objective;
this addition does not renumber or replace either task.

## Parallel hardware roadmap

After Task 090, execute `docs/HARDWARE_VALIDATION_REPORT.md` on N150 and Pi 5:
first device/preflight/storage, then audio duplex/latency/XRUN, MIDI clock and
hotplug, dedicated controller feedback/reconnect, both touch resolutions, soak,
power interruption and recovery. Failed rows become stable software/hardware
issues; unperformed rows remain NOT RUN.

## Milestones

- **First usable standalone instrument:** Tasks 089–095 software acceptance plus
  a passing minimum N150 validation subset (audio, MIDI, touch, save/recovery).
- **Feature-complete initial musical workflow:** reusable song arrangement,
  modulation/macro authoring, live-loop lifecycle and portable recovery.
- **Reference appliance validation:** all applicable report rows passed on N150;
  Pi 5 baseline characterized separately, followed by the V2 physical panel
  checklist once firmware/prototype hardware exists.
- **Creative expansion:** descriptor/preset refinements, richer sample editing,
  then a bounded shared granular core investigation (AUD2-019).

Tasks 095 and 096A–096G now provide independent-client publication, gesture
ownership, reference navigation, supported dedicated clusters, stable
Sequencer contextual editing, portable Performance banks, mapping authoring and
an implementation-ready protocol-1.3 firmware contract. The exact next bounded
milestone is **Task 097 — V2 interaction and presentation closure**, sequenced
as shared modal/focus ownership, graphical assignment/profile lifecycle,
remaining contextual/pad binding matrices, stronger controller-authored lock
and feedback evidence, then physical bring-up when PCB firmware exists. The
closure matrix and acceptance criteria are in `TASK_096G_VALIDATION.md`.

Task 097A completed shared modal/focus ownership. The exact next milestone is
**Task 097B — graphical assignment and profile-management closure**: finish
Performance positions/layers/pads and Setup typed arguments, profile lifecycle,
conflicts and feedback authoring using the same contract.

Task 097B completed all 48 reference Performance positions and the graphical
fields backed by current assignment/profile services. The exact next milestone
is **Task 097C — supported contextual-bank and pad-mode closure** across all
sixteen destinations. The missing coordinator-owned profile library is retained
as a focused RC2-006 follow-up rather than simulated in frontend state.

Task 097C completed the supported contextual definition and shared pad-mode
projection across sixteen destinations. The next bounded milestone is **Task
097D — acceptance closure**: controller-authored lock scheduling/persistence,
held-step cancellation/stale targets, grouped edits, stable note release,
Performance momentary ownership, profile replacement, protocol feedback
overflow convergence, physical-navigation simulation and generated-component
reachability. The coordinator-owned profile library remains a separate task.
## After Task 097F

The general profile-library milestone is closed in software. Task 098 should
now prioritize musical capability slices: (1) metronome/count-in/tap tempo,
(2) solo/arm/panic, (3) repeat/live erase, (4) typed clipboard, (5) loop and
recording/overdub controls, and (6) quantization/swing. Dedicated USB runtime
profile switching and physical bring-up remain separate controller/hardware
follow-ups.
