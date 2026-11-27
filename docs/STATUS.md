# Implementation status

Task 098E adds authoritative live sequencer-event recording. Accepted sample,
synth, external MIDI note, and note-repeat events are timestamped by the audio
transport, transferred through a bounded real-time-safe queue, and committed as
ordinary pattern events with unified history and project persistence. See
[Live event recording](LIVE_EVENT_RECORDING.md). Task 098F completes contextual
LOOP/RANGE and REC/OVERDUB: bounded batches become ordinary prepared timeline
events during a take and play on subsequent passes without stopping transport.
The whole multi-pass take remains one unified history operation.

Task 098D adds the application-owned typed musical clipboard: bounded selected
step, pattern-track and complete reusable-pattern capture; relative transactional
merge/confirmed replacement paste; fresh event identities; unified Undo/Redo;
COPY controller gestures; and Slint compatibility presentation. Independent
automation and live sequencer-event recording remain explicit follow-up work.

Task 098C adds bounded transport-synchronized sample/synth/MIDI note repeat,
exact straight/triplet PPQN divisions, stable release/PANIC ownership, and
transactional matching-note live erase. REPEAT/ERASE controller chords and
Slint state are integrated without changing the 67-control wire inventory.
Persistent repeated-event recording remains blocked on a missing authoritative
live event-recording window; physical validation remains **NOT RUN**.

Task 098B adds transient additive per-track solo, stable per-track recording
arm over the existing single track-channel capture authority, and unified
internal/MIDI panic with next-callback acknowledgement. Reference SOLO/ARM pad
chords, SHIFT clear operations, SHIFT+STOP, Slint track state, and illuminated
encoder design amendments are included. Physical validation remains NOT RUN.

Task 098A adds the transport metronome, scheduled recording count-in, bounded
tap-tempo estimator, V2 CLICK/TAP bindings, Slint state projection, and the
functional visual-language specification. Dedicated cue routing, live-loop
count-in, preference persistence and physical validation remain open.

## OS-001 LinuxKMS and appliance preparation

The flake now exposes an optional `kms` package using pinned Slint 1.19 with
LinuxKMS, libseat/libinput, software and FemtoVG renderers. The desktop default,
headless and SDL packages remain separate. `neurogenic-kms` explicitly selects
`linuxkms-software` unless another KMS renderer is requested. The OS workstream
documents minimal NixOS, Buildroot/Yocto feasibility, immutable storage,
comparative measurement and G10 bring-up. Builds are software evidence only;
all physical DRM, touch, audio and latency results remain **NOT RUN**.

## Task 097D production acceptance closure

The production V2 path now proves controller-authored later-page locks through
prepared scheduling, exact AudioEngine application, project save/reopen and a
fresh engine. Held-step modal cancellation rolls back without history; Sound
pads use stable synth-instance note ownership; Sample/Slice audition captures
explicit targets; Macro/Scene SHIFT pads select without mutation; and bounded
protocol feedback converges exactly after overflow and reconnect. See
[Task 097D validation](audit/task-088/TASK_097D_VALIDATION.md) and the
[acceptance ledger](audit/task-088/V2_ACCEPTANCE_LEDGER.md).

RC2-001/003/004/005/007 gain production evidence but remain partial where the
ledger records incomplete graphical traversal or missing musical capability.
RC2-006/AUD2-006 remain partial for the coordinator-owned profile library and
extended mapping fields. Physical validation remains **NOT RUN**.

## Task 097C contextual-bank and pad-mode closure

The shared resolver now preserves all sixteen V2 destination identities and
publishes explicit encoder, function and destination-owned pad descriptors.
V2 pad dispatch consumes those descriptors; unsupported operations remain
visible but inert, while legacy controllers retain their established pad-mode
behavior. See [Task 097C validation](audit/task-088/TASK_097C_VALIDATION.md).

RC2-004 is substantially implemented for existing capabilities. Missing engine
operations and shared selection metadata keep RC2-001/003 and AUD2-007 partial.
Physical validation remains **NOT RUN**.

## Task 096G physical V2 protocol and firmware conformance

