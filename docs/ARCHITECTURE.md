# Architecture

Recording presentation composition lives in `RecordingWorkflowCoordinator`.
It owns pending stable configuration only; `RecordingService`,
`LiveLoopService` and `ArrangementRecordingService` remain the state
authorities. Sample-processing source/result ownership remains in
`SampleProcessingService`; frontend models never hold PCM.

Integrated performance validation drives the production prepared-timeline,
immutable-sample, mixer/FX/modulation, MIDI, live-loop, launcher, and capture
paths. Simulated devices provide deterministic input and timestamps; there is no
alternate test audio engine.

Performance activation acknowledgements feed the control-side arrangement
recorder. It materializes resolved launches in one validated, undoable timeline
transaction; follow actions prepare at most one future transition per
acknowledgement and never traverse a graph in the callback.

Performance launching is a control-side coordinator over the authoritative
timeline. It validates slot/scene definitions and publishes prepared pattern or
audio plans at an absolute frame; existing scheduling, SampleVoice ownership
and mixer routing remain the only playback paths.

Hybrid plans combine arrangement and session material in that scheduler.
Per-track ownership and a bounded affected-track list constrain voice cleanup;
absolute-frame seek restores arrangement phase on return.

Live looping composes the callback recorder, a prepared sample-version/voice
handoff, immutable publication and audio-clip transactions. Overdub mixing and
activation remain control-side.

Audio arrangement overlaps reuse the bounded sample voice pool. The timeline
compiler resolves crossfade IDs and prepares frame ramps; the callback only
evaluates scalar envelopes.

Audio clips compile into bounded timeline start/stop edges and instantiate the
existing timeline-owned sample voices. Audio follows the normal track →
group/aux → master graph; there is no parallel clip mixer or sample engine.

Offline processing workers retain immutable source versions, build new samples
away from the callback, save WAV assets through sibling temporary files, and
use the existing callback-acknowledged `SampleManager` publication path. Active
voices retain old shared versions until normal control-side reclamation.

Sample analysis workers retain immutable `SampleManager` versions and publish
only transient control-side result snapshots. Accepted boundaries enter the
existing `SliceService` region/slice model through a validated batch; the audio
engine never analyzes PCM or observes editable candidate objects.

Sample regions are control-side metadata compiled into bounded targets. A voice
copies boundaries, fade/loop settings and an immutable sample-version reference
at trigger time, so edits affect new triggers without mutating active voices.

Reusable sound configuration is stored outside projects by the versioned
`PresetLibrary`. It composes the authoritative slice, native-FX and modulation
serializers, validates and remaps template IDs on the control thread, then
hands bounded configurations to existing publication paths. See
[PRESETS.md](PRESETS.md).

Preset application uses a fixed, control-prepared activation pool. Records
contain mixer/modulation/synth state and reserved sample-version indices. The
callback performs bounded activation and acknowledgement without allocation,
decoding, locks, logging, filesystem access, or sample destruction.

The versioned [application discovery contract](APPLICATION_DISCOVERY.md)
projects existing actions, parameters, objects and subsystem descriptors into a
single copied control-side model. The
[typed parameter binding layer](PARAMETER_BINDINGS.md) resolves stable or
selection-relative object references, validates typed values and routes writes
through `ApplicationService` semantic actions. Bounded
capability/state/telemetry notifications require explicit snapshot
resynchronization after overflow. These facilities perform no callback work or
own engine state.

Integrated ownership and publication are exercised by the headless four-tier
harness in [INTEGRATION_VALIDATION.md](INTEGRATION_VALIDATION.md). It combines
immutable samples, compiled timelines, prepared mixer/effect/modulation
snapshots, bounded MIDI and recording queues, and callback macro morphs while
keeping project I/O and preparation on the control side.

