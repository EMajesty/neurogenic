# Application control and controller API

Musical clipboard mappings invoke shared semantic actions. Controller profiles
do not own clipboard payloads or copy/paste rules. Reference COPY chords capture
stable logical targets and alternative controllers may call the same bounded
application actions.

Reference V2 CLICK and TAP bind the same metronome/count-in/tap-tempo semantic
surface available to generic MIDI and independent clients.

## Contextual controls

Reference V2 sessions consume the same destination-specific encoder, function
and pad descriptors as the frontend. Pad descriptors are independent of wire
IDs and physical positions. Legacy profiles continue to use their existing
`PerformanceGrid` behavior.

## Physical Reference Controller V2

The reference panel is a normal descriptor/profile consumer, not a core engine
assumption. Its USB CDC contract is protocol 1.3 and negotiates exact identity,
the canonical 67-control inventory, a fresh input generation and a full button
snapshot before enabling musical input. Wire IDs come from
[`controller_v2_wire.h`](../include/neurogenic/controller_v2_wire.h); see the
[firmware guide](REFERENCE_CONTROLLER_V2_FIRMWARE.md). Protocols 1.0–1.2 and
generic MIDI retain their existing behavior.

## Shared assignment authoring

Task 096F uses `AssignmentTargetPicker` as a presentation-neutral view of the
existing action, parameter and object discovery registries. Project-owned
Performance assignments and appliance-owned `.ngc` mappings consume the same
descriptors and typed validation, but retain separate persistence and history.
Manual mappings support MIDI note, absolute CC, relative two's-complement CC and
relative binary-offset CC sources. Candidate profiles are validated before an
explicit conflicting-source replacement becomes active; session replacement
cancels held ownership and advances the connection generation.

The current Setup form targets an existing endpoint/profile. Complete graphical
profile-library lifecycle, rich conflict lists and arbitrary action-argument
forms remain open. See
[Task 096F validation](audit/task-088/TASK_096F_VALIDATION.md).

Controller profiles may address logical Performance encoder/function/pad
positions without owning their musical targets. The project supplies the
active bank; V2, generic MIDI and alternative inventories resolve the same
descriptors. A one-encoder simulated profile is covered by Task 096E. See
[Performance banks](PERFORMANCE_BANKS.md).

Task 096D adds sequencer-scoped pad deferral to the optional V2 profile.
Generic MIDI, existing dedicated profiles and direct semantic clients do not
inherit it. Explicit context suffixes select richer shared descriptors; legacy
unsuffixed contexts retain their prior assignments.

Task 096B provides an explicit optional Reference Controller V2
descriptor/profile with stable human-readable logical IDs. The default binds
page routing, contextual encoders/functions, SHIFT and performance pads;
navigation/edit/transport controls not yet implemented are explicitly
unassigned. Older dedicated profiles and generic MIDI profiles retain their
identities and behavior.

Task 096C binds the supported cluster controls without changing that boundary.
DATA uses shared application focus; TRACK/PAGE/MUTE/ERASE use stable-ID pad
chords; Undo/Redo, Fill, configured recording and transport use registered
semantic actions. See the [complete matrix](audit/task-088/TASK_096C_VALIDATION.md).

One shared primary/secondary destination table drives controller and Slint
routing. Held or latched SHIFT selects the secondary table; releasing SHIFT
changes labels back without navigating away. Repeating the active destination
publishes a root-navigation revision. Task 096C supplies cluster bindings and
Task 096D the full contextual banks.

Press/release ownership, chords, modifiers, modal precedence, and connection
generations are defined in [Input gestures](INPUT_GESTURES.md). They use logical
control identities and do not require the reference panel.

The authoritative intended panel and default interaction design is
[Reference Controller V2](REFERENCE_CONTROLLER_V2.md). Its inventory and
relative placement are locked for current development; default assignments
remain subject to prototype ergonomics. The implementation remains
hardware-independent, and current gaps are maintained in
[REFERENCE_CONTROLLER_V2_GAPS.md](audit/task-088/REFERENCE_CONTROLLER_V2_GAPS.md).
Developers building a different surface should use the extension-oriented
[Controller Design guide](CONTROLLER_DESIGN.md); the reference inventory is not
an application or engine requirement.

The Slint and dedicated-controller contextual paths use the same copied
`ContextControl` assignments. In the Modulation context, Depth-/Depth+ and
Enable carry the selected stable route ID plus validated typed values; a page
or selection change regenerates those arguments rather than redirecting a
previously confirmed invocation. The reference controller requires no encoder
push. Macro performance values remain semantic musical objects and are bound to
physical controls separately through controller profiles.

