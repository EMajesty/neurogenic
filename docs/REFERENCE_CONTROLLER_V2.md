# Reference Controller V2 interaction design

Task 096D implements the Sequencer P1-P8 step-property/lock interaction,
sequencer-only deferred pad taps, stable multi-step selection and grouped
history. Other page bank rows remain working defaults unless their registered
binding is listed in `TASK_096D_VALIDATION.md`; a label alone is not an
implementation claim. Physical validation remains NOT RUN.

Task 096A supplies the presentation-neutral ownership, consume-on-chord,
modifier, modal-context, repeat-timing, and reconnect-generation foundation.
V2 page tables and bindings remain subsequent work; see
[Input gestures](INPUT_GESTURES.md).

Task 096B implements the stable logical inventory and the locked primary/SHIFT
destination table as the explicit reference descriptor/profile. The production
Slint reference shell consumes the same destination definitions as controller
routing and composes a top page strip, central content, right-side 2×4 encoder
zone, and bottom function strip. Dedicated edit/navigation/transport defaults
are integrated to the extent supported by existing semantics in Task 096C;
page-specific banks remain Task 096D work. Physical validation remains NOT RUN.

Task 096C now integrates the supported dedicated-cluster subset: focus-aware
DATA, stable TRACK/PAGE/MUTE/ERASE pad chords, unified Undo/Redo, Fill,
configured recording and supported transport controls. The authoritative
[availability matrix](audit/task-088/TASK_096C_VALIDATION.md) identifies every
unavailable operation and its first missing layer. Physical validation remains
NOT RUN.

This document is the authoritative design reference for NEUROGENIC's intended
Reference Controller V2. It supersedes earlier simplified panel assumptions,
but not the hardware-independent application architecture: application
services own musical state and semantic operations; profiles map logical
controls; transports translate events; firmware scans hardware and displays
feedback.

Three status terms are used throughout:

- **Locked baseline** — the agreed physical inventory, relative placement and
  interaction principles for current development. Changes require an explicit
  design revision.
- **Default assignment** — the agreed working mapping, subject to prototype
  ergonomics testing and profile revision.
- **Implementation status** — **implemented**, **partial**, or **missing** in
  current production software. A design requirement is never evidence that it
  works.

The implementation reconciliation and backlog are in
[REFERENCE_CONTROLLER_V2_GAPS.md](audit/task-088/REFERENCE_CONTROLLER_V2_GAPS.md).

## 1. Locked physical baseline

The panel contains one central 1920×1200-primary touchscreen, 42 non-pad
buttons, 16 pads and nine non-push rotary encoders:

- eight static page buttons above the display;
- eight contextual function buttons below it;
- eight contextual encoders right of the display, in two columns of four;
- one large DATA encoder at the upper left;
- six navigation buttons below DATA, in two rows of three;
- ten editing/modifier buttons at lower left, in two columns of five;
- ten transport/performance buttons at lower right, in two columns of five;
- sixteen performance/step pads below the display, in two rows of eight.

No encoder-push interaction is required. There is no physical alphanumeric
keyboard; text entry uses the shared on-screen keyboard. Exact dimensions,
components, electrical design and owner-supplied CAD/vector geometry are
outside this specification.

```text
                   STATIC PAGE BUTTONS 1–8

   DATA           +--------------------------+      P1     P2
                  |                          |
                  |                          |      P3     P4
 BACK UP ENTER    |         DISPLAY          |
 LEFT DN RIGHT    |                          |      P5     P6
                  |                          |
                  +--------------------------+      P7     P8

 MUTE SOLO          F1 F2 F3 F4 F5 F6 F7 F8          CLICK TAP
 ARM  TRACK                                         QUANT MODE
 PAGE ERASE       PAD1 PAD2 ...       PAD8          FILL  REPEAT
 UNDO COPY        PAD9 PAD10 ...      PAD16         LOOP  REC
 SHIFT SELECT                                       STOP  PLAY
```

This is a conceptual relationship diagram, not a mechanical drawing. Logical
identities remain independent of coordinates, scan order and protocol IDs.

## 2. Static page navigation

**Default assignment:**

