# Implementation status

- Task 067: optional pinned Slint 1.19 frontend, renderer-independent
  state/action adapter, reusable component foundation and responsive initial
  performance page. SDL3 and headless builds remain available. Full workflow
  parity, final styling and graphical screenshot validation remain deferred.

- Task 064: bounded integrated live-performance validation and opt-in soak mode;
  fixed replace-capture launch-source identity and loop-range transaction bugs.

- Task 063: bounded follow-action execution, editable performance scenes,
  acknowledged launch capture, and transactional conversion to ordinary
  pattern/audio arrangement clips.

- Task 062: stable pattern/audio/live-loop launch slots, bounded coordinated
  scenes, quantization through eight bars/custom subdivisions, deterministic
  follow selection, velocity policies, semantic actions/discovery and
  timeline-v10 persistence. Automatic follow execution, scene editing actions,
  gesture capture and general hybrid merging remain deferred.

- Task 061: gapless first-pass handoff reserves PCM, sample-version and voice
  capacity before arming and activates on the exact stop frame. Overdub/replace
  handoff remains control-side.

- Task 060: stable live-loop sessions, immediate/subdivision/beat/bar capture,
  immutable feedback overdub, replace, acknowledged clip activation and
  feedback-loop validation are implemented. PCM undo remains deferred.

- Task 059: same-track audio overlap, musical-time linear/equal-power fades,
  stable-ID crossfades, transactional cleanup, timeline-v9 migration and
  prepared voice-budget validation are implemented.

- Task 058: the shared realtime stretcher now supports wrapped forward/reverse
  loops, bounded equal-power loop crossfades, deterministic seek reconstruction,
  song-loop reactivation and smoothly rebased internal/external tempo changes.
  Correlation search, formant preservation and hardware performance validation
  remain deferred.

- Task 057: stable arrangement audio clips, transactional edits, timeline-v8
  persistence, bounded start/stop compilation, native looping, seek/song-loop
  phase recovery, existing sample-voice/mixer routing, tempo-synced non-looping
  stretch, actions and discovery are implemented. Same-track overlap and general
  clip crossfades remain deferred.

- Task 056: bounded offline processing jobs implement trim, peak/RMS
  normalization, reverse, gain, fades, silence, portable sinc resampling and
  mono/stereo conversion; region/slice bounce, PCM16/24/float32 atomic WAV save,
  and callback-acknowledged immutable publication are implemented. Automatic
  metadata remapping and offline tempo-stretch/pitch-shift remain deferred.
- Task 055: immutable-version transient/quiet detection, equal and tempo-grid
  boundaries, cancellable sessions, candidate editing, validated model apply,
  pad-capacity checks and presentation-neutral discovery.

- Task 054: stable nondestructive sample regions, source-frame fades, bounded
  forward/reverse loops and crossfades, slice conversion, pad playback,
  version-4 persistence/presets, actions and discovery are implemented.
  Stretch looping, destructive rendering, GUI editing and grouped region
  Undo/Redo remain deferred.

- Task 052 adds versioned synth, sample-instrument, native FX-chain and track
  sound presets with generation-based saves, asset diagnostics, fresh runtime
  ID remapping and deterministic rollback tests. A preset-browser GUI remains
  intentionally deferred.
- Task 053 adds acknowledged callback-boundary activation across mixer/FX,
  modulation, synth parameters, samples and pad mappings, plus explicit asset
  relinking and collision-safe self-contained packages. Preset Undo/Redo is
  deferred.

Task 050 adds `neurogenic.application-discovery.v1`, a deterministic typed
contract for capabilities, object types/instances, native FX and modulator
registries, instance parameters, semantic actions, state bindings and resource
limits. `neurogenic-discovery --json` is headless. Tests cover object creation,
stable IDs, deterministic serialization, negotiation, binding validation and
stale-reference removal after project replacement. Historical action
descriptors still lack complete numeric-range metadata.

Task 049 adds a deterministic four-tier integrated engine suite covering
sample/synth playback, maximum track/group routing, native FX, aux sends, dense
modulation, macro morphs, variable callbacks, MIDI clock, internal recording,
six-channel processing and project round-trip together. Numeric audio, resource
and queue assertions produce JSON reports. Focused suites remain authoritative
for pattern chains, arrangement capture, external clock, voice modulation,
external FX and migration edge cases. Long-duration and physical N150/Pi 5,
audio-interface, MIDI and touchscreen testing remain outstanding.