Task 086 makes MIDI learn transactional: eligible note/CC input is held pending
until explicit confirmation; cancel and timeout do not modify mappings. Setup
can inspect/remove mappings and save `.ngc`. The coordinator and MIDI mapper
copy shared endpoint/surface handles and release registry locks before
transport, feedback or callback work. Action bindings carry typed arguments
validated against semantic descriptors.

Performance slots expose semantic launch/stop actions and an independent
velocity policy. Fixed buttons, velocity MIDI input and optional Full Level do
not imply a grid or dedicated hardware control.

Controller clients may inspect
[application discovery](APPLICATION_DISCOVERY.md) for semantic actions,
[typed parameter bindings](PARAMETER_BINDINGS.md) for absolute, relative,
toggle and momentary controls, and the discovery document for parameters,
object availability and resource limits. Discovery contains no
button counts or physical layout; profiles continue to map stable semantic IDs.

Pattern and arrangement actions are semantic and layout-independent. Task 045
adds `pattern.chain-entry-set/chain-play/chain-stop/chain-reset/follow-set.v1`,
`pattern.launch-quantization.v1`, `arrangement.voice-transition/song-end/return.v1`
and copied discovery/diagnostics for current, queued, and next transitions.
These complement:
`pattern.create/duplicate/rename/delete/launch.v1` and
`arrangement.mode-set/clip-create/clip-delete/loop-set.v1`. Their stable IDs
and copied application-state collections allow launchers or song navigators
without assuming a 16-button bank. Task 044 adds no physical mapping.

The dedicated 2×8 controller uses the host protocol in
[CONTROLLER_PROTOCOL.md](CONTROLLER_PROTOCOL.md). Its MCU reports raw controls;
the host resolves pages, modes, actions, parameters and RGB feedback. The eight
upper parameter encoders and the main data encoder are explicitly non-push
controls. Eight page buttons, eight lower contextual buttons, two ten-position
clusters and six navigation buttons are distinct logical groups. See the
[hardware boundary](CONTROLLER_HARDWARE_MAPPING.md).

`resolve_controller_context()` is the shared mapping source for controller
sessions, serial feedback and GUI edge labels; `build_controller_context()` is
the application-global compatibility wrapper. Contexts name explicit stable parameters rather than taking the
first eight registry entries. Relative encoders clamp through
`ParameterRegistry`; Shift gives fine adjustment and larger deltas receive
bounded acceleration. The dedicated profile has no integrated faders or
crossfader; generic MIDI controllers may still use binding-service pickup. The
stable ID table and 1.0–1.2 formats are in the protocol document.

The Slint Setup page lists copied coordinator endpoint state. It can start or
cancel MIDI learn for an existing logical profile control and atomically save
the resulting `.ngc`. Descriptor inventory remains separate from profile
mappings, and Slint never receives a live `ControllerSession`. Full semantic
binding creation/removal remains a profile editing workflow.

Task 069 adds transport-independent device descriptors and per-device
`ControllerSession` objects. Device identity is separate from `.ngc` profile
mapping; pages, modifiers, pickup and feedback caches are session-local while
musical state stays authoritative in `ApplicationService`. Generic MIDI notes
and CCs may opt into a profile without consuming ordinary musical MIDI input.
See [controller integration](CONTROLLER_INTEGRATION.md).

Task 070 connects these sessions to the appliance through
`ControllerCoordinator`. `--controller` and `--controller-id` are repeatable;
each exact CDC path retains independent reconnect/context/feedback state.
`--midi-controller-profile` loads an opt-in MIDI surface without changing
ordinary musical MIDI routing. Local feedback revisions and queue acceptance
drive bounded overflow recovery.

Mixer clients use `mixer.channel-select.v1`, `mixer.gain-set.v1`,
`mixer.pan-set.v1`, `mixer.mute-toggle.v1`, `mixer.solo-toggle.v1`,
`mixer.aux-send-set.v1` and `mixer.output-pair-set.v1`. Eight encoders naturally
map to selected-channel parameters/sends and four faders to gain. A minimal
data-wheel client selects a channel/property and invokes the same actions; no
physical layout is embedded in the engine.

