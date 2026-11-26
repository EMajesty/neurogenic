# Reference Controller V2 implementation gaps

Task 097D closes the highest-risk software acceptance gaps: scheduled and
persisted controller-authored locks, transactional held-step cancellation,
stable synth-note release, explicit-target audition, non-mutating Macro/Scene
selection, interrupted SHIFT double-tap behavior and exact protocol feedback
convergence. See `TASK_097D_VALIDATION.md` and `V2_ACCEPTANCE_LEDGER.md`.
RC2-001/003/004/005/007 remain **Partial** only for ledgered presentation,
missing-capability or physical rows; RC2-006/AUD2-006 remain Partial for the
profile-library boundary. AUD2-007 remains Partial until physical execution and
the remaining generated/physical-navigation rows are complete.

Task 097C substantially resolves RC2-004 for currently registered operations:
all sixteen destinations have explicit encoder/function/pad descriptors, both
layers are accounted for, V2 dispatch and Slint use the same destination ID,
and unavailable positions are inert. Stable Sound note release and legacy
profile compatibility are deterministic. Task 097D supplies lock playback and
persistence evidence plus narrow Macro/Scene selections and explicit audition;
missing musical algorithms and physical validation remain open.

Task 096D implements contextual step ownership in software and partially
resolves RC2-004: the Sequencer property/compatible-lock bank, stable bounded
multi-selection and grouped history path have production evidence. The full
page/pad-mode matrix, Performance customization, firmware and physical
validation remain open. See `TASK_096D_VALIDATION.md`.

Task 096B evidence: RC2-001 remains **partially implemented** at the shared
infrastructure layer by `InputGestureArbiter`, `ControllerSession` integration,
and `neurogenic_input_gesture_tests`. V2-specific policies, full profile and
physical validation remain open. RC2-002 is **implemented in software**: the
explicit V2 descriptor/profile, shared destinations, primary/SHIFT routing,
root revision, and reference shell zones have production-path tests. RC2-007's
foundational modal ownership and input generations are implemented; complete
modal bindings remain open. See `docs/INPUT_GESTURES.md` and
`TASK_096B_VALIDATION.md`.

This report reconciles the locked
[Reference Controller V2](../../REFERENCE_CONTROLLER_V2.md) design with the
production tree after Task 093 (`d60bc22`). It does not treat a semantic action,
label or simulated packet as physical validation.

Status vocabulary:

- **Implemented** — production path and deterministic software evidence exist.
- **Partial** — reusable infrastructure exists, but the V2 workflow is not
  complete.
- **Missing presentation access**, **missing semantic action**, **missing engine
  capability**, or **missing firmware/protocol support** identifies the first
  absent layer.
- **Requires physical validation** applies to every panel behavior even when
  software-complete.

## Evidence anchors

- Inventory/profile: `rp2040_controller_descriptor()` and
  `full_performance_profile()` in `src/application/controller.cpp`.
- Event ownership: `ControllerSession::input()` in
  `src/application/controller_session.cpp`.
- Context banks: `resolve_controller_context()` in
  `src/application/controller_protocol.cpp`.
- Transport/feedback: `ControllerHost` and `SimulatedController` in
  `src/application/controller_protocol.cpp`.
- Semantic authority: `ApplicationService`, action descriptors and parameter
  registry in `src/application/control.cpp`.
- Slint copy/invocation: `SlintFrontendAdapter` and `slint_frontend.cpp`.
- Project pad positions: `LaunchSlot::logical_position`, timeline schema 11.
- Existing tests: `tests/tests.cpp`, `tests/slint_adapter_tests.cpp`,
  `tests/slint_rendered_tests.cpp`, and `tests/performance_launching_tests.cpp`.

## Control-group reconciliation