Live arrangement capture uses a bounded callback-to-control acknowledgement
queue. The callback emits only the exact activation frame; the application
thread converts it to musical time, builds clips/automation, validates a full
snapshot and publishes through `TimelineService`. No editing, allocation,
locking or serialization occurs in the callback. See
`ARRANGEMENT_RECORDING.md`.

Pattern definitions, clip placements, and song ranges are control-side data in
the revisioned `Timeline`. `materialize_arrangement` validates and expands a
live pattern or song into a temporary legacy-shaped timeline before
`compile_timeline` creates the immutable playback plan. The callback receives
only that prepared plan and retains the deterministic scheduler.

Pattern chains and follow actions are likewise materialized control-side into
a bounded prepared plan. Scheduled publication carries an absolute activation
frame and voice policy through a bounded SPSC queue. The callback may split one
buffer at that frame, atomically exchange plan slots, and reclaim neither plan
nor voice resources. Song-end frames are prepared with the plan and stop the
transport without polling.

Sample voices contain preallocated `SampleVoiceModulationState`. Persisted
definitions are immutable shared configuration, while phase, envelopes and
random state live in the bounded voice pool. Note events allocate and lock
nothing, and publication remains control-side prepared.

Dynamic modulators have stable identities and explicit track, group, aux or master
ownership. Discovery metadata stays separate from compact callback state and
complete configurations are validated before bounded publication. See
[Dynamic modulator instances](MODULATOR_INSTANCES.md).

Cross-owner and modulator-parameter connections compile control-side into a
stable-ID topological order. Audio-derived inputs use previous-block mixer
observations. See [Modulation graph](MODULATION_GRAPH.md).

Native modulation processors use descriptor-defined fixed input/output ports.
Step tables and runtime state are bounded inside prepared instances; utility
processors and generators therefore share graph ordering, publication and
ownership rules without callback allocation or dynamic dispatch.

Native processing uses a bounded effect-rack snapshot within mixer publication.
Track, group, aux and master chains own stable instance IDs; preallocated runtime state
never enters application snapshots. See [FX_CHAINS.md](FX_CHAINS.md).

Physical controllers are `ApplicationService` clients through independent
`ControllerSession` objects. Transport-independent device descriptors declare
controls and feedback; configurable profiles resolve those controls into
semantic actions, typed parameters and contextual roles. USB CDC and MIDI
adapters only normalize transport events. Session-local pages, modifiers,
pickup and feedback caches cannot become a second musical-state authority.
The dedicated physical controller is one built-in descriptor/profile pair;
protocol 1.0/1.1 use explicit legacy descriptors rather than mutating the
current descriptor from profile controls. `ControllerCoordinator` owns any
number of explicitly configured CDC and MIDI sessions in the appliance.
`ControllerSerialTransport` owns nonblocking Linux TTY I/O,
`ControllerFrameDecoder` owns bounded wire framing, and `ControllerHost` owns
negotiation, semantic dispatch and revisioned feedback. None is visible to the
audio callback. The packet/session boundary permits a future HID transport.
The revisioned `ApplicationState::controller_context` selects the global UI
mapping group. `resolve_controller_context()` combines an explicit client
context, focus and local modifiers; `build_controller_context()` is its global
compatibility wrapper. It combines context, focus, modifiers,
performance mode and parameter metadata for SDL3, Slint and CDC clients. The
protocol exposes logical controls and LEDs, never RP2040 GPIOs, shift-register
positions or LED-chain offsets. Protocol 1.2's current dedicated profile has no
analog controls; optional MIDI/legacy analog adapters retain binding-service
pickup independently.

Timeline-owned sample and synth voices render into fixed per-track buffers.
The bounded mixer applies channel gain/pan/mute/solo, eight acyclic stereo group
buses, two aux buses, physical
inputs, master gain, metering and explicit outputs before the existing master
effects. Complete validated snapshots cross a bounded SPSC queue; the callback
never constructs a graph. See [mixer architecture](MIXER.md).
Group topology is validated and topologically prepared on the control thread;
callback storage and processing remain fixed. See [group buses](GROUP_BUSES.md).
External hardware loops are bounded members of the same snapshot, not a
parallel graph. Delay lines and return taps are fixed storage; routing scenes
remain control-side data and publish atomically.

