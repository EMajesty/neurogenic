# Reference Controller V2 implementation gaps

Task 096A evidence: RC2-001 is **partially implemented** at the shared
infrastructure layer by `InputGestureArbiter`, `ControllerSession` integration,
and `neurogenic_input_gesture_tests`. V2-specific policies, full profile and
physical validation remain open. RC2-007's foundational modal ownership and
input generations are implemented; complete modal bindings and rendered V2
presentation remain open. See `docs/INPUT_GESTURES.md`.

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
| Physical inventory | Descriptor has 16 grid, 8 page, 8 function, 8 contextual encoders, DATA, 6 navigation, 10 edit and 10 performance controls | Partial | Descriptor names/types and protocol IDs must be reconciled to the locked V2 identities; all hardware validation is NOT RUN |
| Static pages | Eight `local-context` bindings; Slint page/bank identity exists | Partial | Implement the V2 primary/SHIFT destination table, active-page root and shifted top labels |
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
| Screen zones | Top navigation and eight contextual labels/functions exist | Partial | Locked top/right-2×4/bottom composition and SHIFT label changes at both resolutions |
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

### RC2-003 — Dedicated navigation/edit/transport bindings (P1)

Add descriptor-backed defaults for the navigation, editing and transport
clusters, including availability and safe destructive confirmation. Implement
focus-aware DATA and only those hold-and-turn shortcuts backed by existing
semantics. Acceptance: production-path simulation traverses and edits a song,
records/loops, saves safely, panics safely and proves consumed modifiers do not
fire taps.

### RC2-004 — Descriptor-driven page banks and held-step locks (P1)

Populate primary/secondary encoder/function banks for the V2 destinations;
capture stable held-step targets and route encoder edits to step properties or
locks. Acceptance: page matrix tests verify every label/action/value/argument,
unsupported entries are unavailable, and audio/MIDI behavior confirms locks.

### RC2-005 — Project-owned Performance banks (P2, musical)

Extend the existing project presentation model from explicit pads to banked
P1–P8/F1–F8 primary/SHIFT assignments with stable targets and labels. Keep this
separate from `.ngc` physical mappings. Acceptance: save/reopen without a
controller preserves behavior; generic MIDI and V2 profiles can both address it.

### RC2-006 — Manual mapping closure (P2; AUD2-006)

Finish manual source creation, typed argument editing/selection, conflicts,
feedback/context properties and recovery-profile workflows. Acceptance: create
without MIDI learn, persist/reload, dispatch stable targets and reject stale or
incompatible bindings transactionally.

### RC2-007 — Feedback semantics and firmware contract (P2, hardware-bound)

Specify protocol connection epochs, lost-release recovery and RGB
color/brightness/pulse/non-color cues; implement simulator conformance tests.
Firmware and panel validation remain separate. Acceptance requires both software
tests and later physical rollover, reconnect and illumination evidence.

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