External routing uses `mixer.fx-loop-select.v1`, `mixer.fx-loop-set.v1`,
`mixer.fx-loop-levels.v1`, and `mixer.fx-loop-latency.v1`. Scenes use
`mixer.scene-save/recall/rename/delete.v1`. The selected loop's wet/dry is also
`mixer.external.selected.wet`, allowing full, MIDI and minimal data-wheel
profiles to reach it without adapter-specific routing logic.

The control architecture is deliberately client-neutral:

```text
hardware / touchscreen / desktop / MIDI / Lua / remote
                         |
        C++ actions, parameters, commands and snapshots
                         |
     timeline / samples / transport / audio engine
```

All types described here run on application or control threads. They never
expose editable engine objects to frontends and are not called by the audio
callback. Lua remains an optional adapter over C++ services; hardware and UI
clients do not require a Lua runtime.

## Actions and commands

`ActionRegistry` publishes `ActionDescriptor` records with a stable versioned
string ID, display name, description, category, typed argument schema,
availability and an explicit unavailable reason. `ApplicationValue` supports
booleans, 64-bit integers, doubles, text and normalized X/Y values.

Implemented actions cover transport, track selection/navigation/mute and note
targeting, pattern/step selection, toggling/resolution/pages, sample slot selection/load/replace/unload, pad modes,
focus/confirm/cancel, text input, modifiers and X/Y input. Undo, redo, grouped
timeline editing, and selected-event property editing are implemented. Direct
underspecified generation/mutation requests remain unavailable.
This is intentional: discovery never implies an operation is
implemented.

`ApplicationService` accepts `ApplicationCommand` values through a bounded
256-command queue by default. A mutex gives multiple application producers one
monotonic sequence; the control-side consumer executes commands in that order.
Overflow rejects the newest command and increments `rejected_commands()`.
Commands carry a source ID and source sequence for correlation. They only call
control-side `TimelineService`, `SampleManager`, and `AudioEngine` command
methods; no command mutates callback-owned data.

## Observable state

`ApplicationState` is a value snapshot containing transport state, frame and tick,
tempo and time signature, selected track/sample/pattern/step, step page, pad
mode, parameter page, focus, modifier state, currently available action IDs,
sample loading snapshots and timeline revision. Frontends receive copies, not
mutable pointers.

The application revision increases for accepted state changes and for observed
transport, timeline or sample-state changes. Timeline and sample revisions are
also retained so a client can correlate subsystem updates precisely. Rapid
commands remain ordered; asynchronous sample state is refreshed from
`SampleManager` without allowing an old completion to replace newer metadata.
Polling snapshots is the initial notification mechanism. Subscription and
delta transport can be added without changing command semantics.

## Parameters

`ParameterRegistry` holds stable IDs and widget-independent metadata: name,
type, range, default/current value, unit, step size, continuous/discrete flags,
automation support and modulation support. Boolean, integer, floating-point and
enumerated values are supported.

The registry provides set, normalized set, relative adjustment, query,
enumeration and reset. Normalized input maps an absolute fader or potentiometer
from 0..1 into the declared range. Relative encoders apply `steps * step` and
clamp. This is application parameter infrastructure, not a DSP parameter or
audio-rate modulation system.

## Capabilities and profiles

Controllers declare only the controls and feedback they actually possess.
Input types include momentary/toggle buttons, relative encoders, absolute pots,
faders, crossfaders, joysticks, touch X/Y, keyboards and pointers. Feedback
types include RGB/single LEDs, text, parameter values and a future motorized
fader capability.

`ControllerProfile` schema 2 maps control IDs to semantic action, parameter or
contextual-role IDs with
independent Shift and Select layers, labels, absolute/relative behavior and
feedback destinations. `ControllerMapper` validates profiles, resolves a
control event against effective momentary/latched modifiers, and produces a
semantic command or parameter update. Feedback includes the application
revision and origin; a destination matching the origin is suppressed to avoid
echo loops.

The `.ngc` line format is intentionally small and dependency-free. Existing
version-1 records remain valid; schema-2 options add contextual roles, explicit
unassigned controls, indices and pickup:

```text
# NEUROGENIC_CONTROLLER 1
controller|controller.id|Display name
control|play|momentary|Play
control|tempo|encoder|Tempo
bind|play|action|transport.play.v1|feedback=led.play
bind|tempo|parameter|transport.tempo|shift|label=Fine tempo
bind|page-1|semantic|local-context|context=mixer
bind|unused|unassigned|-
```

Optional control capabilities are declared as `feedback=rgb`,
`feedback=indicator`, `feedback=text`, `feedback=value`, or
`feedback=motorized`; a binding's
`feedback=CONTROL_ID` then selects that declared output. Profiles cannot gain
feedback functionality merely by adding a MIDI output mapping.