| Position | Primary destination | SHIFT destination |
|---:|---|---|
| 1 | Performance | XY performance |
| 2 | Sequencer | Song arrangement |
| 3 | Sample | Slice editor |
| 4 | Sound/instrument | Presets |
| 5 | Mixer | FX |
| 6 | Modulation | Modulation routing |
| 7 | Macros | Scenes |
| 8 | Browser | Setup |

The upper display edge shows the eight current labels and switches to secondary
labels while SHIFT is effective. Pressing the active page returns to that
page's root. Destinations may be pages or subviews of existing services; they
must not create duplicate musical state. Stable destination identities, not
button indices, are persisted or dispatched.

**Implementation status:** partial. Eight logical page controls and local
contexts exist, but the production reference profile still maps the earlier
eight primary contexts. SHIFT destinations, active-page root behavior and
authoritative upper-edge labels require V2 integration.

## 3. Focus and navigation cluster

| Control | Primary | SHIFT |
|---|---|---|
| DATA | focused value adjustment or list scrolling | fine adjustment |
| BACK | cancel or navigate back | return to page root |
| UP | navigate up | page up |
| DOWN | navigate down | page down |
| LEFT | navigate left | first item |
| RIGHT | navigate right | last item |
| ENTER | confirm, open or select | save project |

DATA is focus-aware: it scrolls Browser entries, adjusts a focused descriptor
parameter, edits a selected Arrangement clip/timeline field, or navigates and
adjusts Sequencer properties. Directional buttons repeat while held with
context-appropriate bounded acceleration. Modal ownership takes precedence.
SHIFT+ENTER may request Save, but must never bypass dirty-state, overwrite or
destructive confirmation.

**Implementation status:** partial. Focus previous/next, confirm/cancel,
step-page navigation, relative DATA and fine SHIFT adjustment exist. Four-way
focus geometry, held repetition, first/last, page up/down, root return and
safe SHIFT+ENTER binding are incomplete.

## 4. Editing and modifier cluster

**Locked arrangement and default assignment:**

| Row | Left | SHIFT function | Right | SHIFT function |
|---:|---|---|---|---|
| 1 | MUTE | Unmute all | SOLO | Unsolo all |
| 2 | ARM | Disarm all | TRACK | Instrument selection |
| 3 | PAGE | Pattern length | ERASE | Delete |
| 4 | UNDO | Redo | COPY | Paste |
| 5 | SHIFT | double-tap latch | SELECT | Clear selection |

Held modifiers alter pads:

- MUTE/SOLO/ARM + pad toggle the corresponding track state;
- TRACK + pad selects a logical track;
- PAGE + pad selects an absolute step or pattern page;
- ERASE/COPY + pad erase or copy the contextual musical content;
- SELECT + pad selects or extends selection;
- SHIFT + pad invokes the contextual secondary pad behavior.

MUTE, SOLO, ARM, TRACK and PAGE may also enter visibly indicated persistent
modes. Gesture ownership follows a consume-on-chord policy: a press becomes a
potential tap; using it in a chord consumes its tap; release ends the chord and
must not emit the ordinary tap. A long-press is not interpreted by the musical
engine. ERASE removes musical events/content, SHIFT+ERASE DELETE removes an
object and requires confirmation, and SHIFT+SELECT CLEAR clears selection.
Live erase during recording is a desired MPC-inspired behavior and is
currently missing as an integrated controller workflow.

SHIFT is momentary and may be double-tap latched; latch state must be visible.
SELECT follows the same event-ownership rules. Timing thresholds belong to
configuration/firmware input normalization, not musical state.

**Implementation status:** partial. SHIFT and SELECT press/release state,
latched state fields, modifier mapping layers, track mute and timeline
Undo/Redo actions exist. The V2 chord arbiter, double-tap latch transition,
persistent modes, most edit-cluster bindings, copy/paste and live erase are
not production-complete.

## 5. Transport and performance cluster

| Row | Left | SHIFT function | Right | SHIFT function |
|---:|---|---|---|---|
| 1 | CLICK | Count-in | TAP | BPM editor |
| 2 | QUANT | Swing/groove | REC MODE | Recording source |
| 3 | FILL | Latch fill | REPEAT | Triplet mode |
| 4 | LOOP | Loop range | REC | Overdub |
| 5 | STOP | Panic | PLAY | Restart |

