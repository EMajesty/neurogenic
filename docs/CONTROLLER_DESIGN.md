# Designing a controller for NEUROGENIC

Contextual clients should consume descriptor roles rather than assuming a V2
position. `parameter` adjusts a base binding; `step-property` and `step-lock`
require captured musical ownership. Alternative controllers may expose any
subset, order or count and may keep ordinary immediate dispatch.

The Reference Controller V2 descriptor is a worked profile, not a required
inventory. Custom devices declare only their own stable logical controls and
need not expose pages, pads, SHIFT, eight encoders, or reference destinations.
Reference screen placement and firmware scan positions are deliberately absent
from `ControllerDeviceDescriptor`.

The V2 cluster is profile policy, not an API requirement. Its bindings use
ordinary descriptors, gesture configuration and semantic commands. A custom
device may bind actions directly and omit navigation, modifiers or pads.

Devices needing held modifiers or chords can opt into the bounded
[gesture ownership contract](INPUT_GESTURES.md). Simple MIDI profiles and direct
semantic clients do not have to emulate Reference Controller V2.

NEUROGENIC controllers are clients of the application semantic API. A control
surface reports logical input, a profile maps that input to discovered actions
or parameters, and authoritative application state drives feedback. Neither the
engine nor a project needs to know the controller's button count or geometry.
The [Reference Controller V2](REFERENCE_CONTROLLER_V2.md) is the official
reference instrument, not a mandatory layout.

See [controller integration](CONTROLLER_INTEGRATION.md), [controller profiles](CONTROLLERS.md),
and the [dedicated protocol](CONTROLLER_PROTOCOL.md) for the complete existing
contracts. This guide explains how to combine them.

## Supported extension mechanisms

### Logical controls and device descriptors

Use stable, device-local strings such as `play`, `encoder.cutoff`, or
`pad.03` as logical control identities. Physical matrix positions, MIDI note
numbers, and USB packet IDs belong in the transport adapter or firmware mapping,
not in musical actions. `ControllerDeviceDescriptor` declares transport,
protocol version, available controls, feedback, queue capacity, and brightness
limits. `ControlCapability` supports momentary and toggle buttons, relative
encoders, absolute pots, faders, crossfaders, joysticks, touch XY, keyboards,
and pointers. Supported feedback includes RGB or single LEDs, text, parameter
values, and declared motorized-fader capability.

A `ControllerProfile` then maps those logical IDs. Profiles are independent of
device descriptors: the descriptor says what exists; the profile says what it
does. The versioned `.ngc` format supports action, parameter, contextual-role,
and explicitly unassigned bindings, Shift/Select layers, relative or absolute
behavior, pickup, inversion, feedback destinations, labels, and typed bound
arguments. Existing examples live in [`examples/controllers`](../examples/controllers).

Profiles and descriptors are validated against each other and against the
current `ApplicationService`. An unknown control, action, parameter, argument,
or unavailable target is rejected rather than redirected.

### Discovery, typed arguments, and context

Call `discover_application()` or run `neurogenic-discovery --json` to enumerate
stable objects, parameters, actions, argument descriptors, state bindings, and
limits. See [Application Discovery](APPLICATION_DISCOVERY.md) and
[Parameter Bindings](PARAMETER_BINDINGS.md). Do not derive targets from display
order. Preserve stable object identities such as `track:17` and bind every
required action argument with its declared type.

Contextual assignments use the shared application context resolver. A page,
subview, bank, selection, or modal may change which assignments are available,
but a confirmed command retains its stable target. Context is application-side
policy, not firmware behavior. A custom controller may expose no contextual
controls, one data wheel, or many banks; it does not need to resemble the
reference panel.

Modifier and gesture ownership belongs above raw transport. Firmware should
provide reliable press and release events, encoder deltas, and simultaneous
inputs. `ControllerSession` owns momentary/latched modifier state and mapping
resolution. Modal ownership and held-control arbitration must prevent double
dispatch. The musical engine never measures button timing.

