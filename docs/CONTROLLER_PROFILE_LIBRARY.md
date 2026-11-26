# Controller profile library

## Ownership

`ControllerProfileLibrary` is appliance configuration composed beside
`ControllerCoordinator`. It owns bounded profile definitions and stable
endpoint-to-profile associations. `ControllerCoordinator` continues to own
active sessions and transports; projects continue to own Performance banks and
musical objects. Profile operations never enter musical Undo/Redo.

The library distinguishes device descriptors, immutable physical endpoint
identities, profile definitions, library entries and active sessions. Profile
IDs are stable restricted identifiers; names are mutable presentation. The
capacity is 64 entries. Built-in entries are read-only and can be duplicated
into independently identified user entries.

## Lifecycle and validation

The production API supports create, rename, duplicate, inspect, replace,
associate, activate, import/export, save/reload, remove and known-valid
recovery. Every candidate is validated against its registered device descriptor
and the application action/parameter registry before it replaces an entry or
active session. A duplicate copies mapping configuration but receives a new
profile identity.

Activation is explicit per endpoint. Editing a shared definition does not
silently replace associated sessions; each endpoint must explicitly activate
the new revision. Activation constructs and validates the replacement adapter,
cancels the prior adapter, advances the new session revision beyond the old
one, swaps under the coordinator lock, removes the old surface outside that
critical section, refreshes feedback, and records the association. Other
endpoints are unaffected. An invalid candidate leaves the old session intact.

Removing an associated profile fails unless the caller explicitly authorizes
active removal. Explicit active removal clears associations and does not select
an unrelated fallback.

## Persistence and recovery

Profile definitions remain ordinary `.ngc` files. `library.ngcl` schema 1 is a
bounded appliance manifest containing escaped names, stable IDs, descriptor
IDs, built-in flags and endpoint associations. Writes use sibling temporary
files and atomic rename. Each saved definition also has a
`.known-valid.ngc` recovery copy. Loading validates the complete candidate
manifest before replacing live library state and uses the known-valid copy if a
profile file is corrupt. Imports are parsed without modifying the source.

Missing device descriptors are configuration unavailability, while missing
project musical targets are dispatch-time target unavailability. Neither is
silently redirected by name or collection order. Logical Performance-role
mappings remain valid when a project position is unassigned.

## Mapping authoring

The Setup editor uses the shared target picker and now publishes every bounded
descriptor argument rather than four fixed fields. The same dynamic argument
model is used by Performance authoring. `performance.action-assign.v1` accepts
up to 16 descriptor pairs, matching the bounded form and persisted argument
map. Boolean, integer, real, text, enumeration and stable-object choices still
use `AssignmentTargetPicker` parsing and validation.

Mapping forms expose global or stable context scope, primary/secondary SHIFT
layer, SELECT condition, pickup, inversion, fine scale, acceleration and a
logical feedback destination. Candidate profile validation rejects feedback
destinations absent from the descriptor or without feedback capability. The
historical `parameter.display.*` virtual display destination remains an
explicit legacy compatibility exception.

MIDI learn and manual sources share the same proposal path. The Setup model
shows the captured endpoint/port, channel, number and mode before confirmation.
Learning does not replace a mapping until explicit confirmation; cancellation
clears learn ownership.

## Frontend and alternatives

Setup presents the library list, status, associations, create/rename/duplicate,
activate, import/export, save/reload, recovery and removal controls. The list
and argument forms are scrollable at 1920x1200 and 1024x600. Generated
callbacks invoke the same appliance operations used by non-Slint clients.
Text entry may use the shared keyboard. No Slint type is required by the core
library, and generic MIDI, legacy dedicated profiles and direct semantic
clients remain independent.

Physical USB and PCB behavior is not established by these simulated and
generated tests. See the Task 097F validation report for exact evidence.