Reference Controller V2 now has a concrete USB CDC protocol-1.3 contract:
canonical dependency-free wire IDs for all 67 controls, exact identity and
inventory negotiation, host-issued input generations, sequence rejection,
full button-state recovery, heartbeat/reset handling, and bounded logical RGB
rendering. A byte-stream firmware simulator traverses the production decoder,
host, controller session and semantic dispatch path; the headless diagnostic
mode reports negotiation and synchronization without an audio device. See the
[firmware guide](REFERENCE_CONTROLLER_V2_FIRMWARE.md) and
[Task 096G validation](audit/task-088/TASK_096G_VALIDATION.md).

RC2-007 is implemented as a software protocol/firmware contract but remains
hardware-bound: MCU firmware, PCB electrical behavior, USB reliability,
debounce, rollover, encoder and illumination validation are **NOT RUN**.
Earlier contextual/presentation gaps keep AUD2-007 partial.

## Task 096F controller mapping and shared assignment authoring

Task 096F introduces a shared discovery-backed target picker used by controller
mapping and Performance assignment authoring. Manual MIDI note, absolute CC and
supported relative CC sources can be prepared while disconnected, validated,
persisted, dispatched through the production mapper/session path and replaced
transactionally after explicit conflict confirmation. Performance parameters
and safe typed actions use the same picker while retaining project ownership and
unified musical history. See
[Task 096F validation](audit/task-088/TASK_096F_VALIDATION.md).

RC2-005, RC2-006 and AUD2-006 remain partial for the explicitly documented
graphical profile-library, Setup typed-argument and full logical-position editor
gaps. Reference Controller V2 firmware and all physical validation remain NOT
RUN.

## Task 096E project-owned Performance banks

Projects now persist bounded named Performance banks with stable primary and
SHIFT parameter/action layers and banked launcher-pad assignments. Reference
Controller V2 and deliberately non-reference controllers consume the same
context descriptors; momentary Fill retains its press target through release
and cancels safely on bank/page/modal/disconnect ownership loss. Definition
authoring participates in unified history, while live values and active-bank
selection do not. See [Performance banks](PERFORMANCE_BANKS.md) and
[Task 096E validation](audit/task-088/TASK_096E_VALIDATION.md).

RC2-005 remains partial pending the complete graphical typed-argument target
picker and physical validation. AUD2-007 remains partial; physical validation
is **NOT RUN**.

## Task 096D contextual musical editing

Reference Controller V2 Sequencer pads now have context-specific ownership:
ordinary taps toggle once, while held-pad P1-P8 movement consumes the toggle
and edits a captured reusable-pattern/track/absolute-step target. P1-P7 cover
pitch, velocity, duration, probability, condition, ratchets and microtiming;
P8 creates or updates a compatible parameter lock without changing its base
parameter. Up to 64 stable same-pattern/track steps can receive one grouped
edit, and multiple deltas commit as one unified history entry. Legacy contexts
and immediate Performance pads remain unchanged. See
[`TASK_096D_VALIDATION.md`](audit/task-088/TASK_096D_VALIDATION.md).

RC2-004 remains partial for exhaustive non-Sequencer banks/pad modes. Physical
validation remains **NOT RUN**.

Task 096B adds an explicit Reference Controller V2 descriptor/profile, the
shared sixteen-destination primary/SHIFT resolver, production controller page
routing, and the Slint reference shell's top/right/bottom zones. The locked
inventory is 58 buttons (including 16 pads) and nine relative encoders; logical
IDs remain distinct from placement and protocol IDs. Simulated and generated
component evidence is documented in
`docs/audit/task-088/TASK_096B_VALIDATION.md`; physical validation is NOT RUN.

Task 096A adds opt-in presentation-neutral gesture arbitration to controller
sessions: correlated presses/releases, bounded policies and repetition,
consume-on-chord, stable semantic targets, modal/context generations, and safe
disconnect reset. Reference Controller V2 page/binding integration and physical
validation remain outstanding.

## Task 095 frontend verification and client independence

Frontend refresh now consumes a revision-cached immutable application snapshot;
same-revision clients avoid another value copy. Generated Slint history
callbacks and structured shell geometry run at both supported resolutions. A
non-Slint/non-SDL fixture proves discovery, typed operations, history, audible
rendering, save/reopen and a deliberately non-reference controller inventory.
See [Task 095 validation](audit/task-088/TASK_095_VALIDATION.md). AUD2-011 and
AUD2-012 remain partial; coordinate and physical input are **NOT RUN**.