Task 046 adds bounded live arrangement capture with callback-acknowledged launch
timing, pattern/chain/follow clip construction, replace/overdub, count-in,
instance-aware discrete automation thinning, one-transaction undo/redo,
overflow/conflict diagnostics, timeline-v7/project-v4 persistence and a
deterministic recording benchmark. See `ARRANGEMENT_RECORDING.md`.

Task 045 adds repeatable/one-shot pattern chains, deterministic follow actions,
precompiled exact-frame launch activation, timeline-owned voice/MIDI cleanup,
song-end modes, launch diagnostics, semantic discovery, timeline-v6/project-v3
persistence, tests and a dense-chain benchmark. Return-to-live stops exactly at
song end and publishes the selected live plan on the next control update.

Task 044 added stable reusable multi-track patterns, transformed/repeating
clips, live/song modes, loop regions and transactional history.

Task 043 adds eight stable stereo group buses, exclusive track/group routing,
acyclic group-to-group topology, serial group FX, gain/pan/mute/solo, aux sends,
meters, group-owned modulation and follower taps, three recording tap points,
semantic application actions, mixer-metadata-v4/modulation-v8 persistence, deterministic
tests and bounded benchmarks. Projects without groups retain their previous
direct-to-master signal path. See `GROUP_BUSES.md`.

Task 042 adds track-owned sample voice definitions with independent envelope,
LFO, function, step and random state. Velocity and gate/trigger are native
voice sources; pitch, gain and pan are supported destinations. Metadata is
version 7 with version 6 compatibility. Position/filter modulation and
arbitrary cross-voice routing remain deferred.

Task 041 expands the native registry to 21 types: step, Euclidean, clocked
weighted random, looping function, nine math/signal utilities and logistic
chaos join the existing sources. Stable multi-input/multi-output ports,
descriptor-driven editing, version-6 persistence and bounded mixed workloads
use the Task 040 graph unchanged.

Task 040 adds a stable-ID directed acyclic modulation graph with intentional
cross-owner sharing, modulator-parameter edges, cycle diagnostics,
deterministic topological evaluation, previous-block mixer follower taps,
semantic graph actions and version-5 persistence.

Task 039 adds a discoverable native modulator registry and stable dynamic
instances owned by tracks, aux buses or master. Instances have independent
phase/envelope/random state, bounded 64-instance/128-route publication,
semantic management actions, version-4 persistence and legacy migration.

Task 038 adds bounded serial native FX chains for tracks, two aux buses and
master. Gain, stereo delay, low-pass filter and saturation instances have stable
IDs, typed descriptors, semantic actions, modulation/automation destinations and
versioned persistence. Plugin hosting, topology crossfades, presets and latency
compensation remain deferred.

Task 068 reconciles the dedicated controller with the current modular RP2040
surface. Compatible protocol 1.2 adds explicit page, edit, performance and
navigation groups, removes analog and encoder-push requirements from the current
capability profile, preserves legacy IDs, shares authoritative contextual data
with Slint/SDL3, and keeps LED-chain/scanning details in future firmware.
Physical electrical and firmware validation remain outstanding.

Task 037 advances the controller protocol to compatible version 1.1 with
bounded formatted values/ranges/units/revisions, explicit page mappings shared
by GUI and hardware, finalized dedicated IDs, fine/accelerated encoders,
calibrated target-aware pickup, richer mode/performance LEDs and full-refresh
retry after feedback overflow. Calibration persistence and physical firmware
testing remain open.

Task 036 adds bounded versioned controller framing with CRC, nonblocking USB
CDC serial transport, negotiation/capabilities, semantic dispatch, RGB and
context feedback, exact-path reconnection, simulator tests, JSON diagnostics
and matching GUI edge labels. USB HID and MCU firmware are intentionally
unimplemented, and physical USB behavior remains unverified.

Task 035 adds revisioned appliance startup/shutdown stages, separate persistent
preferences, explicit project override/reopen-last behavior, degraded GUI or
interactive startup without audio, coordinated recording/transport/MIDI/device
shutdown, and shared dirty-session protection. Example launcher/systemd user
units are shipped but never installed or enabled automatically. Unexpected
power-loss recovery remains limited to generation-based project save guarantees.

