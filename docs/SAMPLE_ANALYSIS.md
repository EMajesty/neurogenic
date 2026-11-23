# Sample analysis

Accepted candidates can be passed to the offline processing service as slice or
region sources. Analysis results remain proposals; bounce creates a separate
immutable sample and WAV asset.

NEUROGENIC performs sample analysis on immutable, audio-active `Sample`
versions. `SampleAnalysisService` retains a shared reference to the exact source
version while a bounded control-side worker analyzes it. Analysis never runs in
the audio callback and never changes PCM.

## Session and result model

At most eight analysis sessions may exist. A session has a stable numeric ID,
progress, status, structured error, source slot/revision/publication, settings,
candidate boundaries, strengths and suggested regions. Results are transient:
only slices or regions accepted through `sample-analysis.apply.v1` become
project/preset data. Replacing or unloading the sample marks old results stale;
they remain inspectable but cannot be applied.

Candidates use source-sample frame coordinates. Endpoints are implicit and
candidate IDs are stable for the life of a session. Clients may add, move,
remove or disable candidates before applying them. Apply validates the complete
boundary set, source publication and optional pad capacity before changing the
slice/region model. Existing voices retain their copied playback configuration;
new triggers use the new metadata.

## Algorithms

- `transients` computes channel-averaged RMS in fixed 128-frame hops, then uses
  positive energy flux, an adaptive mean threshold and a minimum onset
  separation. Sensitivity raises the threshold. Strength is normalized against
  the strongest flux peak. Timing resolution is 128 source frames.
- `quiet-regions` uses block RMS with separate enter/leave thresholds. Minimum
  quiet duration and hysteresis reject brief noise-floor crossings; optional
  padding moves the suggested boundary.
- `equal-divisions` uses integer multiplication/division, so remainders are
  distributed deterministically and the final interval ends exactly at the
  selected range. Zero-length intervals are rejected.
- `tempo-grid` converts user-supplied source BPM and beats-per-slice to source
  frames, with an optional frame offset. Project tempo does not rewrite source
  coordinates. Changing tempo after apply does not move slices.

Automatic BPM estimation and FFT spectral-flux analysis are intentionally not
implemented. The lightweight energy detector favors drum attacks; soft pads,
legato sources and heavily limited material may require candidate editing.

## Application and discovery

Semantic actions start, cancel, discard and apply analyses and edit candidates.
The discovery capability `sample-analysis` exposes four algorithm objects,
live session objects, state/progress bindings, the session budget and action
argument schemas. This supports a future waveform editor without embedding
screen layout or physical-pad assumptions in the engine.

Analysis of recordings is allowed only after `SampleManager` acknowledges the
recording's immutable publication. Multiple requests retain independent source
versions. Cancellation is cooperative at an analysis-block boundary; a partial
result is never published as complete.

The Slint Sample page exposes this transient workflow without adding another
analysis implementation. It reports session progress and errors and offers
explicit Accept and Reject operations. Detailed candidate editing remains
available through the same semantic API for future inspectors.

## Resource bounds

Temporary memory is linear in analysis length at one or two floats per
128-frame block. Candidate and resulting slice counts are bounded by sample
length, the eight-session budget and the existing 16-pad assignment capacity.
Sequential assignment may start at any logical pad; insufficient capacity
rejects the complete model transaction.
