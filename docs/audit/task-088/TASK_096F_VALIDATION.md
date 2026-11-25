# Task 096F validation

Task 096F adds a shared, copy-only assignment target picker and closes the
production service path for manual MIDI mapping authoring. Project Performance
assignments and appliance controller profiles remain separate authorities.

## Implemented evidence

- `AssignmentTargetPicker` derives parameters, safe semantic actions, typed
  arguments, object choices, contextual roles and Performance roles from the
  existing discovery and action registries. It owns neither values nor state.
- Manual MIDI proposals support note, absolute CC, two's-complement relative CC
  and binary-offset relative CC sources, including disconnected-device
  authoring against a known descriptor.
- Source validation covers port/control identity, channel and number ranges,
  descriptor compatibility and duplicate-source conflicts. Replacement is
  candidate-first and atomic; cancellation or invalid replacement preserves the
  active profile.
- Controller profile activation reconnects the affected session, cancelling
  captured momentary/held ownership and advancing its input generation before
  new input is accepted.
- The Performance editor uses the same target model for parameter and
  Performance-safe action assignments, including typed argument validation,
  custom labels, confirmation and cancellation. Project edits continue through
  unified musical history; appliance profile edits do not.
- `.ngc` persistence round-trips supported MIDI sources, bindings and typed
  arguments. Project Performance banks remain in project format 12.

## Supported mapping fields

| Area | Supported in Task 096F |
|---|---|
| Source | logical endpoint/control, note or CC, channel 1–16, number 0–127 |
| Input | momentary note, absolute CC, relative two's complement, relative binary offset |
| Binding | direct parameter, typed action, contextual role, Performance role |
| Adjustment | pickup, inversion, fine scale, acceleration where compatible |
| Scope | existing stable context/index and modifier fields |
| Feedback | existing descriptor-compatible feedback flag/destination |
| Conflicts | duplicate source detection, explicit candidate replacement |

## Production-path tests

`controller_mapping_authoring_tests` covers disconnected profile preparation,
manual absolute/relative/button sources, typed Fill arguments, mapper-to-session
dispatch, authoritative parameter changes, Performance-role bank following,
direct-target stability, conflict cancellation/replacement, `.ngc` save/reload
and reconnect recovery. `slint_adapter_tests` covers shared Performance target
selection, typed action authoring, cancellation, manual Setup source proposals
and explicit conflict replacement. Generated production-component tests invoke
the Performance target selection/confirmation/cancellation callbacks and the
Setup manual-source callback.

Final clean validation used:

- `cmake --build --preset dev -j2 && ctest --test-dir build/dev --output-on-failure`: 38/38 passed.
- `cmake --build --preset slint -j2 && ctest --test-dir build/slint --output-on-failure`: 41/41 passed.
- `cmake --build --preset production -j2 && ctest --test-dir build/production --output-on-failure`: 41/41 passed.
- `cmake --build --preset sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure`: 38/38 passed.
- `./build/production/neurogenic_slint_benchmark`: passed.
- `nix build path:. --no-write-lock-file`: passed outside the restricted
  sandbox after the sandboxed daemon socket returned `Operation not permitted`.
- `nix flake check path:. --no-write-lock-file`: passed; the evaluator noted
  that the incompatible native `aarch64-linux` system was omitted while the
  configured AArch64 cross-compile check passed.
- `./result/bin/neurogenic --headless --list-devices`: passed using the installed
  production result and reported the explicit null headless audio backend and
  ALSA raw MIDI backend.

Physical touchscreen, MIDI hardware, controller PCB and audio validation remain
**NOT RUN**.

## Remaining limitations

- Setup exposes manual source creation for an existing endpoint/profile, but a
  complete graphical profile-library create/name/duplicate/recovery workflow is
  still missing.
- Conflict diagnostics are textual rather than a rich side-by-side conflict
  list. Mapping identity remains the validated source/control identity rather
  than a new UUID.
- The shared model validates typed action arguments; the Performance editor
  exposes them, while the Setup action-argument form is not yet complete.
- Feedback authoring is limited to existing descriptor destinations; RGB,
  pulses, heartbeat and firmware conformance remain Task 096G.
- The Performance page exposes representative P1/SHIFT+P1/F1 entry points; the
  adapter/service supports arbitrary logical positions, but the full graphical
  position chooser remains incomplete.
- Physical validation is not implied by simulated controller or generated Slint
  tests.

Therefore RC2-006 and AUD2-006 are **partially resolved with production service
evidence**, and RC2-005 gains shared graphical authoring evidence but remains
partial until the remaining position and Setup typed-argument presentation is
complete.

## Task 096G preparation

Task 096G should specify and test stable V2 wire control IDs, identity/version
negotiation, connection epochs and input sequencing, lost-release recovery,
heartbeat/state synchronization, bounded RGB/brightness/pulse feedback,
overflow/reconnect behavior, standalone diagnostics and firmware conformance
fixtures. It must not invent PCB scan positions, pins or LED-chain order.
