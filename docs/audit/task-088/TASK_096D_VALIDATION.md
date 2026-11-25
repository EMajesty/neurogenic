# Task 096D validation

Task 096D extends the shared `resolve_controller_context()` path with explicit
bank and control-role metadata and implements the production held-step editing
path. It does not introduce a V2-only musical model. All edits still enter
`ApplicationService`, `TimelineTransaction`, unified application history and
the existing prepared timeline publication.

## Implemented architecture

- `ContextControl` carries copied bank/role/lock metadata with its existing
  label, value, unit, stable parameter/action and typed arguments.
- An explicit context suffix opts a client into richer contextual banks.
  Unsuffixed legacy contexts retain their previous bindings.
- V2 pads are immediate outside Sequencer and deferred only in Sequencer. A
  release without a chord submits one stable `step.toggle.v1`; P1-P8 movement
  while held consumes that tap.
- `ControllerSession` captures `PatternId`, `TrackId`, absolute step, an
  unambiguous `EventId`, and input generations. It retains no engine pointer.
- `step.adjust-stable.v1`, `step.lock-adjust-stable.v1`, and
  `step.lock-remove-stable.v1` are narrow wrappers over the existing timeline
  transaction and lock scheduler. Empty/ambiguous notes and incompatible lock
  destinations are rejected.
- Step selection contains at most 64 stable references. Invalid references are
  pruned after timeline replacement; grouped edits stay within one pattern and
  track.
- Held encoder begin/update/end uses the existing edit group, producing one
  history entry for multiple deltas. Modal/context/disconnect cancellation
  discards the group.

## Sequencer contextual coverage

| Position | Registered binding | Availability |
|---|---|---|
| P1 | `step.adjust-stable.v1(property=pitch)` | Unique note required |
| P2 | `step.adjust-stable.v1(property=velocity)` | Production tested |
| P3 | `step.adjust-stable.v1(property=duration)` | Tick units |
| P4 | `step.adjust-stable.v1(property=probability)` | Clamped 0..1 |
| P5 | `step.adjust-stable.v1(property=condition)` | Existing conditions |
| P6 | `step.adjust-stable.v1(property=ratchets)` | Range 1..8 |
| P7 | `step.adjust-stable.v1(property=microtiming)` | Event tick offset |
| P8 | `step.lock-adjust-stable.v1(selected parameter)` | Compatible numeric automatable destinations |
| SHIFT F1/F2 | selection toggle / clear | Stable typed arguments |
| SHIFT F3/F4 | selected-lock remove / all-lock clear | Distinct from event erase |
| SHIFT F5/F6 | `edit.undo.v1` / `edit.redo.v1` | Unified history |
| SHIFT F7/F8 | step page previous / next | Existing paging |

Unsupported V2 positions resolve inactive and labels alone are not counted as
implementation. Dynamic instrument/effect parameters still use application
discovery and `ParameterBindingService`.

## Production and generated evidence

`neurogenic_reference_controller_tests` uses the real V2 descriptor/profile,
arbiter, controller session, queued dispatch, application service, timeline
publication and engine acknowledgement. It covers two reusable patterns, later
steps, exactly one tap toggle, multiple held velocity deltas, one coalesced
history entry, a `synth.cutoff` lock without base mutation, Undo/Redo, pattern
isolation, and a two-step grouped edit.

The production generated-component fixture inspects generated Sequencer P1 and
P8 rows (`PITCH` and `LOCK ...`) and invokes the generated encoder callback.
The Slint adapter consumes the same roles as the controller. Existing rendered
tests retain both target resolutions. This is not coordinate or physical touch
evidence.

## Destination status

| Destination | Status after Task 096D |
|---|---|
| Sequencer | Supported P1-P8 property/lock bank and core primary/SHIFT editing implemented |
| Performance / XY | Existing launch/macro behavior preserved; custom banks deferred to 096E |
| Song | Partial existing arrangement/navigation bindings |
| Sample / Slice | Partial existing descriptors; full function matrix open |
| Sound / Presets | Descriptor-backed parameters; pagination/default functions partial |
| Mixer | Existing gain/pan/send descriptors; group/aux pad modes open |
| FX | Existing effect/global descriptors; instance pagination partial |
| Modulation / Route | Existing source/route actions; full route-property bank partial |
| Macros / Scenes | Existing actions; dedicated pad modes partial |
| Browser / Setup | Existing focus/confirm/cancel; secondary shortcuts partial |

RC2-004 is therefore **partially resolved** around the complete Sequencer
authoring core. AUD2-007 remains open. Physical tests are **NOT RUN**.

## Validation results

- `cmake --build --preset dev -j2`: pass.
- `ctest --test-dir build/dev --output-on-failure`: 36/36 pass.
- `cmake --build --preset slint -j2` and
  `ctest --test-dir build/slint --output-on-failure`: 39/39 pass.
- `cmake --build --preset production -j2` and
  `ctest --test-dir build/production --output-on-failure`: 39/39 pass.
- The first direct sanitizer CTest attempt stopped after LeakSanitizer reported
  its known ptrace incompatibility; this was an environment failure, not
  counted as a pass.
- `env ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize
  --output-on-failure`: 36/36 pass (ASan/UBSan remain enabled).
- `nix build path:. --no-write-lock-file`: pass after granting system Nix
  daemon access to the workspace sandbox.
- `nix flake check path:. --no-write-lock-file`: pass; x86_64 checks and the
  configured AArch64 cross-compile derivation passed. Native AArch64 execution
  was not performed.
- `./result/bin/neurogenic --headless --list-devices`: pass; explicit Null
  audio, ALSA raw MIDI, no endpoints discovered.
- `./build/slint/neurogenic_slint_benchmark`: pass; full rebuilds 259,
  telemetry updates 257, skipped 255. This task adds no performance claim.

No physical controller, touchscreen, audio-latency or subjective listening
test was run.

## Task 096E

Add project-owned named Performance banks with primary/SHIFT P1-P8 and F1-F8
assignments, labels and stable pad targets, persistence/migration, and explicit
availability after target deletion. Keep these musical assignments separate
from appliance-owned controller profiles.