Unknown records, control types, capability/modifier options, controls and
actions fail validation. Complete examples are under
[`examples/controllers`](../examples/controllers):

- `full-performance.ngc`: a legacy representative profile. The C++ dedicated
  capability profile declares 16 RGB performance buttons, eight contextual
  encoders, a non-push data encoder and semantic transport/modifier controls;
  it deliberately declares no integrated analog controls.
- `minimal.ngc`: data encoder, Enter, Back and optional joystick/display.
- `generic-midi.ngc`: generic buttons, knob and fader mappings.
- `generic-midi-surface.ngc`: runnable MIDI wrapper records with an exact
  selector placeholder, absolute pickup, relative encoding and optional
  feedback.

MIDI wrapper serialization uses `note`, `cc-absolute`, `cc-relative-2c`, and
`cc-relative-offset`. Earlier `absolute`, `relative-twos`, and
`relative-offset` spellings remain load-compatible. Unknown modes and trailing
options are rejected with the accepted values in the diagnostic.

These profiles describe capabilities; they do not claim physical hardware has
been tested. MIDI learn is available through the control-side adapter API with
explicit arm, cancel, timeout and atomic profile persistence; a GUI editor is
not implemented.

## Performance grid

The generic parameter actions expose stable `synth.*` and `effect.delay.*` IDs.
Eight encoders or a single data wheel can adjust the same native DSP parameters
without Lua or direct engine access. MIDI notes and timeline STEP events can
target `synth.subtractive.v1`. The 16 NOTES and 16 LEVELS modes define note and
velocity semantics; translating those semantic grid events into live instrument
commands remains an application-mapping extension.

Parameter-lock clients use `step.lock-select.v1`, `step.lock-set.v1`,
`step.lock-remove.v1`, and `step.locks-clear.v1`. A full controller can issue
`step.lock-set.v1` while a step is held and an encoder moves; the action carries
the semantic parameter ID and value, not an encoder number. A minimal controller
selects the step, focuses the parameter list, adjusts with its data wheel and
confirms. Both paths edit the same timeline transaction and observable lock
list; no controller owns a shadow pattern.

The built-in Sequencer context exposes `PAGE-`, `PAGE+`, `RESET STEP`, `MUTE`,
`UNDO`, `REDO`, `FILL` and `PLAY` as the eight contextual function assignments.
Page actions operate on absolute semantic step pages and do not depend on the
physical 2x8 pad arrangement. Advanced continuous fields remain discoverable
semantic parameters/actions and are fully available to touch; a future profile
may bind relative controls without adding encoder-push assumptions. The
frontend and controller continue to consume the same authoritative context
resolver.

`PerformanceGrid` converts any virtual or physical index 0..15 to semantic
events independently of MIDI notes or button placement:

- STEP normally toggles `page * 16 + pad`; Select produces selection only.
  `grid_application_command` maps both to shared application actions. Pages
  permit arbitrary pattern lengths.
- PAD resolves an initial pad/sample semantic event. Sample/instrument triggers
  are supported by the engine; slicing is not.
- 16 LEVELS targets the selected slot with 16 velocity values. The semantics
  are ready, but pitched-instrument behavior is not implemented.
- 16 NOTES emits a configurable chromatic note layout. Scale layouts and
  pitched sample playback remain future work.

Changing `PadMode` only changes application state and never sends a transport
command. A controller without 16 buttons can present the same grid virtually.

## Navigation, text and X/Y

Focus next/previous, relative adjust, confirm and cancel are semantic actions.
The minimal profile uses Enter to move Transport → Tracks → Steps and the
encoder to select a track or step. Enter on a step selects its note (creating
the target note when empty) and opens properties. Encoder turns adjust the
active property, Enter cycles velocity/duration/pitch/timing, and Back returns
one level. Full controllers can map Select+grid to event selection, encoders to
property actions, and buttons to Undo/Redo.
Shift and Select each
have momentary and latched state. Mapping layers resolve before an action is
emitted, preventing a mode change from implicitly firing the base binding.

`TextEntryModel` supports direct text, encoder character selection, insertion,
Back deletion/cancel, cursor movement, commit and discard. It has no rendering
dependency; host tests enter and edit text using encoder, Enter and Back
semantics.

`XYControl` accepts normalized coordinates from touch, joystick, paired
encoders, MIDI, Lua or remote adapters. Metadata covers spring return,
latching, relative movement, inversion and per-axis scaling. Updates can travel
through the bounded application queue; no modulation or audio callback work is
performed here.