| Area | Current evidence | Classification | First remaining layer |
|---|---|---|---|
| Physical inventory | Explicit `reference_controller_v2_descriptor()` has 67 unique logical identities: 58 buttons including pads and nine encoders | Implemented in software | Firmware/wire negotiation and all physical validation are NOT RUN |
| Static pages | Shared primary/SHIFT table drives `reference-page` controller dispatch and Slint labels; repeated destination increments root revision | Implemented in software | Physical ergonomics and complete nested root policies remain validation/integration work |
| DATA encoder | Relative input, fine SHIFT and acceleration exist; default is tempo | Partial | Add focus-aware descriptor target resolution and hold-and-turn chord targets |
| Navigation cluster | Focus previous/next, confirm/cancel, step-page left/right | Partial | Four-way focus geometry, repeat/acceleration, first/last, page up/down, root and safe Save |
| Editing cluster | SHIFT and SELECT mapped; most other controls explicitly unassigned | Partial | Gesture owner plus V2 MUTE/SOLO/ARM/TRACK/PAGE/ERASE/UNDO/COPY bindings and availability |
| Modifier state | Press/release and momentary/latch fields exist | Partial | Double-tap latch transitions, consume-on-chord suppression, persistent modes and reconnect epoch reset |
| Transport cluster | Play, Stop and recording toggle mapped to three logical positions; Fill/live-loop semantics exist | Partial | Map all V2 controls; metronome/count-in, tap, repeat, quant/swing, LOOP/restart require API review |
| Hold-and-turn | Encoder events and button events are independent | Missing presentation/gesture access | Capture held control and stable target, dispatch descriptor-backed adjustment, handle release/cancel |
| Performance pads | Explicit logical position, assign/reassign/clear, legacy migration and launch path | Implemented in software | Banked V2 assignment UI and physical feedback/ergonomics remain |
| Page pad modes | Step select/toggle, sample/pad modes and Performance launch exist | Partial | Complete V2 context table and explicit logical banks; Setup inactive policy |
| Held-step locks | Stable reusable edit target and lock actions exist | Partial | Session-level held-pad target and encoder-to-lock dispatch; multi-step lock editing |
| Context encoders | Eight shared fields, parameter registry, arguments, feedback and bank identity | Partial | Populate descriptor-driven V2 page/subview banks; secondary banks currently fall back |
| Context functions | Eight shared typed actions and feedback | Partial | Populate V2 defaults/secondary layers; leave unsupported entries unavailable |
| Performance P/F customization | Generic profile mappings and project pad assignment exist separately | Missing presentation access/data model | Add project-owned banked encoder/function assignments and user labels without merging controller profiles |
| Modal precedence | Confirm/cancel semantic operations and shell modals exist | Partial | Shared modal context stack, input epoch, transport exceptions and stale-release suppression |
| Press/release ownership | Protocol sends button state; session ignores most releases | Partial | Chord arbitration, momentary release dispatch, consumed-tap tracking and lost-release recovery |
| RGB feedback | Capability, queueing, overflow recovery and reconnect resync exist | Partial | V2 semantic palette/pulse patterns, non-color cues and RGB inventory; physical LEDs NOT RUN |
| Screen zones | Production Slint shell has top navigation, central content, right 2×4 encoder grid and bottom functions | Implemented in software | Final visual design, coordinate touch and physical display validation remain open |
| Manual mappings | MIDI learn, target selection, pickup/invert/fine/acceleration and profile persistence | Partial (AUD2-006) | Manual source creation, complex typed arguments, conflict replacement, feedback/context editing |
| Firmware | Protocol supports press/release, encoder deltas, RGB/context feedback and reconnection | Missing firmware/protocol support | Physical firmware, rollover/ghosting/debounce tests, connection epoch/lost-release contract |
| Temporary performance state | Task 094 establishes one bounded application history and specifies a single application-owned checkpoint | Design complete; implementation deferred | Implement prepared capture/restore after Task 095, without PCM retention or a shadow snapshot stack |

## Semantic and engine gaps

The V2 defaults must bind only registered operations. Investigation finds these
important distinctions:

- Existing reusable paths: transport play/stop/tempo, MIDI panic, recording
  coordinator and live-loop passes, track mute/solo, reusable patterns and
  arrangement clips, advanced step properties/locks, sample processing,
  modulation/macros/scenes, Browser/Setup navigation, and project Save.
- Presentation gaps over existing semantics: most dedicated cluster mappings,
  page/subview destinations, bank contents, modal ownership, held-step lock
  gestures, focus-aware DATA, and project Performance P/F assignments.
- Semantic/API investigation required: metronome/count-in control, tap-tempo
  gesture, input-quantization toggle and swing/groove selection, note-repeat
  lifecycle/division/triplet, context-aware loop/restart, clip split, some
  Sample/Slice function defaults, and temporary performance restore.
- Engine capability must not be claimed for live erase while recording,
  arbitrary Fill duration, or note-repeat behavior until deterministic engine
  evidence exists.

## Page and subview status

