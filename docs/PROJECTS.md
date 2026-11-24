# Project persistence

Committed performance captures persist as ordinary arrangement clips. Timeline
version 10 scene, slot, and follow definitions remain compatible; transient
capture queues and active session ownership are not serialized.

Timeline metadata version 10 stores launch slots, scenes and velocity policy.
Queued commands and active voices remain transient; older projects migrate to
an empty launcher without changing playback.

Live-loop sessions are transient. Completed clips use existing metadata;
durable restoration requires explicitly saving the recorded WAV asset.

Timeline v9 adds `audio_clip_fade` and `audio_crossfade` records. Version 8
clips migrate with unity clip envelopes and no crossfade references.

Timeline v8 persists audio clips and asset references. Runtime publication IDs
are rebuilt rather than serialized. Missing clip sources remain inspectable and
are never rebound to an unrelated slot automatically.

Processed WAV files use ordinary project sample references. Processing jobs,
temporary PCM, progress and unpublished results are transient and are never
embedded in project metadata.

Accepted analysis results persist as ordinary versioned slices/regions. Active
analysis jobs and unaccepted candidates are transient and are not project data.

Sample metadata version 4 persists stable regions, fades, loop boundaries and
region pad assignments. Older sidecars migrate with an empty region set and
unchanged playback. Grouped region Undo/Redo is not yet implemented.

Projects restore complete sessions; presets are smaller reusable sound
configurations. Applying a preset does not implicitly change project routing,
group or master ownership. See [PRESETS.md](PRESETS.md).

Capturing a preset does not dirty or mutate the current project. It copies the
supported instrument, local modulation and/or track FX state identified by a
stable source track into the separate preset library. Timeline events, track
identity, arrangement placement, launcher state and controller profiles remain
project-owned. Applying an acknowledged preset does dirty the destination
project as described below.

Acknowledged preset application marks the project dirty. Later saves include
the activated sample/slice state, mixer/FX, modulation and synth bases. Pending
jobs and callback activation records are transient. Preset application is not
yet a single timeline Undo/Redo entry.

Project format 5 uses timeline metadata version 7 to persist pattern
collections, clips, song and loop
ranges, stable-ID chain entries/repeats, follow actions, launch/voice policies,
song-end behavior and committed live-recording automation in `timeline.ngt`.
Project format 3/timeline version 6 loads with an empty automation lane; format
2/timeline version 5 loads with compatible chain defaults. Older versions migrate to arrangement
`legacy` mode without changing events or sound. Transient queued-boundary
timing, pending recording passes, active voices, and callback diagnostics are
not persisted.

Modulation metadata version 8 adds group owners, group follower taps and stable
group parameter destinations. Version 7 persists stable instances and owners plus graph
destination kinds, explicit owner and sharing metadata, and follower tap
identities. It adds native type settings, bounded step values, named source
output and destination input IDs. Version 7 adds voice definitions, velocity
and gate sources, and voice-scoped sample routes. Version-6 projects load
unchanged;
version-4 projects retain their implicit local routes and sound, and older
fixed-bank files migrate during load. Unknown future types are rejected rather
than substituted. Transient phase, envelope, voice state and prepared graph
order are never project data. See
[modulator instances](MODULATOR_INSTANCES.md).

Mixer metadata version 4 persists stable group definitions, exclusive track and
group routing, group mixer values, aux sends, and group FX chains in addition
to version-3 native track/aux/master chains. Older versions load with no groups
and retain their direct-to-master sound. Unsupported types are reported, not
substituted.

`midi.ngm` stores the selected clock source, logical input/output selectors,
clock output ports, external forwarding preference, route filters/remapping,
and output-latency metadata. Task 031 version-1 MIDI metadata remains readable;
version 2 adds forwarding without persisting PLL phase or timestamps.
Logical selectors are retained when devices are missing; loading never binds an
unrelated physical port automatically. Track MIDI destinations remain in the
timeline data.

NEUROGENIC projects use a directory format independent of SDL, Lua, and
controller layouts. Project format 5 adds optional `macros.ngm`
performance-macro and scene metadata. Formats through Task 046 load with an
empty collection. Macro metadata version 2 adds persisted scene interpolation
curves; version 1 scenes migrate to linear curves. Runtime morph progress,
absolute scheduling frames and smoothing state are intentionally excluded.

```text
My Set/
  manifest.ngp
  generations/
    generation-.../
      timeline.ngt
      samples.ngs
      parameters.ngp
      mixer.ngm
      scenes.ngr
      scene-<stable-id>.ngm
```

`manifest.ngp` names exactly one committed generation. A save validates a
control-side snapshot, writes a new temporary generation, renames that
generation into place, and replaces the manifest last. A failed write leaves
the previously referenced generation readable. Old and unreferenced
generations are retained; version 1 does not claim durability across every
filesystem/power-loss combination and does not automatically prune history.
Metadata is human-inspectable and bounded to 16 MiB per file. Versions newer
than the application understands are rejected.

