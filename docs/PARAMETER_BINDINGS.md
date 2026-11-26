# Typed parameter bindings

## Task 097D scheduled-lock evidence

The V2 production test authors `synth.cutoff` from a held later-page step,
verifies the other reusable pattern and stored base value remain unchanged,
compiles the prepared plan, observes the exact lock value in AudioEngine, then
saves, reloads and repeats playback in a fresh engine. The engine exposes only
bounded atomic diagnostic count/value observations; parameter discovery and
mutation remain outside the audio callback.

## Contextual pages

Sound, FX and modulation banks consume discovered descriptors; the built-in
synth retains its established ordering for profile compatibility. Intended
Sample, Slice, Song and Routing fields without a registered parameter remain
unavailable. Mutable values still come from the authoritative registry.

## Graphical assignment forms

Performance encoder assignments and Setup direct-parameter mappings resolve
through `AssignmentTargetPicker` and `ParameterBindingService`. The form shows
descriptor labels, units and validation metadata; pickup, inversion, fine
scale and acceleration configure the existing controller binding rather than a
frontend scaling path. Indexed Performance roles remain logical indirection,
whereas a direct parameter binding retains its stable parameter identity.

Task 096F reuses `ParameterBindingService` for manual absolute and relative
controller mappings and Performance assignments. The shared picker publishes
stable parameter identity, ranges and units but never caches the authoritative
value. Pickup, inversion, fine scale and acceleration remain properties of the
existing binding path rather than a second scaling implementation.

Context descriptors now distinguish ordinary `parameter` bindings from
`step-property` and `step-lock` roles. A held step never adjusts the stored base
parameter: the session captures a stable musical target and submits a validated
lock adjustment. Only numeric automatable destinations accepted by
`resolve_parameter_destination()` are exposed as compatible locks.

NEUROGENIC's binding layer is a control-side adapter over application
discovery and semantic actions. It does not provide another DSP control path.
`ParameterBindingService` resolves a `BindingReference`, reads the discovered
base/effective values, validates a typed proposal, and invokes the existing
semantic setter action.

## References and lifetime

A reference contains a target kind, object ID, parameter ID and the discovery
revision from which it was obtained. Explicit object IDs are never substituted.
If an FX, modulator, track or other object disappears, resolution returns a
structured `missing_object` or `stale_reference` error. A `selected_track`
reference resolves the current selection on every use; changing selection is
therefore intentional rebinding. State and telemetry paths resolve only when
listed by application discovery.

Errors distinguish missing/stale objects, unknown parameters or state paths,
type/range/choice failures, read-only values, unsupported operations, rejected
semantic actions and update-stream overflow.

## Descriptor and control semantics

Descriptors retain authoritative subsystem IDs, bounds, defaults, choices,
base/effective values and automation/modulation flags. Discovery adds neutral
format hints (`decimal`, `integer`, `percent`, `boolean`, or `choice`), display
precision, absolute/relative support, pickup recommendation and high-resolution
support. These hints do not prescribe a widget.

The generic control model supports:

- absolute native or normalized values;
- relative encoder steps;
- boolean toggle and momentary operation;
- 14-bit or other bounded integer normalization;
- nonmotorized-fader pickup with observable above/below direction.

Native values are range-checked before conversion to the existing normalized
semantic setter. Dynamic FX and modulator instance writes use their existing
instance-aware semantic actions. Presentation clients never receive DSP
pointers.

## Revisions and resynchronization

`BindingUpdateStream` separates capability, ordinary state and telemetry
notifications. Delivery is bounded. When capacity is exceeded, queued deltas
are replaced by one `resync_required` marker and a dropped-update counter;
clients must fetch a fresh discovery document and application snapshot, then
call `resynchronize`. No complete snapshot diffing, serialization or update
queue work occurs in the audio callback.

Typical client flow:

1. Export discovery and choose a compatible object/parameter descriptor.
2. Store its stable binding reference and discovery revision.
3. Resolve/read, then render its base or effective value.
4. Validate and apply input through `ParameterBindingService`.
5. Consume revision notifications; on overflow, explicitly resynchronize.
6. Show structured errors and keep missing explicit targets unbound.

## Current limitations

The binding stream reports revision classes, not per-field patches. Clients
still read the bounded application snapshot after notification. Historical
semantic action arguments do not all carry numeric ranges or enum choices;
parameter descriptors remain authoritative for value validation. Text-valued
project metadata uses semantic actions and is not yet represented by the
numeric `ParameterType` registry. No network transport, declarative renderer or
UI Studio editor is included.

The deterministic host test also reports control-side read cost. This measures
discovery/resolution overhead on the current machine and is not an audio-thread
or Raspberry Pi 5 performance claim.
## Profile-library bindings

Library candidates reuse `ParameterBindingService`; a profile stores stable
parameter identity and behavior metadata, never a copied authoritative value.
Explicit activation validates descriptor compatibility before the binding can
receive input. Bank changes affect logical Performance-role bindings but never
redirect direct parameter bindings.
