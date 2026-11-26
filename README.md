<p align="center">
  <img src="docs/assets/readme/vibeslop.png" alt="Absolute vibeslop" width="512">
</p>

NEUROGENIC supports reusable multi-track patterns, quantized live launching,
and linear clip arrangements through one authoritative timeline. Live patterns
support repeat chains, deterministic follow actions, exact quantized launches,
and explicit outgoing-voice policies. See
[Patterns, clips, and arrangements](docs/ARRANGEMENT.md).

The mixer supports bounded native track, group, aux and master effect chains. See
[Native FX chains](docs/FX_CHAINS.md).

# NEUROGENIC

See [metronome/count-in behavior](docs/METRONOME_AND_COUNT_IN.md) and the
[shared functional visual language](docs/VISUAL_LANGUAGE.md).
Track isolation, recording ownership and emergency silence are documented in
[Track solo and arm](docs/TRACK_SOLO_AND_ARM.md) and
[Unified panic](docs/PANIC.md).

## Build your own instrument

NEUROGENIC is designed to be independent of any particular hardware layout,
graphical interface or musical workflow. The reference instrument is one
implementation of the platform, not a requirement.

- **Want to design your own control surface?** See [Controller Design](docs/CONTROLLER_DESIGN.md).
- **Want to create your own graphical interface?** See [Frontend Design](docs/FRONTEND_DESIGN.md).
- **Want to rethink how a musical instrument works?** See [Workflow Design](docs/WORKFLOW_DESIGN.md).

Build a different instrument around the same engine, combine your own hardware
and software interfaces, or operate entirely without a graphical frontend.

For an example of a complete instrument design, see the
[Reference Controller V2 specification](docs/REFERENCE_CONTROLLER_V2.md).

Performance launching now coordinates stable pattern, audio-clip and completed
live-loop slots plus multi-track scenes through the existing timeline and audio
paths. See [docs/PERFORMANCE_LAUNCHING.md](docs/PERFORMANCE_LAUNCHING.md).

Offline sample processing now renders whole samples, slices and regions through
ordered trim, normalization, reverse, gain, fade, silence, resampling and
channel-conversion jobs. Results are new immutable WAV assets and become active
only after callback acknowledgement. See [sample processing](docs/SAMPLE_PROCESSING.md).

Offline sample analysis now provides version-aware transient, quiet-gap, equal
and tempo-grid boundary proposals before nondestructive application as slices or
regions. See [sample analysis](docs/SAMPLE_ANALYSIS.md).

Inspect the presentation-neutral application API without audio or GUI hardware:

```sh
./build/dev/neurogenic-discovery --json
```

See [application discovery](docs/APPLICATION_DISCOVERY.md) for the schema,
stable identities and future UI Studio binding contract.

Dynamic modulation sources are discoverable stable instances owned by tracks,
groups, aux buses or master; see
[docs/MODULATOR_INSTANCES.md](docs/MODULATOR_INSTANCES.md) for the API,
capacity, persistence and migration model.
Cross-track and modulator-to-modulator routing is documented in
[docs/MODULATION_GRAPH.md](docs/MODULATION_GRAPH.md).

The dedicated controller host interface uses a versioned USB CDC binary
protocol; see [controller protocol](docs/CONTROLLER_PROTOCOL.md). Launch it with
`--controller /dev/serial/by-id/...`. Controller hardware remains optional.

NEUROGENIC is an open-source, hardware-independent C++20 audio and MIDI
instrument engine. Production paths include sample and synth playback, reusable
patterns and arrangements, recording, mixing, modulation, native FX, MIDI and
controller integration, with deterministic offline rendering and tests. Lua 5.4
through sol2 remains an optional thin client rather than a required workflow.

The engine now includes a bounded per-track mixer, eight stable stereo group
buses, two stereo aux buses,
master routing, six-channel-capable device adaptation and track/aux/master
recording taps plus group pre/post taps. Group routing, processing and
recording are described in
[group buses](docs/GROUP_BUSES.md). See also [mixer and routing](docs/MIXER.md).
Two disabled-by-default external FX loops can route track, group, aux, or master audio
through physical mono/stereo sends and returns with wet/dry control, manual
latency compensation, recording taps and recallable routing scenes. Routing
uses the same application service and atomic mixer publication; consult the
mixer document before connecting external hardware.

This is foundation software, not a DAW or a finished groovebox. The engine does
not assume a UI, controller, or hardware layout. Linux x86_64 and AArch64 are
supported build targets; portable C++ and narrow backend interfaces remain the
default on both.

The sequencing foundation uses a UI-independent 960-PPQ timeline with stable
events, arbitrary pattern lengths, independent tracks, mute state, duration
notes, and deterministic sample-accurate loop scheduling. The 16-step Lua API
is retained as a compatibility workflow rather than the fundamental data model.

