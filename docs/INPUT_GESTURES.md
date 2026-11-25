# Input gesture ownership

## Assignment-mode ownership

Opening a Performance or Setup assignment proposal captures the bank/profile,
logical position/source and layer. Selecting a pad target is an editor gesture,
not a performance press. Telemetry and later selection changes do not rewrite
the proposal; confirmation validates the captured target and cancellation
restores the prior assignment. Active-session mapping replacement continues to
use the existing generation cancellation and feedback resynchronization path.

Protocol 1.3 supplies each physical V2 connection with a host-issued generation
and full-state reconciliation. Sequence gaps, reset, disconnect and state
mismatches cancel arbiter ownership; snapshots release missing holds and
quarantine buttons already down until a fresh transition. Snapshots never
synthesize presses, and physical reconnection clears latched modifiers.

Task 096D adds context-scoped deferred taps and wildcard logical chord inputs.
V2 pads remain immediate generally but become deferred in Sequencer: release
toggles once, while P1-P8 movement consumes the pending tap and begins a stable
held-step edit. The wildcard names a validated logical encoder family, not
arbitrary scripting. Modal/context/disconnect cancellation clears ownership
and cancels its grouped transaction.

NEUROGENIC resolves optional multi-control gestures in a bounded control-side
`InputGestureArbiter`. It sits between normalized device events and
`ControllerSession` semantic dispatch. It is not part of the audio engine and
is not required by clients that invoke semantic actions directly.

Task 096C exercises this ownership in the production V2 profile. TRACK, PAGE,
MUTE and ERASE own pad chords and consume normal pad behavior; Fill has paired
press/release ownership; SHIFT+Fill latches; and TAP+DATA captures the tempo
target. Reconnect cancels these owners and stale generations remain rejected.

## Investigation result

Before Task 096A, `ControllerSession::input()` dispatched presses immediately,
ignored most releases, stored only two modifier booleans, and resolved current
context again for every event. Held controls had no explicit owner; reconnect
did not clear them; and selection changes could redirect multi-event input.

The arbiter correlates press and release by stable logical control ID, input
sequence, connection generation, and context generation. Configuration is
validated against the device descriptor and active state is bounded by that
descriptor. Targets retain semantic IDs, typed arguments, and parameter IDs,
never mutable engine pointers.

## Policies and ownership

- `immediate` dispatches on press, without delaying musical input.
- `deferred_tap` dispatches once on release unless a chord consumes the tap.
- `momentary` retains ownership for matching release or cancellation.
- `modifier_only` contributes an effective modifier without a base action.
- `repeatable` emits bounded repeats using configurable delay, rate,
  acceleration threshold, and per-update maximum.

An explicit chord matches logical identities and may consume its owner's tap.
Either release order is safe. Unsupported combinations are rejected rather
than falling through to an unrelated action. Policies do not encode PCB
indices, MIDI notes, page counts, or Reference Controller V2 geometry.

## Modifiers, modal input, and timing

Logical modifiers support momentary hold and configurable double-tap latching
using an injectable monotonic clock. Chord use consumes the pending tap and
interrupts double-tap recognition. The precedence contract is modal ownership,
explicit held modifier/chord, effective SHIFT, then normal context.

`set_context()` cancels context-owned controls before advancing its generation.
Policies explicitly marked `global_across_context` survive a normal context
transition and are rebound to the new generation. Task 096B uses that narrow
policy for global SHIFT and the page press that initiates navigation; other
held controls remain cancelled. A modal is a
presentation-neutral context identity; its visual state remains frontend-owned.
Existing held controls therefore cannot confirm a newly opened modal, and an
old-page release cannot invoke a new-page action. Explicit global exceptions
are profile policy.

Held repetition uses no timer thread. A control-side update emits at most the
configured count and skips excess catch-up after a delayed update.

## Reconnection, feedback, and concurrency

Each controller session owns its arbiter. Reconnect cancels active ownership,
clears physical and latched modifiers, advances connection/context generations,
and requests authoritative feedback resynchronization. Old generations and
duplicate/non-increasing per-control sequences are rejected. Endpoints cannot
release each other's ownership.

Observable state contains held logical controls, effective/latched modifiers,
active owner, modal/context identity, generations, and last cancellation. It
contains no raw scan position. Task 096B can consume effective SHIFT and context
generations for page labels without embedding the panel in the engine.

Processing is controller/control-side only: no audio-callback work, timer
threads, transport I/O, or mutable engine pointer retention. Task 086's
two-phase coordinator and lock ordering remain unchanged.

## History contract

The lifecycle for future coalescing is begin (ownership), update
(encoder/repeat), end (release), and cancel (context/modal/target loss or
disconnect). Stable targets are captured at begin. This is not a history stack,
and individual physical events are not Undo entries. Future authoring clients
may delimit Task 094 transactions with this lifecycle; navigation and transient
performance manipulation remain outside history.

## Evidence and remaining work

`neurogenic_input_gesture_tests` uses an injected clock and an actual
`ControllerSession`/`ApplicationService` path. It covers immediate dispatch,
one-shot deferred taps, consumed button chords, held-control encoder targets,
modal cancellation, production semantic dispatch, disconnect cleanup, and
stale-generation rejection.

V2 page/SHIFT routing and the complete logical inventory are implemented by
Task 096B. Complete navigation and transport bindings, held-step
parameter locks, performance banks, and RGB firmware feedback remain later
Task 096 work. Physical rollover, latency, lost packets, and ergonomics remain
NOT RUN.

Task 097A integrates shared modal generations and presentation focus. Modal
opening cancels inappropriate context ownership; page controls are blocked while
navigation and explicitly safe global controls remain available. Repeatable
navigation dispatches immediately and then at a bounded rate. Disconnect and
protocol reconciliation invalidate stale releases without synthesizing ENTER.