## Task 094 unified history and atomic content recovery

`ApplicationService` now presents one bounded chronological Undo/Redo sequence
for timeline edits, modulation/macro authoring and acknowledged compatible
preset activation. Configuration restoration uses the existing prepared audio
activation and moves history only after the matching callback acknowledgement;
failed or cancelled operations leave history and active state intact. History
stores configuration and sample paths/metadata, not PCM, and is capped at 32
entries.

Project directory-prefix recovery validates the complete candidate and all WAV,
slice and region dependencies before publishing every immutable sample version
as one activation. Preset dependency repair persists through a generation-
atomic library write. Browser enumeration uses bounded windows and exposes
entries beyond the former 8192 ceiling. Logical cancellation and stale-result
rejection are bounded; an arbitrary uninterruptible filesystem call still has
no portable strict shutdown bound.

See [`APPLICATION_HISTORY.md`](APPLICATION_HISTORY.md) for transaction rules,
memory bounds, listing consistency and the future single-checkpoint temporary
performance-state design. Physical Reference Controller V2 interaction remains
Task 096 and physical validation is **NOT RUN**.

Task 094 validation passed the dependency-enabled development suite (33/33),
production Slint suite (36/36, including both software-rendered target sizes),
ASan/UBSan suite (33/33), sandboxed production `nix build`, `nix flake check`
(including the AArch64 compilation check), and an installed-package headless
device-enumeration smoke test. Dedicated generated-component callbacks for the
new history and recovery operations were not added; existing Browser callbacks
and rendered states compile and pass, so that interaction-specific coverage
remains a documented Task 095 verification gap.

## Task 092 standalone live-loop workflows

The shared recording surface now discovers and selects stable live-loop
sessions, reports the actual pass and iteration, and exposes initial capture,
overdub, replacement, cancellation, export and confirmed session clearing.
Performance contextual buttons bind the selected/active session identity for
arm/start, stop, overdub, replace and cancel.

Two correctness defects were fixed. Empty projects may now capture into any of
the instrument's sixteen sample slots rather than being rejected according to
the number of already-active slots. Finalization waits for the exact pending
sample revision, so an older loaded version cannot acknowledge a newer pass.
Cancelled or failed overdub/replacement passes preserve the previous valid
audio-active loop.

The deterministic production workflow verifies a 100-frame capture, `.25`
initial PCM, `.625` feedback overdub, `.1` replacement, cancellation recovery,
float WAV export, project save/reopen and restored audible playback. Physical
gaplessness, input latency and controller/touch operation remain **NOT RUN**.

## Task 091 modulation and performance macro authoring

The production FX/Modulation surface now authors the existing graph instead of
only inspecting it: users can choose implemented source types, create,
duplicate, enable and remove sources,
edit their descriptor-backed parameters, create compatible effect routes, and
edit route depth and state. The same surface creates/names performance macros,
assigns a selected compatible synth/sample/mixer/FX parameter, edits or inverts assignment
ranges, adjusts live macro values, and captures, updates, recalls or deletes
macro scenes. Sound continues to expose the focused track-local route view.

`modulation.source-reset.v1` now actually restores defaults and rejects a reset
that would invalidate existing scoped routes. Macro assignment updates and
scene rename/update/duplication are presentation-neutral semantic actions. A
deterministic production-path test verifies graph publication, cycle rejection,
multi-destination macro response, scene recall and save/reopen restoration.
General authoring Undo/Redo remains a documented limitation because the current
history authority is timeline-scoped.

Task validation passed 33/33 development tests, 36/36 Slint-enabled tests at
both supported render sizes, and 33/33 ASan/UBSan tests (LeakSanitizer disabled
under ptrace). The sandboxed production `nix build`, full `nix flake check`
including the AArch64 cross-compile check, and installed headless device
enumeration smoke test also passed. Physical controller, touchscreen and audio
hardware validation remains **NOT RUN**.

## Task 090 missing-device runtime and production packaging

