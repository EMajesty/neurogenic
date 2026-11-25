# Task 097C validation

## Outcome

Task 097C replaces the remaining global V2 pad interpretation with a shared,
destination-specific projection. `resolve_controller_context()` returns eight
encoder and eight function descriptors; `resolve_controller_pads()` returns
sixteen pad descriptors containing role, stable target, press/release semantic
actions, arguments, selection/activity and availability. ControllerSession and
Slint consume the same destination identity. Unsupported entries are explicit
and cannot dispatch.

This is software validation. Controller PCB, USB CDC hardware, touchscreen
coordinates, illumination, latency and ergonomics are **NOT RUN**.

## Sixteen-destination closure matrix

“Unavailable” means the intended position is represented but no authoritative
registered operation or stable target exists. It is not a placeholder action.

| Destination | Encoder bank | Primary functions | SHIFT functions | Pad primary / SHIFT | Production evidence | Remaining blocker |
|---|---|---|---|---|---|---|
| Performance | Project P1–P8 assignments | Project F1–F8 | Project secondary F1–F8 | Stable launch / assignment select | Performance-bank tests plus exhaustive descriptor matrix | Temporary checkpoint feature |
| XY | No invented parameters | Shared safe Performance actions where assigned | Explicit secondary assignments | Stable launch / select | Descriptor matrix | No separate XY pad engine |
| Sequencer | Seven step properties + compatible lock | Page/reset/history/pattern | Selection/lock/history/page | Toggle / stable selection; held modifiers retain precedence | Reference-controller and contextual tests | Stronger scheduled lock save/reopen evidence in 097D |
| Song | Intended clip/position/duration/offset/track/repeat/loop roles; unavailable without descriptors | Existing clip/pattern operations where complete | Undo/Redo; other slots unavailable | Pattern launch / select | Descriptor matrix | Clip-selection service and unsupported transforms |
| Sample | Start/end/loop/tune/gain/pan/zoom roles | Preview/select/process where valid | Undo/Redo | Selected-slot audition / slot select | Descriptor and adapter tests | Per-slot preview action with explicit slot target |
| Slice | Selection/start/end/tune/gain/pan/mode/zoom roles | Preview/stop/delete; argument-requiring algorithms gated | Undo/Redo | Selected-slice audition / slice select | Descriptor matrix | Explicit-target audition and missing algorithms |
| Sound | Established synth order, descriptor fallback for other instruments | Existing query/navigation; incomplete authoring gated | Undo/Redo | Immediate note with captured release / unavailable selection | Protocol encoder regression and descriptor release test | Shared instrument-bank selection metadata |
| Presets | DATA/focus owned; no invented numeric bank | Query/apply where safe; proposal operations gated | Undo/Redo | Inactive until stable preset selection exists | Descriptor matrix | Shared preset-library stable selection |
| Mixer | Gain/pan/sends/returns/master/external FX | Track/group selection with stable args; unavailable categories explicit | Undo/Redo | Track selection / current alternate unavailable | Descriptor matrix | Stable group/aux pad inventory; solo/arm semantics |
| FX | Discovered effect/external-FX descriptors | Query; object operations gated without selected instance args | Undo/Redo | Inactive until stable effect selection service exists | Descriptor matrix | Selected effect instance identity in shared state |
| Modulation | Discovered source descriptors; selected-route compatibility fallback | Existing selected-route depth/enable, otherwise source functions gated | Undo/Redo | Inactive until source selection action exists | Modulation production regression | Stable selected source action/metadata |
| Modulation Routing | Route roles and authoritative selected depth | Selected-route depth/enable with typed ID | Undo/Redo | Stable route selection / same safe selection | Modulation and contextual tests | Additional route property descriptors |
| Macros | Up to eight stable macro values | Existing operations gated by stable args | Undo/Redo | Explicit invoke / selection unavailable | Descriptor matrix | Non-mutating macro selection service |
| Scenes | Stable scene selection field | Recall/update/duplicate/rename/delete with captured scene args | Undo/Redo | Recall / selection unavailable | Descriptor matrix | Non-mutating scene selection service and confirmation bridge coverage |
| Browser | Focus/DATA contract; no musical parameters | Previous/next/parent/open/page | Undo/Redo only where registered | Inactive | Navigation and descriptor tests | Additional explicit filter/sort actions |
| Setup | Focus/DATA contract; no musical parameters | Shared confirm entry points for supported subviews | Undo/Redo slots unavailable when not meaningful | Inactive | Descriptor and Task 097B generated coverage | Coordinator-owned profile library |

