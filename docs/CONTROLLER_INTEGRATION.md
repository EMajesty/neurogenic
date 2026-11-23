# Controller integration

NEUROGENIC separates musical state, controller sessions, transport adapters,
and firmware. `ApplicationService` remains the sole authority for transport,
selection, tracks, parameters, and actions. `ControllerSession` resolves a
profile against copied application state. CDC and MIDI adapters only normalize
messages and encode supported feedback. Firmware owns scanning, debounce,
quadrature decoding, GPIO and LED-chain order.

## Device descriptors

`ControllerDeviceDescriptor` schema 1 describes a device class independently
of transport. It contains stable device/descriptor IDs, protocol information,
logical controls, input ranges, feedback capabilities, queue limits, and a
brightness cap. Counts are dynamic. GPIOs, shift-register bits and LED offsets
are deliberately absent.

`rp2040_controller_descriptor()` describes the confirmed Task 068 inventory:
16 pads, eight contextual encoders, one non-push data encoder, eight contextual
buttons, eight page buttons, two ten-button clusters, and six navigation
buttons. It has no faders, crossfader, or encoder switches. Protocol 1.2 still
advertises capability groups rather than this complete inventory; the host
selects this descriptor after identity/version negotiation and does not claim
wire-level inventory discovery.

## Profiles and sessions

`ControllerProfile` schema 2 maps physical logical IDs to actions, typed
parameters, or contextual roles. Bindings carry modifier layers, relative fine
and acceleration scales, optional absolute pickup, feedback targets, or an
explicit unassigned state. `.ngc` remains the text format; legacy records parse
unchanged, while `semantic`, `context=`, `index=`, and `pickup` extend it.
Serialization is deterministic. Profile validation rejects unknown controls,
actions, parameters, device mismatches, ambiguous targets and malformed data.
The built-in RP2040 descriptors are selected from negotiated protocol identity
and capabilities (but remain physically unverified). Generic MIDI inventory is
explicitly user-declared because MIDI 1.0 does not advertise surface controls;
those declarations are configuration claims, not firmware capabilities, and
cannot augment a dedicated-controller descriptor.

Each `ControllerSession` owns its profile, local page, modifiers, pickup state,
feedback cache/queue, counters and resynchronization state. Musical state is
never copied into the session. Page and Shift changes on one surface do not
change another surface. Application selection remains global today; local
pages are per device. Stable and selection-relative parameter resolution uses
the same application descriptors and semantic setters as GUI clients.

The current built-in dedicated profile contains only the Task 068 inventory.
Explicit legacy descriptors/profiles retain protocol 1.0/1.1 aliases without
adding those aliases to a 1.2 device. Cluster positions not assigned
by that profile are explicitly inert. These assignments are defaults, not
firmware semantics, and may be replaced by a user profile.

## MIDI surfaces

`MidiControlSurfaceAdapter` opt-in mappings translate a named port number,
channel, note/CC and configured mode into a session control ID. Supported modes
are note momentary, CC absolute, two's-complement relative and binary-offset
relative. Unmatched MIDI continues through the existing musical note/raw MIDI
path, so a keyboard is not implicitly treated as a control surface. Existing
Controller-profile selectors require an exact device display name on first
connection. Reconnect uses the backend port identity captured at that
connection, not a substring or a possibly duplicated display label.
MIDI learn is explicit and control-side: the caller selects a descriptor
control and input mode, then the next eligible note/CC records its exact
logical port, channel and number. Cancellation and timeout leave the profile
unchanged. Only an active learn consumes its eligible event; unrelated musical
input remains on the normal MIDI path. Optional note/CC feedback uses the
configured logical output port.

MIDI-aware `.ngc` files add `midi-device|EXACT SELECTOR` and
`midi|PORT|CHANNEL|NUMBER|MODE|CONTROL|feedback` records around ordinary
schema-2 controller records. `--midi-controller-profile FILE` loads one in the
appliance. Profiles are saved atomically through the control-side API; MIDI
learn does not yet have a Slint page. The serializer emits canonical modes
`note`, `cc-absolute`, `cc-relative-2c`, and `cc-relative-offset`; the parser
also accepts the documented legacy aliases `absolute`, `relative-twos`, and
`relative-offset` (plus their long forms).

## Feedback and overflow

Sessions produce generic RGB, indicator, context and text records only when the
descriptor supports them. Adapters decide the wire encoding. Queues are
bounded. Cache entries advance only after queue acceptance, CDC feedback is not
removed until its transport has capacity, and overflow guarantees a later full
resynchronization. Local page/modifier/pickup changes have their own revision;
reconnect/reset requests a full refresh. Logical LED IDs never imply physical chain positions. Contextual data
comes from `resolve_controller_context()`, which is also used by the SDL3 and
Slint adapters without requiring a connected controller.

`MidiMapper` registration is explicit and synchronized. A coordinator removes
each adapter before destroying its session, including failed endpoint setup and
shutdown, so the dispatcher cannot retain a stale adapter pointer. MIDI
feedback send failure retains one bounded pending message; disconnect/reconnect
invalidates queued feedback and regenerates a full authoritative refresh.

## Compatibility and limitations

CDC protocol 1.0, 1.1 and 1.2 framing, CRC, sequence and reconnect behavior is
preserved. Explicit 1.0/1.1 descriptors supply historical aliases because
those firmwares do not advertise a full inventory. A profile is validated as a
subset of its descriptor and cannot create hardware capability. No project format changed;
controller profiles remain user/device configuration.

`ControllerCoordinator` is operational in the appliance. Repeated
`--controller PATH` options create independent exact-path CDC endpoints;
corresponding repeated `--controller-id ID` values require exact negotiated
identities. MIDI profiles create independent sessions. Each endpoint owns its
reconnect, page, modifiers, pickup, feedback and diagnostics while sharing
`ApplicationService`; unknown devices are never auto-attached.
Text-display feedback has a generic representation but CDC 1.2 has only its
existing contextual-label encoding. RP2040 firmware and physical electrical,
timing, LED, reconnect and multi-device tests remain unimplemented.