Task 034 adds versioned system/audio/MIDI/display inventories, native audio
format and negotiated-rate diagnostics, bounded offline/device stress reports,
display QA profiles, and a controller event inspector. Headless CI covers all
schemas with simulated capabilities. Physical N150/Pi 5 performance, touch
alignment, six-channel I/O, audio/MIDI loopback and backend XRUN reporting remain
hardware-dependent and are not claimed.

Task 033 adds a bounded production-path workflow runner covering beat creation,
sample loading/slicing, synth instances/modulation, mixer/resampling, simulated
external FX, external MIDI/clock, performance-grid modes, compact GUI text
entry and project round-trips. The audit fixed missing track create/delete,
external MIDI destination, project rename and authoritative tempo bindings.
CTest and sanitizer runs include the runner with a 30-second ceiling. Physical
audio/MIDI timing, external cabling/levels, 8-inch touch comfort and subjective
listening remain explicitly hardware-dependent. See `WORKFLOWS.md`.

Task 027 adds two validated external track/aux/master effect loops, mono/stereo
six-channel mappings, linear wet/dry, return gain/mute, manual bounded latency
compensation, return/mix recording taps, atomic routing scenes, v2 mixer/scene
persistence, semantic actions/parameters, a Routing GUI, controller mappings
and deterministic benchmarks. Simulated I/O is host-tested; physical processor
levels/cabling, round-trip latency, scene-switch clicks and multichannel device
behavior require hardware verification. Automatic latency calibration and
scene morphing are not implemented.

Task 026 adds a bounded 32-track mixer with gain/balance pan/mute/solo and peak
meters, two stereo aux sends/returns with optional native delay, six-channel-
capable input/output routing, track/aux/input/master recording taps, semantic
actions/parameters, versioned project metadata, a Mixer GUI and deterministic
benchmarks. Six-channel paths are simulated and cross-compiled; physical
multichannel hardware, channel labels and monitoring latency are not verified.

Task 023 adds optional bounded realtime pitch-preserving stretch for full
samples and slices, independent time/pitch ratios, beat/BPM tempo sync, three
window/latency presets, semantic actions, GUI/controller access, v3 persistence
and deterministic benchmarks. The ordinary playback-rate path remains default
and reverse stretch falls back to it. Prepared caching, reverse stretch,
transient matching, warp markers and hardware Pi 5/N150 measurements remain
future work.

Task 022 makes pads and slices expressive instruments with one-shot, Gate and
Toggle modes, deterministic choke groups, fractional playback-rate pitch,
reverse boundaries, gain and bounded per-voice ADSR. Settings are revisioned in
`SliceService`, published through bounded callback queues, exposed through
semantic actions/application snapshots, rendered in the Samples inspector and
persisted in backward-compatible slice sidecars. Pitch changes duration; there
is no time stretching, choke curve editor or physical-controller verification.

Task 021 adds revisioned stable slices, deterministic equal/transient slicing,
callback-safe version/boundary playback, semantic pad mapping, preview
ownership, cached waveform peaks, GUI marker editing, Lua adapters, sidecar
persistence and benchmarks. Host tests cover overlap, boundaries, stale
revisions, mapping, replacement invalidation and missing references. Dedicated
zoom controls, automatic project loading, choke editing, stretching, pitch
shifting and physical touch/controller audition remain future work.

Task 019 adds validated stable synth/delay parameter destinations, sample-
accurate discrete timeline automation, step locks with multiple destinations,
shared Undo/Redo, Lua adapters, lock-aware GUI/controller actions and automation
benchmarks. Locked values persist until changed and are reapplied on loops;
curves, chase-on-seek and automatic reversion are not implemented. Nix guidance
now favors incremental CMake development and includes read-only store/root
diagnostics without automated cleanup.

Task 018 adds stable track-to-instrument routing, a bounded native instrument
interface, a playable 32-voice subtractive synth, a reusable DSP processor
interface and preallocated stereo delay. Timeline and MIDI notes share
scheduling with sample playback; synth/effect controls use the application
parameter registry and bounded callback queue. GUI selection, parameters,
voice/steal statistics and deterministic DSP benchmarks are host-tested. There
now has bounded track-local instances and one master delay; per-track chains,
presets and plugins remain future work. Physical audio/MIDI
playability and subjective sound quality are not hardware-tested.

