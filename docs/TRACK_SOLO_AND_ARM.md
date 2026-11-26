# Track solo and recording arm

Track solo and recording arm are bounded, transient performance state owned by
`ApplicationService`. They use stable `TrackId` values and are not serialized
or added to musical Undo/Redo.

## Solo

Solo is additive: any number of tracks may be soloed. When no track is soloed,
every explicitly unmuted track is audible. When one or more tracks are soloed,
only soloed and explicitly unmuted tracks are audible. Explicit mute therefore
wins over solo, and clearing solo never changes mute flags.

The existing mixer implements the audio isolation. Group paths and post-fader
auxiliary sends follow that effective audio path; pre-fader sends retain the
existing pre-fader semantics. Timeline MIDI note creation is filtered by the
same effective track audibility, and a solo transition sends MIDI panic so a
previously sounding external note cannot become stuck. External effect and
hardware tail behavior remains device-dependent.

## Recording arm

Multiple tracks may be marked armed for presentation and preparation, but the
current audio recorder supports one capture owner. `recording.arm.v1` accepts
exactly one armed track and captures its stable identity as a
`track_channel` source. More than one armed track is rejected explicitly.
With no armed tracks the pre-existing explicitly configured recording source
continues to work for compatibility.

Selection changes cannot redirect a captured owner. Disarming during the
armed/count-in phase cancels the pending request. Disarming during active
capture removes future arm state but does not discard the active capture;
ordinary stop/finalization retains recorded content. Deleting the owner before
capture cancels it; project replacement clears every arm and owner.

Sequencer-event recording, live-loop recording, and sample capture retain
their existing coordinators. This milestone does not claim simultaneous
multitrack audio capture.

## Semantic operations

- `track.solo-set.v1`, `track.solo-toggle.v1`, `track.solo-clear.v1`
- `track.record-arm-set.v1`, `track.record-arm-toggle.v1`,
  `track.record-arm-clear.v1`

Snapshots distinguish explicit `muted`, explicit `solo`, `record_armed`, and
derived `effectively_audible` state.
