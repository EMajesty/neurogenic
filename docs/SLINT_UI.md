# Slint UI foundation

Slint 1.19.0 is the preferred frontend foundation. The existing SDL3 frontend
remains available while the Slint pages gain coverage. Neither frontend owns
musical state: both consume copied `ApplicationState` snapshots and invoke the
same semantic application actions.

## Build and run

Slint is deliberately opt-in. A normal dependency-enabled or headless build
does not resolve or compile Slint:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Build the frontend with the dedicated preset. The production executable uses
the same appliance lifecycle as SDL and headless operation:

```sh
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic --frontend slint --headless
./build/slint/neurogenic --frontend slint --fullscreen
# Development-only isolated adapter executable:
./build/slint/neurogenic-slint --width 1024 --height 600
```

The CMake integration first accepts an installed `Slint` 1.19 package and
otherwise fetches the pinned upstream `v1.19.0` source tag. Building from source
requires the Rust toolchain supported by that Slint release. Linux x86_64 and
AArch64 are supported by Slint and by NEUROGENIC's portable C++ baseline.
The source build disables jemalloc only for Slint's build-time compiler because
its configure probe is incompatible with Nix's wrapped GCC; this does not alter
the frontend runtime allocator.

For live preview, open `ui/slint/performance.slint` in `slint-viewer` or use the
official Slint editor extension. Preview shows presentation structure only;
real application state and semantic callbacks require `neurogenic-slint`.

## Structure

- `ui/slint/theme.slint` contains temporary presentation tokens.
- `ui/slint/components.slint` contains navigation, contextual controls,
  transport, track/content, parameter, waveform, notification and modal
  components.
- `ui/slint/performance.slint` contains the shared application shell and page
  registry. `performance_page.slint`, `sequencer_page.slint`,
  `sample_page.slint`, `sound_page.slint`, `mixer_page.slint`,
  `fx_page.slint` and `browser_page.slint` are independent typed page components.
- `SlintFrontendAdapter` is renderer-independent. Its `SlintUiState` separates
  shell, contextual, Performance, Sequencer and Sample presentation models
  while all values remain copied from production application services.
- `slint_frontend.cpp` marshals adapter snapshots onto Slint's event loop.

The page uses flexible layout at both 1920x1200 and the 1024x600 minimum rather
than scaling a fixed canvas. Longer labels elide, inactive assignments are
visibly unavailable, and touch/mouse hit handling is provided by Slint.

## Input and state

The shell exposes Performance, Sequencer, Sample, Sound, Mixer, FX, Browser and
Setup. All eight destinations have independent production page components.
Sequencer has Step and Arrangement subviews. Modulation remains under Sound/FX, and
MIDI/controller/system work under Setup.

Contextual buttons invoke the action IDs supplied by the shared controller
mapping. Encoders use `ParameterBindingService`; pads use `PerformanceGrid` and
then semantic commands when no performance launch slot is assigned. Assigned
performance slots dispatch `performance.slot-launch.v1`. Track navigation,
transport play/stop, and Return to Arrangement likewise use semantic actions.
Launch states and per-track arrangement/session ownership are copied from the
authoritative performance-launching snapshot. Record is visibly rejected
because there is not yet one unambiguous transport-record semantic action.

Development keyboard input maps Space to transport, Shift to the semantic
modifier, 1-8 to the first pad row and F1-F8 to registered pages. This is a
development convenience, not a physical-controller layout contract.

A 16 ms event-loop timer obtains copied revisioned snapshots. It does not touch
engine internals, perform audio callback work or establish another state
authority. Page-local focus and subview revisions are tracked separately, so a
subview or selection update is published without waiting for an unrelated
application revision. Shutdown stops the timer before releasing the adapter.

## Sequencer

The Step subview displays the selected track and pattern, length, resolution,
sixteen steps for the current page, playhead, selection and note presence.
Previous/next controls navigate arbitrary-length patterns in absolute 16-step
pages; a partial final page exposes unavailable cells instead of editable fake
steps. Optional playhead follow changes pages only when the playhead crosses a
page boundary. Manual paging disables follow so telemetry cannot steal the
editor's focus.

Step presses invoke `step.toggle.v1`. The compact inspector edits the selected
event's velocity, duration, pitch, probability, trigger condition, ratchet
count and timing offset through existing semantic actions. Parameter locks are
selected from discoverable modulatable parameter descriptors, then set,
adjusted or removed through `step.lock-*.v1`; the UI never asks for an internal
parameter string. Values are refreshed from authoritative snapshots.

Track controls select stable IDs, create sample or synth tracks, duplicate the
selected track, toggle mute and confirm deletion. Pattern controls select,
create, duplicate, queue, resize and confirm deletion. Shortening either a
track pattern or reusable arrangement pattern is rejected if it would exclude
events, rather than silently deleting notes. Deleting a referenced reusable
pattern is rejected by the timeline transaction, and a deleted selection
recovers to the first remaining pattern. Naming actions exist semantically but
touch text entry is deferred to Task 084. Track duplication copies the local
instrument, MIDI destination, mute state and legacy pattern; project-wide
routing, launcher assignments and modulation are not cloned.

