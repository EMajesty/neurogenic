# Modulation matrix

For samples, effective values combine base or automation, global/track
contributions and private voice contributions exactly once. Voice pitch is
additive in semitones, gain is multiplicative and pan is summed then clamped.
Realtime stretch keeps time ratio independent from modulated pitch. Pitch,
gain and pan are supported; position and filter modulation remain deferred.

New configurations use independently creatable stable-ID instances owned by a
track, aux bus or master. The fixed bank remains a compatibility adapter. See
[Dynamic modulator instances](MODULATOR_INSTANCES.md).

Cross-owner and modulator-parameter connections form a deterministic DAG; see
[Modulation graph](MODULATION_GRAPH.md).

The native library includes tempo-synchronized step and Euclidean generators,
clocked weighted/correlated random, a looping function generator, bounded math
utilities and logistic-map evolving modulation. All run at the same bounded
control rate and publish discoverable named ports; there is no parallel
sequencer or modulation engine.

Native effects use `fx.INSTANCE.PARAMETER` destinations. Only descriptor-approved
numeric values are modulatable; topology and resources are rejected. Base values
remain separate from effective automation/modulation values.

The modulation matrix is a bounded control-rate client of the existing numeric
DSP destinations. It does not replace stored parameters or timeline events.
For each destination the engine computes:

`effective = clamp(automation-or-base + sum(source × bipolar depth))`

[Performance macros](PERFORMANCE_MACROS.md) are discoverable manual modulator
instances. Their assignments contribute native-unit deltas relative to the
macro default, allowing several macros and ordinary modulators to share one
destination without changing its stored base or automation value.

Macro source values may be manual, timeline-automated, modulator-driven or
produced by a prepared scene morph. Morphing changes only the macro source's
effective value. Destination composition remains
`automation-or-base + modulation + macro deltas`; locks and stored bases are
not rewritten. Macro morph interpolation runs at the same bounded 32-frame
control cadence, and existing destination smoothing is retained rather than
duplicated.

The sum is performed in the destination's normalized range and converted back
to native units. A step lock or automation event replaces the base layer until
another event changes it; routes remain active. Snapshots expose stored base
values, effective values, route source values, and a generation counter.

## Sources, scope, and timing

The source bank contains four sine/triangle/saw/square/random LFOs, an ADSR
modulation envelope, sample-and-hold, smooth random, X/Y, note velocity, MIDI
CC, and a peak envelope follower. Every source declares `global`, `track`, or
`voice` scope. Global
sources have one state. Track sources create fixed-capacity independent state
for every routed track, including phase, envelope gate/stage, retrigger state,
and a random stream derived deterministically from `(seed, track ID)`.

Voice scope is implemented for the subtractive synth. Each preallocated voice
owns independent ADSR, velocity, note-triggered LFO phase, and deterministic
random state. Voice routes currently accept pitch, filter cutoff, and gain.
Other voice destinations are rejected rather than promoted to global scope.

The engine updates at a bounded interval (32 samples by default). Phase advances
from elapsed frames, so callback partitioning does not alter frequency. Free
rates use hertz; synchronized rates use beats per cycle and authoritative
transport tempo. Restart resets phase and automation. Retrigger-enabled global
sources restart on any routed note; track sources only restart for their track.
The global envelope is currently a monophonic gate; every track envelope has an
independent gate. Runtime phase is not persisted.

Random sources use a fixed integer generator with an explicit seed, never
process randomness. Reconfiguration produces repeatable output for identical
settings and track IDs. X/Y and MIDI CC enter through bounded commands. The
initial follower reads the previous master pre-effects block and applies
configurable attack, release, sensitivity and threshold. This deliberate
one-block observation latency keeps the tap bounded; selectable track, aux and
physical-input taps remain deferred.

## Routes and destinations

Routes have stable IDs, source, explicit scope/track ID, numeric destination,
bipolar depth, polarity, and enabled state. Sources may feed multiple
destinations and contributions to one destination sum before clamping. Initial
real-time destinations are subtractive-synth and native-delay controls; sample
pitch/gain/pan; track gain/pan/two aux sends; aux return and master gain; and
external-FX wet/send/return gain. Fixed-size transient overlays feed the mixer,
so modulation never republishes its configuration. With stretching enabled,
sample pitch changes the independent pitch ratio without changing time ratio.

Discrete values, files, channel assignments, latency sizes, routing topology
and immutable sample ownership are rejected. Sample filter cutoff is deferred
because sample voices do not yet own a filter. Stored pad, mixer and scene
values remain unchanged.

## Ownership and clients

`ApplicationService` owns editable source/route configuration and validates
`modulation.*.v1` actions. It publishes complete fixed-size snapshots through a
bounded SPSC queue. The callback resolves no strings and performs no allocation,
locks, logging, or graph construction. Limits are 32 routes and 32 routed track
state banks. Atomic snapshots provide read-only UI/controller feedback.

The GUI `MOD` view selects routes with Up/Down, adjusts depth with Left/Right,
and toggles enable with Enter. The same actions support pointer clients, eight
encoders, a data wheel, XY, MIDI, and remote clients without Lua.

The Slint Sound page provides a compact track-local assignment inspector. It
can adjust depth, enable state or delete an existing route through the same
semantic actions. It intentionally does not duplicate graph creation or
topology validation; detailed multi-owner editing belongs to FX/Modulation.

Projects write optional version-3 `modulation.ngm`. It preserves source settings, seeds,
route IDs, scopes, track IDs, depths, and destinations. Older projects load an
empty matrix. Forward versions and unsafe destinations are rejected.

Current limitations: no per-voice sample filter, audio-rate modulation,
recursive graphs, modulation curves, or per-track MIDI CC/XY lanes. Dynamic
voice-local sample ADSR/LFO/function/random/step/velocity/gate sources are
implemented; group follower taps observe post-fader group audio. Delay
destinations remain block-applied.

## Scope resolution

Destinations carry a stable numeric identity. Synth identities select an
instrument instance, sample identities select a timeline track (`0` means all
sample voices), and mixer/FX identities are one-based channel or bus slots.
Track routes always carry a track ID. Voice routes require a concrete synth
instance. Ambiguous routes and out-of-range identities fail validation.

For a voice destination, the instance-level effective parameter is the base.
The voice's normalized contributions are then summed once and clamped. This
avoids double-applying global or track modulation and never changes stored
instance parameters.