Step edits support shared bounded Undo/Redo, grouped transactions, and stable
event editing of velocity, duration, pitch, and tick-relative timing. GUI and
controller clients call the same C++ timeline service; Lua is optional.

Selected steps can hold multiple synth or delay parameter locks. Locks are
stable timeline events, participate in shared Undo/Redo, run sample-accurately
before simultaneous note-ons, and persist until another event changes the
destination. The GUI marks locked cells with `L`; use Shift with parameter
editing to add, adjust or remove locks.

Timeline and MIDI notes can also drive the first native subtractive synthesizer
through the same scheduler. It provides bounded polyphony, four oscillator
shapes, ADSR, a resonant low-pass filter, velocity response and a native stereo
delay. See the [instrument and DSP architecture](docs/INSTRUMENTS.md).

A bounded [modulation matrix](docs/MODULATION.md) combines stored base values,
timeline automation and bipolar modulation without overwriting edits. Sources
have explicit global or independent per-track scope. Voice scope supports synth
pitch, cutoff, and gain through independent per-voice ADSR, velocity, and
note-triggered LFO state. Tracks may use independent bounded synth instances or
deliberately share one.
Sample pitch/gain/pan, mixer gain/pan/aux sends, aux/master gain and external-FX
wet/send/return now use the same bounded matrix without overwriting stored
settings or republishing routing topology.