The same 2x8 positions remain the step-entry surface without making the engine
depend on that layout. The Arrangement subview reads existing pattern and audio
clips plus the loop range. It supports clip selection only; unavailable
DAW-style edits are intentionally absent. Editing reusable pattern-track
contents independently of the selected track is also deferred.

Performance launch slots map to displayed positions by ascending stable slot
ID, independent of collection order. Empty positions are unavailable.
Recording presentation distinguishes Idle, Armed and Recording. Musical
position uses the configured denominator to determine beat length.

## Sample Editor

The Sample page is an application-service client, not an audio player or PCM
editor. Select a slot, enter a WAV path and load or replace it; the page updates
only after the immutable version becomes audio-active. The waveform consumes
the bounded stereo min/max overview already published by `SliceService`. Zoom
and horizontal position select a bounded view of that overview, so rendering
never scans PCM or retains a retired sample version.

Touch or click the waveform to insert a slice marker. Existing semantic actions
provide slice selection, deletion, pad assignment and sample/slice preview.
Regions expose precise start/end, loop and fade edits, and active voices keep
their copied region configuration. Transient analysis is asynchronous: Detect
creates a session, Accept atomically replaces the slice set, and Reject discards
the transient result. Normalize, Reverse, Trim and Fade create existing offline
processing jobs; their status and errors are shown without silently replacing
the active sample. Normal project saving persists accepted metadata.

At 1024x600 secondary metadata is compacted while waveform, essential editing
and contextual controls remain accessible. The 1920x1200 layout gives the
waveform and metadata more space. These are layout/model checks, not physical
touchscreen validation. The current application snapshot does not expose a
preview-playhead acknowledgement, so preview state is not fabricated.

## Sound and Instrument

The Sound page follows the selected track and exposes the two production native
instrument types: `sample.main` and `synth.subtractive.v1`. Instrument changes
use `track.instrument-set.v1`. The synth panel is generated from authoritative
parameter descriptors and edits the selected track's stable instrument instance
through `instrument.instance-parameter-set.v1`; unsupported controls are never
invented. Parameters are grouped as oscillator, filter, envelope and output.

For sample instruments, the page displays the selected pad's playback mode,
tuning and ADSR settings and dispatches existing pad semantic actions. Detailed
waveform, region and slice editing stays on the Sample page. The Modulation
subview lists track-owned assignments and supports bounded depth, enable and
remove actions. The Presets subview supports transactional validate, prepare,
apply and cancel with an explicit library path and preset ID.

## Mixer

The Mixer page is an independent `mixer_page.slint` component. It builds stable
track, group, auxiliary-return and master strips from copied `MixerState`
snapshots and never reads DSP objects or audio buffers. Selection, gain, pan,
mute, solo, aux sends, group levels and output routing use the existing
`mixer.*.v1` semantic actions. Eight-wide banking keeps the same component
usable at 1920x1200 and 1024x600. Peak and clip displays consume the mixer's
bounded non-real-time meter snapshot.

External FX rows show configured send/return pairs, wet level, return meters
and backend availability. The frontend does not infer extra physical outputs:
unsupported routes remain unavailable or produce the authoritative mixer
validation error. Complete external-loop remapping is deferred to FX/Setup.

## FX and modulation

`fx_page.slint` is an independent production page with Chain, Parameters,
Modulation, Macros and External FX subviews. `SlintFxPageState` copies stable
owner, effect, modulator, route and macro identities from `ApplicationState`;
the component never receives rack or graph runtime objects.

The Chain view selects track, group, aux or master owners and invokes the
existing create/insert/remove/delete/reorder/bypass actions. Parameters are
generated from native processor descriptors, including units, ranges and
automation/modulation eligibility. The Modulation view selects existing
sources, creates a route to the selected effect parameter, and edits route
depth, enable state and lifetime through graph actions. Macro values use the
performance-macro action, while External FX deliberately reuses the Mixer's
validated loop state and actions.

The page does not yet provide effect-preset browsing, modulator creation or
all macro-assignment fields. Those operations remain explicit unavailable
states rather than UI-side mutations. External loop pair and latency editing
remain semantic-API/Setup workflows; the page exposes availability and never
invents absent hardware channels.

## Browser and content management

`browser_page.slint` consumes `SlintBrowserPageState`; it never owns project,
sample or preset data. Projects, WAV samples, instrument presets and FX presets
are capability-backed categories. Listings are non-recursive, permission
errors are reported, unsupported files are filtered, and a control-side worker
publishes deterministic 128-row pages. Requests carry generation IDs, so a
slow result cannot replace a newer path; the bounded scan limit is 8192
entries. Filtering, sorting, parent navigation and refresh update only Browser
state.

