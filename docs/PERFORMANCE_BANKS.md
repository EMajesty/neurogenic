# Project-owned Performance banks

## Shared graphical authoring

Task 096F connects the Performance editor to the shared discovery target model.
Parameter assignments and the explicit Performance-safe semantic-action subset
are validated by stable identity; action arguments are parsed from registered
boolean, integer, real and text descriptors before the existing project
transaction is invoked. Confirmation replaces an assignment atomically,
cancellation preserves it, and controller profile data is never written into
the project bank model.

The production page currently provides representative P1, SHIFT+P1 and F1 entry
points. The service and adapter accept arbitrary logical positions, while a
complete graphical position chooser remains follow-up work.

Performance banks are the project's portable live-control surface. They map
logical positions to existing parameters, semantic actions and launcher slots;
they do not own parameter values, duplicate musical material, or describe a
physical controller. Reference Controller V2 presents eight primary and eight
SHIFT encoders/functions plus sixteen pads, while other clients may expose any
subset of the same logical positions.

## Ownership and limits

`SongArrangement::performance_banks` is authoritative and participates in the
existing timeline transaction, prepared publication and unified history paths.
The model uses stable `PerformanceBankId` and globally stable
`PerformanceAssignmentId` values. A project may contain 32 banks. Each bank is
bounded to 64 continuous assignments, 64 action assignments and 128 pad
assignments. Positions are unsigned logical identities up to 65535; an absent
record is explicitly unassigned.

Active-bank and selected-assignment state are presentation state. Changing a
bank changes bindings, not musical values, and therefore does not create a
history entry. Bank definitions and assignment edits are persistent authoring
transactions and are reversible through the one application history.

## Assignment types

Continuous assignments retain a stable parameter identifier, layer, logical
position, optional label and positive adjustment scale. Numeric discovered
instrument, sample, mixer, FX, modulation parameters and performance macro
values are supported. Values always come from their authoritative registry or
macro service; the bank stores no copied live value. Multi-destination control
uses a Performance macro rather than a second scaling graph.

Action assignments retain a registered semantic action, typed arguments,
behavior (`one-shot`, `toggle`, or `momentary`), layer and optional label. The
authoring service accepts only an explicit performance-safe allowlist: launcher
start/stop/retrigger, scene recall, Fill, macro control/scene recall, supported
mute/bypass and transport play/stop. Filesystem, project replacement,
administrative and destructive actions cannot be assigned. The initial
momentary compatibility contract is implemented for `performance.fill-set.v1`;
its captured release target survives SHIFT changes and is cancelled on bank,
page, modal or connection ownership loss.

Pad assignments reference existing stable `LaunchSlotId` values. The launcher
remains the only playback authority. SHIFT+pad selects the assignment without
launching it. Deleting a referenced slot leaves an unavailable, repairable
assignment rather than redirecting or deleting it.

## Layers and navigation

On Performance, an encoder delta resolves the effective primary or SHIFT layer
at the start of that atomic delta. There is no fallback from an unassigned
secondary position. Outside Performance, SHIFT retains its fine-adjustment
meaning. A Performance assignment may set a smaller explicit scale when fine
control is desired.

Banks support direct selection and previous/next semantic actions. The Slint
reference page exposes named bank selection and create/duplicate/delete
controls without permanently consuming F7/F8. Alternative controllers may map
the same bank actions or address selected logical positions directly.

## Persistence and migration

Timeline format 12 stores banks and typed arguments. Version 11 and older
projects deterministically migrate explicit `LaunchSlot::logical_position`
values into a named `Performance` bank in memory; merely loading does not
rewrite the source project. Projects with no bank retain the legacy global pad
surface until the musician creates a bank. Bank restoration requires no
controller profile and no Slint frontend.

Renames and reordering preserve identities. Missing parameters, actions or
launcher targets remain visible as unavailable. Replacement with an explicit
compatible assignment reuses its assignment identity; no name-only or
collection-index redirection occurs.

## Examples

### Synth performance

- P1: `synth.cutoff`, label `Tone`.
- SHIFT+P1: `synth.resonance`, label `Reso`.
- P2: `macro.<id>.value`, label `Energy`, controlling the macro's existing
  multiple destinations.
- F1 momentary: `performance.fill-set.v1`.
- SHIFT+F1: `transport.play.v1`.
- Pads: stable pattern launch slots.

### Mixer and scene performance

- P1/P2: discovered selected-channel gain and pan.
- P3: a discovered FX wet parameter.
- F1: a validated track mute toggle with its stable track argument.
- F2: `macro.scene-recall.v1` with the stable scene ID.
- Pads: pattern/audio/live-loop launch slots shared with the session launcher.

## Current presentation limits

The production Slint page exposes bank lifecycle, direct selection, pad
assignment and the shared primary/secondary P/F descriptors. Parameter
assignment is available through the adapter/semantic path; the full
category/object/typed-argument picker and shared-keyboard label workflow still
needs the Task 096F target-selector reuse. Toggle-state discovery is limited to
actions whose authoritative state is already published. Physical hardware and
touchscreen validation are **NOT RUN**.
