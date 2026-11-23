# Live sample management

Arrangement audio clips reuse immutable sample versions and the sample voice
pool. Active clips retain old versions across replacement; new activations use
the current audio-active slot. See [AUDIO_CLIPS.md](AUDIO_CLIPS.md).

Offline bounce is also control-side: an immutable source version is retained
while an ordered processing job creates a new finite mono/stereo sample. WAV
save and `SampleManager` publication are explicit; the original PCM and voices
already using it remain valid. See [SAMPLE_PROCESSING.md](SAMPLE_PROCESSING.md).

Transient, quiet-gap, equal and tempo-grid analysis is control-side and
nondestructive. See [SAMPLE_ANALYSIS.md](SAMPLE_ANALYSIS.md). Results retain the
source publication identity and are rejected after sample replacement.

Nondestructive playback regions, fades, looping, slice conversion and stable
pad assignments are documented in [SAMPLE_REGIONS.md](SAMPLE_REGIONS.md).
Regions use source-frame coordinates and never alter immutable PCM.

Sample-instrument presets reuse slice sidecars and immutable sample references.
They store paths and playback metadata, never PCM; missing assets stay
inspectable for relinking. See [PRESETS.md](PRESETS.md).

Preset samples are decoded and sample-rate prepared before publication. Their
immutable versions and pad mappings activate at one callback boundary; old
voices keep the prior version until retirement. Exact and prefix relinking
validate WAV data and never substitute by filename.

## Polyphonic modulation

Sample and slice voices instantiate track-owned voice definitions. Note-on
resets or deterministically initializes private state; note-off lowers its gate
even for one-shots. Gate release, choke, toggle and voice stealing affect only
the corresponding voice. Stretch retains independent time and pitch ratios;
reverse keeps its documented non-stretched fallback.

`SampleManager` is the hardware-independent command/state boundary for sample
slots. Its stable semantic actions are `sample.load.v1`, `sample.replace.v1`,
`sample.unload.v1`, and `sample.query.v1`. A desktop UI, hardware adapter, Lua
binding, MIDI mapping, or remote protocol can call the same interface without
exposing audio-engine objects.

The shared modulation matrix can apply control-rate pitch, gain and pan to
active sample voices without touching immutable PCM or persisted pad settings.
When realtime stretch is enabled, pitch modulation changes its pitch ratio but
not its independent time ratio. Per-voice sample sources and filtering remain
deferred.

Loading is asynchronous. A bounded control queue feeds one preparation worker;
that worker performs file I/O, WAV decoding, allocation, and resampling. Slot
state is queryable as `empty`, `queued`, `preparing`, `prepared`,
`pending-activation`, `loaded`, or `error`. `prepared` is normally brief:
publication immediately follows preparation when capacity is available.
`pending-activation` means the immutable update is in the bounded audio queue;
`loaded` or `empty` means the audio callback has acknowledged the install or
unload. Progress is currently zero or one.

The primary source path/rate, engine rate, channels, frame count, and duration
always describe the audio-active sample. `pending_source_path` separately
describes the newest requested load while it is queued, preparing, prepared, or
awaiting activation; an unload has no pending path. Every accepted command has
a monotonic revision, and every accepted publication has a monotonic audio
sequence. Older completions may update active metadata only after their own
audio acknowledgement; they cannot replace the state, error, or pending
metadata of a newer revision. This makes polling deterministic for controller
feedback and UI state replication.

A failed replacement reports an error for the newest revision but retains the
previous audio-active sample and all of its metadata. If an earlier successful
publication was already pending, its later callback acknowledgement is still
reflected as active without erasing the newer failure.

Prepared samples enter a bounded version pool (64 versions by default and
configurable at engine construction). Its publication queue is allocated to
the same configured capacity. At callback boundaries,
the audio thread atomically changes which immutable version a stable slot ID
resolves to. Existing voices retain a pointer and version index for the old
sample and finish normally. New triggers resolve the replacement. Unload
removes the slot from new-trigger lookup but also lets active voices finish.
The callback only marks unreferenced old versions retired; the control thread
alone destroys them. Consequently the callback performs no allocation,
deallocation, file I/O, decoding, resampling, locking, logging, or scripting.
If long-lived voices plus rapid replacements exhaust the pool, the command
fails observably and the current sample remains intact.

The publication queue has one serialized control-side producer path (normally
`SampleManager`) and the audio callback is its sole consumer. A control-side
mutex also serializes direct engine publications and version reclamation; the
audio callback never takes it. Status queries only read an atomic
applied-sequence acknowledgement; they never inspect sample objects. Shutdown
drains preparation commands, but a prepared update remains
`pending-activation` until a subsequent callback consumes it. Destroy the audio
device before the engine so no callback can outlive the immutable version pool.

The WAV decoder supports mono and stereo PCM16, PCM24, PCM32, and IEEE float32
RIFF/WAVE files. Other containers and compressed formats are rejected with a
path-specific error. Source rates must be positive. Files already at the engine
rate are not converted. Other rates, including 44.1, 48, 88.2, and 96 kHz, are
converted during preparation with a replaceable 32-tap Hann-windowed sinc
converter. Output length is rounded to preserve duration, and the low-pass
cutoff follows the conversion ratio to limit downsampling aliasing. This is a
general-purpose offline preparation path, not time stretching: extreme ratios,
mastering-grade conversion, streaming very large files, and metadata chunks are
not yet specialized.

For a live CLI demonstration, audio may start empty:

```sh
./build/dev/neurogenic --interactive
```

Commands on standard input are:

```text
load 0 samples/kick.wav
replace 0 samples/another-kick.wav
status
status 0
unload 0
quit
```

