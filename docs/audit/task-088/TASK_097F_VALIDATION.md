# Task 097F validation

## Outcome

Task 097F adds the missing appliance-owned profile-library authority without
changing controller or musical runtime ownership. Stable library entries,
explicit per-endpoint activation, atomic persistence, known-valid recovery,
offline authoring, generated Setup controls, learned-source preview and dynamic
typed-argument forms are implemented.

Physical controller, USB, display/touchscreen, PCB, audio-interface and native
AArch64 validation remain **NOT RUN**.

## Production acceptance

`neurogenic_controller_profile_library_tests` creates disconnected profiles,
duplicates and independently edits one, attaches two simulated MIDI endpoints,
activates different profiles, dispatches both into distinct authoritative
parameters, replaces one endpoint profile with generation advancement, saves a
manifest and `.ngc` files, restores them into a fresh library, corrupts a
profile, recovers from known-valid data and exercises explicit active removal.
Project data and musical history are never accessed by the library.

The existing mapping-authoring tests continue to prove direct parameter,
typed-action and Performance-role dispatch, candidate-first conflict handling,
profile serialization and stale generation invalidation. Generated Slint tests
exercise the profile list callbacks and verify that an action descriptor with
more than four fields publishes without truncation.

## Ownership and persistence

- Maximum 64 profiles; stable restricted IDs and mutable names.
- Built-ins are read-only and duplicable.
- User entries retain `.ngc` schema compatibility.
- `library.ngcl` schema 1 stores metadata and endpoint associations.
- Definition and manifest writes use temporary files and atomic rename.
- Known-valid copies support explicit and load-time recovery.
- Shared profile edits require explicit activation per endpoint.
- Active removal requires explicit authorization and never chooses a fallback.

## Mapping closure

The graphical form now exposes every descriptor argument through a scrollable
model shared by Setup and Performance. The authoring command supports 16
bounded pairs. Context, SHIFT/SELECT conditions and feedback destination are
preserved when another behavior field is edited. Learned source preview shows
the captured port/channel/number/mode. Feedback compatibility is checked
against descriptor capabilities, with the documented legacy virtual-display
exception.

## Remaining limitations

- The library manifest coordinates individually atomic files; it is not a
  transactional database across a sudden power loss between all file renames.
  The previous known-valid definition remains recoverable.
- USB-CDC active-session profile replacement is still negotiated by the
  dedicated protocol identity/profile path; the new explicit runtime activation
  operation currently targets MIDI endpoints.
- MIDI source conflict detection remains deliberately conservative when one
  physical source is proposed for multiple logical controls; authoring does not
  yet infer that arbitrary contextual scopes are mutually exclusive.
- Physical navigation is simulated through generated/shared interaction paths;
  actual hardware is NOT RUN.
- Musical capabilities listed for Task 098 remain outside this task.

## Audit status

RC2-006 and AUD2-006 are **Substantially implemented in software**. They remain
Partial only for dedicated USB runtime profile switching, exhaustive physical
navigation/hardware evidence and the conservative same-source layered-conflict
policy. Other RC2 and AUD2 findings are unchanged.

## Regression record

| Command | Result |
|---|---|
| `ctest --preset dev --output-on-failure` | PASS, 42/42, 0 failed, 0 skipped |
| `cmake --preset production` | PASS; ALSA, SDL3 and Slint enabled |
| `cmake --build --preset production -j2` | PASS |
| `ctest --preset production --output-on-failure` | PASS, 45/45, 0 failed, 0 skipped; includes both generated rendered resolutions |
| `cmake --preset sanitize` | PASS |
| `cmake --build --preset sanitize -j2` | PASS |
| `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | PASS after correction, 42/42, 0 failed, 0 skipped |
| `./build/production/neurogenic_slint_benchmark --iterations 200` | PASS; full median 212,477 ns, incremental median 66,754 ns, assignment-discovery rebuilds 1 |
| `nix build path:. --no-write-lock-file` | PASS through the system daemon; the sandboxed attempt was blocked by daemon-socket permissions |
| `nix flake check path:. --no-write-lock-file` | PASS; production, headless and AArch64 cross checks evaluated/built; native `aarch64-linux` omitted on the x86_64 host |
| `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices` | PASS; clean headless Null audio and ALSA MIDI enumeration |
| `./result/bin/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, 67 controls, synchronized generation 1 |

The first sanitizer run found a heap-use-after-free in the new adapter test:
the test retained an iterator into a copied frontend snapshot across an adapter
refresh. The argument count is now copied before refresh; the focused sanitizer
test and the complete sanitizer suite both pass. This was test-evidence
ownership, not controller runtime state, but it was corrected rather than
waived.

Physical USB, controller PCB, display/touchscreen, audio-interface latency and
native AArch64 hardware validation are **NOT RUN** and are not included in the
software pass totals.

## Task 098 sequence

1. Metronome/count-in/tap tempo.
2. Per-track solo/arm and unified internal/MIDI panic.
3. Note/triplet repeat and live erase.
4. Typed musical clipboard.
5. Contextual loop and recording/overdub controls.
6. Quantization and swing.

Each subtask must add authoritative engine behavior where missing, semantic
actions, controller binding, frontend state and deterministic PCM/MIDI or
scheduler-output acceptance.