| V2 destination | Current production area | Status |
|---|---|---|
| Performance / XY | Performance page and XY semantic control | Partial; XY secondary navigation and custom banks missing |
| Sequencer / Song | Sequencer plus Arrangement subview | Partial; correct musical authority exists, V2 page gesture/banks missing |
| Sample / Slice | Sample page with slice model/actions | Partial; navigation and listed function banks incomplete |
| Sound / Presets | Sound page and Browser/preset workflows | Partial; V2 destination and physical authoring banks incomplete |
| Mixer / FX | Independent Mixer and FX pages | Partial; V2 shared position/SHIFT navigation missing |
| Modulation / Route | FX/Sound modulation presentation and route authoring | Partial; dedicated destination and banks missing |
| Macros / Scenes | Performance/FX macro and scene models | Partial; dedicated destination and pad/function grammar missing |
| Browser / Setup | Independent pages | Partial; page destination exists, V2 hardware navigation/modal ownership incomplete |

## Prioritized Reference Controller V2 backlog

### RC2-001 — Gesture ownership and input epochs (P1, foundational)

Implement a control-side gesture arbiter above normalized events: held sets,
consume-on-chord taps, momentary release pairing, configurable double-tap latch,
repeat/acceleration, modal ownership and connection epochs. No musical timing in
the audio engine. Acceptance: deterministic tests cover chords, release order,
modal entry, stale packets, disconnect/reconnect and no double dispatch.

### RC2-002 — V2 identities, page routing and screen contract (P1)

Reconcile descriptor/profile identities to the locked inventory and implement
the primary/SHIFT page table, active-page root, top labels and right-side 2×4
context layout without embedding indices in application state. Acceptance:
Slint and simulated controller resolve identical stable destinations/labels at
1920×1200 and 1024×600.

**Task 096B status: implemented in software.** `reference_controller_tests`
validates the complete inventory, all destinations, held SHIFT across context
changes, modal rejection and reconnect generations. Generated Slint tests cover
primary/secondary callbacks and structured zones at both resolutions. Physical
hardware and coordinate-driven touch remain NOT RUN.

### RC2-003 — Dedicated navigation/edit/transport bindings (P1)

Add descriptor-backed defaults for the navigation, editing and transport
clusters, including availability and safe destructive confirmation. Implement
focus-aware DATA and only those hold-and-turn shortcuts backed by existing
semantics. Acceptance: production-path simulation traverses and edits a song,
records/loops, saves safely, panics safely and proves consumed modifiers do not
fire taps.

**Task 096C status: Partially implemented with the supported semantic set.**
Focus-aware DATA, repeated linear focus navigation, BACK/ENTER, TRACK/PAGE/
MUTE/ERASE pad chords, Undo/Redo, momentary/latched Fill, configured recording
toggle, play/stop/restart, MIDI panic and TAP+DATA are production bindings.
Stable track/pattern/step arguments prevent selection redirection. The complete
matrix and evidence are in `TASK_096C_VALIDATION.md`. RC2-003 remains open for
shared visual-modal ENTER/BACK ownership, per-track solo/arm semantics, typed
clipboard, contextual loop/record configuration, unified voice+MIDI panic and
the intentionally absent optional musical capabilities.

### RC2-004 — Descriptor-driven page banks and held-step locks (P1)

Populate primary/secondary encoder/function banks for the V2 destinations;
capture stable held-step targets and route encoder edits to step properties or
locks. Acceptance: page matrix tests verify every label/action/value/argument,
unsupported entries are unavailable, and audio/MIDI behavior confirms locks.

**Task 097C status: substantially implemented in software.** Sequencer P1-P8 now authors all
existing step properties and compatible numeric locks through captured
reusable-pattern targets, bounded multi-selection and one coalesced history
entry. All destinations now have explicit shared primary/secondary functions
and pad modes. Existing operations are bound; unavailable actions are inert.
Some destinations still lack stable selection services or musical capability,
and the stronger Task 097D acceptance evidence and physical validation are open.

### RC2-005 — Project-owned Performance banks (P2, musical)

Extend the existing project presentation model from explicit pads to banked
P1–P8/F1–F8 primary/SHIFT assignments with stable targets and labels. Keep this
separate from `.ngc` physical mappings. Acceptance: save/reopen without a
controller preserves behavior; generic MIDI and V2 profiles can both address it.

**Task 096E status: partially resolved.** Format-12 project data now owns
bounded named banks, stable primary/SHIFT parameter and safe-action layers,
labels and banked launch-slot pads. V2 and a non-reference controller exercise
the shared resolver; momentary Fill has captured release/cancellation,
definition edits use unified history, and save/reopen is controller-independent.
The complete production graphical category/object/typed-argument picker,
broader momentary/toggle state discovery and physical validation remain open.
See `TASK_096E_VALIDATION.md`.

