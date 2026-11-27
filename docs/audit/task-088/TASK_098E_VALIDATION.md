# Task 098E validation

## Implemented evidence

The production `AudioEngine` now captures accepted sample, synth, external MIDI,
and note-repeat events at their actual audio dispatch point. A 2,048-record SPSC
queue transfers value-owned events to `ApplicationService`; no callback timeline
mutation or allocation was introduced. Stable sessions retain PatternId,
TrackId, instrument identity/instance, extent, MIDI filter, generation, and
start tick.

`neurogenic_live_event_recording_tests` proves nonzero reusable-pattern sample
capture, polyphonic synth duration correlation, zero-velocity MIDI note-off,
endpoint/channel filtering, straight repeat timing and 50% gates, unrelated
pattern preservation, controller REC MODE/REC operation, unified Undo/Redo,
Task 098D clipboard reuse, save/reopen, and fresh-engine PCM playback.

Generated Slint publication exposes recording mode, phase, active state, and
pending event count. The existing rendered tests cover both reference
resolutions. Physical controller, MIDI, display, and touchscreen evidence is
**NOT RUN**.

## Exact validation

All commands ran from `/home/emaj/git/neurogenic` on 2026-11-27.

| Command | Result |
|---|---|
| `cmake --build build/dev -j2 && ctest --test-dir build/dev --output-on-failure` | PASS, 47/47 tests |
| `cmake --preset slint && cmake --build build/slint -j2 && ctest --test-dir build/slint --output-on-failure` | PASS, 50/50 tests, including both rendered resolutions |
| `cmake --preset production && cmake --build build/production -j2 && ctest --test-dir build/production --output-on-failure` | PASS, 50/50 tests |
| `cmake --preset sanitize && cmake --build build/sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure` | PASS, 47/47 tests under ASan/UBSan |
| `./build/production/neurogenic_slint_benchmark` | PASS; full median 237,192 ns, incremental median 70,592 ns, assignment discovery rebuilds 1 |
| `./build/dev/neurogenic-diagnostics --headless --json` | PASS; explicit null/headless audio, ALSA linked, no physical devices claimed |
| `./build/dev/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, 67 controls, generation 1, synchronized input state |
| `nix build path:. --no-write-lock-file` | PASS |
| `nix build path:.#kms --no-write-lock-file` | PASS (build only; physical KMS runtime NOT RUN) |
| `nix flake check path:. --no-write-lock-file` | PASS; production, KMS, headless, SDL and AArch64 cross derivations evaluated and both host checks built; Nix reported the expected omission of the incompatible native `aarch64-linux` system |
| `env -i PATH=/usr/bin:/bin result-production/bin/neurogenic-diagnostics --headless --json` | PASS; installed closure resolved without development-shell library paths (benign missing `XDG_RUNTIME_DIR` messages in the deliberately empty environment) |

The first development-suite attempt exposed a stack-size regression: embedding
the 2,048-record capture queue directly in every `AudioEngine` enlarged the
monolithic test's stack object enough to fault. The queue is now allocated once
when the engine is constructed and remains fixed-capacity and allocation-free
in the callback. The targeted regression and every full suite above passed
after that correction.

Physical controller, display/touch, audio interface and MIDI hardware tests:
**NOT RUN**. The byte-stream controller result is simulator evidence only.

## Remaining limitations

- Commit occurs at recording completion, not after each loop pass.
- External MIDI input is callback-aligned, not hardware-timestamp calibrated.
- MIDI CC/automation capture and simultaneous live erase are unavailable.
- One stable destination is captured per session; simultaneous multitrack event
  recording is unavailable.
- Task 098F owns full LOOP/RANGE, REC/OVERDUB, mode/source/destination forms,
  and replace policy. Task 098G owns quantization and swing.

## Task 098F plan

Extend the existing recording workflow rather than adding another coordinator:

1. expose descriptor-backed REC MODE/SOURCE and stable destination selection;
2. add contextual LOOP/RANGE with a validated pattern-relative recording window;
3. expose non-destructive overdub versus confirmed replacement policy;
4. decide whether bounded per-pass publication is required for next-pass
   monitoring while retaining one edit group per session;
5. reconcile audio capture, musical events, and live-loop availability in one
   shared form and controller projection;
6. add deterministic loop-wrap, replacement, count-in, cancellation, history,
   persistence, controller, and generated-component acceptance.