The control thread owns files, projects, scripts, mappings, editable state, and
immutable compiled timeline plans. The audio thread consumes only plan pointers,
bounded event staging, and sample memory prepared in advance.

```text
clients -> application actions / parameters / state -> timeline / sample manager
                         | compiled, bounded PlaybackPlan snapshot
                         v
authoritative transport -> scheduler -> instrument routing -> track/master DSP -> backend
                                                    \-> offline WAV
```

- `core`: stable command identifiers and application-domain boundary.
- `application`: typed semantic actions and parameters, bounded ordered command
  processing, immutable state snapshots, controller capabilities/mappings,
  feedback, grid/navigation/text, and X/Y interaction models.
- `timeline`: UI-independent tick events, tracks, arbitrary loops, transport
  metadata, compilation, and sample-accurate playback.
- `sequencer`: the legacy 16-step compatibility scheduler and adapter, including
  next-bar Lua pattern swaps.
- `audio`: prepared sample voices, miniaudio playback adapter, and offline renderer.
- `midi`: multi-port libremidi input/output adapters, stable controller
  mappings, validated thru routes, a bounded outbound scheduler, external MIDI
  timeline tracks, and rational 24-PPQN clock generation/reception.
- `scripting`: genuine Lua 5.4/sol2 `neurogenic.v1` API on a non-audio thread.
- `instrument`: bounded native instrument contract and subtractive synth.
- `dsp`: allocation-free processor contract, gain stage and stereo delay.
- `project`: versioned generation-directory persistence for complete control-side
  timeline, sample/slice, pad, routing, and native parameter state. It publishes
  only after parse/preparation and never serializes callback-owned objects.
- `recording`: a preallocated SPSC capture buffer with external duplex and
  master pre/post-effect taps. Callback production is separate from control-side
  finalization, WAV output, immutable publication, and project retention.

No callback operation performs I/O, locks, allocation, logging, or Lua work.
Pattern snapshots and control-thread triggers cross into audio through bounded
SPSC queues. The producer drops the newest item on overflow and increments an
atomic diagnostic counter. Each callback drains every trigger that fits its
fixed 64-event staging area, orders triggers by frame offset and enqueue order,
and carries future offsets into later buffers. At equal offsets, external
control events precede sequencer events.

Sample slots use stable 32-bit identifiers. The musical slot namespace is not
limited to a fixed bank; 16 is only the initial note-bank policy, mapping
MIDI/sequencer notes 36–51 to
slots 0–15. Embedders can install any number of slots and map any note before
processing starts. The positional single-WAV compatibility API maps all notes
to slot 0. Each voice stores the resolved immutable sample-version pointer and index, so callback
lookup performs no string work, allocation, locking, decoding, or I/O.

The voice pool is allocated when `AudioEngine` is constructed (32 voices by
default, configurable by the embedding application). Allocation chooses the
lowest-index idle voice. If all voices are active, it steals the oldest-started
voice, with the lowest index breaking ties. Retriggering therefore never resets
an unrelated voice unless the configured pool is exhausted. The stolen-voice
counter exposes that overload condition.

The live MIDI callback copies one- to three-byte messages into a 256-item SPSC
inbox; it does not call mappings, devices, Lua, logging, or filesystem code. A
dedicated `ControlDispatcher` drains mappings and passes raw packets through a
second bounded queue. The audio callback is the sole producer for scheduled
output, thru and panic messages; a non-real-time dispatcher is the sole
libremidi output caller. This preserves SPSC ownership while supporting notes,
CC, program, bend, clock and transport. See [MIDI](MIDI.md).

