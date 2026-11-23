# Native FX chains

The production Slint FX page presents every track, group, aux and master serial
chain using stable owner and effect-instance identities. Add, remove, reorder,
bypass and parameter edits are semantic application commands. Parameter rows
are descriptor-driven rather than processor-name-specific UI code. Dynamic FX
parameters are validated from the authoritative rack when used as modulation
destinations; no raw processor pointer is exposed to the frontend.

FX preset IDs are template identities. Import allocates fresh runtime IDs and
remaps chain/modulation references, so repeated instances have independent DSP
state. See [PRESETS.md](PRESETS.md).

Insertion remaps processor identities before appending to the selected bounded
track chain. FX and referenced modulation activate in the same acknowledged
record, so the callback cannot observe half-remapped destinations.

NEUROGENIC has bounded serial native effect chains on mixer tracks, group
buses, both auxiliary buses, and the stereo master. Processor types and processor instances
are separate: a type describes code and parameters, while each instance has a
stable numeric ID and independent DSP state.

## Signal order

```text
instrument/sample -> track chain -> pan -> track gain/mute/solo
                  -> optional group chain -> group gain/pan -> aux sends
                  -> aux chain -> aux gain/return
                  -> external FX routing -> master gain -> master chain
                  -> legacy master gain-stage and delay -> output
```

Track recording taps observe post-track-chain/post-fader audio. Aux taps observe
post-chain/post-aux-gain audio. `master_pre_effects` remains before master gain
and the master chain; `master_post_effects` includes the chain and legacy master
processors. Empty chains preserve the pre-chain project signal path.

## Model and limits

- A rack contains at most 32 native instances.
- Every track, group, aux, or master chain contains at most four instances.
- Chains are serial and may be bypassed as a unit.
- An instance may be enabled or bypassed and has a linear 0..1 wet value.
- Initial types are `native.gain.v1`, `native.stereo-delay.v1`,
  `native.low-pass.v1`, and `native.saturation.v1`.
- Mono/stereo declarations are validated; arbitrary layout conversion is not
  implemented yet.

`EffectInstanceId` is the external identity. Names, GUI order, and controller
positions are not identities. Removing an instance still referenced by a chain
is rejected.

## Parameters, automation, and modulation

Every type publishes stable numeric and string parameter IDs, units, ranges,
defaults, and automation/modulation eligibility. Clients use semantic
`effect.*.v1` actions. Timeline and modulation destinations use
`fx.INSTANCE.PARAMETER`, for example `fx.17.1`.

Stored base values remain in rack configuration. Automation and modulation set
callback-safe effective values and do not overwrite base values. Values are
clamped to descriptor ranges. Topology, types, layouts, and resource ownership
are not modulatable.

## Publication and real-time ownership

Rack structure is validated off the audio thread and travels with bounded mixer
publication. The callback copies only fixed-capacity configuration and switches
at a buffer boundary. Processor slots, delay memory, and scratch buffers are
allocated with the mixer. Delay reset uses generation tags, so publication never
clears a large buffer or destroys a processor in the callback. Processing has no
allocation, locks, file I/O, logging, or dynamic graph construction.

Publication does not crossfade topology changes, which can click. Bypass retains
processor state; re-enabling resumes it.

## Persistence and extensibility

Mixer metadata version 3 stores instance types, IDs, order, base parameters,
wet values, layouts, and bypass flags. Versions 1 and 2 load empty chains and
retain their prior sound. Unknown processors and invalid references fail rather
than being substituted.

The descriptor/configuration boundary can support a future plugin adapter.
Plugin discovery, loading, opaque state, presets, latency compensation, and
plugin lifecycle are deferred.
