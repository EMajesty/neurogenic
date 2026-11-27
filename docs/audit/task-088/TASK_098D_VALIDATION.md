# Task 098D validation

Baseline: Task 098C `40d5b94318f9623b74d0350a8bef749cefa7947b`.

## Implemented production paths

- One application-owned, value-owned clipboard with explicit selection,
  pattern-track and reusable-pattern payloads.
- 1,024-event, 64-track and 64-selected-step bounds.
- Complete supported `MusicalEvent` property capture, relative placement and
  fresh identity/link remapping.
- Default non-destructive merge, deterministic conflicts, explicitly confirmed
  extent replacement and strict instrument compatibility.
- Atomic `TimelineTransaction` publication and one unified history entry per
  successful paste.
- COPY tap, COPY+pad and SHIFT+COPY through the reference gesture arbiter.
- Slint publication of population, summary and destination compatibility.

## Deterministic evidence

`neurogenic_musical_clipboard_tests` uses actual `ApplicationService`,
`TimelineService`, `ControllerSession` and `AudioEngine` publication paths. It
asserts full note/condition/ratchet/lock preservation, fresh IDs, sparse gaps,
later-page placement, atomic conflicts and overflow rejection, incompatible
track diagnostics, source deletion after capture, repeated transactional paste,
Undo/Redo, independent complete-pattern paste, COPY controller chords and fresh
engine activation.

`neurogenic_slint_adapter_tests` asserts the same authoritative clipboard state
reaches the production Slint publication model. Slint-enabled generated builds
exercise the generated properties and both established render resolutions.

## Exact validation results

- `cmake --build build/dev -j2 && ctest --test-dir build/dev --output-on-failure`:
  **46/46 passed**, 0 failed.
- `cmake --build build/slint -j2 && ctest --test-dir build/slint --output-on-failure`:
  **49/49 passed**, including generated Slint and rendered 1920x1200 and
  1024x600 states.
- `cmake --build build/production -j2 && ctest --test-dir build/production --output-on-failure`:
  **49/49 passed**. Ninja recovered its existing depfile-end warning during the
  initial production regeneration; the complete build and suite succeeded.
- `cmake --build build/sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure`:
  **46/46 passed** with AddressSanitizer and UndefinedBehaviorSanitizer. Leak
  detection is disabled because LeakSanitizer cannot operate under the managed
  ptrace environment.
- `./build/slint/neurogenic_slint_benchmark`: passed; full reconstruction
  median **413843 ns**, incremental median **71904 ns**, assignment discovery
  rebuilds **1**.
- `nix build path:. --no-write-lock-file`: passed.
- `nix build path:.#kms --no-write-lock-file`: passed; KMS runtime hardware is
  **NOT RUN**.
- `nix flake check path:. --no-write-lock-file`: passed, including the headless
  package/test derivation and existing AArch64 cross-compile check. Nix noted
  that the host-only command omitted the incompatible `aarch64-linux` system.
- `env -i PATH=/usr/bin:/bin ./result/bin/neurogenic-diagnostics --headless --json`
  against the clean production Nix result:
  passed without development-shell library paths.
- An attempted separate `nix build path:.#headless --out-link result-headless`
  completed without a reported build error but the managed execution did not
  materialize the requested link, so that link-specific smoke invocation
  failed with “No such file or directory”; the equivalent installed binary in
  the verified production closure passed as reported above.
- `./build/dev/neurogenic-diagnostics --controller-v2-sim-test`:
  passed protocol 1.3 negotiation, 67-control inventory and synchronized input
  state. This is simulator evidence, not physical hardware evidence.

## Partial or excluded scope

- Independent arrangement automation lanes are not copied.
- Instrument settings, effect chains, modulation graphs and sample PCM are not
  clipboard payloads.
- Cross-instrument reinterpretation and parameter remapping are rejected.
- Physical controller and touchscreen validation remain **NOT RUN**.
- Authoritative live sequencer-event recording remains **NOT IMPLEMENTED** and
  is mandatory Task 098E scope.

## Task 098E source-grounded plan

Add one bounded event-capture path to the existing recording coordinator. It
must capture a stable armed pattern/track owner before count-in or quantized
wait, timestamp sample triggers, synth notes and external MIDI input against the
authoritative transport, correlate bounded note-on/off ownership into velocity
and duration, and publish batches as ordinary timeline transactions. Explicit
event-recording mode must remain distinct from audio capture. Overdub and loop
wrapping need deterministic collision policy; repeat-generated events require
one capture point to prevent duplication. Stop/cancel/project replacement must
close or discard pending notes predictably. Each committed recording gesture
uses unified history and immutable prepared publication, followed by playback,
save/reopen and fresh-engine acceptance.