Task 017 adds optional bounded callback timing/load statistics, active-voice
and existing overflow diagnostics, a 2 Hz Linux process/system sampler, copied
application performance snapshots, JSON headless diagnostics, a compact GUI
indicator and detailed Performance view. Deterministic JSONL benchmarks cover
audio, scheduler, publication, resampling preparation and three GUI sizes.
Host tests do not establish Raspberry Pi 5, Intel N150, physical-device XRUN,
GPU presentation, thermal, or worst-case real-time performance.

Task 016 makes the 8-inch 1920 × 1200 display the primary visual target while
retaining responsive 1024 × 800 and 1024 × 600 layouts. Window, drawable,
logical and UI-scale dimensions are separate; automatic/manual live scaling,
persistent recovery-safe settings, a Display view, native-resolution rendering,
and high-DPI hit conversion are host-tested. Physical HDMI fullscreen, USB HID
touch calibration, ARM GPU performance, touch comfort and visual QA remain
hardware-dependent and unverified.

Task 015 adds shared bounded timeline Undo/Redo, atomic grouped edits, stable
multi-note event selection, and velocity, duration, pitch, and tick-relative
timing editing. The SDL inspector and minimal-controller flow use the same C++
semantic actions. These paths are host-tested; physical controller interaction
remains untested.

Task 014 adds a timeline-backed step editor: straight/triplet quarter through
32nd grids, partial and independent track lengths, per-track MIDI note targets,
16-cell paging, Select-versus-toggle grid semantics, pointer/keyboard/encoder
workflows, active-step/playhead rendering, responsive resize invalidation, and
safe live publication. Note-target persistence, parameter locks and
musical-boundary publication remain future work.

Task 013 adds an optional SDL3 graphical client with a resolution-independent
1024 × 600 logical layout, sequencer, sample-slot, parameter and X/Y views,
fullscreen mode, state-revision synchronization, keyboard/mouse/touch input,
and offscreen initialization tests. Graphical text entry,
waveform editing and DSP parameter bindings remain unavailable. Physical HDMI
and touchscreen behavior has not been hardware-tested.

Implemented and host-tested: PCM16/24/32 and float32 WAV loading, mono/stereo sample playback,
miniaudio callback output, device enumeration/selection, libremidi input
enumeration/selection, MIDI note/action dispatch, genuine Lua 5.4 execution,
validated versioned pattern APIs, sample-accurate frame offsets, rational
fractional-tempo scheduling, next-bar replacement, bounded real-time queues,
overflow reporting, variable callback sizes, and offline WAV rendering.

The local gain stage remains original NEUROGENIC code with an Airwindows-facing
identifier. It is not Airwindows DSP; exact upstream integration remains a
separate reviewed task.

Verified on this host before the Nix environment was added: dependency-enabled
build, automated tests, miniaudio null-device enumeration, continuous callback
playback to the null device, clean SIGINT shutdown, and graceful absence of
MIDI. That build lacked ALSA development headers and MIDI ports.

Task 003 adds a locked x86_64-linux Nix package, development shell, application,
and build/test check; a dependency-enabled CMake preset; direnv integration; and
GitHub Actions. Nix builds confirm that ALSA is found and libremidi compiles its
ALSA sequencer backend. In the headless verification environment, miniaudio
correctly fell back to Null and ALSA sequencer was compiled but unavailable at
runtime. Physical devices remain untested.

Task 004 retains `libasound` as an explicit runtime dependency so miniaudio and
libremidi can resolve their dynamically loaded ALSA symbols on NixOS without
`LD_LIBRARY_PATH`. The dependency-enabled Linux tests verify resolution with
that variable unset. On the development host, the unpackaged and `nix run`
executables enumerated PipeWire-compatible ALSA outputs, SSL 12 audio/MIDI, and
an A-Series MIDI keyboard without environment changes. Enumeration is verified;
audio playback and MIDI event delivery on physical hardware were not tested.
`--headless` explicitly requests null audio for CI.

