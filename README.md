<p align="center">
  <img src="docs/assets/readme/vibeslop.png" alt="NEUROGENIC" width="512">
</p>

# NEUROGENIC

**An open-source platform for building your own electronic musical instrument.**

NEUROGENIC combines a sampler, synthesizer, sequencer, mixer and live
performance environment in a hardware-independent C++20 engine. It is designed
for standalone instruments, custom control surfaces and alternative workflows,
without requiring a particular screen, controller or graphical interface.

The reference instrument is a hardware-first groovebox, not a requirement for
using the platform.

**Development status:** active prototype. Core musical workflows are implemented
and tested in software; physical appliance and controller validation is pending.

## Features

| Area | Implemented capabilities |
| --- | --- |
| Sampling | Polyphonic playback, waveform editing, slices and regions, choke groups, gate/one-shot/toggle modes, tuning, reverse, envelopes, pitch-preserving time stretching, offline analysis and processing |
| Sequencing | 960 PPQN timeline, independent tracks and pattern lengths, reusable patterns, microtiming, probability, conditions, ratchets, parameter locks, typed musical clipboard and unified Undo/Redo |
| Arrangement | Quantized pattern and audio-clip launching, scenes, follow actions, linear arrangements, looping and crossfades |
| Recording | External audio capture, resampling, audio live loops, live sample/synth/MIDI event recording, continuous loop overdub, range-scoped replacement, count-in and note-repeat capture |
| Synthesis | Native polyphonic subtractive synthesis, oscillators, ADSR, resonant filtering and delay |
| Mixing | Track, group, aux and master FX chains, eight stereo groups, two stereo aux buses, recording taps and two optional external FX loops |
| Modulation | Scoped modulation sources, cross-track routing, automation, performance macros and recallable scenes |
| Performance | Configurable banks, note repeat and triplets, live erase, solo, mute, recording arm, tap tempo, metronome and unified panic |
| Integration | MIDI input/output and clock, configurable controller profiles, negotiated USB controller protocol, typed application discovery and optional Lua scripting |
| Projects | Versioned project directories, presets, immutable sample assets, dependency relinking and shared edit history |

## Build your own instrument

The engine and application services are independent of the presentation layer.
Hardware controllers, graphical interfaces and scripted clients use the same
typed actions, stable identities and revisioned state.

- [Controller design](docs/CONTROLLER_DESIGN.md) — design a custom control surface.
- [Frontend design](docs/FRONTEND_DESIGN.md) — build an alternative interface.
- [Workflow design](docs/WORKFLOW_DESIGN.md) — define a different musical workflow.
- [Application discovery](docs/APPLICATION_DISCOVERY.md) — inspect available actions, parameters and capabilities.

### Reference instrument

The reference design combines a physical control surface with a Slint frontend
targeting 1920 × 1200 and 1024 × 600 displays. Physical controls are the primary
interaction method; the screen provides contextual information and touch access.