Editable timelines own strings and vectors on the control side. `compile_timeline`
validates stable identifiers and converts note durations into explicit on/off
events in a preparation-time-allocated `PlaybackPlan`. There is no fixed track
or compiled-event count. Four control-owned plan slots have atomic
free/queued/active/retired states. The callback only changes states and pointers;
the control thread alone destroys retired plans, so queue saturation cannot
leak a plan or invoke a destructor in real time. Scheduling uses a configurable
preallocated staging buffer (512 events by default). Overflow defers the next
event without advancing its cursor and increments an overrun counter. See
[TIMELINE.md](TIMELINE.md).

Each compiled track has audio-owned cursor state, and a preparation-time-sized heap contains
one next-event entry per audible track. A continuous callback visits the heap
root and due events rather than scanning the pattern. Steady-state scheduling is
`O(D (log T + log D) + 1)`, where `T` is playing tracks and `D` is dispatched
events; stored events far from the playhead do not add callback work. Seek,
restart, and plan activation rebuild cursors in `O(T log E + T)`.

Timeline voices are owned by `(track ID, note-on event ID, loop occurrence)`.
Generated duration note-offs preserve that token across loop boundaries.
Explicit note-offs name a target note-on ID. Voice stealing removes only the
selected voice; its later note-off finds no owner and cannot stop another voice.

`AudioEngine` owns the authoritative transport frame and play state. Both the
timeline and 16-step compatibility schedulers receive the same frame range and
tempo; neither owns a clock. Stop freezes position and cleans all voices, play
resumes, and restart cleans voices and returns to zero. Timeline replacement
preserves position and cleans timeline-owned voices. Plan destruction and final
reclamation happen only after the audio device has stopped.

With external MIDI sync selected, a bounded Q32 phase/frequency follower owns
the rate of that same transport frame; it does not create another transport.
The timeline scheduler accepts a prepared-frame range plus the physical buffer
length and scales event offsets into that callback. Clock loss freezes the
authoritative frame after bounded holdover and cleans voices. All follower
state is preallocated and callback-owned.

`SampleManager` owns a bounded asynchronous command queue and one preparation
worker. WAV I/O, decoding, resampling, allocation, status locking, and errors
remain on that worker/control side. It publishes immutable samples through a
bounded version pool. The callback swaps stable-slot resolution, lets voices
retain old versions, and only marks unreferenced versions retired. Destruction
and pool reuse occur on the control side. Unload follows the same rule: new
triggers see an empty slot while existing voices finish. Publication overload
is rejected and counted rather than blocking or freeing memory in the callback.
Monotonic publication sequences acknowledge callback activation back to
`SampleManager`. Frontend snapshots keep audio-active metadata separate from
the newest pending request, so stale preparation completions cannot overwrite a
newer command's visible revision or status.
See [SAMPLES.md](SAMPLES.md).

## CPU and ABI portability

The engine uses standard C++20 and C11 without an x86 SIMD baseline. No target
enables AVX, AVX2, SSE, `-march=native`, or architecture-specific intrinsics.
Any future optimized DSP path must be optional, selected from supported runtime
CPU features, and retain a portable implementation as the default/fallback.

Timeline and legacy scheduler products use compiler-provided 128-bit integers
to avoid cumulative timing drift and overflow. GCC and Clang provide this type
on both supported 64-bit Linux targets. Public musical and serialized values
use fixed-width integer types; container indices remain process-local
`std::size_t`. The supported AArch64 target is little-endian Linux, matching
the RIFF/WAVE byte order used by the current decoder.

## Application editing services

Hardware, touchscreen, desktop, Lua, MIDI, and remote clients sit above shared
C++ command/query/state services. `TimelineService` is the first complete
musical editing service: it returns detached transactions and observable
snapshots, owns optimistic revision checks, validates through the authoritative
timeline compiler, and publishes complete plans through the existing bounded
audio-engine path. Built-in generators, deterministic RNG, and transformations
are C++ operations. Lua contains binding and transaction-lifetime glue only.

