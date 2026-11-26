# Metronome, count-in, and tap tempo

## Authority and timing

The metronome is part of `AudioEngine` transport processing. It does not own a
clock: beat positions are derived from the active timeline tempo, meter and
transport frame. Rational integer boundary calculation avoids accumulated
drift when samples per beat is not integral. A bounded click voice is started
at the exact sample offset within each callback. Restart returns the downbeat
to frame zero; stop silences the ordinary metronome.

The synthesized click is a 12.5 ms decaying sine burst (1320 Hz beat, 1760 Hz
downbeat). Level is bounded to 0..1 and the downbeat has a distinct amplitude
as well as pitch. There is no sample asset, allocation, lock, logging or
discovery in the callback.

## Routing

`master-monitor` is the currently implemented route. It is mixed after all
internal recorder taps and before physical master routing. Consequently it is
audible on the normal stereo pair and any existing master-output mapping, but
is absent from track, group, auxiliary, master pre/post, live-loop and internal
resampling captures. The mixer does not yet expose a dedicated cue bus, so a
separate headphone click and independent multi-output cue selection remain
unavailable. Export and capture isolation are intentional.

## Count-in

The authoritative setting is Off, 1, 2, or 4 bars. A recording arm computes a
future `RecordingService` start frame from the current tempo and meter; start
enables that already prepared boundary rather than replacing it with the
current callback frame. The recording therefore remains `armed` while count-in
is active and becomes `recording` only at the boundary. Count-in clicks are
audible even when the ordinary metronome is disabled.

- From stopped transport, count-in is measured from the request boundary and
  does not start playback or add a second quantization wait.
- From running transport, existing beat/bar quantization is preserved and the
  configured whole bars follow that boundary.
- STOP and recording cancellation cancel both the pending recorder start and
  click state. Project replacement uses the existing recording cancellation
  path.
- Arrangement recording already has a tick-domain count-in argument. General
  recording uses the frame-domain coordinator described above. Live-loop
  count-in is not advertised until its ownership contract is integrated.

An active count-in is transient and is never serialized as a pending recording
request.

## Tap tempo

`TapTempoEstimator` consumes injected monotonic nanoseconds. It retains at most
seven valid intervals, requires two intervals (three taps), rejects intervals
outside 20..300 BPM, and resets after 2.5 seconds. The estimate averages only
intervals within 20% of the median, preventing one anomalous tap from causing a
large jump. A sustained new tempo replaces the bounded history predictably.
The resulting value is committed through the existing authoritative timeline
tempo setter; no second BPM value exists.

Tap tempo and manual tempo authority are unavailable while external MIDI clock
is selected. Metronome playback follows the engine's authoritative external
phase where available. External-clock loss uses the existing transport stop
policy; externally synchronized count-in reliability beyond that policy is not
claimed.

## Public surface

Actions:

- `transport.metronome-toggle.v1`
- `transport.metronome-enabled-set.v1(enabled)`
- `transport.count-in-set.v1(bars)`
- `transport.count-in-configure.v1`
- `transport.tap-tempo.v1(timestamp_ns?)`

Parameter `transport.metronome-level` provides normalized click level. State
publishes enabled/beat/downbeat, route, count-in configuration/progress,
tap-observation count, and tempo authority. CLICK toggles; SHIFT+CLICK cycles
Off/1/2/4 bars and focuses the shared count-in interaction; TAP is deferred;
TAP+DATA consumes that tap
and retains direct tempo adjustment.

Tempo remains project-owned. Metronome enable, level, route and count-in are
currently transient performance/application configuration with deterministic
defaults (off, 0.35, master-monitor, off). They do not enter musical Undo/Redo.
Project persistence of these preferences is intentionally deferred until the
project-versus-appliance preference policy is expanded.

Clearing a captured track arm during count-in cancels the pending recording.
Unified panic cancels count-in and stops active capture through the normal
recorder so already captured content is not silently discarded.
