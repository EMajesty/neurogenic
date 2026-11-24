# Test coverage audit

## Executed for Task 080

On x86_64 Linux at baseline `ec615a1`:

- dependency-enabled development build: no work required; CTest **29/29
  passed** in 3.44 s;
- Slint build (Slint 1.19): built successfully; CTest **30/30 passed** in
  3.26 s, including missing-display behavior;
- ASan/UBSan CTest: **29/29 passed** in 15.73 s with
  `ASAN_OPTIONS=detect_leaks=0` (LeakSanitizer cannot operate under the managed
  environment's ptrace policy);
- `nix build --no-update-lock-file`: passed, including its 29-test release
  check; `nix flake check --no-update-lock-file`: passed, including the
  AArch64 cross compile;
- packaged `neurogenic --headless --list-devices`: passed using the explicit
  null audio backend; ALSA reported no MIDI ports in this environment.

The ordinary CTest result demonstrates deterministic host behavior, not a
complete instrument workflow or hardware readiness.

## Inventory

- Large consolidated unit/application suite: `tests/tests.cpp`.
- Dedicated discovery, binding, preset, sample analysis/processing, audio clip,
  live-loop and launching suites.
- Renderer-neutral Slint adapter suite: `tests/slint_adapter_tests.cpp`.
- SDL dummy-driver smoke test: `tests/gui_smoke.cpp`.
- Four semantic/audio workflow scenarios: `tests/workflow_scenarios.cpp`.
- Bounded engine integration/performance validation:
  `tests/integration_validation.cpp`.
- Nix x86_64 build/test, sanitizer job and native AArch64 CI; x86 cross compile
  is also a flake check.
- Diagnostic and hardware runbooks exist, but physical HIL is manual.

## What tests prove poorly or not at all

### Slint rendering and interaction

`slint_adapter_tests` directly calls C++ adapter methods. It does not instantiate
the rendered page, click controls, test focus/touch/drag behavior, take
screenshots, verify clipping, or inspect accessibility. The missing-display
test proves only graceful failure. Both target resolutions are declarations,
not rendered layout assertions.

Nix packages and GitHub CI set `NEUROGENIC_ENABLE_SLINT=OFF`; only local opt-in
Slint presets compile generated UI. A syntax or linkage regression can escape
the primary package jobs.

### Assertions of acceptance versus outcome

Several adapter checks accept a command immediately and do not wait for or
assert its complete lifecycle: transient analysis content, processing result,
preset transaction activation, preview audio, and some route operations. The
dedicated backend tests are stronger, but no test joins those results to actual
Slint interaction.

### Missing workflow coverage

- empty-project track creation entirely through UI;
- sequence page navigation beyond 16 steps;
- record/resample/live-loop through UI;
- processing result publish and subsequent playback;
- modulation source and macro assignment creation through UI;
- preset capture/save/rename/delete through UI;
- MIDI route/controller target editing through Setup;
- missing-asset relink through Browser;
- complete Save As, dirty close, restart restore through rendered Slint;
- touchscreen-only text/path entry;
- removable-media disappearance during scan/load/save;
- combined worker cancellation and application shutdown.

### Failure and concurrency gaps

- Coordinator callback reentrancy and sender blocking while its mutex is held.
- Raw coordinator endpoint pointer lifetime under concurrent shutdown.
- Full Slint refresh allocation/latency under high meter/transport revisions.
- Browser scan latency and cancellation on slow/removable filesystems.
- Queue overflow convergence under the production multi-controller appliance
  threads (library-level overflow tests exist).
- Callback allocation/lock/filesystem instrumentation for all integrated paths.

### Hardware-only gaps

N150 and Raspberry Pi 5 callback performance, ALSA XRUN accounting, actual
64/128/256/512-frame support, round-trip latency, multichannel routing,
external-FX compensation, MIDI jitter, device hotplug, RP2040 protocol/LEDs,
third-party controller feedback, touchscreen behavior, thermal soak and clean
power-cycle persistence are unverified.

## Recommended test additions

1. Add a Slint testing-backend suite that instantiates generated components,
   invokes real callbacks and verifies visible properties at both resolutions.
2. Add a UI-to-service workflow fixture for the P1 workflows, asserting audio
   or persisted results after acknowledgements.
3. Add performance counters around adapter refresh, snapshot, publish and model
   allocation; establish bounded budgets before refactoring.
4. Add coordinator reentrancy/lifecycle tests and ThreadSanitizer coverage where
   supported.
5. Add an opt-in Slint Nix/CI build so generated UI and production linkage are
   continuously checked.
6. Keep physical tests separate and emit machine-readable device, kernel,
   buffer, latency and XRUN evidence.
