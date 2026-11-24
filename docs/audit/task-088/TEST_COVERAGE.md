# Test and validation audit

## Commands executed at `d8172a7`

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