The libremidi abort was reproduced at `udev.hpp:101`: the Slint Nix environment
made `libudev.h` discoverable, enabling an optional helper that asserts when a
restricted process cannot create its netlink monitor. The supported
`LIBREMIDI_NO_UDEV` configuration now disables only that metadata path; ALSA
sequencer MIDI, exact endpoint matching and announce-event hotplug remain.
Missing MIDI infrastructure is diagnostic rather than process-fatal, with a
dedicated construction/exact-match regression test.

The default flake package is now the pinned, sandboxed Slint 1.19.0 production
appliance. It uses fixed Slint/Corrosion inputs and a fixed-output Cargo vendor
closure, installs the frontend, diagnostics, discovery, launcher, preflight and
service resource, and performs installed-runtime checks. Explicit `headless`
and `sdl` packages preserve lightweight development. x86_64 sandbox build and
virtual-X11 installed frontend execution pass; physical hardware and native
Raspberry Pi 5 validation remain **NOT RUN**.

## Task 089 reusable patterns and arrangement authoring

Reusable `PatternDefinition` content is now the explicit edit authority whenever
a nonzero pattern is selected. Step notes, advanced trigger properties,
microtiming and parameter locks resolve through the stable `(PatternId,
TrackId)` target; pattern ID zero remains the isolated legacy `Track::pattern`
compatibility path.

The Slint Arrangement subview now places pattern and audio clips, selects,
moves, resizes, duplicates and confirms deletion, edits loop boundaries, and
seeks to a clip using existing timeline actions. The deterministic
`reusable-pattern-arrangement` workflow authors two patterns on one external
MIDI track, verifies isolated advanced edits and parameter locks, renders
distinct MIDI, exercises arrangement Undo/Redo, saves/reopens, and rejects
referenced-pattern deletion. AUD2-001 and AUD2-002 are resolved for this core
workflow; full DAW gestures and physical validation remain out of scope.

## Task 088 consolidated audit

The post-remediation evidence audit is published in
[`docs/audit/task-088/`](audit/task-088/IMPLEMENTATION_AUDIT.md). It reconciles
AUD-001–AUD-017 and separates engine, semantic API, Slint, dedicated/generic
controller, persistence, automated test and physical evidence.

The engine/application surface is broad and the eight Slint pages are real, but
the first complete standalone song-authoring workflow remains blocked. Step
editing mutates the legacy `Track::pattern`, whereas reusable pattern lifecycle,
launching and arrangement clips use `SongArrangement::patterns`; the Slint
Arrangement subview is selection-only. The audit therefore reopens AUD-002 and
recommends Task 089 to unify reusable pattern editing and add transactional
arrangement authoring before optional DSP expansion.

No callback-safety or data-loss P0 was confirmed. A direct host Slint test run
did expose an assertion abort in libremidi when its udev monitor was unavailable;
this is tracked as AUD2-004 pending clean-environment reproduction. The default
Nix package still disables Slint. All N150, Raspberry Pi 5, physical audio/MIDI,
RP2040 controller and touchscreen validation remains **NOT RUN**.

Task 087 adds a Release `production` preset with Slint, an installable appliance
tree, Slint-first launcher, storage/device preflight and a graphical-session
user service with bounded restart. Maintenance mode uses Null audio and isolated
preferences for configuration recovery. Reference x86_64/AArch64 deployment and
the validation template are documented. All physical audio, MIDI, controller,
touchscreen, storage and thermal tests remain **NOT RUN**. A fully sandboxed
Slint flake package also remains outstanding; the pinned CMake/CI path is the
current production frontend build.

Task 086 completes the safe core of standalone MIDI/controller configuration.
Registry locks are released before external work; reentrant shutdown and failed
feedback are tested. Setup exposes bounded musical route editing, mapping
inspection/removal, transactional learn and `.ngc` persistence. Required action
arguments are validated. Manual scaling/inversion/target editing remains, and
Linux hotplug identity plus physical feedback still require hardware QA.

## Task 080 audit summary