## Current limitations

An unbound Shift or Select layer falls back to its base binding by default;
profiles can disable this with `fallback_to_base`. Step selection and step
toggling remain distinct semantic actions.

There is no device firmware, MIDI learn, project persistence for note targets
or profiles, state-delta subscription, modulation matrix,
scale engine or pitch shifting. The profile files are validated
examples, not a commitment to a particular master-controller layout.

Slice clients use stable `slice.select.v1`, `slice.create.v1`,
`slice.bounds-set.v1`, delete, auto-slice, pad-assignment and preview actions.
`pad.trigger.v1` carries a semantic pad index and velocity: PAD mode does not
depend on physical MIDI IDs or the step editor, and an assigned slice overrides
the default 36–51 sample-bank mapping. A data wheel can select a slice and
adjust its numeric boundaries without touch; bindings remain profile policy.
Expressive playback uses `pad.select.v1`, `pad.playback-mode-set.v1`, choke,
pitch/fine, gain, reverse and envelope actions plus `pad.trigger.v1` and
`pad.release.v1`. PAD targets the selected assignment, 16 LEVELS supplies
sixteen velocities to that pad, and 16 NOTES supplies chromatic playback-rate
offsets relative to its configurable layout root (C4/60 is unity). A mapping may bind these to encoders/buttons or use the same
focus/data-wheel navigation; the core contains no physical layout.
Stretch mappings use `pad.stretch-mode-set.v1`,
`pad.stretch-ratio-set.v1`, source-beats/source-BPM, quality and reset actions.
Eight encoders may bind them directly; a minimal controller focuses
`pad-playback`, cycles fields with Enter and adjusts with its wheel. PAD, STEP
and 16 NOTES all reach the same copied pad settings.
Trigger editing uses stable actions `step.probability-set.v1`,
`step.condition-set.v1`, `step.ratchet-set.v1`, and `step.trigger-reset.v1`.
Fill adapters use `performance.fill-set.v1` on press/release for momentary
operation or `performance.fill-latch.v1` for a latch. This supports a held step
plus encoder and a focus-driven data-wheel workflow without assigning physical
controls in the core.
# Project actions

Project New/Open/Save/Save As/Close/Relink/Query are stable semantic actions and
can be bound by full or minimal controllers. Their path arguments are supplied
by a client-neutral filesystem navigator; no desktop file dialog or physical
layout is assumed. Project progress, errors, dirty state, and missing assets are
copied application state suitable for controller feedback.

# Recording actions

Source, destination, channel mode, quantization, fixed bars, monitoring,
arm/start/stop/cancel, save, and pad assignment are semantic actions. A minimal
data-wheel client can operate the Record view with Enter/Back; full controllers
may bind dedicated controls without changing recording logic.

# Modulation actions

Adapters use `modulation.route-create/delete/select/depth/enable.v1`,
`modulation.source-configure/reset.v1`, `modulation.xy.v1`, and
`modulation.midi-cc.v1`. Scope and track ID are semantic route data, not a
physical layout. Eight encoders may address settings directly; a minimal wheel
navigates the MOD route list and reads copied source/effective feedback.
`track.instrument-set.v1` accepts an optional stable `instance_id`, and
`instrument.instance-parameter-set.v1` edits one instance. Route creation also
carries `instance_id`; adapters never infer global scope for a local target.
Destination categories include synth, sample, mixer/aux/master and external
FX. Numeric identities select tracks, channel slots or loop slots; adapters do
not manipulate a routing graph or republish mixer configurations.

# Preset actions

Preset browsing, capture, validation, preparation, activation, rename,
duplication and deletion are semantic operations. Capture binds an explicit
stable source track plus library identity; controllers must use a validated
context that supplies those arguments and must not advertise a bare Save or
Apply assignment when they are unavailable. Confirmation and naming use the
shared modal/text-entry context. No encoder push is assumed, and touch remains
an independent complete path. Preset content never includes controller
profiles or physical mappings.

# MIDI routing actions

Adapters can select clock ownership and outputs, create/delete explicit
channel-filter/remap routes, and invoke panic through stable `midi.*.v1`
actions. Controller mappings and musical routing remain separate, so using a
keyboard for both roles does not imply MIDI thru. MIDI learn remains registered
as unavailable rather than simulated.
Copied state exposes sync status, BPM, phase error, SPP, port connectivity and
queue counters without requiring backend access. External forwarding is the
semantic `midi.clock-forward-set.v1` action.

