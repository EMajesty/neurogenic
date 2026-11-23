# Mixer and audio routing

Tracks, groups, both aux buses and master have bounded serial native chains. Track
chains are pre-fader, aux chains precede aux gain/return, and master follows
master gain. See [FX_CHAINS.md](FX_CHAINS.md) for complete signal order.

NEUROGENIC uses a bounded mixer between instruments and the existing master
effects. GUI, controller, MIDI and future remote clients submit semantic
actions through `ApplicationService`; none mutate callback state directly.

Track gain/pan/aux sends, aux return gain, master gain, and external-loop
wet/send/return gain accept fixed-size numeric modulation overlays. The mixer
consumes effective values without rebuilding or publishing routing snapshots;
physical assignments, loop latency and topology are never modulatable.

## Signal flow

```text
timeline track -> sample/synth -> track channel -> optional group(s) -> master
                                 |                  |
                                 +-> aux -> return--+
physical input -> input channel ------------------+

track/aux/master -> external send -> physical processor -> return -> master

master pre tap -> master gain -> master effects -> master post tap -> output
```

Direct track, group, or aux assignments bypass the master and target a physical
pair. Group-to-group routing is supported only as a validated acyclic graph.
See [GROUP_BUSES.md](GROUP_BUSES.md).
Legacy pad, preview and compatibility-pattern voices without a timeline track
continue to the master compatibility path.

## Ownership and publication

`MixerConfiguration` is prepared and validated on the control thread. It holds
up to 32 track channels, eight stereo groups, six input channels, two stereo aux buses and six
physical outputs. A complete copy crosses a bounded SPSC queue and becomes
active at a callback boundary. Queue exhaustion rejects the entire change and
increments `mixer_publication_failures`.

The callback owns fixed 1024-frame track, input and aux buffers. It performs no
allocation, locking, file access or graph construction. Control-visible
configuration is a separate copy; meters use atomics.

## External effects loops

Two fixed loop slots are part of the same compiled mixer snapshot. A loop has a
stable numeric ID and selects a track, group, aux bus, or the master as its source. It
maps a mono or stereo send and return, and provides linear send/return gains,
linear wet/dry (`dry = 1-wet`, `return = wet`), bypass, return mute, and manual
round-trip compensation up to 16,384 frames. New loops are disabled.

The ordinary source-to-master path is the dry path. When that source normally
feeds master, enabling a loop replaces its contribution with the compensated
dry/return blend; a directly routed source retains its direct output and adds
only wet return to master. One master-source loop may be active at a time.

Send channels may not overlap master, a direct track/aux output, or another
enabled send. Stereo endpoints must be distinct and source IDs must exist.
Return audio is never internally routed into its send source. Unavailable
channels remain silent and visible rather than being remapped. Physical cabling
can still create feedback, so connect and gain-stage external equipment
conservatively.

Compensation delays the loop source's dry contribution using fixed preallocated
ring buffers. Calibration is manual; NEUROGENIC never emits an automatic test
tone. Hardware latency can vary with backend and device buffering.

## Channel and bus semantics

- Gain is linear `0.0..2.0`; pan is `-1.0..1.0` stereo balance.
- Mute removes a channel from master and post-fader sends.
- If any track is soloed, only soloed non-muted tracks reach post-fader paths.
- Pre-fader sends are before gain/mute/solo. Post-fader sends follow all three.
- Two aux buses provide gain, return gain, mute, optional native stereo delay
  insertion and optional direct physical output pairs.
- Master pre-effects recording is before master gain. Master post-effects
  recording includes master gain and effects.
- Peak meters use fixed callback-rate decay and sticky clip indication.

## Physical I/O

The miniaudio adapter accepts one to six capture and playback channels through
`--input-channels` and `--output-channels`. It opens one duplex stream, so input
and output share the backend/device clock. Output-only stereo remains default.
Device open fails if the requested layout is unavailable. Stored assignments
are retained and are never silently redirected to another channel.

`AudioEngine::process_io` and automated tests simulate six inputs and outputs.
Separate devices with unrelated clocks are not combined because that requires
drift compensation. Actual six-channel hardware has not been verified.

## Recording taps

Recording sources include `track` plus stable track ID, `aux` plus bus index,
and `physical-input` plus channel index, in addition to external mono/stereo
and master pre/post. Track taps are post-fader. Aux taps are after bus gain and
before return gain. A source need not be audible at the physical master.
Groups add stable-ID pre-effects, post-effects, and post-fader taps.
`external-fx-return` captures an isolated return; `external-fx-mix` captures the
compensated wet/dry result. Both use the loop index as source ID.

## Persistence and controls

Projects store `mixer.ngm` in each committed generation. Older projects without
it receive unity channels generated from timeline track IDs. Settings follow
stable IDs when tracks reorder. Unavailable physical assignments remain visible
and silent.

Routing scenes contain a stable ID, name, and mixer snapshot only. Recall
validates and queues one complete snapshot, so the callback cannot see a
partial scene; failure leaves the current scene active. Scene morphing and full
click-free switching are not implemented.

The `mixer.*.v1` action family covers selection, gain, pan, mute/solo, sends,
direct outputs, inputs, aux buses and master routing. `mixer.master.gain`,
`mixer.selected.gain` and `mixer.selected.pan` also use the generic parameter
registry. The Mixer GUI supports pointer and non-touch selection, gain and
mute/solo editing.
The Routing GUI shows loops, return meters, channel availability, latency and
scenes. Shared `mixer.fx-loop-*.v1` and `mixer.scene-*.v1` actions serve GUI,
controllers and future clients.

The production Slint Mixer uses the same actions and copied `MixerState`. Its
channel strips cover tracks, groups, both aux returns and master, with stable
identity, eight-channel banking, gain/pan, mute/solo, aux sends and bounded
peak/clip telemetry. External-loop status and physical-channel availability are
presented without assuming a particular device channel count. Unsupported
routes remain unavailable or are rejected by the mixer validator.

The fixed capacities are real-time memory budgets rather than workflow
assumptions: 32 simultaneously mixed timeline tracks, eight groups, six physical inputs and
outputs, two aux buses and 1024 frames per backend chunk. There is no arbitrary
graph, surround panner, automation recording or feedback routing.