Task 005 adds bounded configurable sample polyphony, deterministic oldest-voice
stealing, complete burst consumption with frame-offset ordering, and a dedicated
control dispatcher that isolates MIDI callbacks from mappings and Lua. Host
tests cover overlap, retriggering, simultaneous events, carried offsets, voice
and queue exhaustion, threaded dispatch, draining shutdown, variable buffer
sizes, and offline rendering. Sample replacement during live processing remains
intentionally unsupported; stop and rebuild the engine to load another sample.

Task 006 replaces the single-sample renderer with dynamically sized stable
sample slots. MIDI and sequencer notes share a configurable mapping, initially
notes 36–51 to slots 0–15, and voices retain immutable per-slot sample
references. The CLI accepts repeatable `--slot ID=FILE` and `--map NOTE=ID`
arguments while preserving positional single-WAV behavior. Host tests cover
multi-slot playback, same-slot polyphony, missing mappings, cross-slot stealing,
sequencer and MIDI selection, WAV rejection, Lua, offline rendering, timing,
and bounded queue overload.

Task 007 adds a UI-independent 960-PPQ musical timeline with stable track/event
identifiers, arbitrary per-track loop lengths, duration and explicit note
events, parameter-event representation, mute state, tempo/time-signature
metadata, and deterministic absolute tick-to-frame scheduling. Editable vectors
are compiled on the control thread into bounded callback-safe plans. The audio
engine plays multiple timeline tracks through existing sample slots and stops
track-owned voices on note-off. The original Lua/16-step scheduler remains a
next-bar compatibility lane and has an explicit general-timeline adapter.
Host tests cover multiple tracks, simultaneous ordering, arbitrary lengths,
mute, loop boundaries, tempo conversion, long-run variable-buffer timing,
duration note-off, sample slots, and legacy patterns. Parameter destinations,
tempo maps, arrangement, recording, and quantized timeline-plan replacement
remain future work.

Task 008 makes the audio engine transport authoritative for both the general
timeline and legacy Lua workflow. Host tests cover stop/play/restart, voice
cleanup, active-note replacement, shared tempo/position, durations crossing
loops, overlapping note identity, staging overload, scalable compiled plans,
short loops, large-position saturation, and versioned file round trips.
`NEUROGENIC_TIMELINE 1`, `--timeline`, and a three-track example provide
application playback without C++. Compiled plans no longer impose 32-track or
512-event musical limits; callback staging remains configurable and observable.
MIDI clock, tempo maps, beat-continuous live tempo changes, and physical-device
playback verification remain future work.

Task 009 replaces the fallible retired-plan queue with four control-owned plan
slots; audio only publishes state transitions and never frees plans. Per-track
cursors and a preallocated next-event heap remove full-pattern callback scans.
Voice ownership now includes track, target note-on ID, and loop occurrence, so
overlap, loop crossing, stealing, stop/restart, mute, and replacement cannot
release unrelated voices. Staging overflow defers events instead of discarding
note-offs and reports callback overruns. Host regression and leak-sanitizer
tests cover rapid replacement/reclamation, saturation, exact explicit targets,
stolen owners, dense backlogs, and deferred cleanup. The offline benchmark is
documented in `docs/BENCHMARKS.md`; it is not hardware real-time evidence.
Latency under load, hotplug, physical ARM audio/MIDI devices, and
controller-specific behavior also remain untested.

Task 010 adds live load, replacement, and unload through the asynchronous
hardware-independent `SampleManager` command/state boundary. Immutable sample
versions allow existing voices to finish while new triggers resolve the new
slot contents; callback-side work is bounded and reclamation remains on the
control thread. Host tests cover active-voice replacement/unload, independent
slots, rapid reclamation, failure preservation, concurrent playback/loading,
shutdown, PCM decoding, mono/stereo conversion, 44.1/96 kHz duration and pitch,
and observable state. WAV support now includes PCM16/24/32 and float32 with
preparation-time windowed-sinc conversion. Streaming decode, non-WAV formats,
mastering-grade selectable resampler profiles, hardware listening tests, and
sample-management Lua/MIDI bindings remain future work.

The sample-state consistency follow-up adds explicit queued, preparation,
publication-pending, and callback-acknowledged states. Active metadata and the
newest pending request are reported separately. Deterministic host tests cover
rapid same-slot replacement, mixed unload/load ordering, delayed callback
activation, failed replacement preservation, concurrent playback, and shutdown
with an unapplied publication.

