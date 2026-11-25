# Application discovery contract

## Assignment editor caching

Task 097B keeps picker metadata behind a structural fingerprint containing
action availability, parameter identity/name/unit/type/step/compatibility and
choices, plus stable track, pattern, macro and scene identities and labels.
Create, delete, rename, project replacement and descriptor metadata changes
therefore invalidate the picker. Mutable parameter values are deliberately not
cached and ordinary telemetry does not discard a pending form proposal.

## Assignment target projection

`AssignmentTargetPicker` is a bounded, copy-only projection of application
discovery plus registered action argument descriptors. It groups compatible
parameters, safe actions, contextual roles and Performance roles for multiple
editors. It does not form a second registry, retain mutable engine pointers or
own persistence. Object-valued argument choices carry stable identities rather
than display indices.

Performance capture reuses the arrangement-recording state and adds semantic
capture, scene-editing, and follow-configuration actions. Acknowledged launch
history is ordinary revisioned state rather than high-frequency telemetry.

Task 062 adds launch-slot and launch-scene objects, capacities, typed actions
and revisioned launcher state without prescribing layout.

Capability `live-looping` version 2 describes stable sessions, sources,
quantization, lifecycle, progress, phase and semantic actions.

Capability `audio-clips` version 2 exposes clip fades and stable
`audio-crossfade` objects without assuming arrangement geometry.

Stretch-capable sample regions and audio clips expose their existing stable
mode, source-tempo, direction, loop-boundary and crossfade fields. Effective
ratio and algorithmic latency remain presentation-neutral state. Reverse
stretch, loop continuity and deterministic seek are capabilities of the same
objects and semantic actions, not a UI-specific playback path.

Capability `audio-clips` version 1 exposes stable clip objects parented by
tracks, source availability diagnostics, and typed create, update, duplicate
and delete actions.

The `sample-processing` capability version 1 exposes operation types, bounded
job objects, typed editing/start/save/publish actions, progress and publication
status. PCM buffers are deliberately absent from ordinary state snapshots.

The `sample-analysis` capability exposes discoverable algorithm and session
objects, bounded session usage, candidate-edit/apply actions, ordinary state and
telemetry-rate progress. Waveform PCM is deliberately absent from discovery.

Capability `sample-regions` version 1 exposes region objects and semantic edit
actions. `sample.regions` and `sample.region-assignments` provide revisioned
metadata without placing waveform PCM in ordinary snapshots.

Preset clients consume presentation-neutral metadata, dependency lists,
validation diagnostics and typed `PresetLibrary` operations for synth, sample,
FX-chain and track-sound entries. See [PRESETS.md](PRESETS.md).

Capability `sound-presets` version 2 exposes application modes and semantic
validation, preparation, activation, cancellation, relinking, and portable
export actions. `preset.application` and `preset.missing-assets` are revisioned
bindings. Preparation success is explicitly distinct from audio activation.

NEUROGENIC exposes a presentation-neutral discovery document for native GUI,
UI Studio, controllers, MIDI mapping, Lua and future remote clients. Discovery
is a control-side view over `ApplicationService`, `ActionRegistry`, parameter
descriptors and copied subsystem snapshots. It is not another action system or
authoritative engine model.

Controller profiles validate action and parameter IDs against this metadata.
`ControllerSession` resolves selection-relative context without adding physical
controls to discovery or copying musical state. Slint, SDL3, CDC and MIDI
clients consequently share the same descriptors and semantic setters; device
capabilities remain in the separate
[controller descriptor](CONTROLLER_INTEGRATION.md).

Export capabilities and the current object model without audio or GUI hardware:

```sh
neurogenic-discovery --json
```

Output is deterministic for identical capabilities and state. Schema
`neurogenic.application-discovery.v1` has `schema_version: 1` and
`minimum_client_version: 1`.

## Document sections

