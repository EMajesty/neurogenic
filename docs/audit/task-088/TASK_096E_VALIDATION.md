# Task 096E validation

## Result

Task 096E adds a bounded project-owned Performance surface without adding a
playback, parameter or controller authority. `PerformanceBank` records live in
`SongArrangement`; parameter assignments resolve through descriptors/macros,
actions through the registered semantic layer, and pads through
`PerformanceLauncher` launch slots.

RC2-005 is **partially resolved**. The project model, primary/secondary
resolution, V2 and non-reference controller dispatch, momentary ownership,
persistence/migration and unified history are implemented. The Slint page
provides bank lifecycle and pad editing and publishes shared P/F descriptors.
The complete category/object/typed-argument graphical picker, arbitrary
momentary action compatibility and physical validation remain open.

## Production acceptance evidence

`neurogenic_performance_banks_tests` uses the real `ApplicationService`,
`TimelineService`, `AudioEngine`, Reference Controller V2 profile,
`ControllerSession` and project serializer. It verifies:

- two named banks and stable assignments;
- primary cutoff versus SHIFT resonance isolation;
- macro adjustment through `PerformanceMacroService`;
- captured Fill press/release and cancellation during a bank change;
- banked pattern launch and SHIFT+pad selection without launch;
- missing launch-target retention and explicit same-ID repair;
- bank rename and assignment replacement Undo/Redo;
- format-12 save/reopen of names, layers, labels, arguments and references;
- operation from a deliberately non-reference one-encoder controller.

The generated production component invokes bank creation/selection and banked
pad assignment. Both reference resolutions retain the Task 096B shell geometry
checks. Direct callbacks are generated-component evidence, not coordinate or
physical-touch evidence.

## Persistence and migration

`NEUROGENIC_TIMELINE 12` adds `performance_bank`,
`performance_parameter`, `performance_action` and `performance_pad` records.
Typed action values use explicit boolean/integer/real/text tags. Duplicate
bank/assignment/position identities, malformed values and capacity overflow
are rejected. Pre-12 explicit logical pad positions migrate in memory to bank
ID 1 in stable slot-ID order; source files are not rewritten on load.

## Safety and ownership

- Maximum 32 banks; 64 parameters, 64 actions and 128 pads per bank.
- Performance action authoring uses a narrow allowlist and normal registry
  argument validation. Destructive and filesystem actions are excluded.
- Momentary release retains bank/assignment/action identity and is cancelled on
  modal, page, bank or reconnect ownership loss.
- Missing targets never redirect by order or label.
- Definition edits use timeline transactions and unified application history;
  live values and active-bank selection do not flood history.
- No new audio-callback work, allocation, locks, discovery or filesystem I/O.

## Validation commands

All commands ran on x86_64-linux in the pinned Nix development environment:

- `cmake --build --preset dev -j2 && ctest --test-dir build/dev
  --output-on-failure`: **37/37 passed**.
- `cmake --build --preset slint -j2 && ctest --test-dir build/slint
  --output-on-failure`: **40/40 passed**, including generated-component and
  rendered 1920x1200/1024x600 coverage.
- `cmake --build --preset production -j2 && ctest --test-dir
  build/production --output-on-failure`: **40/40 passed**.
- `cmake --build --preset sanitize -j2 && env ASAN_OPTIONS=detect_leaks=0
  ctest --test-dir build/sanitize --output-on-failure`: **37/37 passed** under
  ASan/UBSan. Leak detection is disabled for the known ptrace-constrained test
  environment; address and undefined-behaviour instrumentation remain active.
- `./build/slint/neurogenic_slint_benchmark`: passed. Median idle/full/
  incremental refresh was 52,889/119,704/69,390 ns; snapshot copy/shared was
  39,364/19,546 ns; discovery/parameter resolution was 272,660/370,814 ns.
- `nix flake check path:. --no-write-lock-file`: **passed all three checks**,
  building the headless package, production package, and AArch64 cross-compile
  check. Native AArch64 execution was not available.
- `/nix/store/p9q5c8jc14i05vp46zhqdhl24c39fjr3-neurogenic-0.1.0/bin/neurogenic
  --headless --list-devices`: **passed** outside the development shell, using
  explicit Null audio and reporting an available ALSA raw-MIDI backend with no
  discovered endpoints.

The separate `nix build path:. --no-write-lock-file` invocation could not
connect to the daemon in the restricted sandbox, and both requested escalation
reviews timed out. The production derivation was nevertheless built by the
successful flake check and its installed output was used for the smoke test.

Physical controller, touchscreen ergonomics, RGB feedback and subjective
musical validation remain **NOT RUN**.

## Remaining work and Task 096F

Task 096F should reuse the safe action/parameter target selection, typed value
parsing and missing-target diagnostics for appliance-owned controller mappings.
It must add manual MIDI source/message/channel/number creation, relative
encoding, scaling/inversion/pickup, contextual scope, feedback target,
conflict replacement, profile persistence and recovery—without moving project
Performance banks into controller profiles.