### Transport choices

- **Generic MIDI:** use the existing MIDI controller mapping/profile layer for
  notes, absolute CC, two's-complement relative CC, and offset relative CC.
  Musical MIDI routing remains separate from controller input. MIDI learn can
  capture supported messages, but conflict replacement remains explicit.
- **Dedicated USB CDC:** implement the bounded versioned protocol in
  [CONTROLLER_PROTOCOL.md](CONTROLLER_PROTOCOL.md). The current Linux adapter
  opens an explicitly configured TTY, negotiates capabilities and identity,
  and carries button, encoder, analog, RGB, context, heartbeat, and state-sync
  messages. It does not scan and silently substitute devices.
- **Simulated:** `SimulatedController` exercises the same session, mapper,
  application dispatch, and feedback paths without physical hardware.

Adding HID, OSC, a network transport, or another native protocol currently
requires C++ source development behind a narrow adapter. No external controller
plugin ABI or RPC controller API is implemented.

## Minimal working profile

The smallest supported approach is an `.ngc` profile over an existing generic
MIDI or dedicated descriptor:

```text
# NEUROGENIC_CONTROLLER 1
controller|my.surface|My Surface
control|play|momentary|Play|feedback=indicator
control|main|encoder|Main encoder
bind|play|action|transport.play.v1|feedback=play
bind|main|parameter|transport.tempo|label=Tempo
```

Start from [`minimal.ngc`](../examples/controllers/minimal.ngc) or
[`generic-midi-surface.ngc`](../examples/controllers/generic-midi-surface.ngc),
replace the exact device selector, then validate and load it through the Setup
controller workflow. The action and parameter IDs must exist in the discovery
document for the running application. This example uses existing profile
loading; it is not a dynamically loaded driver.

For a C++-level device, construct a `ControllerDeviceDescriptor`, a matching
`ControllerProfile`, validate them with `validate_controller_configuration()`,
and connect them through `ControllerCoordinator`. Keep transport reads/writes
outside the coordinator lock and outside the audio callback.

## Feedback, failure, and reconnection

Feedback is derived from copied revisioned application state. Send only changed
values, bound queues, expose drops, and request a full refresh after overflow or
reconnection. A device must not optimistically decide that a launch, recording,
or edit succeeded. Disconnection clears held modifiers; reconnection validates
identity and profile before resynchronizing current state. Explicitly configured
devices are never replaced with a different endpoint.

## Testing

Use `SimulatedController`, controller protocol tests, and deterministic engine
fixtures before hardware bring-up. Existing coverage demonstrates profile
parsing, typed arguments, contextual resolution, generic MIDI, bounded feedback,
reentrancy, removal/reconnection, and semantic dispatch. Run:

```sh
./build/dev/neurogenic-diagnostics --controller-protocol-test
ctest --test-dir build/dev --output-on-failure
```

Simulation is not physical validation. USB rollover, electrical behavior,
latency, illumination, encoder feel, and disconnect behavior still require the
procedures in [Hardware Testing](HARDWARE_TESTING.md).

`neurogenic_client_independence_tests` validates a deliberately non-reference
descriptor: three relative encoders, five buttons, one absolute fader, no pads
or page controls. Typed bindings, feedback and reconnection prove that this
geometry is not an application requirement.

## Current limits and planned work

The supported extension surface is C++ services, descriptors, `.ngc` profiles,
generic MIDI, USB CDC, and simulated endpoints. New transport implementations,
new semantic actions, and custom device-enumeration policy require source-level
development. Some administrative actions remain touch-preferred, descriptor
metadata is incomplete for a few historical actions, and the full Reference
Controller V2 gesture/layout integration remains planned work. The current CDC
protocol has a reference-oriented numeric inventory, but that inventory is
isolated in the descriptor/protocol adapter and is not an engine layout.