The AArch64 enablement task exposes the complete Nix package, app, check, and
development-shell outputs for both `x86_64-linux` and `aarch64-linux`. The code
and dependency audit found no x86 SIMD baseline; scheduler arithmetic relies on
GCC/Clang 128-bit integers supported by both targets. CI is configured with a
native AArch64 hosted runner and an x86-hosted AArch64 compilation-only check.
A successful native AArch64 CI run is host-runtime evidence only; physical ARM
audio/MIDI devices, latency, and subjective playback remain unverified.

Task 011 adds the control-side `TimelineService`, detached revisioned
transactions, deterministic SplitMix64 streams, reusable transformations and
Euclidean generation, thin `neurogenic.v1.timeline` Lua bindings, and four
generative examples. Commits activate at the next audio callback through the
existing immutable plan pool. Musical-boundary commit scheduling, multiple
patterns per track, parameter destinations, and hostile-script sandboxing remain
future work.

Task 012 adds the initial client-neutral `ApplicationService`, typed action and
parameter registries, copied revisioned state snapshots, bounded ordered
multi-client commands, capability-based controller profiles/mapping, feedback,
four performance-grid modes, modifier/focus navigation, encoder-only text entry
and normalized X/Y interaction. Three example controller profiles are parsed
and host-tested. GUI rendering, controller firmware, MIDI learn, undo history,
state subscriptions, complete step editing, scale layouts, slicing, pitched
sample playback and modulation routing remain future work.
Task 020 implements serialized deterministic note probability, nine trigger
conditions, momentary/latched Fill, and 1-8 ratchets with application actions,
Lua fields, Undo/Redo, GUI indicators, tests, and benchmark scenarios. Advanced
ratchet curves, conditional parameter locks, and graphical automation editing
remain out of scope.

Task 024 adds the version-1 unified project directory and shared lifecycle
actions. Generation-based manifest replacement preserves the prior committed
generation on ordinary write failures. Timeline/routing, generative event
fields, sample references, slices, expressive pad/stretch settings, and native
DSP parameters round-trip with stable IDs. Relative assets relocate with the
project; missing assets remain inspectable, savable, and relinkable. Startup
supports `--project`. Automatic asset copying, generation pruning, a full GUI
filesystem browser, reopen-last preference, and power-loss durability claims
remain deferred.

Task 025 adds a bounded 60-second default mono/stereo recorder, master
pre/post-effects resampling, simulated and miniaudio duplex external input,
Off/On/Auto monitoring, immediate/beat/bar starts, fixed-bar/manual stops,
PCM16/float32 WAV output, immutable publication, project recording assets,
semantic actions, GUI status, tests, and benchmarks. Physical duplex hardware,
independent-device clock correction and subjective monitoring remain unverified
or deferred. Task 026 adds track/aux taps and simulated six-channel routing;
physical multichannel hardware remains unverified.

Task 028 adds a fixed-capacity modulation matrix with four LFOs, ADSR,
deterministic random, X/Y, velocity, and MIDI CC. Sources have explicit
global/track/voice scope; global and per-track instances are implemented with
independent phase, gate, retrigger, and seeded random state. Synth/delay destinations,
projects, semantic actions/state, GUI inspection, tests, and benchmarks are
implemented; audio-rate and prepared sample/mixer modulation remain future.

Task 029 replaces the implicit synth singleton with 16 bounded, preallocated
instances. Tracks persist an instance ID, legacy data migrates to shared
instance 1, and numeric callback routing isolates local parameters/modulation
while permitting explicit sharing. Per-voice ADSR, velocity,
retrigger/free-running LFO, pitch, cutoff, and gain modulation are embedded in
synth voices. Sample-voice modulation remains future work.

Task 030 adds callback-safe numeric destinations for sample pitch/gain/pan,
track gain/pan/aux sends, aux/master gain and external-FX wet/send/return. An
attack/release follower observes the preceding master pre-effects block. Global
and track sample routes are supported; per-voice sample sources, sample filters
and selectable follower taps remain explicit deferred work.

Task 031 adds up to eight logical libremidi inputs and outputs, bounded
nonblocking output dispatch, validated channel-filter/remap thru routes,
24-PPQN rational clock generation, filtered external clock/transport reception,
panic/stop cleanup, and external MIDI timeline tracks for notes, CC, program
and pitch bend. Timeline/project formats persist routing and logical device
selectors. Host tests use simulated ports; physical jitter and the full GUI
routing editor remained deferred at that milestone.