The timeline file stores transport, generation seed, stable track/event IDs,
routing, notes, automation and parameter locks, probability, conditions, and
ratchets. The sample metadata stores slot references, stable slices, pad
assignments, expressive playback/envelope settings, and time-stretch settings.
The parameter file stores registered native synth/effect parameter values.
The optional mixer file stores track/input/master channels, two aux buses and
physical assignments. Older projects without it receive unity channels derived
from timeline IDs. Unavailable outputs remain assigned and silent rather than
being redirected.
Mixer metadata v2 adds external-loop mappings and latency while retaining v1
loading. The optional scene index names up to 32 stable routing snapshots, each
stored in the same validated mixer format. Older generations load with no
scenes.
Transient voices, playback position, monitoring counters, GUI caches, focus,
and controller state are deliberately excluded.

## Assets and relocation

Sample PCM is not embedded or copied. External references are the default.
Relative paths are resolved from the project directory, so a project and its
relative `assets/` directory can move together. Absolute paths remain absolute.
Relative paths that escape the project with `..` are rejected. Automatic asset
copying is deferred; users may place a sample under the project themselves and
store a relative reference.

Missing samples do not make a project unreadable. Their intended paths, slices,
pad assignments, and settings remain in metadata and are exposed in
`ApplicationState::missing_assets`. `project.relink-sample.v1` accepts the old
and replacement paths, prepares the replacement off the audio thread, waits for
callback acknowledgement, and restores the associated slice state. Saving an
unresolved project preserves that information.

## Lifecycle and activation

The shared C++ application service provides `project.new.v1`, `open`, `save`,
`save-as`, `close`, `relink-sample`, and `query` actions. State snapshots expose
the name/path, dirty flag, loading/saving flags, last error, and missing assets.
Timeline, slice, sample, pad, and registered parameter edits mark the project
dirty; transport position, monitoring, and GUI redraws do not.

Opening is staged on the control thread: metadata is parsed and validated,
available samples are decoded/resampled, audio-callback publication is
acknowledged, then slices, the complete timeline, routing, and parameters are
restored. Project switching stops transport and never starts playback
automatically. Missing slots remain silent while unaffected tracks are usable.
The audio callback performs no project I/O, parsing, allocation, locking, or
destruction.

Start explicitly with:

```bash
./build/dev/neurogenic --project /path/to/My\ Set --gui
```

`--project` cannot be combined with standalone sample or timeline inputs. In
the Display view the current project, dirty marker, path, and missing-asset
count are visible; Shift+Enter invokes Save. Open, Save As and explicit sample
relinking are available through semantic actions and the production Slint
Browser. Its worker enumerates deterministic pages and the touch keyboard
supplies names without a typed absolute path. Relinking never substitutes by
filename alone: the user selects both the missing identity and replacement.
Folder assistance checks only the original filename in the selected directory;
atomic multi-asset prefix relinking remains deferred.

Version-1 projects may include `modulation.ngm`. Its current metadata version 3
stores source definitions, deterministic seeds, explicit scopes, and stable
routes, including numeric sample/mixer/FX identities and follower settings.
Versions 1 and 2 remain readable. Absence means an empty matrix for backward compatibility. Unsafe or
unavailable destinations and unsupported voice destinations are rejected before
publication.

Timeline version 3 stores track-to-instance assignments and modulation metadata
version 3 stores destination identities and follower coefficients. Older projects and timelines migrate
their former singleton synth routing to shared instance 1. Instance parameter
overrides use stable `instance.N.synth.*` entries in parameter metadata. Active
voices, phases, envelopes, and random runtime state are never persisted.

Legacy `.ngt` timelines and version 1-3 slice sidecars remain accepted by their
existing loaders. They can be assembled into a project through the shared C++
services; there is not yet a one-click GUI migration wizard.

An audio-active recording without a filename is retained during Save as a
float32 `recordings/slot-ID.wav` asset and then referenced relatively. Failure
to write that asset aborts the new generation instead of silently dropping the
capture. Missing recorded assets use the same reporting and relinking path as
other samples.

## Appliance preferences and unsaved work

Appliance preferences are not project data. Versioned `appliance.conf` stores
reopen-last, last project path, preferred audio/MIDI device names,
channel/buffer preferences, fullscreen, and UI scale. `--project` overrides the
preference. A missing remembered project is a recoverable startup issue and
opens an empty session.

`project.new.v1`, `project.open.v1`, and `project.close.v1` reject a dirty
project unless the client saves first or explicitly passes `discard=true`.
Shutdown applies the same Save/Discard/Cancel choice. Save uses the existing
generation-based writer; no stronger power-loss guarantee is implied.

The Slint Browser invokes these same lifecycle actions. It presents explicit
dirty-session confirmation before passing `discard=true`; failed opens retain
the prior usable project and directory selection alone never mutates state.
