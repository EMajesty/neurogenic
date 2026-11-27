# Task 098F validation

Date: 2026-11-27. Baseline: Task 098E
`5f94323acff8a78059c05a4e1e508a6a7e0ced44`.

## Investigation and implementation

The existing authority was retained: `RecordingWorkflowCoordinator` owns the
configured workflow, `ApplicationService` owns stable session state and unified
history, the audio engine emits accepted timestamped events through its bounded
queue, and `TimelineService` validates and publishes immutable ordinary
timelines. Audio capture and live-loop capture remain separate existing modes.

Task 098F adds source, range, policy, loop, pass, and publication state;
presentation-neutral source/range/policy semantic actions; stable configuration
capture before waiting/count-in; and a bounded application-thread event-batch
publisher. REC can activate during PLAY or start stopped transport. Event REC
stop leaves PLAY active; STOP finalizes then stops.

Complete notes are published after their live occurrence. The ordinary prepared
scheduler therefore plays pass-one material on pass two without rewinding or
restarting transport, and later batches accumulate. Pending notes remain owned
until release/finalization. The session retains a baseline so all pass batches
form one Undo/Redo entry; explicit cancellation republishes the baseline.

Confirmed replacement is restricted to the captured pattern-track and half-open
range. It begins only with the first meaningful captured batch, so empty and
failed takes preserve existing material. Existing unrelated tracks, out-of-range
events, and automation are not rewritten. Same-tick/same-pitch collisions retain
the existing event; other pitches remain additive.

## Controller and frontend

The production V2 profile maps LOOP, SHIFT+LOOP/RANGE, SHIFT+REC/OVERDUB, and
SHIFT+REC MODE/SOURCE. The new production test passes LOOP and RANGE through an
actual `ControllerSession` and application dispatch. REC retains its shared
recording toggle and gains truthful feedback; LOOP feedback reflects the
authoritative arrangement state. No protocol inventory changed.

Slint's generated shell receives source, policy, loop, current pass, pending and
published counts. The adapter services the bounded capture drain before state
publication while recording. This is generated-property and adapter evidence,
not coordinate-level touch or physical-controller evidence.

## Deterministic acceptance

`neurogenic_loop_recording_overdub_tests` uses actual ApplicationService,
RecordingWorkflowCoordinator, TimelineService and AudioEngine paths. A 240-tick
reusable loop records a sample trigger on pass one. Because the live voice is
only 96 frames, PCM energy near frame 6,000 proves ordinary scheduler playback
on pass two. A second pass adds another rhythm and greater pass-three PCM proves
both play. REC stop leaves the transport running. The events are editable
timeline events and one Undo removes, while Redo restores, the complete take.

The same fixture proves stable unrelated-track ownership, empty replacement
preservation, meaningful range-only replacement, cancellation rollback after an
intermediate publication, semantic profile inventory, physical-control
simulation through ControllerSession, and shared RANGE focus.

## Capacity and safety

The real-time queue remains 2,048 records, the application batch remains 2,048
events, and pending ownership remains 128 notes. Successful polls clear the
batch, so continuous recording does not grow it indefinitely. Queue or ownership
overflow fails capture explicitly. The callback performs no allocation, lock,
timeline mutation, filesystem access, discovery, logging, protocol, or toolkit
work.

## Regression results

All commands ran from `/home/emaj/git/neurogenic` on 2026-11-27.

| Command | Result |
|---|---|
| `cmake --build build/dev -j2 && ctest --test-dir build/dev --output-on-failure` | PASS, 48/48 |
| `cmake --preset slint && cmake --build --preset slint -j2 && ctest --preset slint --output-on-failure` | PASS, 51/51, including 1920x1200 and 1024x600 rendered fixtures |
| `cmake --preset production && cmake --build --preset production -j4 && ctest --preset production --output-on-failure` | PASS, 51/51 |
| `cmake --preset sanitize && cmake --build --preset sanitize -j4 && ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | PASS, 48/48 under ASan/UBSan; leak detection disabled because the managed ptrace environment makes LSan abort |
| `./build/production/neurogenic_slint_benchmark` | PASS; full median 237,794 ns, incremental median 69,810 ns, assignment discovery rebuilds 1 |
| `./build/dev/neurogenic-diagnostics --headless --json` | PASS; explicit headless/null audio, no hardware claimed |
| `./build/dev/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, 67 controls, generation 1, synchronized input |
| `nix build path:.` | PASS |
| `nix build path:.#kms` | PASS; build evidence only, DRM/touch runtime NOT RUN |
| `nix flake check path:. --no-write-lock-file` | PASS; four checks including AArch64 cross derivation; expected native `aarch64-linux` system omission warning |
| `env -i PATH=/usr/bin:/bin result/bin/neurogenic-diagnostics --headless --json` | PASS; installed clean-environment runtime and dependency closure |

The first sanitizer pass exposed two task defects: a value-owned timeline
baseline enlarged stack state and the fixture referenced a temporary snapshot.
The baseline is now bounded heap-owned control-thread state and the fixture owns
its snapshot. A second sanitizer pass exposed a pre-existing full snapshot copy
inside the now-expanded history path; it now retains the existing immutable
shared snapshot. The final 48/48 sanitizer suite passed.

Physical hardware is NOT RUN. The unrelated AArch64 LinuxKMS blocker was not
investigated.

## Remaining limitations

- Simultaneous live erase and event recording remains rejected: their separate
  open edit transactions cannot yet be merged safely into session history.
- A control-thread poll that arrives after a desired loop boundary makes that
  batch audible on a later pass; the audio callback never waits for preparation.
- One stable musical-event destination is supported per session. Event
  automation recording and mandatory simultaneous multitrack capture remain out
  of scope.
- Audio capture and audio live-loop overdub retain distinct content, lifecycle,
  and history semantics; unsupported audio overdub is not claimed.
- Physical controller, display, audio/MIDI hardware, and subjective listening
  validation are NOT RUN.

## Task 098G plan

First inventory the existing integer PPQN grids, event microtiming, timeline
transforms, descriptor metadata and scheduler swing behavior. Then implement a
single authoritative configuration for input and post-recording quantization,
straight and triplet grids, strength, retained original timing, swing grid and
amount, and selection/pattern/track scopes. QUANT/SWING must use shared focus and
semantic actions. Mutations remain atomic TimelineService operations with one
unified history entry. Deterministic acceptance must compare event positions,
microtiming and scheduler/PCM playback before/after Undo, Redo, save and reopen.
