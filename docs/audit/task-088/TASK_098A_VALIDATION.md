# Task 098A validation

## Implemented

- Transport-derived, sample-aligned synthesized click with accented meter
  downbeat and rational non-integral beat boundaries.
- Master-monitor fallback after every recorder tap, with independent level.
- Off/1/2/4-bar scheduled recording count-in and STOP/cancel recovery.
- Bounded monotonic tap estimator and authoritative tempo commit.
- Semantic actions, parameter metadata, Reference V2 CLICK/TAP bindings,
  application state, Slint adapter state and generated status presentation.
- Functional visual-language specification in `docs/VISUAL_LANGUAGE.md`.

## Evidence

`neurogenic_metronome_tests` uses real `AudioEngine`, `RecordingService`,
`TimelineService` and `ApplicationService`. It asserts PCM output, exact click
counts across 257-frame callbacks, recording-tap isolation, count-in override
and termination, deterministic 120 BPM taps, inactivity reset, delayed capture,
and STOP cancellation. Existing reference-controller coverage continues to
assert TAP+DATA authoritative adjustment; CLICK and deferred TAP bindings are
resolved through the production profile.

## Limitations

- Dedicated cue/headphone routing is absent; `master-monitor` is the truthful
  fallback.
- Live-loop count-in and comprehensive external-clock count-in are unavailable.
- Metronome/count-in preferences are transient; tempo remains project-owned.
- Broad palette migration is intentionally not implemented.
- Physical audio, controller, LED, G10 and display validation: **NOT RUN**.

## Task 098B source-grounded scope

The mixer has track mute and authoritative track identities, but per-track solo
and record-arm require state and routing semantics. `midi.panic.v1` reaches the
external scheduler; engine voice cleanup exists for transport/reset paths but
there is no unified public internal-voice plus MIDI panic contract. Task 098B
should add bounded per-track solo/arm state, recording-owner validation, and a
single semantic panic operation with deterministic PCM voice-stop and emitted
MIDI all-notes-off evidence. It should reuse mixer, recording coordinator,
ControllerSession and current feedback rather than add another authority.

## Exact regression results (2026-11-26)

- `cmake --build --preset dev -j2`: PASS.
- `ctest --test-dir build/dev --output-on-failure`: PASS, 43/43.
- `cmake --build --preset slint -j2`: PASS.
- `ctest --test-dir build/slint --output-on-failure`: PASS, 46/46;
  generated software renders passed at 1920x1200 and 1024x600.
- `cmake --build --preset production -j8`: PASS (Ninja recovered an
  existing prematurely terminated build database before rebuilding).
- `ctest --test-dir build/production --output-on-failure`: PASS, 46/46.
- `cmake --build --preset sanitize -j8`: PASS.
- `ctest --test-dir build/sanitize --output-on-failure`: NOT USABLE with
  default LeakSanitizer because the managed environment runs under ptrace (19
  tests reached; 16 reported the same LSan environment failure before the run
  was stopped).
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize
  --output-on-failure`: PASS, 43/43 with ASan and UBSan active. Leak checking
  alone is an environment limitation.
- `build/slint/neurogenic_slint_benchmark`: PASS; full median 237,604 ns,
  incremental median 71,344 ns, assignment discovery rebuilds 1.
- `nix build path:.`: PASS through the system daemon. The first sandboxed
  attempt failed only with `Operation not permitted` opening the daemon socket.
- `nix build path:.#kms`: PASS; build verification only, no DRM runtime.
- `nix flake check path:.`: PASS, including production, headless, KMS and
  AArch64 cross-compile checks; native `aarch64-linux` was omitted as an
  incompatible host system.
- `/nix/store/cmrx1wzfnk7yahjarlac64s2sfv2h4v0-neurogenic-0.1.0/bin/neurogenic-diagnostics
  --headless --json`: PASS; clean installed closure selected explicit null
  audio and reported ALSA 1.2.15.3 linked.
- `build/production/neurogenic-diagnostics --controller-v2-sim-test`: PASS;
  protocol 1.3, 67 controls, synchronized generation 1.

Physical KMS, touchscreen, ALSA, MIDI, USB controller, acoustic click level,
cue routing and LED appearance remain **NOT RUN**.