Task 032 replaces tempo-only reception with a bounded external-clock follower.
The authoritative timeline now follows filtered 24-PPQN phase, supports SPP,
distinguishes Start/Continue, holds over for 125 ms, freezes and cleans notes at
500 ms loss, and deterministically reacquires. Clock/transport forwarding has
reserved queue capacity. Exact-name input/output reconnect and port-state GUI
feedback are implemented; physical jitter, reconnect timing and interoperability
remain hardware-dependent and unverified.

Task 047 adds stable project/track/group/master performance macros, bounded
multi-destination assignments, linear/exponential/logarithmic/S-curve/stepped
mapping, performance scenes, control-side seconds or tempo morphs, semantic
automation, project format 5 persistence and a versioned discovery snapshot.
Assignment smoothing currently delegates to the destination's existing DSP
smoothing; general project-object Undo/Redo remains deferred.

Task 048 moves scene morph execution into a fixed-capacity audio-thread control
processor. Prepared commands support absolute transport-frame starts,
seconds/beat durations, four curves, 32 simultaneous morphs, replacement,
stop-at-current-value, tempo changes, transport stop/continue and seek rebasing.
Manual and recorded automation values share the ordered DSP queue and interrupt
an active morph deterministically. Runtime discovery exposes effective values,
progress, scheduled frames, completion and queue drops. Macro metadata version
2 persists scene curves; transient morph state remains excluded. Recorded macro
automation is still discrete, and the 32-frame control interval is the current
timing granularity.

Task 051 adds a presentation-neutral typed binding service over Task 050
discovery. It covers stable and selected-object references, authoritative
base/effective values, native/normalized/relative controls, structured
validation errors, fader pickup, high-resolution MIDI normalization and bounded
capability/state/telemetry notifications with explicit overflow
resynchronization. Dynamic FX and modulator parameters use their existing
instance-aware semantic actions. Text metadata and per-field delta payloads
remain future extensions.
Task 065 implements per-track arrangement/session ownership, partial-scene
takeover, track-scoped sample/synth/MIDI cleanup, quantized one/all-track return,
phase-seeked arrangement restoration, follow invalidation and revisioned state.
Task 066 makes realtime diagnostic period selection effective, reports the
negotiated 48 kHz period and I/O layout, exposes device lifecycle events,
separates callback overruns from unavailable backend XRUN counters, and adds
Linux RT-priority/memlock/governor diagnostics plus an N150 runbook. Physical
N150 audio, latency, MIDI jitter, thermal and soak results remain unmeasured.

Task 069 introduces transport-independent controller descriptors, validated
schema-2 `.ngc` profiles, independent controller sessions, shared contextual
resolution, bounded capability-aware feedback and an opt-in generic MIDI
control-surface adapter. The Task 068 RP2040 inventory remains a built-in
descriptor/profile and CDC protocol 1.0–1.2 remains compatible. Multiple
sessions now keep pages, modifiers and pickup state independent while sharing
authoritative application parameters. Physical RP2040 validation and native
CDC text-display packets remain outstanding.

Task 070 hardens delivery and makes this architecture operational in the
appliance. Local context/modifier/pickup revisions invalidate feedback; bounded
session and transport queues retain unsent state and converge after overflow.
Descriptors remain authoritative and profiles cannot invent controls. Repeated
exact-path CDC endpoints coexist with opt-in MIDI profiles supporting absolute,
relative and note mappings, soft takeover, optional feedback, explicit
learn/cancel/timeout and atomic save. Musical MIDI remains independent.
Physical multi-controller/MIDI feedback interoperability and RP2040 validation
remain unmeasured; MIDI learn has no graphical page.

Task 071 stabilizes the integration: shipped and legacy MIDI mode names now
round-trip to one canonical format, adapter registration is synchronized and
explicitly removed on failed setup/shutdown, and controller device selection
is exact with backend-identity reconnect. Logical-port conflicts are rejected,
intentional musical/controller sharing preserves unmatched musical messages,
and bounded feedback retries regenerate authoritative state after transport
failure or reconnect. These are software-verified; physical USB CDC, hotplug,
and third-party MIDI feedback remain hardware validation work.
