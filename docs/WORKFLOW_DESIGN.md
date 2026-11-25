# Designing musical workflows with NEUROGENIC

Reference page routing is one composition of existing semantic workflows.
Clients may group the same patterns, recording services, modulation objects and
Browser operations differently without translating them into V2 page indices.
Stable musical identities and semantic arguments remain the interoperability
boundary.

Multi-control workflows should compose logical controls and stable semantic
targets through [gesture ownership](INPUT_GESTURES.md), not physical positions
or a second musical-state authority.

NEUROGENIC's engine and application services provide musical objects and
validated semantic operations without prescribing one way to compose or
perform. A workflow is an ordered interaction policy over those operations: it
chooses what the musician sees, which stable object is targeted, when edits are
confirmed, and how feedback represents authoritative state.

The [Reference Controller V2](REFERENCE_CONTROLLER_V2.md) defines the official
reference instrument. Its pages, banks, gestures, and panel are one workflow,
not engine architecture.

## Compose workflows from existing semantics

Begin with [Application Discovery](APPLICATION_DISCOVERY.md). It inventories
available objects, semantic actions, typed arguments, parameter descriptors,
state bindings, and limits. Compose those operations through
`ApplicationService`; do not bypass them with direct engine mutation or create a
frontend-owned copy of patterns, routing, samples, modulation, or transport.

Useful composition rules are:

1. Select a stable target explicitly.
2. Validate capability and required arguments.
3. Prepare a transactional edit or publication outside the audio callback.
4. Invoke the semantic operation.
5. Observe the authoritative revision, acknowledgement, or failure.
6. Update feedback without confusing pending and active state.

Contexts, modes, and banks are presentation/controller policy. They may expose
different subsets of the same actions without changing musical ownership.
Profiles can bind physical controls; a frontend can provide custom navigation;
both should resolve to the same semantic target. Logical assignments remain
independent of MIDI note numbers, page indices, pad positions, and screen rows.

## Musical ownership boundaries

- Reusable `PatternDefinition` material and arrangement clips belong to the
  authoritative timeline. Selection is not playback activation. Launcher slots
  refer to stable musical objects rather than copying them.
- Samples use immutable callback-acknowledged versions. Editing metadata and
  pending publication must not masquerade as the active sample.
- Instruments, mixer routing, FX, modulation, macros, recording, and live loops
  remain owned by their application services and prepared engine publication.
- Projects own musical material, performance assignments, and persistent
  musical configuration. Appliance preferences and controller profiles own
  device selection and physical mapping. Focus, scroll, open modals, and most
  bank selections are presentation-local.

The unified [Application History](APPLICATION_HISTORY.md) is the one user-facing
chronological Undo/Redo authority for supported authoring operations. Group a
gesture into a meaningful transaction, keep its stable target fixed, and do not
record failed or cancelled requests as edits. Live performance gestures such as
macro movement need deliberate coalescing policy rather than hundreds of history
entries. Temporary performance-state restoration is designed but not yet an
implemented competing snapshot stack.

Recording workflows compose source/destination selection, arming, musical
boundary waiting, capture, finalization, immutable publication, and result
reuse. Modulation workflows compose discovered sources, compatible destinations,
validated routes, prepared graph publication, and acknowledgement. In both
cases the UI refresh clock must never become the musical timing authority.

## Conceptual instrument examples

These are design examples, not implemented alternative products.

### Tracker-oriented instrument

Present reusable pattern events as rows and columns, use a data encoder or
keyboard for row navigation, and map columns to discovered note, velocity,
duration, probability, condition, ratchet, microtiming, and parameter-lock
operations. Arrangement clips form the song order. The tracker stores no second
pattern grid: every edit targets the same stable pattern/track/event identity
used by session and arrangement playback.

### Performance sampler

Make the launcher and explicit logical pad assignments primary. Banks select
samples, slices, patterns, scenes, or live loops; shifted input selects without
triggering. Recording and processing use existing sample publication, while
macros and modulation expose performance controls. Projects own assignments;
generic MIDI or a custom surface merely invokes them.

### Recording and mixing workstation

Center the workflow on track arming, external/internal sources, arrangement
recording, audio clips, mixer channels, groups, auxiliaries, external FX, and
exports. A large timeline can be one view and the mixer another, but both use
the existing recording coordinator, arrangement services, mixer validation,
history, and project lifecycle. Hardware I/O is selected from discovered
capabilities rather than assumed channel counts.

### Experimental sound-design instrument

Lead with instrument/FX parameter discovery, modulation source and route
authoring, performance macros, scenes, sample processing, and XY or unusual
controllers. A custom controller may map relative encoders, touch surfaces, or
sensor adapters to validated parameters. Unsupported synthesis or modulation
algorithms still require engine development; workflow composition cannot invent
a DSP capability.

### Headless externally controlled instrument

Run without a graphical frontend and use configured MIDI controllers, the USB
CDC controller, transport/MIDI routing, Lua as a thin client where appropriate,
or a purpose-built in-process host. Diagnostics and discovery remain available.
There is currently no supported network RPC service, so remote control requires
source-level adapter work rather than connecting to an advertised endpoint.

## Extending semantic capabilities

If discovery lacks an operation but an authoritative application service already
supports it, add a thin typed semantic action to `ActionRegistry`, validate all
stable IDs and arguments, expose copied observable state, and test the complete
authoritative round trip. If the engine lacks the capability, add or extend a
reusable C++ service first, with prepared publication and deterministic tests;
do not hide a second implementation in a frontend, controller, or Lua script.

New action IDs are stable external identifiers. Preserve compatibility, provide
honest availability, define transaction/history behavior, and keep bounded work
off the audio callback. Controller and frontend access should then arise from
the same discovery metadata.

## What is supported, source-level, planned, or limited

**Supported now:** in-process C++ semantic actions and copied state; discovery;
parameter binding; reusable patterns, arrangement and launcher operations;
recording/live-loop services; modulation/macros; controller profiles; generic
MIDI; dedicated CDC; Slint, SDL3, and headless clients.

**Requires source development:** a new toolkit adapter, new transport, new
semantic capability, new DSP or sequencer behavior, out-of-process control, and
new persistence fields. The repository does not expose a stable plugin ABI.

**Planned:** broader frontend domain snapshots and verification, full Reference
Controller V2 interaction integration, temporary performance-state restoration,
remaining physical appliance validation, and explicitly tracked creative
features in the audit roadmap.

**Current limits:** some semantic descriptors remain incomplete; several
administrative workflows are easier through the reference touchscreen; strict
shutdown cannot interrupt arbitrary uninterruptible filesystem I/O; generated
interaction coverage is not exhaustive; hardware behavior remains separately
validated; and no declarative workflow language, RPC API, media database, or
interchangeable runtime UI/plugin framework exists.

The tracker-like portion of `neurogenic_client_independence_tests` is the
bounded executable example: it creates a reusable pattern, edits it through
semantic actions, arranges it, renders audible output, saves and reopens without
maintaining a competing client-owned musical grid.