No editor holds a playback-plan pointer. The application/control side may copy,
allocate, mutate, compile, or reject timeline data. The callback sees only a
previously compiled immutable plan-slot index, never Lua or an editable object.

`ApplicationService` is the client-neutral facade over transport,
`TimelineService`, and `SampleManager`. Its action registry exposes stable typed
commands and honest availability; its parameter registry owns generic control
metadata; its snapshots expose copied state with application and subsystem
revisions. A bounded multi-producer queue assigns deterministic sequence
numbers and rejects newest commands on overflow. Controller adapters resolve
capability profiles into those commands, and revision-tagged feedback flows
back without a controller directly touching engine state. See
[CONTROLLERS.md](CONTROLLERS.md).

The optional `neurogenic_gui` target is another client of this facade.
`GuiModel` converts snapshots to logical draw commands and input to semantic
commands; the SDL3 adapter only owns windowing, events and rendering. It never
accesses real-time playback structures. X/Y dragging is coalesced to a bounded
125 Hz command rate. Headless operation does not depend on SDL3.

The optional Slint frontend is a second presentation client of the same
facade. `SlintFrontendAdapter` copies revisioned state, resolves context through
the shared controller-context builder, and writes parameters through
`ParameterBindingService`; Slint components receive no engine objects. The
production executable selects exactly one of headless, SDL3, or Slint, so
graphical event loops never compete. Page and focus state are presentation
state, not musical authority.

The Slint snapshot is divided into shell, contextual, Performance, Sequencer,
Sample, Sound, Mixer, FX, Browser and Setup models. Page-local revisions are independent of application
revisions, while every musical edit still passes through `ApplicationService`.
The step grid and Arrangement reader never retain or mutate timeline objects.
The Sample model copies metadata, stable IDs and bounded waveform overview
buckets only. It never owns PCM or a `SampleVersion`; slice, region, analysis,
processing and preview commands remain semantic service calls. This preserves
callback-acknowledged active/pending sample state and safe retirement.
Each implemented page is a separate `.slint` component with typed properties
and callbacks; the shell owns navigation and global context only. The Sound
model derives instrument identity, parameter descriptors and local modulation
routes from one copied application snapshot. Writes remain semantic actions,
including instance-aware synth parameters and transactional preset activation.
The Browser model contains only bounded directory/preset metadata and stable
paths/IDs. Filesystem work stays control-side; project replacement, sample
publication and preset activation retain their transactional semantic services.
The Setup model adds a narrow copy-only appliance bridge. Runtime audio/MIDI
and controller objects remain appliance-owned; the bridge copies device
capabilities, active configuration, endpoint status, lifecycle warnings and
bounded diagnostics. Mutations save validated preferences, dispatch existing
semantic actions, or call synchronized controller-coordinator operations.
Audio settings are staged for restart instead of replacing a running backend.

The GUI keeps SDL window coordinates, drawable pixels, responsive logical
workspace dimensions, and the persisted UI scale as distinct values. SDL
events are normalized into drawable coordinates before `GuiModel` hit testing.
Breakpoints are selected from the post-scale logical workspace, while draw
commands are emitted at native drawable resolution. GUI invalidation uses both
application and UI revisions plus drawable dimensions, so resize, DPI, scale,
view, and focus changes cannot reuse stale geometry. This control-side work is
independent of audio callback cadence.

The step editor is an `ApplicationService` adapter. Selection, resolution and
per-track target context are application state; each musical toggle is a
detached `TimelineService` transaction. Successful commits publish one complete
immutable plan, while conflicts and publication failures preserve the prior
plan. GUI and `PerformanceGrid` clients emit the same stable actions.

Timeline history also belongs to `TimelineService`, so GUI, controllers,
native generators, and Lua timeline commits share it. Snapshot history and
grouped working copies stay on the control side. Commit, Undo, and Redo publish
immutable plans through the existing bounded handoff; the callback never
allocates, locks, or destroys history.