The existing positional WAV, repeatable `--slot ID=FILE`, and `--map NOTE=ID`
forms remain supported. Local files under `samples/` are deliberately ignored
except for `.gitkeep`; tests generate temporary deterministic WAV fixtures.

## Sample slicing

`SliceService` is the shared C++ command/query boundary for chopping. A slice
has a stable ID, parent slot plus callback-acknowledged sample revision,
start/end frame, name, optional root note and gain. Coordinates refer to the
immutable, resampled engine-rate PCM, so playback and waveform markers share
one canonical domain. Slices reference PCM; they never copy it. Replacement or
unload invalidates editable slices and pad mappings, while active voices retain
the old immutable version and finish safely.

Equal slicing supports 2, 4, 8, 16, 32 or 64 divisions. Transient slicing uses
a deterministic amplitude-flux detector with sensitivity and minimum frame
separation. Both analysis and the bounded 1024-bucket stereo peak cache run
outside the callback. Overlap and whole-sample slices are allowed; empty or
out-of-range regions are rejected.

Pads 0–15 map to notes 36–51 through a bounded callback-applied queue. A slice
mapping takes precedence; clearing it restores the note-to-slot fallback. STEP
events retain velocity, probability, conditions and ratchets. Preview uses the
existing voice pool and stops preview-owned voices without stopping transport.

The production Slint Sample Editor presents the same stable slice and region
objects. Its waveform is the bounded cached stereo peak overview, not PCM copied
from the audio engine. Manual marker edits, transient analysis, pad assignment
and preview all dispatch the actions documented above. Loading and offline
processing remain asynchronous and the UI changes its active metadata only
after the normal sample-publication acknowledgement.

The Slint Browser filters selected directories to WAV files and dispatches a
selection to the currently selected slot through `sample.load.v1`. Missing,
invalid or disappearing files are reported; failure cannot replace the prior
callback-acknowledged immutable sample version.

`NEUROGENIC_SLICES 2` is a backward-compatible sidecar format for metadata,
pad assignments, playback settings and external sample paths; PCM is never
embedded. Version-one files load with compatibility defaults. Missing
references fail clearly.

## Expressive pad playback

Each pad has immutable, callback-published playback settings independent of a
physical controller. One-shot ignores note-off and runs to its boundary. Gate
enters the configured release on note-off. Toggle starts on the first note-on
and releases on the next; a further note-on after release begins retriggers it.
Retriggering otherwise creates another bounded voice, so unrelated voices are
not cut off. Timeline note duration only releases Gate voices.

Choke group 0 disables choking; groups 1–16 stop older group members before the
new simultaneous trigger is allocated. Immediate choke is deterministic and a
short-release choke uses the victim voice's envelope release. Playback-rate
pitch combines ±48 semitones, ±100 cents and a transient trigger offset; it
therefore changes duration and is not time stretching. Linear interpolation is
used for fractional positions. Reverse starts at `end_frame - 1` and stops at
`start_frame`, including slice bounds.

The per-voice ADSR uses seconds for attack/decay/release and a normalized
sustain level. Gain is 0–4 before the existing output stage. Defaults preserve
one-shot drum behavior: zero attack/decay/release, sustain one, unity gain,
forward playback and no choke. Old voices retain their immutable sample and
their copied settings across replacement/unload; only new triggers see updates.

## Pitch-preserving time stretching

Stretch Off is the default and does not execute stretch code. Realtime mode is
a deterministic two-grain overlap/add processor: 50%-overlapped Hann grains
read immutable mono/stereo PCM with shared positions, linear interpolation and
no callback allocation. Economy, Balanced and Smooth use 256, 512 and 1024
frame windows and report algorithmic latencies of 128, 256 and 512 frames.
Economy uses a triangular crossfade, Balanced a smooth polynomial, and Smooth
a Hann crossfade. Larger windows reduce modulation but smear transients; Smooth
also spends more CPU on trigonometric window evaluation. Every preset remains
bounded to two reads per output channel.

The supported time and pitch ratios are 0.25–4×. Time ratio controls output
duration (`source frames × ratio`); semitone/cent controls set the independent
within-grain pitch ratio. A source beat length derives the ratio from slice
duration and current project BPM; otherwise source BPM derives
`source BPM / project BPM`; otherwise the explicit ratio is used. Values are
clamped to the supported 0.25–4× range. Tempo changes rebase active voice source
time and ramp the ratio over 20 ms. There is no four-bar assumption.

Full samples and slices support One-shot, Gate/Toggle release, choke, ADSR,
velocity, probability and ratchets through the existing voice/scheduler paths.
Reverse plus stretch uses explicit reverse overlap/add reads. Looped regions and
clips wrap analysis reads while retaining synthesis phase. Prepared/cache mode,
warp markers and transient alignment are not implemented. This lightweight
granular method can produce flamming,
phasiness and boundary coloration, especially on sustained tones or extreme
ratios; it is intended as an initial rhythmic-loop algorithm, not professional
offline stretching.

Sidecar version 3 persists stretch mode, quality, explicit ratio, source beats
and source BPM. Versions 1 and 2 remain readable.
# Project references

Unified projects store sample paths plus slice, pad, expressive playback, and
stretch metadata, never PCM. Relative paths resolve from the project root.
Missing references preserve their metadata and may be relinked through
`project.relink-sample.v1`; unaffected slots remain usable.

Recorded samples use the same immutable version pool. Existing voices retain an
old version when recording replaces a slot; the new capture stays silent until
callback acknowledgement. Recording pad assignment creates a full-sample slice,
so waveform caching and later slicing require no parallel playback path.