Project open/save uses the project lifecycle service and dirty replacement
requires explicit confirmation. Loading a WAV targets the selected sample slot
and opens Sample only after dispatch through the immutable publication path.
Preset rows come from `PresetLibrary`; validation and prepared activation use
the existing semantic actions and show their destination before activation.

Directory and preset enumeration run away from both the UI and audio threads.
The single bounded worker accepts a superseding request, drops stale results,
and is joined on frontend shutdown. Recursive indexing, removable-media
watching and preset rename/delete/save actions remain deferred.

## Standalone text and file selection

`TouchKeyboard` is a shell-level modal shared by Browser, Sequencer, Sample and
Setup. Its temporary UTF-8 buffer supports insertion, code-point-safe cursor
movement/backspace, clear, case/symbol layers, confirm and cancel. Confirm
validates the operation and dispatches the authoritative semantic action;
cancel never mutates project state. Filenames reject path separators and the
special `.`/`..` values. Existing files require explicit confirmation.

Project open/save-as, new-project naming, sample selection, track/pattern
renaming, processing/recording export names and controller-profile save use the
shared Browser/keyboard workflow. Sample loading is initiated from Browser and
targets the already selected slot, avoiding raw absolute-path entry.

Missing project samples appear in the Browser inspector. The user explicitly
selects a missing identity and a candidate WAV before invoking the existing
transactional relink action. “Map this folder” proposes only
`selected-directory/original-filename`; it does not silently search by name or
perform an atomic multi-asset prefix rewrite.

## Current limits

This is a functional shell, not a replacement-complete frontend. Macro services
are represented by contextual mappings but do not yet have dedicated widgets.
Pattern-length editing is not exposed because the semantic API has no matching
action; arrangement clip movement and resizing are also deferred. The Sample
page uses the shared Browser instead of raw path entry, and processing-job
results use the explicit keyboard-named save/publish workflow.
Preset rename, delete and save remain unavailable until those operations have
semantic application actions. Modulation creation and
detailed graph editing remain deferred to the FX page.
Browser confirmation covers dirty project replacement, while a portable native
file picker remains deferred. Some arrangement activity
lacks a single authoritative active-object field and is shown as `Unavailable`
rather than inferred. Physical touch quality, accessibility and screenshot
validation still require display hardware; model tests remain headless.

## Setup and system configuration

`setup_page.slint` consumes `SlintSetupPageState` and is divided into Audio,
MIDI, Controllers, Preferences, Diagnostics and System subviews. The production
appliance supplies a copy-only runtime snapshot; Slint never receives an
`AudioDevice`, MIDI endpoint, controller session or mutable DSP object.

Audio device, channel-count and buffer changes are saved as requested appliance
preferences and marked **restart required**. Active negotiated values remain
separate. NEUROGENIC stays fixed at 48 kHz and the page offers no engine-rate
selector. Fullscreen and UI scale are persistent next-start preferences.

MIDI clock source changes use `midi.clock-source-set.v1`. Controller MIDI learn
and `.ngc` profile save go through synchronized `ControllerCoordinator`
operations. Ordinary musical MIDI routing stays independent. The initial
mapping inspector can re-learn a physical MIDI message for an existing logical
profile control; semantic binding creation/removal remains a profile-file
workflow.

Diagnostics present existing bounded callback-load and backend-XRUN telemetry;
unavailable metrics stay visibly unavailable. Graceful exit uses the same
dirty-project policy as window close. No privileged system operation is exposed.

The eight pages now cover Performance, Sequencer, Sample, Sound, Mixer, FX,
Browser and Setup. A touchscreen keyboard, native file picker, safe live audio
device replacement, full MIDI route editing and graphical controller-layout
editing remain deferred. Styling remains provisional.

## Recording and processing workflows

The global Record control opens explicit mode, source, destination and timing
configuration. `RecordingWorkflowCoordinator` composes existing external,
resampling, live-loop, arrangement and performance-capture actions; it does not
own a recorder. The Sample page exposes processing progress, cancellation,
explicit WAV save and explicit destination publication, and only offers the
published result after immutable-version acknowledgement.

The `slint-production-build-and-test` CI job configures pinned Slint 1.19,
compiles generated components, links the production appliance and runs the
Slint-enabled CTest suite. Headless builds remain independent. Rendered UI
interaction testing remains Task 082.

## Incremental publication and generated-component validation

Task 082 separates structural rebuilds from transport, meter, playhead,
recording/processing and parameter telemetry. Stable Slint models update by row
when shape is unchanged, and inactive page models are not republished for
telemetry-only refreshes. Page activation performs a full authoritative refresh.
See [SLINT_FRONTEND_PERFORMANCE.md](SLINT_FRONTEND_PERFORMANCE.md) for commands,
measurements and exact coverage.

Before appliance release, manually verify at 1920x1200 and 1024x600: touch
activation; scrolling to every essential action; focus/back behavior; recording
confirmation/cancellation; contextual labels against the physical controller;
clipping; and readable meters/playheads. Repeat mouse-only and touchscreen-only.
Automated software rendering is not physical touchscreen validation.