The evidence-based audit in [`docs/audit/`](audit/IMPLEMENTATION_AUDIT.md)
classifies engine, semantic API, Slint, controller, persistence, test and
appliance behavior independently. At the Task 080 baseline, the
engine/application layer was broad and well covered by deterministic host tests,
and all eight Slint destinations were real components rather than blank
placeholders. The Slint surface was not yet a complete standalone-instrument
workflow: recording/resampling/live-loop setup,
sequence pages beyond 16 steps, advanced step properties, processing-result
publication, modulator and macro creation, preset authoring, MIDI routing,
controller-map editing, missing-asset recovery and touchscreen text entry remain
incomplete or backend-only. Tasks 081–085 resolve the listed recording,
processing publication, sequencer, text/path, project relink and preset
authoring gaps to the limits documented below; MIDI/controller configuration
and physical appliance validation remain outstanding.

No P0 data-loss, crash or callback-safety defect was confirmed. P1 blockers and
evidence are tracked with stable IDs in
[`PRIORITIZED_BACKLOG.md`](audit/PRIORITIZED_BACKLOG.md). Notable P2 risks are
full Slint model reconstruction on frequent revisions, synchronous Browser I/O,
controller coordinator callbacks under its global mutex, lack of rendered/touch
interaction tests, and the fact that the primary Nix/CI package disables Slint.
These are not presented as measured failures. Physical N150/Raspberry Pi 5,
audio/MIDI interfaces, RP2040 controller, touchscreen and soak validation remain
explicit hardware work.

The recommended sequence begins with Task 081 (recording and sample-result
workflow completion), followed by measured Slint publication/render testing,
Sequencer completion, touch-only path/relink workflows, preset authoring, MIDI
and controller configuration, then reference-appliance deployment validation.
The shared granular DSP investigation remains deferred and unchanged.

Task 079 completes the initial eight-page Slint rollout with an independent
Setup component. It distinguishes active audio configuration from requested
and pending preferences, presents fixed 48 kHz operation, marks device/channel/
buffer changes restart-required, exposes MIDI clock through semantic actions,
and copies controller, lifecycle and diagnostics state. MIDI learn and `.ngc`
save use synchronized coordinator methods; graceful exit retains dirty-project
protection. Live audio-device replacement, a touchscreen keyboard, complete
graphical MIDI routing and semantic controller-map editing remain deferred.

Roadmap: investigate a shared multi-use granular DSP core for a sample
instrument, native insert effect, rolling/live capture and sequenced or
modulated granular processing. It should reuse discovery, automation,
modulation, macros and controller bindings. Task 079 implements no granular DSP.

- Task 072: Slint is selectable in the production appliance with
  `--frontend slint`; the shared shell provides eight registered destinations,
  authoritative transport/project/context status, track ownership, semantic
  transport/track/slot/return actions and responsive 2x8 performance slots.
  Seven pages, record workflow selection, dedicated scene/macro widgets and
  physical touchscreen validation remain deferred.

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

Task 073 adds the first interactive Slint Sequencer page. Renderer-neutral
shell, contextual, Performance and Sequencer models have deterministic local
revisions; recording states and non-quarter-denominator positions are correct.
The Step view performs semantic note, pattern, resolution, velocity and
duration operations. The Arrangement subview provides clip/loop inspection
and local selection. Launch pads use ascending stable slot IDs. Pattern-length
and full arrangement editing remain deferred where no suitable semantic action
is available.

Task 074 adds a production Slint Sample Editor backed exclusively by existing
sample services. It shows callback-acknowledged metadata and bounded mono/stereo
waveform overviews, edits stable slices and regions, assigns pads, runs preview,
transient analysis and offline-processing actions, and handles local zoom
without waiting for unrelated application revisions. Path entry replaces a
decorative file picker. Processing publication, detailed candidate editing and
preview-playhead feedback remain explicit or deferred where the application API
does not expose an acknowledgement.

Task 075 extracts Performance, Sequencer and Sample into independent typed
Slint components and adds a production Sound component. The page switches the
selected track between the existing sampler and subtractive synth, edits
descriptor-backed instance parameters and sample-pad envelopes/playback,
inspects and edits local modulation routes, and drives transactional preset
validation/application. Preset enumeration/save and detailed graph creation
remain unavailable until semantic application actions exist.

