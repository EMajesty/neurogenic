# Task 096B validation

Date: 2026-11-25. Baseline: Task 096A `a7e18e9`.

## Implemented contract

- `reference_controller_v2_descriptor()` declares 67 unique logical controls:
  58 buttons (42 non-pad plus 16 pads) and nine relative encoders. IDs contain
  no GPIO, scan-matrix, LED-chain, wire-number, or screen-coordinate data.
- The optional V2 profile coexists with legacy, generic MIDI, headless and
  independent clients. Unimplemented cluster controls are explicitly unassigned.
- One immutable table maps eight primary and eight SHIFT destinations to
  existing pages/subviews. ControllerSession and Slint consume it; it owns no
  musical state.
- V2 SHIFT and page gestures survive ordinary page-context transitions while
  modal transitions and reconnect still clear inappropriate ownership.
- Slint composes top page labels, central editors, a right-side P1/P2 through
  P7/P8 grid, and bottom F1–F8 labels. Destination identity is distinct from
  its reused page component.

## Production-path evidence

`neurogenic_reference_controller_tests` constructs the normal
`ApplicationService` and `ControllerSession`. It checks inventory counts and
uniqueness, every destination, held SHIFT, one-destination dispatch, root
revision, modal rejection, reconnect cleanup, stale generation rejection, and
valid new input.

The generated Slint path invokes all primary and secondary callbacks, SHIFT
layer changes and the existing workflow callback matrix. Structured layout
validation requires nonzero top/bottom zones and non-overlapping central/right
regions at 1920×1200 and 1024×600. Generated callbacks are not coordinate
input; physical touchscreen and controller validation are **NOT RUN**.

## Validation commands

- `ctest --test-dir build/dev --output-on-failure`: 36/36 passed.
- `ctest --test-dir build/slint --output-on-failure`: 39/39 passed, including
  generated software rendering at 1920×1200 and 1024×600.
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize
  --output-on-failure`: 36/36 passed. Leak detection is disabled because this
  execution environment does not support it reliably; ASan/UBSan remain active.
- `build/dev/neurogenic_slint_benchmark`: passed; idle median 52.669 µs,
  incremental median 66.053 µs, full median 119.684 µs. These are development
  host measurements, not appliance performance claims.
- `nix build path:. --no-link`: passed, including package install checks and
  installed headless device enumeration.
- `nix flake check path:.`: passed.

No physical controller, touchscreen, audio interface or Raspberry Pi 5 test
was run.

## Task 096C semantic inventory

| Cluster | Existing layer | Next work |
|---|---|---|
| DATA | Relative/pickup/acceleration | Focus-aware stable binding and fine SHIFT |
| Navigation | Focus/confirm/cancel primitives | Four-way repeat, root and safe Save bindings |
| MUTE/SOLO/ARM/TRACK/PAGE | Track/page services | Consume-on-chord policies and stable pad targets |
| ERASE/DELETE/SELECT | Event/object actions and selection | Separate content erase from confirmed deletion |
| UNDO/REDO | Unified history | Dedicated binding and availability feedback |
| COPY/PASTE | Pattern/clip duplication only | Define clipboard scope; do not claim general Paste yet |
| CLICK/TAP/QUANT | Tempo only | Focused metronome, tap and input-quantization API review |
| REC MODE/REC | Recording/live-loop services | Stable mode/source and global bindings |
| FILL/REPEAT | Fill condition; no general repeat lifecycle | Bind supported Fill; leave Repeat unavailable |
| LOOP | Arrangement/live-loop modes | Context-specific stable operation and length shortcut |
| STOP/PANIC, PLAY/RESTART | Play/stop and panic | Global bindings and context-aware restart |
| Hold-and-turn | Stable gesture chords | Descriptor-backed DATA shortcut targets only |

Task 096C should implement navigation/edit/transport clusters over existing
semantics. Page P/F banks, held-step locks, Performance customization and
firmware feedback remain Task 096D–096G.
