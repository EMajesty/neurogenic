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

Cancellation before publication restores the previous playing session for
overdub and replace. Preparation, recorder or publication failure likewise
retains the previous immutable audio version and reports the failed operation.
Initial-capture cancellation has no previous loop and therefore ends in the
explicit canceled state. Once the exact new sample revision is audio-active,
the pass is irreversible as a cancellation; a new replacement pass is the
supported recovery.

Clip changes participate in timeline history, but PCM version history is not
yet exposed as Undo/Redo. WAV saving remains explicit. Sessions and recording
buffers are transient; completed clips persist after their asset is saved.

The production recording surface lists the eight bounded stable-ID sessions,
locks an active pass to its selected session, and exposes pass type, progress,
iteration, errors, overdub, replacement and export. Clear is confirmed and
removes the session's arrangement clip; it intentionally retains the immutable
sample slot because instruments or export workflows may still own that PCM.

## Sources and safety

Existing external, physical-input, track, group, aux and master taps are
supported. Monitoring reuses off/on/automatic policy. Capture or replace from
the destination track's own tap is rejected; intentional layering requires
overdub mode. Overflow completes the bounded pass and reports diagnostics.

Discovery capability `live-looping` version 3 includes handoff diagnostics,
stable session management, export and confirmed-clear actions.

Resource limits are eight sessions, one simultaneous capture, mono or stereo,
the engine's bounded sample-version/publication queues, and the recorder's
preallocated capacity. Repeated passes retire old immutable versions through
the existing non-audio reclamation path.

Known limitations: explicit WAV save for durable
assets, no PCM-version Undo/Redo, fixed pass length after arming, and the
overdub/replace mix remains control-side. Session play/mute/retrigger controls
are currently provided by the arrangement/launcher owner rather than a second
looper transport.

The Slint recording surface can create, select, arm, start, stop and cancel the
initial session and start explicit overdub or replacement passes through this
service. It presents waiting/recording/finalizing/playing-derived states,
exports the audio-active result through the shared keyboard/location workflow,
and confirms clear. Gapless reservation/handoff behavior is unchanged.
