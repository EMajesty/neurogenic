# Task 097D validation

## Outcome

Task 097D closes the highest-risk outstanding software acceptance rows without
adding another interaction or musical authority. The main production scenario
now proves that a V2 held-step gesture authors a later-page `synth.cutoff` lock,
the prepared timeline contains it, AudioEngine applies its exact value, the
stored base and second reusable pattern remain unchanged, and save/reopen
produces equivalent fresh-engine behavior.

Physical controller, USB, display/touchscreen, PCB, audio-interface and native
AArch64 execution are **NOT RUN**.

## Defects found and corrected

1. **Sound pad semantic mismatch:** contextual Sound pads advertised notes but
   used sample-pad trigger/release. They now use captured synth-instance
   `instrument.note-on.v1` / `instrument.note-off.v1`. The test asserts audible
   PCM, destination cancellation, decay to silence and stale-release rejection.
2. **Interrupted double-tap latch:** an unrelated non-chord gesture could leave
   a prior SHIFT tap eligible for latching. Any intervening non-modifier input
   now clears that sequence; input while SHIFT is held consumes only the tap,
   retaining momentary SHIFT effectiveness.
3. **Selection labels without operations:** shifted Macro and Scene pads now use
   stable, non-mutating `macro.select.v1` and `macro.scene-select.v1` actions.
4. **Selection-relative audition:** sample and slice preview actions accept
   optional explicit stable targets; contextual pads always capture them.
5. **Insufficient feedback assertion:** the protocol fixture now compares the
   complete final logical LED projection after bounded overflow and reconnect.

## Lock playback and persistence

`neurogenic_reference_controller_tests` creates patterns 91 and 92 for track
42, edits step 17 from V2 page two, and obtains the lock only through
`ControllerSession`/`InputGestureArbiter`. It verifies no deferred toggle, a
single history entry, stable PatternId/TrackId/EventId, unchanged pattern 92 and
unchanged registry base. `compile_timeline()` proves prepared-plan inclusion.
A fresh AudioEngine reports the exact applied timeline parameter value through
bounded relaxed atomics written in its existing event loop. The project
serializer/loader preserves IDs/base/value and another fresh engine repeats the
assertion. Existing lock semantics hold the automated value until a later
automation/base publication; they do not mutate the stored project base.

## History, cancellation and multi-step evidence

Multiple encoder deltas commit one edit group and one Undo entry. Modal opening
during a later edit submits the existing cancel action; assertions prove the
original value, inactive group, unchanged Undo depth, no tap, and rejected stale
release. Two selected steps receive one grouped compatible property edit while
the other reusable pattern remains unchanged. Broader cross-page lock-removal
and invalid-selection pruning combinations remain ledgered as test gaps rather
than claimed complete.

## Notes, momentaries and profile replacement

Stable Sound note press/release and destination cancellation now have PCM
evidence. Existing Performance Fill press/release, latch independence,
bank-change cancellation, mapping candidate replacement and session generation
advancement retain their Task 096E/096F tests. External-MIDI note interruption,
every momentary cancellation trigger, and the coordinator-owned multi-profile
library remain open.

## Protocol recovery and feedback

Protocol 1.3 continues to cover generation negotiation, initial state sync,
lost Fill release reconciliation, stale generation rejection, heartbeat and
reconnect. Task 097D adds a 16-packet host-output constraint, drains until the
authoritative revision is delivered, and compares the complete final logical
LED array with an unconstrained host. Disconnect/renegotiation repeats the same
equality. These are production byte-stream simulator results, not physical LED
or USB results.

## Modal, physical-navigation and generated evidence

The existing Task 097A/097B production tests remain authoritative for shared
modal generations, focus restoration, all 48 Performance chooser positions,
Setup proposals and generated callback/layout coverage. This task adds actual
controller gesture/engine and protocol reliability evidence. It does not add a
coordinate-driven Slint input injector, so complete simulated traversal of
every Performance/Setup field remains Partial and physical touch remains NOT
RUN.

## Architecture and real-time safety

The only callback observation added is a count and last-value pair of relaxed
atomics. No callback allocation, lock, discovery, logging, filesystem access,
toolkit call or protocol processing was added. Parameter authority remains the
registry/application service; scheduling remains the existing timeline engine.

## Audit status

- RC2-001: **Substantially implemented in software**, Partial for exhaustive
  modal/physical rows.
- RC2-003: **Partial**: supported bindings and stable note release are proven;
  Task 098 capabilities remain missing.
- RC2-004: **Substantially implemented in software** with scheduled and
  persisted lock evidence; remaining multi-step edge matrix is explicit.
- RC2-005: **Substantially implemented in software**; broader momentary state
  discovery and physical traversal remain open.
- RC2-007: **Software protocol contract implemented**, physical validation open.
- AUD2-007: **Partial** until remaining generated traversal and physical work.
- RC2-006/AUD2-006: unchanged **Partial** for Task 097F profile-library scope.

## Remaining milestones

Task 097F should add only the coordinator-owned profile library and supported
extended mapping presentation. Task 098 should be split by musical capability:
metronome/count-in/tap tempo; repeat; clipboard; solo/arm; unified panic; live
erase; loop/recording/overdub; quantization/swing. Each subtask requires an
authoritative semantic action, engine behavior where applicable, controller
binding, presentation and deterministic output acceptance. Physical bring-up
remains an independent hardware workstream.

## Regression record

| Validation | Exact command | Result |
|---|---|---|
| Development | `cmake --build --preset dev -j2 && ctest --preset dev --output-on-failure` | PASS, 41/41 tests |
| Slint-enabled | `cmake --preset slint && cmake --build --preset slint -j2 && ctest --preset slint --output-on-failure` | PASS, 44/44 tests; generated artifacts at 1920x1200 and 1024x600 |
| Production | `cmake --preset production && cmake --build --preset production -j2 && ctest --preset production --output-on-failure` | PASS, 44/44 tests |
| ASan/UBSan | `cmake --preset sanitize && cmake --build --preset sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | PASS, 41/41 tests; LeakSanitizer disabled for the established ptrace environment limitation |
| Frontend benchmark | `./build/slint/neurogenic_slint_benchmark --iterations 200` | PASS; full median 230,981 ns, incremental median 70,742 ns, assignment discovery rebuilds 1 |
| Production derivation | `nix build path:. --no-write-lock-file` | PASS after rerun outside the restricted sandbox; initial attempt could not contact the Nix daemon |
| Flake checks | `nix flake check path:. --no-write-lock-file` | PASS |
| Installed runtime | `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices` | PASS; Null audio, ALSA raw-MIDI backend, no physical MIDI endpoints |
| Controller simulator diagnostics | `./result/bin/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, manifest valid, 67 controls, synchronized generation 1; physical hardware false |

An exploratory invocation of the nonexistent
`./result/bin/neurogenic-diagnostics --controller-v2-self-test` option returned
usage and failed as expected; the supported `--controller-v2-sim-test` command
above is the diagnostic acceptance command. Physical USB/MCU, PCB, display,
touchscreen, audio-interface, latency and native AArch64 validation remain
**NOT RUN**.
