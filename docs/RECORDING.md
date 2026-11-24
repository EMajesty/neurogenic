# Audio recording and internal resampling

The bounded recorder also backs live-loop sessions. A fixed first pass may write
directly into a preallocated sample and activate its reserved voice at the exact
stop frame; see [LIVE_LOOP_HANDOFF.md](LIVE_LOOP_HANDOFF.md). Prepared overdub/replace
results publish without changing callback capture behavior; see
[LIVE_LOOPING.md](LIVE_LOOPING.md).

Track and aux taps include their native chains. Master pre-effects is before
master gain/chain; master post-effects includes native and legacy processors.

The master pre-effects signal also feeds the initial modulation envelope
follower with one-block observation latency. Recording taps remain independent;
follower processing never reallocates or writes into recording buffers.

External processors add `external-fx-return` for an isolated return and
`external-fx-mix` for the compensated wet/dry result, with loop index as
`source_id`. Existing track, aux, physical-input and master sources are
unchanged. A loop need not reach the physical master to be recorded.

Group buses add `group-pre`, `group-post-effects`, and `group-post-fader`.
Their `source_id` is the stable group ID; the taps respectively capture the
summed group input, chain output, and audible post-gain/pan output.

NEUROGENIC's first recording path uses one reusable `RecordingService`. A
preallocated stereo-capable buffer is the bounded real-time producer target;
the audio callback only copies samples and updates lock-free counters. The
application/control thread creates the immutable `Sample`, publishes it through
`SampleManager`, writes WAV files, and updates projects. There is no callback
allocation, lock, logging, filesystem access, decoding, or destruction.

The default capacity is 60 seconds at 48 kHz stereo (about 23 MiB). Embedders
may choose another capacity. When full, accepted audio is retained, excess
frames are counted, recording stops, and existing data is never overwritten.
State distinguishes Idle, Armed, Recording, Completed, Publication Pending,
Audio Active, Canceled, and Error. Snapshots expose captured/dropped frames,
capacity, peaks, overflow count, finalization time, and reserved memory.

## Signal flow and sources

Stable source identifiers currently select:

- `external-mono` and `external-stereo`: miniaudio duplex capture input;
- `master-pre`: the mixed sample/synth signal before master gain/effects;
- `master-post`: output after master gain and stereo delay.
- `track` plus stable track ID: post-fader timeline channel;
- `aux` plus bus index: post-gain aux before return gain;
- `physical-input` plus channel index: one device input independently.
- `group-pre`, `group-post-effects`, and `group-post-fader` plus stable group ID.

Pre-effects excludes master gain; post-effects includes it and current master
processing. Both internal taps include samples, synths, and timeline playback.
Recording does not feed its destination slot back into the current callback;
the immutable sample becomes triggerable only after callback acknowledgement.

The adapter opens one miniaudio duplex device at 48 kHz with one or two capture
channels, giving input and output a shared device clock. Independent devices
with unrelated clocks are not supported; they need asynchronous drift
compensation. Output-only operation is unchanged. Use `--list-devices` to see
inputs and `--input NAME` to open duplex mode. Physical input operation has not
been verified in CI.

Track and aux sources do not need to be routed to the physical master. The
multichannel adapter accepts one to six inputs and outputs, but physical
multichannel behavior remains hardware-unverified.

Input monitoring is Off, On, or Auto. On mixes the current input block into the
output; Auto does so only while armed/recording. Latency is the backend duplex
buffer plus device latency. Internal-source monitoring is ignored to avoid a
feedback loop; the monitor mix occurs after the post-effects recording tap, so
it cannot be recursively resampled. There is no monitoring mixer yet.

## Transport and completion

Start can be immediate, next beat, or next bar. Quantized recording requires a
running authoritative transport. Fixed bars are converted once at arm time
using current tempo and time signature; later tempo changes do not resize that
recording. Manual stop completes at the next matching-source callback boundary.
Cancel discards captured frames. Project switching cancels active recording;
device stop reports an error. Overflow completes the retained prefix.

## Application, WAV, and projects

The action registry exposes source/destination/channel selection, arm,
start/stop/cancel, quantization, fixed bars, monitoring, WAV save, and pad
assignment. Pad assignment creates or reuses a full-sample slice after audio
activation, using ordinary slice persistence.

For a headless internal resample, run `neurogenic --interactive --headless` and
enter:

```text
record-start 20 master-post
record-status
record-stop
record-save recording.wav float32
```

External capture uses `--input NAME` with `external-mono` or
`external-stereo`. WAV export supports PCM16 and IEEE float32 with exact frame
counts; the in-memory capture stays float. Write failure leaves the audio-active
sample intact.

Saving a project with an unnamed recording writes float32
`recordings/slot-ID.wav`, then stores the usual relative reference. PCM is never
embedded. Missing recordings use normal inspectable/relinkable asset behavior.

## Current limitations

Only mono/stereo, one recorder, and one duplex device are implemented.
Separate-device clock
correction, streamed waveform display, input pair selection, overdub/looping,
and physical latency validation remain future work.

## Presentation-neutral workflow

`RecordingWorkflowCoordinator` captures an explicit mode, stable source
identity and destination, then invokes the existing semantic actions. Its
configuring, armed, boundary-wait, recording, finalizing, publication and
terminal phases derive from authoritative recorder/live-loop/arrangement
snapshots. Configuration is locked during active work so later UI selection
cannot redirect a result. Slint uses this contract and never guesses a Record
destination. Audio is reported complete only after `SampleManager`
acknowledges the immutable version.

For live loops the coordinator also owns only the presentation selection of an
authoritative stable session ID. It can reselect existing sessions, start
overdub or replacement, cancel without redirecting to a later UI selection,
export the audio-active sample, and request confirmed clear. The engine/service
remain the only recording-state authorities. Loop publication completion is
matched by sample revision, not merely by observing any loaded sample in the
destination slot.
