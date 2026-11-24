# Offline sample processing and bounce

`SampleProcessingService` is a control-side job system for creating new audio
assets from immutable sample versions. It never changes source PCM. Each job
captures the exact source slot revision/publication and retains that shared
sample version until the job is discarded.

## Sources and rendering

A job may use a whole sample, slice, or nondestructive region. Slice bounds are
rendered directly. Region direction, gain and linear/equal-power fades are
rendered when requested. A region with an enabled loop can be bounced to an
explicit output duration; forward and reverse traversal wrap at its stored loop
boundaries. Region loop crossfade is not rendered in this first offline path.

Source positions use source frames. Existing voices retain their activated
sample and region copies while a result is prepared or published. A result may
target a new slot, or replace its source slot only through an explicit action.

## Ordered operations

Jobs contain up to 16 ordered operations:

- `trim`: retain `[start_frame, end_frame)`.
- `normalize-peak`: scale the absolute peak to a dBFS target. Silence remains
  silent.
- `normalize-rms`: scale full-buffer RMS to a dBFS target. Silence remains
  silent.
- `reverse`: reverse frames while preserving channel alignment.
- `gain`: apply -120 through +48 dB.
- `fade`: linear fade-in and fade-out lengths in processed frames. Overlap uses
  the lower coefficient.
- `insert-silence`: add zero frames at the beginning or end.
- `resample`: reuse the portable windowed-sinc converter at 8–384 kHz.
- `channel-convert`: stereo-to-mono uses an arithmetic mix; mono-to-stereo
  duplicates the channel.

Operation order is significant. Every intermediate sample is validated as
non-empty finite mono/stereo PCM. NaN, infinity, invalid ranges and address-size
overflow fail the job without publishing a result.

## Jobs, cancellation, and publication

There are eight job slots and 16 operations per job. Draft jobs can be edited,
reordered and validated. Starting a job moves processing to a worker and
publishes progress. Cancellation is cooperative between prepared operations;
long sinc resampling may acknowledge cancellation after its current conversion.

A completed result remains control-side until saved or published. Publication
uses `SampleManager::publish_prepared`; the job stays `publication-pending`
until the callback acknowledges the new immutable version, then becomes
`audio-active`. Queue rejection leaves the prior active sample untouched.

## WAV assets

PCM16, PCM24 and IEEE float32 WAV files are supported. Samples outside -1..1
are clipped during asset encoding; the prepared float result remains unchanged.
Saving writes a sibling temporary file, then renames it into place. Existing
assets are rejected unless overwrite is explicit. On the supported Linux
baseline, replacement rename is atomic.

Result files enter projects and presets through ordinary sample paths. Jobs,
temporary PCM and progress are not persisted. Slice/region remapping is not
automatic because mixed trim, resampling, reversal and silence transforms are
ambiguous; clients may create metadata after publication.

## Stretch and pitch status

Offline sample-rate conversion is supported. The real-time stretcher is a
stateful voice processor optimized for callback blocks, while playback pitch
changes playback rate. Reusing either as a high-quality offline transform would
give unclear boundary semantics, so tempo stretch and independent offline pitch
shift are deferred rather than adding a second algorithm in this task.

## Discovery

Capability `sample-processing` version 1 exposes operation-type and job objects,
typed semantic actions, status/progress state, and the job budget. It contains
no waveform buffers or physical-controller layout assumptions.

The production Sample page now completes the lifecycle: progress, cancel,
explicit atomic WAV save, explicit destination-slot publication,
acknowledgement and preview of the audio-active result. A job retains its
immutable source version and is not retargeted by later UI selection. Worker
completion never silently replaces an active sample.
