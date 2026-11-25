# Task 097A validation

## Result

Task 097A establishes one bounded, presentation-neutral interaction record in
`ApplicationState`. It identifies the active destination/subview, stable focus
role, navigation scope, DATA role, and at most one active modal with a stable
target and monotonically increasing generation. Slint retains geometry,
scrolling and toolkit focus; application services retain musical authority.
Direct semantic clients do not have to participate.

Modal opening captures the target and previous focus. Destructive modals start
on Cancel. ENTER/BACK emit a single request owned by that modal, and exact
generation matching is required to close it. Closing restores the previous
valid focus. Page controls are not global modal exceptions; SHIFT, navigation,
STOP and its safety behavior are. Recovered input-state bitmaps never synthesize
confirmation gestures.

The V2 profile maps shifted directions to page/first/last navigation,
SHIFT+BACK to destination root and SHIFT+ENTER to the shared Save request.
Repeatable navigation dispatches once immediately, then repeats at the bounded
rate. Browser DATA and directions are interpreted by the production adapter.
No-context DATA is unavailable rather than falling back to tempo.

## Production acceptance evidence

`neurogenic_interaction_navigation_tests` uses the protocol-1.3 byte-stream
simulator, `ControllerHost`, real V2 session, application service and production
Slint adapter. It verifies physical routing, safe modal focus, captured pattern
deletion despite selection change, page blocking, cancellation, confirmation,
Undo, Browser DATA navigation, physical Save As/persistence, and reconnect
cleanup. Generated-component testing separately verifies shared focus, DATA and
modal publication plus focus restoration.

## Reachability matrix

| Destination | Hardware-reachable operation | Remaining limitation |
|---|---|---|
| Performance | bank/assignment selection | complete position authoring is 097B |
| XY | selection roles, parent/root | detailed grid traversal remains open |
| Sequencer | pattern/track/step focus, confirmation | representative path covered |
| Song | clip/list focus and fields | complete spatial clip traversal remains open |
| Sample | slot/parameter focus | waveform handles remain contextual/touch |
| Slice | slice/parameter focus | waveform geometry remains local |
| Sound | instrument/preset lists | complete preset forms remain 097B |
| Presets | preset list, open/cancel | text uses shared keyboard |
| Mixer | channel/parameter focus | supported fields only |
| FX | effect/parameter focus | routing graph traversal remains open |
| Modulation | source/parameter focus | route forms need generated wiring |
| Route | route/field focus | typed destination form incomplete |
| Macro | macro/assignment focus | chooser completion remains 097B |
| Scene | scene list and mode action | destructive forms use shared modal |
| Browser | list/page/first/last/DATA/open/parent | production covered |
| Setup | list/form roles | profile lifecycle/typed arguments are 097B |

Touch availability alone is not counted as hardware reachability. Recording
and atomic recovery services remain authoritative, but not every historical
Slint-local dialog has migrated to the shared contract.

## Frontend performance investigation

The 096E and current benchmark sources use the same deterministic workload.
096F added two complete assignment-target discoveries on each full adapter
reconstruction, explaining 096G's approximately 0.93 ms median. The picker now
caches stable capability metadata with action/parameter identity invalidation.
The development workload measured 124,843 ns full median and 64,310 ns
incremental median; discovery rebuilt once. A structural assertion rejects
repeated unchanged discovery. Authoritative mutable values are not cached.

## Remaining limitations and 097B

Recording, recovery, mapping and assignment forms still need complete generated
focus traversal. Spatial navigation exists only where a stable semantic neighbor
model exists. Character entry intentionally uses the on-screen keyboard.
Coordinate touchscreen and all physical hardware validation are NOT RUN.

## Regression results

- `ctest --test-dir build/dev --output-on-failure -j2`: 40/40 passed.
- `ctest --test-dir build/slint --output-on-failure -j2`: 43/43 passed,
  including software-rendered 1920x1200 and 1024x600 states.
- `ctest --test-dir build/production --output-on-failure -j2`: 43/43 passed.
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize
  --output-on-failure -j2`: 40/40 passed. LeakSanitizer itself cannot run in
  this ptrace environment; ASan/UBSan execution passed with leak detection off.
- `nix build path:.`: passed after granting Nix-daemon access.
- `nix flake check path:.`: passed; the native checks include the AArch64 cross
  derivation, while Nix reported that native `aarch64-linux` was omitted on this
  x86_64 host.
- `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices`:
  passed with the installed Null audio backend and ALSA MIDI enumeration.

Physical controller, touchscreen and audio/MIDI hardware tests were NOT RUN.

Task 097B should finish all Performance positions/layers/pads and Setup typed
arguments, manual/learned sources, profile create/name/duplicate/select/recover,
conflict replacement and feedback fields. Each form should use this ownership
contract; no new assignment authority is required.