Task 076 adds an independent production Slint Mixer component. It presents
track, group, auxiliary-return and master strips with stable selection,
eight-channel banking, gain/pan, mute/solo, sends, routing and bounded
peak/clip telemetry. External-FX status and wet control respect published
backend availability; physical audio-device behavior remains hardware QA.

Task 077 adds an independent production Slint FX component with owner-aware
serial-chain editing, descriptor-driven parameters, modulation-route editing,
performance macro values and validated external-loop controls. It also closes
a semantic validation gap for dynamic native-FX modulation destinations.

Task 078 adds an independent production Slint Browser component with bounded
project/sample/preset navigation, filtering and sorting. Project lifecycle
operations preserve dirty-session confirmation, WAV loads target the selected
immutable sample slot, and preset activation remains transactional. Recursive
indexing, background pagination, native file picking and preset mutation remain
deferred.

Task 081 adds a presentation-neutral recording coordinator and compact Slint
surface for external capture, resampling, live loops, arrangement recording
and performance capture. Sample processing now exposes progress, cancellation,
explicit save and callback-acknowledged publication. Tests assert captured
stereo PCM and normalized processed PCM. CI compiles generated Slint components
and runs the Slint-enabled suite.

Task 082 adds measured incremental Slint publication and generated-component
rendering tests. Playback telemetry updates the active page without replacing
unrelated structural models; stable vector models use row updates. Production
callbacks and all eight pages render through Slint's software renderer at
1920x1200 and 1024x600 in CI, with failure artifacts retained. The measured
debug x86_64 workload reduced median adapter refresh from 70.591 us to 60.774 us.
Complete snapshot copying, coordinate-level input simulation and physical
touchscreen/controller validation remain limitations.

Task 083 completes the essential Sequencer paging and lifecycle workflow.
Patterns longer than sixteen steps use absolute pages with partial-page
validation and optional playhead follow. The selected event inspector exposes
velocity, duration, pitch, probability, conditions, ratchets, microtiming and
discoverable parameter locks. Stable-ID track and pattern controls add safe
creation, selection, duplication, resizing, activation and confirmed removal.
Timeline transactions reject destructive shortening and referenced pattern
deletion; application selection recovers after object removal. The incremental
publication path updates playhead telemetry without replacing the structural
step, track or pattern models. Generated software-rendered Sequencer coverage
runs at both target resolutions. Touch text entry, reusable-pattern-track
content authoring and full Arrangement editing remain deferred.

Task 084 adds shell-level UTF-8 touch text entry and a reusable asynchronous
filesystem picker. New project/save-as, track and pattern naming, sample
selection, recording/processing export naming and controller-profile save no
longer require typed absolute paths. Browser enumeration and preset parsing run
on a joined, stoppable control worker with generation-based stale-result
rejection, deterministic 128-row pages and an 8192-entry scan bound. Project
missing-sample identities are visible and can be transactionally relinked to an
explicit compatible file; folder assistance proposes the original filename
only. Atomic multi-asset prefix relinking and preset-package recovery remain
open. Generated software-rendered coverage includes the keyboard at both
target resolutions; physical touch and removable-media validation remain
outstanding.

Task 085 completes authoring and management for the existing reusable preset
formats. ApplicationService captures synth, sample-instrument, track FX-chain
and track-sound state from an explicit stable source; Browser/Sound/FX expose
touch-only save, metadata inspection, transactional activation, rename,
duplicate, explicit update and confirmed deletion. Library writes retain the
generation-atomic manifest protocol, while activation allocates fresh runtime
identities and waits for callback acknowledgement. Preset assets can be
explicitly relinked for a prepared activation. Persisting repaired dependencies
back to a package, group/aux/master FX capture and macro capture remain known
limitations.
## Task 093 hardware-first interaction

The controller and Slint clients now share named page/bank context identities,
typed action arguments, discovered parameter/action targets, and explicit
logical performance-surface assignments. The sixteen-pad/eight-encoder/eight-
function layout remains a provisional reference profile: application and
profile identities do not encode PCB positions, and logical launch positions
are unbounded by that presentation count.