## Performance observation

`AudioEngine` owns a bounded atomic callback accumulator. A callback reads the
monotonic clock at entry/exit, records duration/deadline statistics and counts
active voices; no observer calls into callback-owned structures. A Linux
`SystemMonitor` separately samples `/proc` at 2 Hz on a normal control thread.
`ApplicationService` copies both sources and existing engine counters into an
availability-tagged `PerformanceSnapshot`. GUI, CLI and future remote clients
consume that snapshot rather than engine internals. Monitoring never changes
scheduling, voice ownership, publication, or backend synchronization.

## Instruments and DSP

Compiled tracks carry a stable numeric instrument key derived from their stable
string ID. The shared scheduler preserves note identity, velocity, duration and
exact frame offset, then dispatches to the sample voice pool or subtractive
synth. Both paths use the authoritative transport. Unsupported routing is
rejected during control-side compilation.

Sample chopping follows the same ownership boundary. `SliceService` edits
revisioned metadata on the control thread, `SampleManager` retains the
callback-acknowledged immutable PCM, and `AudioEngine` receives numeric
version/boundary commands. Voices store their chosen sample version and end
frame plus a copied pad playback snapshot. Fractional forward/reverse cursors,
ADSR, pitch/gain, playback mode and choke metadata are entirely bounded voice
state. Choking scans only the configured voice pool; callback publication uses
fixed-capacity queues, and old voices keep their sample/settings across live
replacement. `ApplicationService` validates semantic pad actions through
`SliceService`; GUI and controller adapters never mutate voices.

Realtime stretch is a compact DSP extension, not a second sample engine.
`RealtimeTimeStretcher` owns no PCM and allocates nothing: a voice embeds its
configuration, output/source origins and window size, and reads the same immutable
sample version/bounds as ordinary playback. The control side validates and
publishes settings; voice start resolves tempo metadata. Active voices rebase
their source-time origin before a smoothed tempo-ratio change. Wrapped source
reads support forward/reverse region and audio-clip loops without resetting the
synthesis clock. The callback selects the stretch branch only for enabled
voices, so default playback keeps its previous inner loop.
cursor; replacement cannot redirect or invalidate them. Waveform and transient
preparation never enters the callback.

The current built-ins are statically owned and preallocated by `AudioEngine`.
Synth notes use `(track, event, occurrence)` ownership, while external MIDI
notes retain bounded identities. Atomic targets and a bounded DSP-command queue
separate application updates from processing. The native delay processes the
mixed output after the gain stage. Concrete hot paths avoid virtual dispatch per
sample; abstract interfaces remain preparation and extension boundaries. See
[INSTRUMENTS.md](INSTRUMENTS.md).

Parameter automation remains ordinary authoritative timeline data. Editable
events store stable destination strings; `compile_timeline` validates routing
and converts them to numeric `ParameterTarget`/parameter pairs. The callback
does no string lookup and dispatches bounded due events to the preallocated
synth or master delay before simultaneous note-ons. Application lock actions,
GUI, controllers and Lua all call `TimelineTransaction` rather than duplicating
destination validation or maintaining another pattern model.

Modulation is a numeric layer after stored base and timeline automation.

Performance macros compile to ordinary bounded modulator instances and routes;
there is no parallel destination-composition path. Scene morph commands are
prepared on the control side and placed in the same bounded ordered DSP queue as
manual and automated macro values. A fixed-capacity callback processor advances
them from authoritative transport frames at a 32-frame control interval; it
does not allocate, lock, construct graphs or access persistence. Runtime status
is copied through lock-free atomics for application discovery.
Global and per-track source banks are distinct; track banks own independent
phase, envelope, retrigger, and seeded random state. See
[MODULATION.md](MODULATION.md).

## Track-local instrument ownership

