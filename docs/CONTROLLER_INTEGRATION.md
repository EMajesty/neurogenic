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

Each `ControllerSession` owns its profile, local page, modifiers, pickup state,
feedback cache/queue, counters and resynchronization state. Musical state is
never copied into the session. Page and Shift changes on one surface do not
change another surface. Application selection remains global today; local
pages are per device. Stable and selection-relative parameter resolution uses
the same application descriptors and semantic setters as GUI clients.

The built-in dedicated profile retains protocol compatibility aliases while
mapping current controls through profile data. Cluster positions not assigned
by that profile are explicitly inert. These assignments are defaults, not
firmware semantics, and may be replaced by a user profile.

## MIDI surfaces

`MidiControlSurfaceAdapter` opt-in mappings translate a named port number,
channel, note/CC and configured mode into a session control ID. Supported modes
are note momentary, CC absolute, two's-complement relative and binary-offset
relative. Unmatched MIDI continues through the existing musical note/raw MIDI
path, so a keyboard is not implicitly treated as a control surface. Existing
MIDI device selectors provide exact device matching and reconnect behavior.
MIDI learn remains a control-side profile authoring operation to be completed;
there is no automatic learning or broad forwarding.

## Feedback and overflow

Sessions produce generic RGB, indicator, context and text records only when the
descriptor supports them. Adapters decide the wire encoding. Queues are
bounded; overflow sets `resync_required`, and reconnect/reset requests a full
refresh. Logical LED IDs never imply physical chain positions. Contextual data
comes from `resolve_controller_context()`, which is also used by the SDL3 and
Slint adapters without requiring a connected controller.

## Compatibility and limitations

CDC protocol 1.0, 1.1 and 1.2 framing, CRC, sequence and reconnect behavior is
preserved. The host-side 1.x profile supplies historical aliases because those
firmwares do not advertise a full inventory. No project format changed;
controller profiles remain user/device configuration.

Multiple in-process sessions and simulated/MIDI surfaces are covered by tests.
The serial appliance currently owns one `ControllerHost` per configured path;
multi-device enumeration/ownership policy belongs in the appliance coordinator.
Text-display feedback has a generic representation but CDC 1.2 has only its
existing contextual-label encoding. RP2040 firmware and physical electrical,
timing, LED, reconnect and multi-device tests remain unimplemented.