The dedicated RP2040 controller is developed separately in
[neurogenic-control](https://github.com/EMajesty/neurogenic-control), including
KiCad designs, firmware, illumination drivers and standalone diagnostics.

The host implements negotiated protocol 1.4 with exact 1.3 fallback: 67
mandatory controls, eight optional encoder push inputs and eight optional
encoder RGB destinations. The firmware integration of this contract is next.

See [Reference Controller V2](docs/REFERENCE_CONTROLLER_V2.md),
[visual language](docs/VISUAL_LANGUAGE.md) and
[controller protocol 1.4](docs/CONTROLLER_PROTOCOL_1_4.md).

## Getting started

Linux x86_64 and AArch64 are supported build targets. The engine operates at
48 kHz. Nix is the recommended reproducible development environment.

### Installable build

```sh
nix build
./result/bin/neurogenic --frontend slint
```

Inspect available audio and MIDI devices:

```sh
nix run . -- --list-devices
```

### Development

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For the Slint frontend:

```sh
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic --frontend slint
```

For development without Nix, install a C++20 compiler, CMake, Ninja,
pkg-config and ALSA development files, then use the same CMake presets.
See [dependencies](docs/DEPENDENCIES.md) and [CI and build hygiene](docs/CI.md).

### Direct-display appliance

```sh
nix build .#kms
SLINT_BACKEND=linuxkms-software ./result/bin/neurogenic-kms
```

The optional LinuxKMS package includes libseat/libinput and software/FemtoVG
renderers. Physical DRM, GPU and touchscreen compatibility remain unvalidated.
See [LinuxKMS](docs/os/LINUXKMS.md) and the [OS roadmap](docs/os/ROADMAP.md).

### Headless operation and diagnostics

```sh
./build/dev/neurogenic --headless --list-devices
./build/dev/neurogenic-discovery --json
./build/dev/neurogenic-diagnostics --headless --json
```

Headless mode explicitly selects null audio for testing and diagnostics.
SDL3 and optional Lua clients remain available alongside Slint.
See [SDL interface](docs/GUI.md), [Lua API](docs/LUA.md) and
[workflow examples](docs/WORKFLOWS.md).

## Architecture

- Portable C++20 engine with bounded real-time processing and immutable prepared publication.
- Shared musical timeline and application services for all clients.
- Stable musical object identities, transactional editing and unified history.
- Fixed 48 kHz engine rate, with audio-device adaptation and MIDI backends.
- Slint production frontend, optional SDL3, headless and Lua clients.
- Hardware-independent semantic controller mappings and versioned USB protocol.

See [architecture](docs/ARCHITECTURE.md) and [instrument/DSP design](docs/INSTRUMENTS.md).

## Current development

The core recording and editing workflow now supports performing, recording,
editing and replaying musical events, including continuous loop overdub with
subsequent-pass playback and session-level Undo/Redo.

| Workstream | Current state |
| --- | --- |
| Musical engine | Continuous overdub and typed clipboard implemented; quantization/swing expansion next |
| Application frontend | Slint shell and shared semantic controls implemented; further interaction acceptance pending |
| Controller host | Protocol 1.4 implemented and simulator-tested |
| Controller firmware | Standalone diagnostics and illumination software implemented; negotiated production protocol integration pending |
| Appliance | Desktop and LinuxKMS builds available; physical bring-up pending |

Known limitations include one stable musical-event recording destination per
session, no simultaneous live erase during recording, callback-aligned external
MIDI timestamps, and no MIDI CC/automation capture. Late control-thread
publication can defer overdub playback to a later pass. The broader quantization
and swing workflow, temporary performance checkpoints and shared granular DSP
remain planned work.

### Validation

At the HW-006B implementation baseline, the committed validation report records:

| Suite | Result |
| --- | --- |
| Development | 49/49 passed |
| Slint | 52/52 passed |
| Production | 52/52 passed |
| ASan/UBSan | 49/49 passed |

Production and LinuxKMS Nix builds, flake checks (including AArch64
cross-compilation), installed headless runtime and controller protocol tests
also passed. LeakSanitizer was disabled in the managed test environment.

These are software results, not physical hardware acceptance. Audio/MIDI
interfaces, USB controller interoperability, display/touch, latency, sustained
load and illumination calibration still require physical validation.

See [implementation status](docs/STATUS.md),
[latest validation](docs/HW_006B_VALIDATION.md),
[acceptance ledger](docs/audit/task-088/V2_ACCEPTANCE_LEDGER.md) and
[development roadmap](docs/audit/task-088/NEXT_ROADMAP.md).

## Documentation

| Topic | Reference |
| --- | --- |
| Samples and processing | [Samples](docs/SAMPLES.md), [processing](docs/SAMPLE_PROCESSING.md), [analysis](docs/SAMPLE_ANALYSIS.md) |
| Sequencing and arrangement | [Timeline](docs/TIMELINE.md), [arrangement](docs/ARRANGEMENT.md), [clipboard](docs/MUSICAL_CLIPBOARD.md) |
| Recording and performance | [Recording](docs/RECORDING.md), [event recording](docs/LIVE_EVENT_RECORDING.md), [loop overdub](docs/LOOP_RECORDING_AND_OVERDUB.md), [launching](docs/PERFORMANCE_LAUNCHING.md) |
| Mixing and effects | [Mixer](docs/MIXER.md), [FX chains](docs/FX_CHAINS.md), [group buses](docs/GROUP_BUSES.md) |
| Modulation | [Modulation](docs/MODULATION.md), [graph](docs/MODULATION_GRAPH.md), [macros](docs/PERFORMANCE_MACROS.md) |
| Projects and history | [Projects](docs/PROJECTS.md), [history](docs/APPLICATION_HISTORY.md) |
| Interfaces | [Slint](docs/SLINT_UI.md), [controllers](docs/CONTROLLERS.md), [MIDI](docs/MIDI.md) |
| Development and hardware | [Benchmarks](docs/BENCHMARKS.md), [hardware testing](docs/HARDWARE_TESTING.md), [CI](docs/CI.md) |

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
