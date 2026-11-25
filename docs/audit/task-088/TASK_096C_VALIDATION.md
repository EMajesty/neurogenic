# Task 096C validation — navigation, editing and transport clusters

## Result

Task 096C binds the subset of the locked Reference Controller V2 clusters for
which an authoritative application operation already exists. The reference
profile remains optional; the bindings submit the same semantic actions used by
Slint, generic controllers and independent clients. Physical validation is
**NOT RUN**.

The production `ControllerSession` path now provides focus-aware DATA,
repeatable directional focus navigation, BACK/ENTER, stable TRACK/PAGE/MUTE/
ERASE pad chords, unified Undo/Redo, momentary and latched Fill, recording
toggle, play/stop/restart, MIDI panic and TAP+DATA tempo adjustment. Stable pad
chords resolve `TrackId`, `PatternId` and absolute step at chord ownership time.
The new `step.erase.v1` command erases events only; it never toggles an empty
step into existence. `track.mute-toggle.v1` accepts an optional stable
`track_id` while retaining its selection-relative compatibility behavior.

## Binding matrix

| Logical control / gesture | Intended operation | Registered binding | Availability | Policy | Evidence / first blocker |
|---|---|---|---|---|---|
| DATA | focused adjustment/list navigation | `navigation.adjust.v1` | Implemented for existing focus domains | immediate relative; SHIFT fine, large deltas accelerated | `reference_controller_tests`: authoritative tempo/focus path |
| BACK | cancel/back | `navigation.cancel.v1` | Implemented outside visual modal ownership | immediate | Application navigation stack; frontend modal confirmation remains presentation-owned |
| SHIFT+BACK | destination root | reference destination root revision | Partial | immediate | Page-button root exists; dedicated BACK root binding remains unavailable |
| UP/DOWN | previous/next focus | `navigation.focus-previous.v1` / `navigation.focus-next.v1` | Implemented | bounded repeat | Production profile and regression suite |
| LEFT/RIGHT | previous/next focus | same actions | Implemented for linear focus only | bounded repeat | No fabricated spatial navigation |
| SHIFT+directions | page/first/last | — | Unavailable | — | Missing presentation-neutral collection navigation contract |
| ENTER | focused confirm/select | `navigation.confirm.v1` | Implemented outside visual modal ownership | immediate | Existing focus service |
| SHIFT+ENTER | project save | `project.save.v1` | Implemented when project has a valid path | immediate | Save-as/location and overwrite continue through existing frontend workflows |
| TRACK+pad | select stable logical track | `track.select.v1(track_id)` | Implemented, first 16 logical bank positions | modifier chord | Production scenario selects track 42 without pad trigger |
| MUTE+pad | toggle stable track mute | `track.mute-toggle.v1(track_id?)` | Implemented | modifier chord | Production scenario and timeline publication |
| SOLO+pad | track solo | — | Unavailable | — | No authoritative per-track solo semantic/state |
| ARM+pad | track record arm | — | Unavailable | — | Recording families do not share an unambiguous per-track arm authority |
| PAGE+pad | select absolute step page | `step.page-select.v1(page)` | Implemented | modifier chord | Partial final pages validated by ApplicationService |
| SHIFT+PAGE | pattern length | existing pattern length actions | Unbound | — | Requires focused object/range/confirmation policy |
| ERASE+pad | erase selected pattern-track step | `step.erase.v1(pattern_id,track_id,step)` | Implemented | modifier chord | Stable reusable-pattern event removal plus Undo/Redo test |
| SHIFT+ERASE | delete object | existing typed delete actions | Unbound | — | Needs focused object kind and shared destructive modal ownership |
| UNDO / SHIFT+UNDO | Undo / Redo | `edit.undo.v1` / `edit.redo.v1` | Implemented | immediate | Unified application history regression |
| COPY / SHIFT+COPY | Copy / Paste | — | Unavailable | — | No bounded typed application clipboard; design remains a stable-ID value snapshot, never mutable pointers/PCM |
| SELECT | selection modifier | existing modifier state | Implemented infrastructure | modifier-only | Contextual selection behavior remains bank-specific Task 096D work |
| SHIFT+SELECT | clear selection | — | Unavailable | — | No general cross-editor selection authority |
| CLICK / SHIFT+CLICK | metronome / count-in | — | Unavailable | — | Missing authoritative musical capability |
| TAP | tap tempo | — | Unavailable | modifier-only for chord | Tap estimator is not implemented |
| TAP+DATA | tempo adjustment | `parameter.adjust.v1(transport.tempo)` | Implemented | held-control encoder chord | Production scenario verifies tempo and consumes TAP |
| SHIFT+TAP | BPM editor | — | Unavailable | — | Requires presentation focus binding |
| QUANT | input quantization | existing recording quantization setters | Unbound | — | Requires recording-family focus and enum selection policy |
| REC MODE | recording mode | existing recording coordinator actions | Unbound | — | Requires stable family/source/destination selector |
| FILL press/release | momentary conditional Fill | `performance.fill-set.v1(active)` | Implemented | momentary paired ownership | Production scenario verifies authoritative activation/restoration |
| SHIFT+FILL | latched Fill | `performance.fill-latch.v1` | Implemented | SHIFT chord | Arbiter consumes base momentary press |
| REPEAT / SHIFT+REPEAT | note repeat / triplet | — | Unavailable | — | Missing note-repeat engine |
| LOOP / SHIFT+LOOP | applicable loop / range | — | Unavailable | — | Multiple loop authorities require explicit context policy |
| REC | arm/start/stop configured recording | `recording-toggle` over coordinator | Implemented for current configured workflow | immediate | Existing requested/active phase reporting |
| SHIFT+REC | overdub | existing live-loop actions | Unbound | — | Requires stable live-loop session destination |
| STOP | stop transport | `transport.stop.v1` | Implemented | immediate global control | Does not destroy recorded audio |
| SHIFT+STOP | panic | `midi.panic.v1` | Partial | immediate | External MIDI cleanup exists; a unified voice+MIDI panic semantic is still missing |
| PLAY / SHIFT+PLAY | play / restart | `transport.play.v1` / `transport.restart.v1` | Implemented | immediate | Existing engine transport semantics |
| QUANT/LOOP/REC/REPEAT/FILL + DATA | shortcut adjustment | only TAP+DATA implemented | Otherwise unavailable | held-control encoder | First blockers above; no unrelated DATA fallback |

