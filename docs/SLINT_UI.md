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
  `sample_page.slint` and `sound_page.slint` are independent typed page
  components.
- `SlintFrontendAdapter` is renderer-independent. Its `SlintUiState` separates
  shell, contextual, Performance, Sequencer and Sample presentation models
  while all values remain copied from production application services.
- `slint_frontend.cpp` marshals adapter snapshots onto Slint's event loop.

The page uses flexible layout at both 1920x1200 and the 1024x600 minimum rather
than scaling a fixed canvas. Longer labels elide, inactive assignments are
visibly unavailable, and touch/mouse hit handling is provided by Slint.

## Input and state

The shell exposes Performance, Sequencer, Sample, Sound, Mixer, FX, Browser and
Setup. Performance, Sequencer, Sample and Sound are implemented; selecting another
destination keeps navigation operational and labels it as a placeholder.
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
Step presses invoke `step.toggle.v1`; pattern selection, creation/duplication,
resolution, velocity and duration changes use existing semantic actions. The
same 2x8 positions are the step-entry surface. The Arrangement subview reads
existing pattern and audio clips plus the loop range. It supports clip
selection only; unavailable DAW-style edits are intentionally absent.

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

## Current limits

This is a functional shell, not a replacement-complete frontend. Macro services
are represented by contextual mappings but do not yet have dedicated widgets.
Pattern-length editing is not exposed because the semantic API has no matching
action; arrangement clip movement and resizing are also deferred. The Sample
page uses path entry because no portable native picker is available, and
processing-job results require the existing explicit save/publish workflow.
The Sound preset view cannot enumerate, rename, delete or save presets until
those operations have semantic application actions. Modulation creation and
detailed graph editing remain deferred to the FX page.
Confirmation infrastructure exists, while project save/open dialogs and the
six non-Performance/Sequencer pages remain deferred. Some arrangement activity
lacks a single authoritative active-object field and is shown as `Unavailable`
rather than inferred. Physical touch quality, accessibility and screenshot
validation still require display hardware; model tests remain headless.