### RC2-006 — Manual mapping closure (P2; AUD2-006)

**Task 096F status: partially resolved with production-path evidence.** Shared
discovery now drives Performance and Setup target selection; supported manual
MIDI sources validate while disconnected; typed actions, direct parameters,
contextual/Performance roles, conflict-safe candidate replacement, `.ngc`
round-trips and active-session generation invalidation are tested. Remaining
closure is the full graphical profile-library lifecycle, Setup typed-argument
form, rich conflict presentation and complete feedback authoring. See
[`TASK_096F_VALIDATION.md`](TASK_096F_VALIDATION.md).

### RC2-007 — Feedback semantics and firmware contract (P2, hardware-bound)

Specify protocol connection epochs, lost-release recovery and RGB
color/brightness/pulse/non-color cues; implement simulator conformance tests.
Firmware and panel validation remain separate. Acceptance requires both software
tests and later physical rollover, reconnect and illumination evidence.

**Task 096G status: software contract implemented; hardware validation open.**
Protocol 1.3 preserves 1.x framing and legacy descriptors while adding exact V2
identity/inventory negotiation, host generations, sequenced input, full button
state recovery, heartbeat/reset behavior and bounded `LED_RENDER`. A canonical
portable header defines all 67 wire IDs and a byte-stream firmware simulator
exercises production framing, session and semantic paths. Semantic palette and
render modes are specified independently of PCB LED mapping. Actual MCU
firmware, electrical caps, USB behavior, debounce, rollover, encoder and RGB
visibility remain NOT RUN, so RC2-007 is not physically closed.

### RC2-008 — Optional musical semantics (P2/P3)

After capability review, implement missing metronome/tap/count-in,
note-repeat/triplet, live erase, Fill duration and temporary performance state
as separate coherent tasks. Do not stub labels or create frontend state.

## Roadmap integration

Task 094 established atomic dependency recovery, bounded unified history and
content windows. Task 095 established immutable same-revision client snapshots,
structured generated-shell checks and alternative frontend/controller/workflow
conformance. These provide Task 096 test seams without implementing V2 behavior.
The temporary-performance checkpoint design is now documented in
`docs/APPLICATION_HISTORY.md`; implementation remains deferred. The recommended
full integration milestone is **Task 096 —
Reference Controller V2 interaction integration**, beginning with RC2-001–004
after Tasks 094–095, followed by RC2-005–007. RC2-008 remains capability-driven
musical expansion.

## Audit reconciliation

- **AUD2-006 remains Partial.** Task 093 added discovered targets and persisted
  behavior fields, but V2 manual source/argument/conflict authoring is open.
- **AUD2-007 remains Partial and is now measured against V2.** Task 093's shared
  context/argument plumbing is reusable; it did not implement the locked page,
  cluster, gesture, modal or bank specification.
- **AUD2-014 remains Resolved (software).** Explicit project pad positions are
  compatible with V2; banked assignment UI and physical validation remain RC2
  work rather than reopening the identity defect.

All controller ergonomics, electrical behavior, RGB appearance, rollover,
debounce, touchscreen reach and physical performance timing remain **NOT RUN**.

Task 097A partially resolves RC2-001/003/007 presentation ownership: shared
modal generations, safe focus, ENTER/BACK, shifted navigation, Save, Browser
DATA and protocol recovery have production evidence. RC2-001/003 remain partial
because not every dialog/view exposes a complete focus graph. RC2-007 physical
validation remains open. AUD2-011 gains generated focus/modal evidence but
remains partial pending complete form reachability and physical validation.

Task 097B closes RC2-005 graphical selection for all 48 reference positions and
supported parameter, safe-action and launch-target forms. RC2-006/AUD2-006 gain
generated manual-source, typed-target, behavior and structured-conflict
evidence, but remain partial: no coordinator-owned profile library exists for
create/rename/duplicate/select/remove/recovery, and detailed context/modifier
and RGB rendering fields are not part of the current profile schema. RC2-007
physical validation and AUD2-007 remain open.
### Task 097F evidence

RC2-006/AUD2-006 are substantially implemented in software: the missing
coordinator-composed profile library, stable lifecycle, persistence, recovery,
independent MIDI endpoint activation, generated lifecycle controls, learned
source preview, mapping scope/feedback controls and arbitrary four-field UI
limit are closed. Remaining partial rows are dedicated USB runtime switching,
conservative mutually-exclusive source layering and physical validation.