## Deterministic production scenario

`neurogenic_reference_controller_tests` constructs the complete V2 descriptor,
configures the real gesture arbiter and drives `ControllerSession`. It selects
a reusable pattern, selects track 42 with TRACK+pad, changes to absolute page
one with PAGE+pad, erases step 17 with ERASE+pad, performs unified Undo/Redo,
mutes the stable track, asserts momentary Fill press/release, adjusts tempo with
TAP+DATA, starts/stops transport, and disconnects with a modifier held. Commands
are drained through `ApplicationService`; timeline publication is acknowledged
by the engine callback. The test checks exact authoritative state and verifies
that chorded pads do not execute their ordinary operation.

The production generated-component interaction path additionally invokes
`track-selected`, `transport-invoked` and `history-action`; the new two-track
fixture verifies that generated track navigation changes the authoritative
stable target. Task 096B's structured reference-shell assertions and renders
remain active at 1920x1200 and 1024x600.

## Architectural and safety notes

- Gesture processing, stable-ID resolution and history construction remain on
  the control thread. The audio callback receives only existing bounded
  timeline, transport and Fill publications.
- No V2 control count or placement enters engine state. Generic MIDI and direct
  semantic clients continue to bypass the reference profile.
- Track and erase chords snapshot copied application state; they retain IDs and
  scalar arguments, never mutable engine pointers.
- Repeat processing remains bounded by `GestureTiming`; modal/context changes
  and reconnect cancel held ownership.
- Visual modal confirm/cancel ownership is still frontend-local. The arbiter
  prevents underlying reference controls from bypassing an active modal, but a
  complete shared ENTER/BACK modal bridge remains open.

## Validation record

Validation environment: x86_64 Linux 7.3.0, AMD Ryzen 7 7800X3D, GCC 15.3.0,
Slint 1.19.0.

- `cmake --build --preset dev -j2 && ctest --preset dev --output-on-failure`:
  **36/36 passed**.
- `cmake --preset slint && cmake --build --preset slint -j2 && ctest --preset
  slint --output-on-failure`: **39/39 passed**, including software renders at
  1920x1200 and 1024x600.
- `cmake --preset sanitize && cmake --build --preset sanitize -j2`: passed.
  The ordinary sanitizer CTest run was blocked uniformly because LeakSanitizer
  cannot operate under the managed ptrace environment. Re-running as
  `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure`
  produced **36/36 passed** with ASan/UBSan active and leak detection explicitly
  unavailable.
- `cmake --preset production && cmake --build --preset production -j2 &&
  ctest --preset production --output-on-failure`: **39/39 passed**.
- `./build/slint/neurogenic_slint_benchmark`: passed; incremental publication
  invariants remained intact (257 telemetry updates, 255 skipped refreshes).
- `nix build path:.`: passed for the production Slint package.
- `nix flake check path:.`: passed; x86_64 package/check matrix and the AArch64
  cross-compile check built. Native AArch64 execution was not available.
- `./result/bin/neurogenic --headless --list-devices`: passed from the installed
  closure, selecting explicit Null audio and reporting no discovered ALSA raw
  MIDI endpoints without failure.

Physical controller, touchscreen, subjective ergonomics, hardware rollover,
native AArch64 execution and physical audio-interface behavior remain
**NOT RUN**.

## Task 096D preparation

Task 096D should build descriptor-driven contextual banks on the stable gesture
and destination foundations: primary/secondary P1–P8 and F1–F8 layers, pad-mode
descriptors, stable held-step targets, multi-step selection, lock creation and
removal, coalesced history begin/update/end/cancel, and authoritative feedback.
It must not fill unsupported positions with lookalike actions.
