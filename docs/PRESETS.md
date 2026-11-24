# Reusable sound presets

Preset capture includes only modulation owned by the captured instrument or FX
scope. Import remaps modulator and effect identities and repairs internal route
references; project-wide performance macros, scenes, controller mappings and
unrelated routing are excluded. Task 091 does not broaden the preset format to
capture project macros because their ownership is not safely portable as local
instrument state.

Processed assets are referenced exactly like other sample assets; preset save
does not embed a processing job or silently bundle its PCM. Portable export may
copy the resulting WAV through the existing explicit asset packaging path.

Sample presets retain accepted slice/region metadata; analysis sessions and
candidate confidence values are intentionally excluded from preset packages.

NEUROGENIC presets are independent of projects. `PresetLibrary` is the typed,
control-side API for saving, loading, enumerating, validating, duplicating,
deleting, exporting and importing them. Preset work never runs in the audio
callback.

## Format

Each preset is a directory named by a stable library ID:

```text
library/<preset-id>/
  manifest.ngp
  generations/generation-*/
    parameters.ngp
    samples.ngs          # when present
    effects.ngp          # when present
    modulation.ngm
```

The human-readable manifest begins with `NEUROGENIC_PRESET 1` and identifies
the preset type (`synth`, `sample`, `fx-chain`, or `track-sound`), descriptive
metadata, dependencies, active generation and track gain/pan. Subsystem files
retain their own versions. Unknown top-level versions are rejected.

A save validates first, writes a temporary generation, renames it into place,
and replaces the small manifest last. Failure before manifest replacement
leaves the previous generation active. Old generations are retained. This is
generation-atomic on one filesystem, not a claim of power-loss durability
beyond the filesystem's guarantees.

## Contents and ownership

- Synth patches contain base synth/voice parameters and compatible local
  modulation definitions. Active voices and oscillator phases are excluded.
- Sample instruments contain sample references, slices, nondestructive regions,
  pad assignments, playback/stretch/envelope settings, and compatible local
  modulation.
- FX-chain presets contain ordered processors, parameters, wet/dry, bypass and
  local modulation.
- Track sounds combine those compatible parts with track gain/pan. Project
  routing, group/master state, physical I/O and global scenes are excluded.

Authoring captures from an explicit stable track identity, not from whichever
track happens to be selected when a UI operation completes. Synth parameters
come from that instrument instance; sampler presets use portable
sample/slice/region configuration; FX-chain capture copies only the selected
track's ordered native instances; and local modulation is retained only when
all referenced modulator identities are inside the captured scope. Timeline
events, arrangement placement, launcher slots, scenes, unrelated routing,
master/group state, controller profiles and active DSP/voice state are never
captured. Performance-macro assignments are not represented by format version
1 and therefore remain project-only.

Preset IDs identify library entries. Effect, modulator and route IDs inside a
preset are template identities. `prepare_preset_import` allocates fresh runtime
IDs, rewrites internal references, validates the full prepared configurations,
and returns them without changing the active engine. External references are
rejected unless an importing client explicitly maps them. Unrelated instances
are never overwritten.

## Assets and portability

PCM is never embedded in metadata. Sample references may be absolute explicit
library dependencies or relative to the preset directory. Relative paths are
normalized and paths beginning with `..` are rejected. Missing assets are
reported by `validate_preset`; `require_assets=false` keeps metadata inspectable
for relinking.

`export_package` copies the versioned preset directory. `import_package`
validates it, assigns a caller-provided library ID and saves a new generation.
For a self-contained package, clients copy assets deliberately and store
relative paths; the library never silently copies a large sample collection.

## Validation and limits

Validation rejects malformed IDs, non-finite parameters, unsupported native
processors, invalid modulation graphs, unsafe paths and metadata files larger
than 16 MiB. Import checks the real-time budgets: 32 FX instances, four effects
per chain, 64 modulators and 128 routes. Capacity failure or a missing internal
reference leaves the supplied active configurations unchanged.

Metadata, dependencies, compatible targets and diagnostics are
presentation-neutral and suitable for discovery/UI clients. A visual preset
browser is outside this subsystem.

## Current limitations

The library does not automatically bundle PCM, garbage-collect retained preset
generations, translate third-party processor types, or provide cloud sharing.
Coordinated activation covers preset-owned sound state, not unrelated timeline
or project publications. UI authoring of FX presets currently targets track FX
owners; group, auxiliary and master capture remain unavailable. Relinking a
missing dependency updates the prepared activation transaction, but does not
rewrite the stored library entry.

## Transactional application

`ApplicationService` exposes `preset.capture.v1`, `preset.rename.v1`,
`preset.duplicate.v1`, `preset.delete.v1`, `preset.validate.v1`, `preset.prepare.v1`,
`preset.apply.v1`, `preset.cancel.v1`, `preset.query.v1`, relinking actions and
`preset.export-portable.v1`. Modes are `replace`, `insert-fx`,
`merge-modulation`, and validation-only `preview`.

Preparation resolves the target, rewrites track/instrument ownership, validates
capacities and destinations, decodes/resamples samples, and constructs one
bounded `PresetActivationRequest`. `apply` changes status only to `activating`.
Mixer/FX, modulation, synth parameters, sample versions, and pad mappings
become active together at the beginning of a later audio callback. Only after
the control side observes the callback's sequence acknowledgement does status
become `applied` and the project become dirty.

The activation pool has four slots. Queue/capacity failure happens before
active state changes. Old sample versions retire only after their voices finish.
The default policy preserves active voices; replacement may request release.
Cancellation is supported while prepared or queued; once the callback has
activated the snapshot it cannot be rolled back as a real-time operation.
Undo/Redo does not yet capture application as one history entry.

The Slint Sound and FX pages open the shared Browser and capture the current
stable source through `preset.capture.v1`. Browser enumeration and validation
run off the UI thread and provide save, rename, duplicate, explicit update and
confirmed deletion. Existing entries are never overwritten unless the separate
update confirmation dispatches `overwrite=true`. Writes retain the
generation-atomic `PresetLibrary` persistence contract. Browser's **Export
Portable** operation invokes the existing self-contained export service with a
touch-entered, non-existing package directory adjacent to the active library;
referenced WAV assets are copied and rewritten by that service, never by the
frontend. Arbitrary export-directory selection remains a Browser workflow gap.

## Relinking and self-contained export

Missing assets leave status at `awaiting-assets`. Exact-path and
directory-prefix mappings explicitly validate replacement WAV data. No file is
chosen automatically.

Self-contained export is opt-in. It validates WAVs, copies each distinct source
under `assets/`, prefixes duplicate basenames with a deterministic path hash,
rewrites references relative to the package, validates the staged result, and
renames it into place. It reports copied assets, bytes, and missing paths.