Timeline tracks own stable routing references, not DSP objects. A compiled
track carries its track ID, instrument type key, and bounded numeric instrument
instance ID. `AudioEngine` owns a preallocated rack of 16 subtractive-synth
instances; each instance owns its parameters and bounded voice pool. Notes carry
the compiled instance ID into the callback, while the mixer still keys audio by
track ID. Several tracks may explicitly share one instance, or use different
instances for isolated parameters and modulation. Sample voices remain in the
existing shared bounded sample pool and retain track ownership for mixing.

The ownership chain is therefore: timeline track -> numeric instrument instance
-> preallocated voice -> track mixer channel. Global modulation may address all
instances or one instance; track routes require both track and instance IDs;
voice routes are installed into the selected synth instance and evaluated in
each active voice. No callback lookup uses strings and no instance or voice is
created or destroyed on the audio thread.

Sample and mixer modulation uses the same destination IDs and control-rate
engine. Active sample voices hold only scalar pitch/gain/pan overlays. Mixer,
aux, master and external-loop overlays live in fixed arrays adjacent to the
compiled mixer configuration. They alter effective DSP values only; stored
settings, immutable samples and routing ownership are unchanged.

## Integration boundary

`neurogenic_workflows` is a headless client of `ApplicationService`; it does
not duplicate engine logic. Deterministic fixture creation and simulated device
buffers live in the runner, while commands, state, audio, MIDI, persistence and
reclamation use production services. This catches missing semantic bindings
between otherwise unit-tested modules and provides bounded CI coverage. Track
creation/deletion, authoritative tempo changes, project rename and external
MIDI destinations are application actions; adapters no longer need direct
timeline mutation for those workflows.

## Hardware diagnostics boundary

The `neurogenic-diagnostics` client combines Linux system data, copied
audio/MIDI capabilities, SDL display metrics, and existing performance
snapshots. Device tests enter audio only through `AudioDevice` and
`AudioEngine`; report serialization never runs in a callback. Native formats
are enumerated on the control thread, and startup explicitly verifies the
negotiated 48 kHz rate. System tuning and hardware policy remain outside the
engine.

The Linux adapter accepts a preferred period hint, records negotiated period,
rate and channel layout, and publishes atomic lifecycle counters from miniaudio
notifications. Notification callbacks do not log or reopen devices. Recovery
and matching-device selection remain control-side. Scheduling policy/limits,
governor and memory-lock limits are inspected outside audio. Callback duration
overruns and backend XRUN capability are deliberately separate diagnostics.

## Appliance lifecycle

`ApplianceLifecycle` is a control-side state machine. Configuration, project
selection/restoration, audio, MIDI, controller discovery, GUI, ready, shutdown,
and stopped stages produce revisioned snapshots and versioned diagnostic JSON.
Recoverable device failures mark the session degraded without hiding their
reason. `AppliancePreferences` is separate from project musical data; an
explicit project always wins over reopen-last. Filesystem access, device
startup, restoration, and shutdown coordination remain outside audio.

Dirty-session protection exists at both boundaries: lifecycle shutdown requires
save/discard/cancel, while destructive `project.new/open/close` actions reject
dirty state unless the caller explicitly supplies `discard=true`.
Trigger probability and conditions are immutable compiled-plan metadata.
Scheduling uses a stateless integer hash rather than a callback RNG, and Fill
crosses into the audio thread through the bounded transport queue. Ratchets are
expanded at preparation time; the callback performs no allocation or lookup.

## Slint presentation publication

Slint remains a client of copied `ApplicationState`. Structural fingerprints
select full presentation reconstruction; transport/playhead, meter, workflow
progress, parameter and contextual-control changes use bounded telemetry updates.
Only the active page receives telemetry model rows, equal-size vector models
retain identity, and page activation refreshes authoritative state. This adds no
frontend dependency, allocation or lock to the audio callback. Complete snapshot
construction remains control-side and is a measured future optimization boundary.