The Samples view provides the first complete chopping workflow: immutable
loaded samples are displayed through cached waveform peaks, slices use prepared
engine-rate frame coordinates, and pads 1–16 map independently to slices while
the original note-to-slot mapping remains the fallback. Select or drag markers,
press Enter to audition, Shift+Enter for 16 equal divisions, number keys to
assign pads, and Escape to stop preview. See
[sample slicing](docs/SAMPLES.md#sample-slicing).
Pads and slices now support one-shot, Gate and Toggle playback, 16 choke groups,
playback-rate semitone/cent tuning, gain, reverse and per-voice ADSR. The Samples
inspector and the same semantic application actions expose these controls; PAD,
16 LEVELS and 16 NOTES respectively trigger the selected assignment, apply
sixteen velocities, or apply chromatic playback-rate offsets.

Optional realtime pitch-preserving time stretching uses a bounded two-grain
overlap/add path while the original playback-rate path remains the default.
Pads may use an explicit 0.25–4× duration ratio or derive it from source beats
or BPM and project tempo, with independent pitch. See
[sample stretching](docs/SAMPLES.md#pitch-preserving-time-stretching).

## Build and run

### Nix development environment

The committed flake exposes packages, applications, checks, and development
shells for `x86_64-linux` and `aarch64-linux`, and pins nixpkgs, miniaudio,
libremidi, Lua, and sol2 in `flake.lock`:

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
./build/dev/neurogenic_benchmark
./build/dev/neurogenic_workflows all
./build/dev/neurogenic --stats
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --headless --list-devices
./build/dev/neurogenic-diagnostics --json
./build/dev/neurogenic-diagnostics --headless --stress 10
./build/dev/neurogenic --gui --reopen-last
./build/dev/neurogenic --synth --headless
./build/dev/neurogenic path/to/sample.wav
./build/dev/neurogenic --interactive
./build/dev/neurogenic --gui --headless
./build/dev/neurogenic --gui --ui-scale 1.5 --headless
./build/dev/neurogenic --fullscreen --slot 0=samples/kick.wav
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic --frontend slint --headless
```

Slint is the production appliance frontend and the default Nix package includes
it. It remains an optional CMake build feature so lightweight headless and SDL3
development do not require the Slint dependency closure. `--frontend slint`
selects it; `--frontend sdl` or the legacy `--gui` selects SDL3, and
`--frontend headless` selects no graphical loop. Its application shell,
eight-page implementation and reusable component
architecture are documented in [docs/SLINT_UI.md](docs/SLINT_UI.md). The SDL3
graphical frontend is preserved; omit `--gui` for the existing CLI or headless
application. See [docs/GUI.md](docs/GUI.md) for SDL3 controls and scaling.

An additional pinned direct-display build is available without changing the
default desktop package:

```sh
nix build .#kms
SLINT_BACKEND=linuxkms-software ./result/bin/neurogenic-kms
# GPU candidate, validated on the target before adoption:
SLINT_BACKEND=linuxkms-femtovg ./result/bin/neurogenic-kms
```

It compiles Slint 1.19 LinuxKMS with libseat/libinput, software and FemtoVG.
Building it does not establish physical DRM, touchscreen or GPU compatibility.
See [LinuxKMS](docs/os/LINUXKMS.md) and the separate
[OS roadmap](docs/os/ROADMAP.md).
In the Sequencer view, click/tap a cell or select it with arrows and press
Enter to toggle a note. Shift+arrows change page or target note; the resolution
control cycles quarter through 32nd-note straight and triplet grids.
The default scale is automatic (1.5× at the 1920 × 1200 reference target).
Use `--ui-scale 1.0` through `2.5`, `--reset-ui-scale`, the Display view, or
the `+`, `-`, `0`, and `F11` keys to change persistent display settings.
The compact status line and Performance view consume the shared monitoring
snapshot. `--stats` emits `neurogenic.performance.v1` JSON and the benchmark
emits JSON Lines for commit comparisons. See
[docs/BENCHMARKS.md](docs/BENCHMARKS.md) for metric semantics and limitations.
The bounded [workflow suite](docs/WORKFLOWS.md) exercises beat, synth,
resampling, external-routing, MIDI and controller/GUI paths through production
application services and runs in CTest without hardware.
Physical bring-up commands and report schemas are documented in
[docs/HARDWARE_TESTING.md](docs/HARDWARE_TESTING.md).
Standalone preferences, dirty-session shutdown protection, and the optional
launcher/user-service examples are documented there as well. Nothing installs
or enables a service automatically.

With direnv installed, run `direnv allow` once; `.envrc` enters the same flake
shell. Build the installable package with `nix build`, run it with
`nix run . -- --list-devices`, and run all locked build/test checks with
`nix flake check`.

For ordinary iteration, enter `nix develop` once and reuse `build/dev` with
`cmake --build --preset dev` and `ctest --preset dev`; this avoids producing a
new Nix store derivation for every source edit. Reserve package, sanitizer and
AArch64 builds for intentional verification points. `tools/nix-disk-report.sh`
reports capacity, GC roots, project build links and the current-system closure
without deleting anything. See [Nix build hygiene](docs/CI.md#nix-build-hygiene).

On an AArch64 Linux host, the same commands select the native
`aarch64-linux` outputs. From x86_64, `nix build
.#checks.x86_64-linux.aarch64-cross-compile` performs a dependency-enabled
cross-compilation check; it cannot run the resulting application or tests.

### Non-Nix CMake

The existing workflow remains supported. Install a C++20 compiler, CMake,
Ninja, pkg-config, and ALSA development files first, then:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --audio "DEVICE NAME" --midi "PORT NAME" \
  --midi-out "OUTPUT NAME" --script examples/generate.lua path/to/sample.wav
./build/dev/neurogenic --slot 0=kick.wav --slot 1=snare.wav \
  --slot 2=hihat.wav --map 52=2
./build/dev/neurogenic --timeline examples/multitrack.ngt \
  --slot 0=kick.wav --slot 1=snare.wav --slot 2=hihat.wav
./build/dev/neurogenic --script examples/timeline_drums.lua --interactive
```

Omit `--audio` or `--midi` to select the first/default device. Selectors are
the numeric index or a case-sensitive name substring printed by `--list-devices`. MIDI note
36 triggers the sample and executes [`examples/generate.lua`](examples/generate.lua),
which queues a randomized pattern for the next bar. Other note-on messages
trigger the sample. Ctrl-C stops playback. If MIDI is absent, audio continues;
an audio initialization failure exits with a diagnostic.

MIDI inputs and outputs are independently enumerable and repeatable. Logical
ports follow command-line order. External MIDI timeline tracks support notes,
CC, program change and pitch bend; internal 24-PPQN clock, received transport,
explicit thru routes, panic and project persistence use bounded queues. See
[MIDI routing and clock](docs/MIDI.md) for exact timing and hardware limits.
External sync continuously drives timeline phase, supports SPP and bounded
holdover/recovery, and exposes lock quality in the MIDI view.

The positional WAV form remains a compatibility mode: it installs slot 0 and
maps every note to it. Multi-sample mode uses repeatable `--slot ID=FILE`
assignments. Stable slot IDs are unsigned integers and are not limited to 16;
the initial bank policy maps notes 36–51 to slots 0–15. Use repeatable
`--map NOTE=ID` assignments for slots or controller layouts outside that bank.
WAV input supports mono/stereo PCM16, PCM24, PCM32, and float32. Common source
rates are converted to the 48 kHz engine rate during loading; matching-rate
files bypass conversion.

`--interactive` may start without a sample and accepts `load ID FILE`,
`replace ID FILE`, `unload ID`, `status [ID]`, and `quit` on standard input.
Loading and windowed-sinc conversion run on a preparation worker while audio
continues. Existing voices finish on their old immutable sample after replace
or unload. Status keeps audio-active metadata separate from the newest pending
request and reports callback activation explicitly. See
[live sample management](docs/SAMPLES.md).

Live recording supports bounded external mono/stereo duplex input and internal
master pre/post-effects resampling. `--input NAME --input-channels 2
--output-channels 2` selects a duplex layout; interactive
`record-start`, `record-stop`, `record-status`, and `record-save` commands form
a headless workflow. Recordings publish through immutable sample slots and can
be sliced or assigned to pads. See [recording](docs/RECORDING.md).

`--timeline` loads the versioned, line-oriented `NEUROGENIC_TIMELINE 1`
interchange format before audio starts. It carries transport metadata, stable
track/event IDs, loop lengths, mute state, notes, velocities, and durations.
Sample paths and routing remain separate `--slot`/`--map` configuration. See
[`examples/multitrack.ngt`](examples/multitrack.ngt) and the
[timeline reference](docs/TIMELINE.md).

Complete sessions can be restored with `--project PATH`:

```bash
./build/dev/neurogenic --project "projects/My Set" --gui
```

The versioned directory format preserves the timeline, routing, sample/slice
references, pad/stretch settings, and native DSP parameters without embedding
PCM. Missing assets remain inspectable and relinkable. See
[`docs/PROJECTS.md`](docs/PROJECTS.md).

The engine provides 32 polyphonic sample voices shared across all slots by default. Embedders may set a
different fixed capacity when constructing `AudioEngine`; exhaustion steals the
oldest active voice deterministically. MIDI transport callbacks only enqueue
messages. Controller actions and Lua run on a dedicated control thread.

One transport frame position drives both general timelines and legacy Lua
patterns. Stop freezes scheduled time and cleans voices, play resumes, and
restart cleans voices and returns to musical position zero. Tracks loop at
their own pattern lengths.

`--headless` explicitly selects miniaudio's null backend. It is intended for
CI and diagnostic enumeration without an audio device, not normal playback.

The dependency-free build remains useful for core/offline development, but live
audio, MIDI, and genuine Lua require `NEUROGENIC_FETCH_DEPENDENCIES=ON`.

Lua and native clients can create and mutate authoritative multitrack timeline
working copies through the shared C++ `TimelineService`. Commits use explicit
revisions, deterministic seeds, validation, and the existing bounded playback
publication path. See the [Lua API](docs/LUA.md) and the drum, mutation,
polyrhythm, and variation scripts under [`examples/`](examples/).

Future hardware, touchscreen, desktop, MIDI, Lua and remote clients share the
C++ application control layer: typed stable actions, generic parameters,
revisioned state snapshots and capability-driven controller mappings. Example
full-performance, minimal and generic-MIDI profiles live in
[`examples/controllers`](examples/controllers). Implemented and placeholder
operations are distinguished in the [application/controller API](docs/CONTROLLERS.md).

### Linux audio and MIDI troubleshooting

`--list-devices` reports the selected miniaudio and libremidi backends. A Nix
shell supplies headers and libraries at build time. The executable retains a
direct `libasound` dependency so Nix records its store path in ELF RUNPATH; no
manual `LD_LIBRARY_PATH` is required. Nix does not start host
audio services, create `/dev/snd` devices, grant permissions, or expose devices
inside containers. On a PipeWire desktop, NEUROGENIC uses PipeWire's ALSA
compatibility devices; PipeWire itself is not a build dependency.

- A null-backend diagnostic reports whether ALSA was linked but unavailable.
  Use `--headless` when null audio is intentional; otherwise check host audio
  services, device access, and the complete diagnostic.
- `ALSA sequencer compiled, but unavailable` commonly means `/dev/snd/seq` is
  absent or inaccessible. Check the `snd_seq` kernel module and user permissions.
- `(none discovered)` with an active backend means the backend works but has no
  visible device or port.
- An open error names the backend and distinguishes discovery from failure to
  open or start the selected device.

GitHub Actions is configured to run native x86_64 and AArch64 Nix builds,
tests, and explicit headless enumeration smoke tests. The x86_64 flake check
also cross-compiles the application for AArch64. This verifies compilation,
deterministic host tests, a resolvable retained ALSA dependency, and graceful
device absence; it does not verify physical ARM audio/MIDI hardware.

See [architecture](docs/ARCHITECTURE.md), [live sample management](docs/SAMPLES.md), [musical timeline](docs/TIMELINE.md), [dependencies](docs/DEPENDENCIES.md),
[benchmarks](docs/BENCHMARKS.md), [Lua API](docs/LUA.md), and [controller mappings](docs/CONTROLLERS.md).

Notes support deterministic probability, loop and Fill conditions, and 1-8
evenly spaced ratchets. These properties live in the timeline, participate in
Undo/Redo, and are available through semantic actions and Lua.

Bounded [performance macros](docs/PERFORMANCE_MACROS.md) expose reusable
project/track/group/master controls through the modulation graph, with
nonlinear native-unit mappings, scene capture, smooth recall and editable
semantic automation.

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
