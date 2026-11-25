# Task 095 frontend verification and client independence

## Scope and environment

Evidence was collected on an x86_64 AMD Ryzen 7 7800X3D NixOS host with the
CMake `slint` debug preset, GCC 15.3 and pinned Slint 1.19.0. The workload uses
16 tracks, 16 events per track, 64-frame callbacks and 256 iterations. These
are development-host measurements, not appliance measurements.

## Identical-workload measurements

| Path | Before median / p95 / p99 / max (us) | After median / p95 / p99 / max (us) |
|---|---:|---:|
| Unchanged refresh | 65.983 / 78.888 / 86.922 / 155.150 | 50.384 / 56.415 / 65.983 / 152.164 |
| Forced page reconstruction | 129.161 / 142.205 / 158.526 / 226.563 | 113.662 / 125.214 / 130.003 / 160.510 |
| Playback telemetry | 63.328 / 71.162 / 89.858 / 120.706 | 62.647 / 67.967 / 73.497 / 81.362 |

The unchanged median fell 23.6% and forced reconstruction median fell 12.0%.
Playback telemetry was essentially unchanged at the median. Structural checks
still prove no telemetry-triggered full reconstruction (`259` full, `257`
telemetry, `255` unchanged skips).

| Post-change operation | median / p95 / p99 / max (us) |
|---|---:|
| Compatibility `snapshot()` value copy | 35.677 / 43.631 / 50.815 / 70.702 |
| Same-revision `snapshot_shared()` | 18.384 / 21.901 / 26.770 / 184.675 |
| Complete discovery | 231.302 / 263.893 / 327.000 / 521.644 |
| Descriptor-backed parameter resolution | 306.903 / 366.564 / 458.496 / 553.474 |

`snapshot_shared()` publishes one immutable copy per application revision and
reuses it for same-revision clients. `snapshot()` remains for compatibility.
Changed revisions still copy complete state; discovery and resolution remain
measurable. AUD2-012 is therefore **partially resolved**.

## Generated callback coverage

The production generated `PerformanceWindow` now exercises: all page callbacks,
play/stop, contextual-bank change, logical pad assignment; sequencer page and
advanced-property edits; reusable-pattern creation; arrangement create, move,
Undo and Redo with authoritative clip-position checks; Sound preset naming;
modulation-source creation and parameter adjustment; macro and scene naming;
MIDI route and mapping-target callbacks; live-loop configure/arm/select/cancel;
and UTF-8 keyboard editing.

Sample-processing publication, successful overdub/replacement, multi-asset and
persistent-preset repair, MIDI-learn confirmation and all mapping properties
remain primarily service/adapter tested.

## Layout and input evidence

Production components are software-rendered at 1920x1200 and 1024x600. Tests
assert exact framebuffer size and usable nonzero geometry for the actual status,
page, contextual-encoder and contextual-function strips. Populated page,
recording and keyboard PPMs are retained as failure artifacts.

This is structured shell validation, not a reviewed visual golden. The pinned
Slint 1.19 C++ offscreen integration has no stable public coordinate-level
pointer/touch injection path. Coordinate input, physical touch, GPU rendering,
focus order and comprehensive modal/scroll reachability remain unverified.
AUD2-011 is **partially resolved**.

## Independent-client conformance

`neurogenic_client_independence_tests` is a non-Slint, non-SDL production-path
client. It discovers capabilities, consumes copied state, invokes typed actions,
adjusts a descriptor-backed parameter, performs Undo/Redo, authors and arranges
a reusable pattern, verifies audible synth PCM, saves and reopens the project.

Its simulated controller deliberately has three relative encoders, five
buttons, one absolute fader, no pads and no page controls. It validates a
profile, exercises typed action and parameter bindings, feedback, disconnect
and reconnect. The reusable-pattern path is also the bounded tracker-like
workflow proof; it does not store a competing client-owned grid.

Documentation verification corrected the frontend example to the implemented
`master:main` / `mixer.master.gain` binding used by the conformance test. Its
host construction/error helpers remain explicitly illustrative. Relative links
in all Task 095-touched design and audit documents were checked and resolve.

## Remaining limitations

- Complete changed-revision snapshots and complete discovery remain.
- Descriptor caching was not added without a safe independent invalidation key.
- Generated callbacks are representative rather than exhaustive.
- PPMs are generated but not aesthetically judged.
- Coordinate input and all physical validation are **NOT RUN**.
- Reference Controller V2 remains Task 096 work.

## Regression results

Final-code commands on the environment above:

| Command | Result |
|---|---|
| `cmake --build --preset dev -j2 && ctest --preset dev --output-on-failure` | 34/34 passed |
| `cmake --build --preset slint -j2 && ctest --preset slint --output-on-failure` | 37/37 passed, including both rendered sizes |
| `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | 34/34 passed |
| `./build/slint/neurogenic_slint_benchmark` | passed; measurements above |
| `nix build path:. --no-write-lock-file` | passed; default production Slint package |
| `nix flake check path:. --no-write-lock-file` | all 17 checks passed; AArch64 compile check included, native AArch64 omitted on x86_64 |
| clean-environment installed device listing, diagnostics JSON and discovery JSON | passed without `LD_LIBRARY_PATH` |

No physical test was executed.

## Recommended Task 096 sequence

1. Define stable V2 logical control identities plus input epochs, press/release
   ownership and reconnect reset; keep geometry in the profile/presentation.
2. Implement modal precedence, SHIFT/momentary/latch arbitration and static-page
   routing without embedding counts in application services.
3. Compose the top-page, right 2x4 parameter and lower-function screen zones
   from the existing context descriptors, retaining alternative layouts.
4. Bind navigation, editing and global transport clusters to typed semantic
   operations, then complete descriptor-driven contextual banks.
5. Add held-step parameter-lock gestures and project-owned Performance
   assignments with stable targets.
6. Close manual mapping argument/conflict/feedback gaps and add authoritative RGB
   feedback plus firmware protocol conformance tests.
7. Run generated, simulated-controller and physical checklists separately;
   ergonomics, rollover, illumination and latency remain physical evidence.
