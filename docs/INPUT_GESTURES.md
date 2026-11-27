# Input gesture ownership

Task 098F binds LOOP, SHIFT+LOOP/RANGE, REC, SHIFT+REC/OVERDUB,
REC MODE, and SHIFT+REC MODE/SOURCE through the existing shift/chord arbiter.
Consumed modifier chords do not emit their ordinary action on release. Stable
recording ownership survives selection changes, and PANIC retains modal safety
precedence.

REC MODE selects the shared musical-events mode and REC arms/starts/stops its
captured destination through `ControllerSession`. Only musical actions accepted
after modifier/modal resolution reach the audio capture boundary; consumed pad
gestures cannot be recorded accidentally.

COPY is a deferred-tap modifier. Tap copies the current compatible selection,
COPY+pad captures that stable sequencer step without dispatching its ordinary
action, and SHIFT+COPY merge-pastes at the selected stable destination. Chord
consumption covers either release order; context/modal/disconnect cancellation
cannot synthesize copy or paste.

TAP is a deferred owner. A plain tap invokes tap tempo; TAP+DATA captures direct
tempo adjustment and consumes the pending tap. CLICK is immediate and
SHIFT+CLICK resolves only the shared count-in configuration cycle.

## Task 097D reliability closure

Any non-modifier input between two SHIFT taps interrupts the double-tap latch
sequence. When another input occurs while SHIFT is physically held, SHIFT
remains effective for that operation but its pending latch tap is consumed.
Modal/context cancellation of a held-step edit discards the uncommitted
timeline transaction, clears the edit group and rejects the later stale
release; it cannot leave an applied edit without history.

Sound note and explicit audition releases retain the descriptor captured at
press. Destination/modal/reconnect cancellation submits that captured release
before ownership is cleared. A later unmatched physical release is rejected
rather than re-resolved in the new context.

## Task 097C contextual pad ownership

Reference V2 pad presses resolve once from a destination-specific shared pad
descriptor. It captures a stable target and any matching release. Context
change, modal cancellation and disconnect use the existing cancellation path.
Generic profiles are not forced through V2 pad policy. Precedence remains modal,
explicit held modifier, effective SHIFT, then ordinary contextual behavior.

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
## Profile activation

Explicit runtime profile activation cancels the removed endpoint adapter's
held and momentary ownership before it is detached. The replacement session is
advanced beyond the prior local generation, so queued input cannot acquire new
ownership. Other endpoints and their gesture states are independent.

SOLO+pad and ARM+pad are consumed stable-target chords and suppress ordinary
pad behavior. SHIFT+SOLO and SHIFT+ARM clear their transient sets.
SHIFT+STOP invokes modal-safe unified panic. Overlapping SOLO, ARM, MUTE, and
TRACK ownership never falls through to an unrelated pad operation.
# Task 098C REPEAT and live ERASE ownership

REPEAT is a modifier-only owner. REPEAT+pad consumes the ordinary pad gesture;
the pad and modifier may be released in either order and address the captured
generation. REPEAT+DATA changes division and SHIFT+REPEAT changes family.
During running Sequencer playback ERASE+pad captures live-erase ownership;
stopped ERASE+pad retains ordinary step erase. REPEAT+ERASE ambiguity is
rejected. Modal/context/reconnect cancellation uses the existing arbiter path.
