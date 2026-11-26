# Task 098B validation

## Capability investigation and ownership

The mixer already owned bounded track/group solo routing, but the legacy mixer
solo toggle was ordinary mixer configuration and timeline MIDI dispatch did
not share effective audibility. `ApplicationService` now owns transient stable
track solo and arm sets. The existing prepared mixer performs PCM isolation;
the engine consults the same active configuration before note/clip/MIDI start
dispatch. `RecordingService` remains the sole audio capture authority and
`AudioEngine`/`MidiOutputScheduler` remain the sound cleanup authorities.

## Implemented semantics

- additive per-track solo; explicit mute wins; clear does not alter mute;
- derived `effectively_audible` snapshot state and group-path-aware scheduling;
- bounded multiple arm preparation with exactly one accepted audio capture
  owner through the existing track-channel recorder;
- captured owner survives selection changes; pre-capture disarm cancels;
- unified `transport.panic.v1`, next-callback atomic delivery and monotonic
  acknowledgement;
- internal sample/synth reset, queued trigger removal, timeline MIDI note-off,
  sustain release, All Notes Off/All Sound Off, stopped transport and explicit
  resume requirement;
- SOLO/ARM stable pad chords, tap-latched mode, SHIFT clear, SHIFT+STOP panic,
  and modifier-dependent pad feedback;
- Slint track mute/solo/arm/effective-audibility projection and generated
  callbacks for solo, arm, clear, and panic.

Solo/arm/panic are transient and excluded from project history and persistence.
Project save explicitly clears transient solo bits from the serialized mixer.

## Deterministic evidence

- `tests/solo_arm_panic_tests.cpp`: authoritative action state, additive solo,
  panned two-track AudioEngine PCM isolation, multi-arm rejection, captured
  recorder owner, explicit external-MIDI audio-arm rejection, cancellation,
  internal voice cleanup, MIDI safety queue,
  saturated transport-independent panic delivery and acknowledgement, plus
  zero post-panic PCM after clearing standalone and rack effect tails.
- `tests/reference_controller_tests.cpp`: actual Reference Controller V2
  SOLO+pad, SHIFT+SOLO, ARM+pad, SHIFT+STOP, consumed chord behavior and
  authoritative engine state.
- `src/ui/slint_frontend.cpp` generated-component fixture: solo/arm/clear/panic
  callbacks and authoritative round trips; rendered tests cover 1920x1200 and
  1024x600.

## Limitations

- The recorder still supports one simultaneous audio capture. Track arm maps
  to the existing track-channel capture source; it is not a new event recorder.
- Pre-fader sends retain existing pre-fader behavior during track solo.
- External devices may ignore MIDI channel-mode panic messages.
- No physical controller, encoder LEDs, touchscreen, audio interface, or
  worst-case panic latency was tested: physical validation is **NOT RUN**.
- Optional encoder pushes/RGB destinations require a future negotiated
  protocol inventory extension; protocol 1.3 remains unchanged.
- Note/triplet repeat, repeated-event recording and live erase remain Task
  098C.

## Regression results

All commands below were run from the repository root on 2026-11-26:

- `cmake --build --preset dev -j2` — PASS.
- `ctest --preset dev --output-on-failure` — PASS, 44/44, 0 failed.
- `cmake --build --preset slint -j2` — PASS.
- `ctest --preset slint --output-on-failure` — PASS, 47/47, 0 failed;
  generated callbacks and rendered 1920x1200/1024x600 fixtures included.
- `cmake --build --preset production -j2` — PASS.
- `ctest --preset production --output-on-failure` — PASS, 47/47, 0 failed.
- `cmake --build --preset sanitize -j2` — PASS.
- `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure`
  — PASS, 44/44, 0 failed.
- `build/production/neurogenic_slint_benchmark` — PASS; final full median
  226,823 ns, incremental median 68,247 ns, assignment discovery rebuilds 1.
- `nix build path:. --no-write-lock-file` — PASS, production derivation.
- `nix build path:.#kms --no-write-lock-file` — PASS, LinuxKMS derivation.
- `nix flake check path:. --no-write-lock-file` — PASS.
- `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices`
  — PASS; clean runtime linked ALSA 1.2.15.3, explicit null headless audio,
  no physical MIDI endpoints discovered.
- `env -u LD_LIBRARY_PATH ./result/bin/neurogenic-diagnostics
  --controller-v2-sim-test` — PASS; protocol 1.3, 67 controls, negotiated and
  synchronized, `physical_hardware=false`.

No tests were skipped by these suites. Physical controller, display,
touchscreen, audio interface, external MIDI device and subjective listening
validation remain **NOT RUN**. The unrelated AArch64 LinuxKMS cross-build
blocker was intentionally not investigated in Task 098B.

During final review, the new acceptance test initially exposed that an
external-MIDI-only track could be armed for an audio track-channel capture.
The arm validator now rejects that incompatible owner and the final suites
above include the regression. One development-suite run made concurrently
with outstanding flake-check workers also timed out in the Slint adapter test;
the isolated load condition was removed and the complete final development,
Slint, production and sanitizer suites all passed with no disabled tests.

## Task 098C recommendation

Use the existing integer musical clock and prepared scheduler to add a bounded
repeat owner keyed by stable track/note/controller generation. Support straight
and triplet divisions, deterministic phase and velocity, both release orders,
context/disconnect/panic cancellation, internal synth/sample and external MIDI
note-off safety, and transactional quantized insertion of repeated events when
recording. Live erase should remove only matching stable events in the active
recording window through unified history. Do not reuse UI repeat timers or add
a second scheduler.