CLICK toggles metronome; TAP captures tap tempo; QUANT toggles input
quantization; REC MODE selects the explicit recording family/source; FILL and
REPEAT are momentary; LOOP toggles the applicable arrangement, sample or
live-loop playback range; REC arms/starts the configured operation; STOP stops
transport or the authoritative recording phase as displayed; PLAY starts or
resumes. SHIFT+STOP is a safe panic that stops playback and silences voices.
SHIFT+PLAY restarts from the context's defined beginning.

Transport stop, recording stop, playback stop and destructive loop clear are
distinct operations. Transport remains global unless a modal explicitly
captures it for safety; destructive actions never inherit global access.

**Implementation status:** partial. Play, stop, recording workflow toggle,
Fill state, live-loop actions, tempo and MIDI panic semantic paths exist. The
complete dedicated cluster, metronome/count-in, tap-tempo gesture, note repeat,
input-quantize toggle, swing/groove surface, context-aware LOOP, and restart
policy require integration or engine/API work as detailed in the gap report.

### Hold-and-turn shortcuts

| Held control | DATA rotation |
|---|---|
| TAP | adjust BPM |
| REPEAT | adjust repeat division |
| QUANT | adjust quantization resolution |
| LOOP | adjust loop length |
| REC | adjust recording length |
| FILL | select supported fill duration |

These are accelerators, never the only editing path. Tempo, quantization and
recording/loop length have reusable semantic state; the chord resolver is
missing. Repeat division and fill-duration support must remain unavailable
until backed by authoritative descriptors/actions.

## 6. Contextual pads

Pads are logical controls, not fixed MIDI notes, protocol indices or engine
object IDs. Explicit modifiers override page behavior. Additional logical banks
address projects larger than sixteen objects.

| Context | Primary pads | SHIFT pads |
|---|---|---|
| Performance | trigger assigned musical material | select assignment without triggering |
| XY | trigger performance assignments | select XY assignment |
| Sequencer | toggle or enter steps | select steps without toggling |
| Song | launch/select sections | select arrangement clips |
| Sample | audition samples | select sample slots |
| Slice | trigger slices | select slices |
| Sound | play instrument notes | select instrument/voice bank |
| Mixer | select channels | select groups/aux channels |
| FX | select effect instances | select FX owners |
| Modulation | select sources | select routes |
| Macro | trigger assigned macro actions | select macros |
| Scene | recall scenes | select without recalling |
| Browser | contextual shortcuts | secondary shortcuts |
| Setup | normally inactive | normally inactive |

SHIFT+pad normally selects without invoking the musical action; it is not a
global shortcut layer. TRACK and PAGE select logical track/page positions.

**Implementation status:** partial. Task 093 persists explicit logical
performance positions, provides unassigned/reassign/clear behavior and
deterministic legacy migration. Task 096B implements the complete destination
table; Sequencer select-versus-toggle and several sample modes exist. Physical
pad banks and temporary modifier feedback are not implemented.

## 7. Held-step editing and parameter locks

**Locked interaction principle:** hold a Sequencer pad and rotate a contextual
encoder to edit that absolute step, or create/update a compatible parameter
lock from the active descriptor-driven bank.

The gesture captures stable reusable-pattern, track, step and destination
identities at press time. It must not toggle the step or mutate the base
parameter. Existing locks are indicated; ERASE+held step removes the selected
lock/content; SELECT can build a multi-step selection and SELECT+rotation can
apply compatible edits to it. Ordinary pitch, velocity, duration, probability,
condition, ratchet and microtiming edits remain distinct from instrument,
mixer and FX locks.

**Implementation status:** partial. Stable edit targets, absolute paging,
advanced step actions and lock create/remove semantics exist. The controller
session does not yet own held-pad gesture targets or redirect contextual encoder
rotation into lock actions, and multi-step lock editing is incomplete.

## 8. Contextual encoders P1–P8

```text
P1  P2
P3  P4
P5  P6
P7  P8
```

The right display zone mirrors this 2×4 arrangement with logical identity,
name, value, unit, availability and applicable modulation/lock indication.
Normal rotation adjusts the assignment; SHIFT gives fine adjustment except in
explicit user Performance layers; SELECT may apply compatible multi-selection
edits. COPY/ERASE rotation shortcuts are optional future behavior.

