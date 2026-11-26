# Task 098C validation

Baseline: Task 098B `a316e3df1bdeb4e96c8bf650e5c257c6e1799975`.

## Implemented

- Fixed-capacity (8) callback-owned sample, synth and external-MIDI repeat.
- Exact 960-PPQN straight and triplet grids with absolute tick-to-frame
  conversion and a 50% gate.
- Stable owner/generation cancellation, STOP/PANIC cleanup, MIDI note-offs and
  bounded command transport.
- REPEAT+pad, REPEAT+DATA and SHIFT+REPEAT through the existing gesture arbiter.
- Transport-aware matching-note live erase in one prepared timeline transaction
  and one unified history entry.
- Application, Slint shell and bounded controller feedback projection.

## Deterministic evidence

`neurogenic_note_repeat_live_erase_tests` verifies exact MIDI note-on frames at
0/6000/12000 for sixteenths and 0/4000/8000 for sixteenth triplets with
257-frame callbacks, exact half-grid note-offs, sample PCM, synth voices,
release/PANIC cancellation, stable matching live erase, unvisited/nonmatching
preservation, and unified Undo.

Recorded-repeat insertion is **NOT IMPLEMENTED**: the current recording
authority has no live sequencer-event recording window. External-clock and
stopped-transport repeat are explicitly unavailable. Physical controller,
firmware and LED validation are **NOT RUN**.

## Exact validation results

- `ctest --test-dir build/dev --output-on-failure`: **45/45 passed**, 0 failed.
- `ctest --test-dir build/slint --output-on-failure`: **48/48 passed**, 0 failed;
  generated-component and 1920x1200/1024x600 render checks passed.
- `ctest --test-dir build/production --output-on-failure`: **48/48 passed**, 0
  failed.
- `ctest --test-dir build/sanitize --output-on-failure`: LeakSanitizer cannot
  execute under the managed ptrace environment (8 completed, 37 reported the
  same environment error). The supported retry,
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure`,
  passed **45/45** with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
- `./build/production/neurogenic_slint_benchmark`: passed; full reconstruction
  median **233175 ns**, incremental median **68498 ns**, and unchanged
  assignment discovery rebuilds **1**.
- `nix build path:. --no-write-lock-file`: passed.
- `nix build path:.#kms --no-write-lock-file`: passed (build evidence only; KMS
  display and touch hardware remain **NOT RUN**).
- `nix flake check path:. --no-write-lock-file`: passed, including the headless
  build/test and existing AArch64 cross-compile check; Nix reported that the
  host-only invocation omitted the incompatible `aarch64-linux` system output.
- `nix build path:.#headless --no-write-lock-file --out-link result-headless`
  followed by
  `env -i PATH=/usr/bin:/bin ./result-headless/bin/neurogenic-diagnostics --headless --json`:
  passed with explicit null audio and no development-shell library path.
- `./build/production/neurogenic-diagnostics --controller-v2-sim-test`: passed;
  protocol 1.3 negotiated, all 67 controls validated, and input state
  synchronized. This is simulator evidence, not physical hardware evidence.

Physical controller, firmware, display, touchscreen, audio-interface and
subjective listening validation remain **NOT RUN**.

## Task 098D recommendation

Add one typed musical clipboard authority above existing timeline transactions:
bounded step/event and pattern-track payloads, stable source metadata,
destination-relative tick placement, compatibility validation, new stable IDs,
atomic prepared publication, unified Undo/Redo, and COPY/SHIFT+COPY bindings.
Do not store mutable engine pointers or add a second history stack.