Every row is exercised for P1–P8, F1–F8 and PAD1–PAD16 in both modifier layers
by `neurogenic_contextual_coverage_tests`. The test verifies descriptor content
and dispatch safety, not merely labels.

## Ownership and compatibility

- Semantic destination identity is separate from reused Slint page identity.
- V2 pad dispatch captures the descriptor selected at press time. Sound and
  Slice momentary operations retain release actions; modal/context/disconnect
  cancellation uses the existing session cancellation path.
- Generic MIDI and legacy dedicated profiles retain `PerformanceGrid` behavior.
- No mutable engine pointer or physical wire identity enters a descriptor.
- No work was added to the audio callback.

## Frontend and cache evidence

Slint copies destination-owned pad metadata beside the existing context fields.
A regression navigates to shifted Song, checks `song:*` context identity, then
returns to Sequencer. The deterministic benchmark (`--iterations 200`) reported:

| path | median | p95 | p99 | maximum |
|---|---:|---:|---:|---:|
| idle | 53,269 ns | 69,479 ns | 77,755 ns | 296,143 ns |
| full | 232,815 ns | 329,716 ns | 398,915 ns | 428,450 ns |
| incremental | 69,250 ns | 108,844 ns | 136,515 ns | 167,933 ns |

The run retained `assignment_discovery_rebuilds=1`, 259 full rebuilds, 257
telemetry updates and 255 skips. Task 097B's same Slint-preset report measured
129,552 ns full and 67,786 ns incremental medians. Incremental publication is
effectively unchanged within environmental variation; full destination changes
are 79.7% slower because they now resolve and copy the additional sixteen-pad
destination projection. A bounded 32-entry metadata cache prevents unchanged
same-destination reconstruction but cannot hide this benchmark's intentional
page change on every full sample. This is a measured remaining optimization,
not presented as an improvement or converted into a brittle CI threshold.

## Exact regression command ledger

| Command | Result |
|---|---|
| `cmake --build --preset dev -j2` | passed |
| `ctest --test-dir build/dev --output-on-failure` | 41/41 passed |
| `cmake --preset slint && cmake --build --preset slint -j2` | passed, Slint 1.19 |
| `ctest --test-dir build/slint --output-on-failure` | 44/44 passed; rendered 1920x1200 and 1024x600 |
| `cmake --preset sanitize && cmake --build --preset sanitize -j2` | passed |
| `ctest --test-dir build/sanitize --output-on-failure` | environment failure: LeakSanitizer cannot run under ptrace |
| `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure` | ASan/UBSan 41/41 passed |
| `cmake --preset production && cmake --build --preset production -j2` | passed |
| `ctest --test-dir build/production --output-on-failure` | 44/44 passed |
| `./build/slint/neurogenic_slint_benchmark --iterations 200` | passed; values above |
| `nix build path:. --no-write-lock-file` | sandbox daemon denied, approved daemon rerun passed |
| `nix flake check path:. --no-write-lock-file` | passed; production, headless and AArch64 cross checks |
| `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices` | passed; Null audio, ALSA raw MIDI, no physical endpoints |

Generated rendering, protocol simulation and headless enumeration are software
evidence. Physical USB, controller PCB, audio/MIDI devices and touchscreen
interaction were not present and remain **NOT RUN**.

## Audit status and remaining blockers

- RC2-004: substantially implemented in software for supported capabilities.
- RC2-001/003: improved by destination-owned stable pad/release semantics; still
  partial for missing shared selections and dedicated musical capabilities.
- AUD2-007: partial. Exhaustive descriptor accounting exists, but physical and
  stronger engine-observable acceptance are outstanding.
- RC2-006/AUD2-006 remain partial for the coordinator-owned profile library and
  extended profile fields; no frontend-owned workaround was added.

Task 097D should close controller-authored lock scheduling and persistence,
held-step stale/cancel cases, grouped editing, note/momentary release across
replacement, feedback overflow convergence, physical-navigation simulation and
generated reachability. Missing musical algorithms remain a separate milestone.