Track lifecycle and external sequencing use `track.create.v1`,
`track.delete.v1`, `track.instrument-set.v1` and
`track.midi-destination-set.v1`. Tempo uses `transport.tempo-set.v1`; changing
the displayed tempo now changes the authoritative timeline rather than a
detached UI value. Project naming uses `project.rename.v1` and the reusable text
entry model. These actions are reachable from minimal data-wheel clients and do
not depend on Lua or pointer input.

## Appliance recovery

Minimal and full controllers use the same project Save action and dirty-state
feedback. Destructive New/Open/Close commands cannot bypass unsaved-work
protection accidentally; an explicit `discard` argument is required. Startup,
degraded-device recovery, and shutdown do not depend on touchscreen input.

The Sequencer context assigns function controls to step-page navigation, step
reset, track mute, Undo/Redo and previous/next reusable-pattern selection.
Sixteen pads remain the current absolute step page. Slint preserves validated
contextual action arguments instead of reducing assignments to bare action IDs.
Arrangement editing is exposed as discrete stable-ID actions suitable for
profile bindings; no encoder push or pixel-precise drag is assumed.

When a live-loop session exists, the Performance context exposes stable-ID
Arm/Start, Stop, Overdub, Replace and Cancel actions. Availability follows the
authoritative pass state; the controller cannot redirect an armed pass by
changing a later UI selection. Destructive Clear is deliberately confirmation
only and is not assigned to an ordinary performance button.
## Task 093 interaction grammar

The functional reference profile uses sixteen musical pads, eight contextual
encoders and eight contextual function controls, but these counts and their
panel placement are provisional. Device descriptors remain variable-length;
profiles bind logical control IDs, and application services never depend on a
GPIO, shift-register bit, MIDI note, or panel coordinate.

Default page contexts are Performance, Sequencer, Sample, Sound, Mixer, FX,
Browser and Setup. Each may select named banks such as `primary` and
`secondary`. Encoders adjust values or selection, function controls invoke
actions/navigation, and pads manipulate page-specific musical material. Shift
continues to provide fine adjustment. No encoder push is required.

Controller action bindings persist typed arguments plus pickup, inversion,
fine scale and acceleration. Required arguments are validated against semantic
descriptors. Reconnection invalidates feedback and republishes current context.
The maintenance profile remains the recovery path for a profile that removes
navigation.

Task 093 predates the locked V2 assignment table. Its shared contexts, typed
arguments, configurable descriptors and logical pad positions are reusable,
but its provisional default profile is not the V2 panel implementation. V2
transport/navigation groups, shifted destinations, chord ownership and screen
zones must be integrated explicitly, never inferred from numeric profile IDs.

Navigation mappings may address stable presentation roles without owning visual
focus. The active frontend reports copied focus and DATA roles and consumes
revisioned requests. Modal and connection generations prevent stale events from
confirming replacement dialogs. Generic MIDI and direct actions remain valid
without this optional bridge.

Task 097B exposes indexed contextual and Performance roles in the shared target
picker (`context.encoder.0` through `.7`, functions, pads, and corresponding
Performance roles). Setup presents absolute, two's-complement relative,
binary-offset relative and note sources, pickup, inversion, fine scale,
acceleration, semantic targets and candidate-first conflicts. These edit the
active appliance profile; they never enter project Undo/Redo.

The coordinator still has no profile-library authority for graphical
create/rename/duplicate/select/remove/recovery. Save As for an active endpoint
is supported, but presenting a fake frontend-only library would violate profile
ownership. This remains an explicit RC2-006 blocker.
## Profile library ownership

Task 097F composes a bounded `ControllerProfileLibrary` beside the coordinator.
Profiles are appliance definitions with stable IDs; endpoints and sessions are
separate, and activation is explicit per endpoint. Built-ins are read-only but
duplicable. See [Controller profile library](CONTROLLER_PROFILE_LIBRARY.md) for
lifecycle, persistence, recovery and mapping-field details.

Stable semantic actions now expose track solo, track arm, and unified panic to
generic MIDI and alternative profiles. The reference controller's SOLO/ARM
pad chords and SHIFT+STOP use the same application authority.
# Task 098C controller semantics

Generic clients may invoke the registered repeat/live-erase semantic actions.
Reference REPEAT and ERASE remain existing logical controls; no protocol wire
IDs were added. Musical timing is host-owned and controller firmware must emit
only ordinary press/release transitions.
