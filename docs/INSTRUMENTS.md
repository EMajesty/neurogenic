# Native instruments and DSP

The shared matrix also targets sample pitch/gain/pan and mixer/external-FX
numeric controls as effective overlays. Synth voices remain the only fully
polyphonic modulation-source implementation in this revision.

Timeline, MIDI and controller clients emit semantic note and parameter events.
Timeline tracks route those events to a stable instrument ID; the first IDs are
`sample.main` and `synth.subtractive.v1`. Routing is compiled with the timeline,
so the callback performs integer dispatch and never resolves strings or owns an
editable track. Multiple tracks may share the synth instance. Invalid IDs are
rejected before publication.

`Instrument` defines preparation/control operations, note-on/off, bounded block
processing, parameters, reset and copied state inspection. The current audio
engine uses concrete dispatch for its built-in synth, avoiding virtual calls in
the per-sample loop. This is an extension boundary, not a plugin ABI.
`AudioProcessor` similarly defines channel layout, parameters, bypass, reset and
bounded block processing. The first processor is a preallocated stereo delay.

## Subtractive synthesizer

The built-in synth provides 32 voices by default, deterministic idle-then-oldest
allocation, exact note-owner release, sine/triangle/PolyBLEP saw and pulse
oscillators, pulse width, velocity response, ADSR amplitude, a resonant low-pass
filter and master gain. Saw and pulse discontinuities use PolyBLEP; triangle and
filter implementations are deliberately lightweight and are not oversampled.
Parameters are clamped, finite-checked and smoothed at control rate. Extreme
resonance is bounded for numerical stability.

The modulation matrix targets pitch, oscillator/filter/envelope controls, and
gain through numeric destinations. Per-track sources are independent; voice
ADSR, velocity, and LFO sources target per-voice pitch, cutoff, and gain.

```sh
./build/dev/neurogenic --synth
./build/dev/neurogenic --synth --headless
```

The demo timeline targets MIDI note 60. Incoming MIDI note-on/off messages play
the synth. In the GUI, click the track instrument field to switch the selected
track between the sample instrument and synth; the Parameters view exposes the
synth and delay controls. Generic application commands use
`track.instrument-set.v1` and the existing parameter actions. No GUI or
controller touches DSP objects directly.

The production Slint Sound page uses that same boundary. Its synth controls are
descriptor-driven and instance-aware; oscillator, filter, ADSR and output
groups contain only registered engine parameters. The sample instrument panel
uses selected-pad playback, tuning and envelope actions and leaves region and
slice editing to the separate Sample Editor.

Timeline automation uses the same stable application parameter IDs. Synth
destinations are valid only on tracks routed to the subtractive synth; the
master delay is global and may be automated from any track. Resolution and
range validation happen during control-side editing/compilation. The callback
receives only numeric target/parameter pairs and applies discrete changes at
their scheduled frame. Values persist until changed; smoothing remains the
responsibility of the destination DSP implementation.

## Real-time ownership

Synth voices and delay storage are allocated when the engine is constructed.
The audio callback consumes bounded note and DSP-command queues, updates atomic
targets, and processes preallocated state. It does not allocate, destroy, lock,
log, perform I/O or start threads. A full DSP queue rejects the newest update;
the application parameter value is rolled back so visible state cannot claim a
change the engine did not accept. Instrument routing changes use the existing
immutable timeline-plan publication and retirement mechanism.

Stop, restart, timeline replacement, and instrument reassignment reset affected
timeline playback through the immutable-plan activation path. The engine owns
16 preallocated subtractive-synth instances. Tracks default to legacy shared
instance 1 but may select independent instances; multiple tracks may still
share an instance deliberately. Each instance has its own parameters, smoothing
state, voice pool, and voice-stealing counter. There are no per-track effect
chains, presets, audio-rate modulation, plugin ABI, or oversampling.

Per-voice modulation state is embedded in every synth voice. Note-on initializes
its modulation envelope, velocity value, deterministic random seed, and LFO
phase; retriggered LFOs start at zero while free-running LFOs inherit the
instance phase. Exact note ownership drives release, and stealing replaces only
the selected oldest voice. Voice contributions currently target pitch, cutoff,
and gain after instance/global parameter composition.