| Context | P1–P4 | P5–P8 |
|---|---|---|
| Sequencer | pitch, velocity, duration, probability | condition, ratchets, microtiming, lock value |
| Sample | start, end, loop start, loop end | tune, gain, pan, zoom |
| Slice | selection, start, end, tune | gain, pan, playback mode, zoom |
| Sound | instrument parameters 1–4 | instrument parameters 5–8 |
| Mixer | gain, pan, send 1, send 2 | more sends/channel parameters |
| FX | effect parameters 1–4 | effect parameters 5–8 |
| Modulation | source parameters 1–4 | source parameters 5–8 |
| Route | source, destination, depth, polarity | supported route extensions |
| Song | clip, position, duration, offset | track, repetition, loop start, loop end |
| Performance | user assignments | user assignments |

Banks are descriptor-driven and extend beyond eight parameters without
hard-coded synth/effect inventories.

**Implementation status:** partial. Shared `ContextControl` feedback,
descriptor parameters, typed stable arguments, relative/fine/accelerated input
and named `page:bank` identities exist. Most listed banks are not populated;
secondary bank resolution currently falls back to page defaults.

## 9. Contextual functions F1–F8

The lower display edge mirrors F1–F8. SHIFT shows a secondary contextual layer.
Dedicated edit/transport controls leave functions focused on page operations.

| Context | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|---|
| Performance | Assign | Assign | Assign | Assign | Assign | Assign | Assign | Assign |
| Sequencer | Notes | Triggers | Locks | Automation | Pattern | Transform | Bank− | Bank+ |
| Song | Add clip | Split | Duplicate | Snap | Pattern clip | Audio clip | Zoom− | Zoom+ |
| Sample | Preview | Trim | Normalize | Process | Reverse | Loop | Zoom− | Zoom+ |
| Slice | Add | Remove | Auto-slice | Audition | Equal slices | Transients | Zoom− | Zoom+ |
| Sound | Instrument | Voice | Modulation | Preset | Reset | Save preset | Bank− | Bank+ |
| Mixer | Tracks | Groups | Aux | Master | Routing | Sends | Bank− | Bank+ |
| FX | Add | Remove | Move up | Move down | Bypass | Preset | Bank− | Bank+ |
| Modulation | New | Duplicate | Enable | Remove | Scope | Reset | Bank− | Bank+ |
| Route | Add | Remove | Enable | Destination | Invert | Reset | Bank− | Bank+ |
| Macro | New | Assign | Range | Remove | Rename | Reset | Bank− | Bank+ |
| Scene | Capture | Recall | Update | Duplicate | Rename | Delete | Bank− | Bank+ |
| Browser | Open | Preview | Refresh | Parent | Filter | Sort | Page− | Page+ |
| Setup | Audio | MIDI | Controller | Storage | Display | System | Diagnostics | Apply |

These are default labels, not invented action IDs. A field is unavailable until
an existing semantic operation and required stable arguments can back it.
Secondary banks follow the same rule. Candidate Sample secondary operations
include loop preview, crop to selection, gain, processing history, reverse
selection, loop mode, zoom-to-fit and zoom-to-selection only where supported.

**Implementation status:** partial. A subset of Sequencer, Sample, Mixer,
Modulation/FX, Browser and live-loop functions resolves today. Most V2 banks
and secondary assignments require semantic/presentation work.

## 10. Performance customization

Performance is project-owned musical presentation:

- primary and SHIFT P1–P8 may target compatible parameters, macros or depths;
- primary and SHIFT F1–F8 may target compatible action/toggle/momentary
  operations;
- pads may target samples, patterns, clips, scenes or launch targets;
- SHIFT+pad selects an assignment without triggering it;
- multiple named banks persist stable target identities and human labels.

Dedicated global navigation, editing and transport controls remain available.
These assignments are distinct from appliance-owned controller profiles and
must restore without the reference controller attached.

**Implementation status:** partial. Project-owned logical pad assignment and
profile-owned generic mappings exist. Project-owned custom encoder/function
banks, secondary layers, multiple Performance banks and labels are missing.

## 11. Precedence, ownership and modal safety

The general precedence is:

1. modal ownership of relevant controls;
2. explicit held edit/selection modifier;
3. SHIFT layer;
4. normal page behavior.

