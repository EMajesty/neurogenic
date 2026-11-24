# Live loop recording

Completed live loops may be captured after launcher activation. Their gapless
first-pass handoff remains independent and is never delayed for capture.

Completed loops may be assigned after their gapless first-pass handoff. Slot
assignment never delays first playback; subsequent launches use the ordinary
audio-clip scheduler.

Live looping is a control-side session layer over the existing bounded
`RecordingService`, immutable `SampleManager` publication, and arrangement
audio clips. It does not add a callback recorder or playback engine.

## Sessions and lifecycle

Up to eight stable-ID sessions may exist; one may capture at a time. States are
idle, armed, waiting, recording, finalizing, playing, overdubbing, replacing,
canceled, and error. Status includes source, destination, requested musical
boundary, actual callback frames, length, iteration, clip ID, overflow and
diagnostics.

Immediate recording starts at the callback position. Subdivision, beat and bar
quantization choose the next strict boundary in authoritative transport time.
This remains authoritative under external MIDI clock. Quantized capture
requires running transport. A fixed musical length is converted when armed;
tempo changes do not resize an in-progress pass.

Signed input-latency compensation adjusts the requested capture frame. It is
user supplied; no hardware latency is invented. Start/stop offsets inside a
callback use the existing bounded recorder.

## Publication and first playback

First-pass PCM, a sample-version slot and one voice token are reserved before
arming. The callback seals and starts the buffer at the exact stop frame;
control-side publication and ordinary arrangement-clip creation no longer delay
the first repetition. The clip replaces the handoff voice by stable clip ID and
current phase. See `LIVE_LOOP_HANDOFF.md`.

## Overdub and replace

Overdub creates a new version using `old * feedback + captured`; format and
frame count must match. Multiple passes stay phase aligned and never mutate
active PCM. Replace publishes the new pass without mixing. Both preserve clip
identity and musical position. Existing voices retain old immutable versions.

Clip changes participate in timeline history, but PCM version history is not
yet exposed as Undo/Redo. WAV saving remains explicit. Sessions and recording
buffers are transient; completed clips persist after their asset is saved.

## Sources and safety

Existing external, physical-input, track, group, aux and master taps are
supported. Monitoring reuses off/on/automatic policy. Capture or replace from
the destination track's own tap is rejected; intentional layering requires
overdub mode. Overflow completes the bounded pass and reports diagnostics.

Discovery capability `live-looping` version 2 includes handoff diagnostics.

Known limitations: one simultaneous capture, explicit WAV save for durable
assets, no PCM-version Undo/Redo, fixed pass length after arming, and the
overdub/replace path remains control-side.

The Slint recording surface can create, arm, start, stop and cancel the initial
session and start explicit overdub or replacement passes through this service.
It presents waiting/finalizing/playing-derived states. Gapless
reservation/handoff behavior is unchanged; destructive clear remains deferred.