Timeline schema 11 persists `LaunchSlot::logical_position`. Older timelines
migrate the previous ascending-stable-slot presentation deterministically at
load time without rewriting the source. New and duplicated slots remain
unassigned until explicitly placed. Slint exposes bank identity and bank
navigation; Setup exposes discovered mapping targets, while controller
profiles now round-trip inversion, fine scale and acceleration.

Software simulation covers physical page selection and launch, explicit
assignment/reassignment/clear, contextual argument copying, profile round-trip,
generated bank and mapping-target callbacks, and both rendered resolutions.
Physical panel ergonomics and final label placement remain NOT RUN. Task 093A
subsequently locks the V2 inventory and relative layout; it does not provide
prototype measurements or implement the new assignments.

## Task 093A Reference Controller V2 specification

`REFERENCE_CONTROLLER_V2.md` locks the intended inventory and relative
placement: eight static page controls, eight contextual functions, eight
right-side contextual encoders, DATA plus six navigation controls, ten editing
controls, ten transport/performance controls and sixteen pads. It defines
primary/SHIFT destinations, modifier chord ownership, held-step locks,
contextual banks, modal precedence, project-owned Performance customization,
feedback semantics and the firmware boundary without coupling application state
to panel positions.

This is a design specification, not an implementation claim. Task 093 provides
reusable typed contexts, descriptors, logical pad assignments and feedback;
`REFERENCE_CONTROLLER_V2_GAPS.md` records the missing V2 gesture arbiter,
shifted page routing, dedicated cluster mappings, held-pad lock path,
project-owned encoder/function banks, modal focus, RGB policy and firmware/
physical validation. All physical V2 validation remains NOT RUN.

## Task 096C Reference Controller V2 dedicated clusters

The optional V2 reference profile now drives the existing semantic application
through focus-aware DATA, bounded directional repetition, BACK/ENTER, stable
TRACK/PAGE/MUTE/ERASE pad chords, unified Undo/Redo, conditional Fill,
configured recording, tempo, play/stop/restart and MIDI panic controls. The
stable erase action removes only existing reusable-pattern events and remains
transactional. Unsupported solo/arm pad modes, typed clipboard, metronome,
count-in, note repeat, groove, live erase and ambiguous loop shortcuts remain
unassigned and visible in the Task 096C matrix. Software simulation passes;
physical validation remains **NOT RUN**.

Task 097A adds shared destination, stable focus, DATA-role and generation-owned
modal state. V2 ENTER/BACK, shifted navigation, root navigation and physical
Save now reach production Slint workflows. A protocol-level scenario covers
captured destructive confirmation, Browser navigation, Save As and reconnect
cleanup. Assignment/profile forms and physical validation remain open.

Task 097B completes the policy-derived 48-position Performance chooser,
descriptor-driven parameter/safe-action arguments, non-triggering pad launch
assignment form, indexed contextual/Performance role targets, manual MIDI
behavior fields and structured conflict confirmation in the generated Slint
shell. RC2-005 graphical authoring is closed in software. RC2-006/AUD2-006 stay
partial at an exact service boundary: the coordinator does not yet own a
multi-profile create/rename/duplicate/select/remove/recovery library, and the
current profile schema has no detailed context/modifier or temporal RGB
feedback authoring records. Physical validation remains **NOT RUN**.
## Task 097F controller profile library (2026-11-26)

Appliance-owned controller profiles now have stable library identities,
bounded lifecycle operations, explicit per-endpoint activation, atomic `.ngc`
and manifest persistence, known-valid recovery, offline authoring and generated
Setup presentation. Setup and Performance typed-argument forms are descriptor
driven rather than limited to four fields. RC2-006/AUD2-006 are substantially
implemented in software; dedicated USB runtime switching, conservative layered
source conflicts and physical validation remain explicit limitations.

## HW-006B negotiated controller protocol 1.4 (2026-11-27)

The host mutually negotiates minor 1.4 while preserving exact 1.3 frames.
Independent optional P1-P8 push and encoder-RGB capabilities require canonical
inventories before use. Pushes are inert by default; extended snapshots retain
no-synthetic-press reconciliation. Descriptor-driven encoder illumination
reuses bounded LED_RENDER publication. Normative fixtures and the HW-006C
contract are published; firmware and physical USB/RGB evidence remain NOT RUN.