Documented global transport exceptions remain available only when safe. One
gesture has one owner: no stale action after a context change, no double
dispatch, release accompanies momentary press, and a consumed modifier tap does
not fire on release. Opening a modal suppresses unrelated musical triggers and
must drain/reject stale confirmation events. Reconnection clears physical held
state and restores only authoritative latch state, preventing stuck modifiers.

**Implementation status:** partial. Task 096A implements bounded release
correlation, consume-on-chord, double-tap latch, modal ownership and host-side
connection/context generations. Task 096B integrates held/latched SHIFT with
page routing and reconnect cleanup. Complete V2 cluster policies, nested modal
bindings and physical firmware validation remain open.

## 12. Temporary performance state

An Elektron-inspired future operation may capture and restore a bounded
performance state containing explicitly scoped pattern state, instrument/FX
base parameters, macro values and performance changes. It must be a musical
transaction integrated with the future unified history/persistence authority,
not a competing project snapshot or Undo/Redo stack. Exact capture scope,
active-playback publication and failure rollback require investigation in the
Task 094 history work before implementation.

**Implementation status:** desired future feature; missing.

## 13. Illumination and authoritative feedback

| Color | Default semantic meaning |
|---|---|
| dim white/off | available but inactive |
| white | selected/focused |
| green | playing/enabled |
| amber | queued/waiting |
| red | recording/armed |
| blue | modifier/secondary mode |
| purple | parameter lock/special assignment |

Brightness, pulse and short flashes add cues; essential state is also conveyed
by labels, patterns or brightness, never color alone. Modifier-held pad feedback
shows temporary meaning. Unavailable controls are dark/dim and labeled
unavailable; latch, bank, queued launch and recording phases follow copied
authoritative application state rather than optimistic firmware state.

**Implementation status:** partial. The protocol advertises RGB controls and
bounded feedback/resynchronization. Current V2 inventory describes many
buttons as single-LED, and the semantic color/pulse policy is not comprehensively
encoded or physically validated.

## 14. Firmware and protocol boundary

Firmware must report stable logical identities, press and release, simultaneous
buttons, rollover without ghosting, encoder movement during holds, timing
observations needed for double-tap/long-press interpretation, and connection
epochs. It drives individually addressable RGB feedback, handles scanning,
debounce and low-level illumination, and recovers from interrupted communication
or lost releases. Bidirectional communication and reconnection are required.

Musical gesture interpretation remains in the application wherever practical;
firmware does not encode page-specific music behavior. Alternative layouts and
generic MIDI clients use the same semantic APIs.

**Implementation status:** software protocol partial; RP2040 firmware and
physical electrical behavior are not implemented or validated by this task.

## 15. Screen zones

- top edge: eight static-page labels, switching with SHIFT;
- right edge: eight contextual parameter fields in a 2×4 arrangement;
- bottom edge: eight contextual function labels;
- center: active page content.

Modal contexts may replace relevant fields. The layout must remain functional
at 1024×600. Typography, color, artwork and exact pixels are owner-led visual
design work and are not fixed here.

**Implementation status:** partial. Slint has top navigation, eight context
fields and eight functions, but the encoder fields are not yet the locked
right-side 2×4 V2 presentation and shifted/static label behavior is incomplete.

## 16. Interaction rationale

The design combines, without replicating any product:

- Elektron-inspired held-step locks, trigger-based track/pattern selection,
  conditional Fill, momentary modifiers and temporary-state restoration;
- MPC-inspired note repeat, live erase, track mute/pad modes, record versus
  overdub, input quantization and swing;
- Polyend Tracker-inspired directional focus, a large DATA encoder, contextual
  functions and selection-based editing/transforms.

The result must remain one coherent NEUROGENIC grammar: semantic state and
validation are shared; physical gestures are profile/presentation policy.

## 17. Revision and validation policy

This V2 inventory and relative placement are locked for current development.
Default mappings may change after documented prototype evidence. Revisions must
update this document, the gap matrix, profiles, Slint labels and validation
checklists together. Automated simulation does not establish switch feel,
rollover, debounce, illumination readability, touchscreen reach, latency or
ergonomics; all remain NOT RUN until measured on the physical prototype.
