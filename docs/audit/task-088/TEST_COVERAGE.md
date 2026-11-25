# Test and validation audit

Task 096B adds `neurogenic_reference_controller_tests` for the explicit V2
inventory and production ControllerSession routing. The generated Slint harness
invokes primary/secondary destination callbacks and SHIFT label changes, and
validates top/central/right-2×4/bottom shell geometry at both resolutions. See
[Task 096B validation](TASK_096B_VALIDATION.md). Coordinate input and physical
validation are NOT RUN.

Task 096C extends the same production executable through V2 TRACK/PAGE/MUTE/
ERASE chords, ordinary-pad suppression, absolute page addressing, reusable-
pattern erasure, unified Undo/Redo, momentary Fill, TAP+DATA, play/stop and
disconnect cleanup. See [Task 096C validation](TASK_096C_VALIDATION.md).
The generated component fixture now also invokes `track-selected` against a
two-track project and verifies the authoritative stable selection round trip.

## Commands executed at `d8172a7`

Task 090 superseding validation is recorded in
[`TASK_090_VALIDATION.md`](TASK_090_VALIDATION.md). The historical results below
remain the evidence that reproduced AUD2-004 before correction.

| Command | Result |
|---|---|
| `ctest --preset dev --output-on-failure` | 31/31 passed, 3.66 s |
| direct `ctest --preset slint --output-on-failure` | 25/33 passed; 8 aborted in libremidi udev assertion |
| `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | 31/31 passed, 17.73 s |
| `./build/slint/neurogenic_slint_benchmark` | passed; timings in architecture audit |
| `./build/dev/neurogenic_benchmark --iterations 200` | passed; checksum 58822531, 0 simulated overruns |
| `nix build path:. --no-write-lock-file` | passed |
| `nix flake check path:. --no-write-lock-file` | passed, including configured AArch64 compile check |
| `./result/bin/neurogenic --headless --list-devices` | passed; Null audio, ALSA MIDI, no MIDI endpoints |

The documented Nix Slint rerun was attempted three times; in this environment
`nix develop` did not complete during the bounded audit window and produced no
CTest log. It is therefore **not** reported as passing. Nix package/flake checks
and sanitizer results above are fresh Task 088 evidence. Physical tests: all
**NOT RUN**.

## Quality by class

- **Unit/service:** strong for transactions, descriptors, bindings, protocol,
  sample analysis/processing and persistence parsing.
- **Engine integration/audio:** meaningful numeric assertions cover sample and
  synth voices, effects, stretching, audio clips, live-loop boundary/PCM and
  parameter locks. `workflow_scenarios` proves audible save/reopen for a beat.
- **Recording:** live-loop tests check exact boundary and samples. Internal
  resampling checks non-silence/frame counts; a full external-input waveform
  round trip through production backend is not tested without hardware.
- **Preset:** tests cover library lifecycle, missing assets, activation queue,
  identity changes and PCM for sample presets. Preset activation is not tested
  as Undo/Redo because it is not history-integrated.
- **MIDI/controller:** protocol/session/coordinator tests exercise semantic
  dispatch, feedback, learn, routing, reentrancy and persistence using simulated
  endpoints. No physical timing/electrical validation exists.
- **Frontend adapter:** broad state/callback tests, but many call adapter methods
  directly rather than pointer/touch events.
- **Generated Slint:** `run_slint_frontend(interaction_test)` invokes production
  generated callbacks across all pages and renders PPMs at 1920x1200/1024x600.
  It asserts dimensions, artifact size, model counters and a small set of state
  transitions. Screenshots are generated but neither pixel-diffed nor manually
  approved in CI; clipping and hit targets are not proven.
- **Packaging:** CMake install/preflight smoke exists. The default Nix package
  explicitly has `NEUROGENIC_ENABLE_SLINT=OFF`, so it does not prove the
  deployable production frontend closure.

## Acceptance gaps from Tasks 081–087

1. Reusable pattern editing and arrangement authoring lack production-path
   tests because the UI path is absent.
2. External capture, low-latency loop handoff, hotplug, touch and controller
   claims need hardware.
3. Slow filesystem tests demonstrate supersession but cannot reliably model an
   uninterruptible kernel/network filesystem call during shutdown.
4. There is no coordinate-level Slint pointer/touch suite or accessibility
   geometry assertion.
5. Production package tests install a CMake tree; no Nix Slint package runtime
   closure is tested.
6. Direct missing-device Slint execution can abort in libremidi rather than
   degrade, now tracked as `AUD2-004`.

## Task 095 superseding frontend evidence

See [`TASK_095_VALIDATION.md`](TASK_095_VALIDATION.md). Production generated
callbacks now include arrangement Undo/Redo and a broader representative matrix;
the software renderer asserts actual shell-strip geometry at 1920x1200 and
1024x600. A non-Slint/non-SDL conformance fixture verifies audible musical and
save/reopen behavior plus a non-reference controller inventory. Coordinate-level
touch remains unavailable in the pinned offscreen integration and physical
validation remains NOT RUN. Historical packaging and udev statements above are
superseded by Task 090.