- `capabilities`: stable feature ID, version, availability and reason.
- `object_types`: presentation-neutral create/delete/select capabilities.
- `objects`: typed identities, parent, label, availability and diagnostics.
- `parameters`: stable ID, parent, type, unit, bounds, default, base/effective
  values, access flags, automation/modulation eligibility, choices and
  presentation-neutral formatting/control hints.
- `actions`: the existing semantic registry with descriptions, availability
  and typed required/optional arguments.
- `state_bindings`: stable paths, value kinds and update-rate classification.
- `limits`: current usage and configured bounded maximum.
- `validation_errors`: graph and subsystem diagnostics.

Static metadata, ordinary state and telemetry are distinct. `static` changes
only with capabilities, `state` follows revisioned application changes, and
`telemetry` may update at a bounded display rate. PCM, waveform caches and
callback-owned pointers are never exported.

## Objects and identities

IDs use typed forms such as `track:17`, `fx-instance:42`, `modulator:91` and
`pattern:5`, plus singletons `project:current`, `transport:main`,
`arrangement:main` and `master:main`. Collection order is not identity. Missing
objects remain missing; clients must not bind to another object in their place.

The schema covers project, transport, tracks, instruments, sample slots,
patterns, clips, arrangement, groups, aux buses, master, FX processor types and
instances, modulator types and instances, modulation connections, macros,
scenes, recording, and stable logical MIDI ports. Unconnected MIDI ports remain
visible but unavailable so a project binding cannot silently move.

Native FX and modulator registries are type objects with parameter metadata.
FX and modulator instances additionally expose current instance-aware base and
effective parameter values. Existing DSP and semantic parameter IDs are
retained. The [typed binding contract](PARAMETER_BINDINGS.md) resolves these
descriptors and routes writes through the existing semantic actions.

## Actions and state bindings

Clients invoke the action ID and argument object through `ApplicationService`;
discovery describes that registry rather than inventing callbacks. Validation
and execution errors remain authoritative application errors. FX creation
accepts both legacy aliases and registered stable processor type IDs.

State paths cover project status, transport, selected track,
track/pattern/clip collections, mixer meters, modulator outputs, effective macro
values, recording, queued patterns and validation errors.
`validate_state_binding` rejects unknown paths rather than returning defaults.

## Future UI document references

A toolkit-neutral UI definition can reference discovered IDs:

```json
{
  "component": "continuous-control",
  "object": "fx-instance:42",
  "parameter": "fx.42.mix",
  "action": "effect.parameter-set.v1",
  "enabled_when": "project.dirty",
  "navigation_target": "fx-instance:42",
  "theme_token": "control.accent",
  "asset": "assets/filter.svg",
  "modifier": "shift"
}
```

Discovery does not prescribe component classes, layout, SVG rendering,
navigation pages, browser DOM behavior or controller positions.

## Compatibility and safety

Clients provide a supported version interval; it is compatible when it contains
the current version. Unknown JSON fields must be ignored. Unknown object,
parameter, action or capability IDs are unsupported and never substituted.
Future removals retain deprecated IDs or increment the schema when interpretation
cannot remain safe. Unsupported processors and absent objects carry diagnostics.

Assembly, validation and JSON serialization run outside the audio callback.
Discovery adds no callback allocation, locking, JSON work or DSP processing.

Current limitations: historical action descriptors do not all expose numeric
ranges or enum choices; text project metadata is action-driven rather than a
registered parameter; export is local rather than a network service; and no
declarative renderer or UI Studio editor exists yet.

Task 095 measures discovery separately from snapshots and parameter resolution.
Discovery has no independent revision token, so retain it only within a known
object/configuration revision and rediscover after structural edits. The
executable `neurogenic_client_independence_tests` verifies this contract.

# Hybrid playback state

Performance launch state API version 2 exposes each track owner, pending frame
and slot. `performance.return-arrangement.v1` returns one stable track or all
session tracks with immediate, quarter or bar quantization.
