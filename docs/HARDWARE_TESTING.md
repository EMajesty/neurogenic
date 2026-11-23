# Hardware bring-up and diagnostic reports

For the dedicated controller, first run
`neurogenic-diagnostics --controller-protocol-test`, then launch with
`neurogenic --gui --controller /dev/serial/by-id/...`. Verify every transition,
encoder direction, fader range, RGB ordering/brightness, heartbeat, unplug and
replug, and full-state restoration. The host never substitutes another TTY.
See [CONTROLLER_PROTOCOL.md](CONTROLLER_PROTOCOL.md).
Firmware bring-up must negotiate minor 1 before using IDs 24–35 or extended
context feedback. Confirm value/range/unit formatting, Shift fine movement,
encoder direction, pickup arrows, scene-recall pickup invalidation, incremental
LED traffic and complete refresh after reset/reconnect.

NEUROGENIC does not infer hardware compatibility from compilation. These tools
collect repeatable evidence on the N150, Raspberry Pi 5, and development hosts
without changing system configuration.
The focused N150 runbook is [N150_BRINGUP.md](N150_BRINGUP.md).

## Initial N150 procedure

```sh
nix develop
cmake --preset dev && cmake --build --preset dev
./build/dev/neurogenic-diagnostics --json > n150-inventory.json
./build/dev/neurogenic --headless --list-devices
```

The inventory reports architecture, CPU, kernel/distribution, RAM, build
commit, audio backend, discovered native audio rate/channel combinations, MIDI
ports, and SDL display resolution/content scale. Omit `--json` for a concise
summary. `nix build` installs `result/bin/neurogenic-diagnostics`. A pure
`path:.` flake input cannot observe `.git` and labels itself
`local-path-unversioned`; normal Git-flake builds and local CMake builds record
their source revision.

Confirm that the chosen interface advertises 48 kHz and the required channels.
NEUROGENIC requests 48 kHz and rejects a different negotiated rate rather than
silently resampling the device stream. Six-channel support remains a simulated
capability until tested on a physical six-input/six-output interface.

## Audio verification and stress

Run the bounded integrated session before physical stress and retain its JSONL:

```sh
./build/dev/neurogenic --headless --list-devices
./build/dev/neurogenic-diagnostics --headless --json
./build/dev/neurogenic_integration_validation | tee integrated.jsonl
./build/dev/neurogenic_integration_validation --soak-seconds 3600 \
  | tee soak.jsonl
```

For N150 and Raspberry Pi 5 comparisons, record the commit, CPU governor,
kernel, interface, 48 kHz buffer size, deadline misses and backend XRUN count.
Use explicit loopback tests for audio latency and MIDI clock jitter; never infer
those values from offline callback timing.

Offline engine stress is deterministic but is not hardware real-time evidence:

```sh
./build/dev/neurogenic-diagnostics --headless --stress 10 \
  --buffer 256 --sample-voices 16 --synth-voices 16 > offline-report.json
```

Explicit device-backed output and duplex runs are:

```sh
./build/dev/neurogenic-diagnostics --stress 60 --realtime \
  --audio "DEVICE NAME" --sample-voices 16 --synth-voices 16

./build/dev/neurogenic-diagnostics --stress 60 --realtime \
  --audio "DEVICE NAME" --input "DEVICE NAME" \
  --input-channels 2 --output-channels 2
```

Use mono or six channels only after capability inspection. Failed opens return
nonzero and successful runs close the device cleanly. Callback deadline misses
are processing overruns, not confirmed ALSA/PipeWire XRUNs; the current backend
adapter has no portable XRUN counter. Offline `--buffer` controls callback size.
With `--realtime`, `--buffer` requests the miniaudio device period; reports
retain both requested and negotiated frames. The adapter emits
`backend_xruns_available=false` rather than fabricating a zero XRUN count.

The combined mixer/aux/recording/MIDI/GUI workloads remain in
`neurogenic_benchmark` and `neurogenic_workflows`; run them in addition to the
physical device test.

## Latency and loopback

Audio round-trip, MIDI loopback/clock jitter, and audio/MIDI alignment require
physical cables and explicit initiation. Start with conservative monitor/output
gain, disable speakers when patching output to input, and record interface,
channels, buffer and rate. This release exposes manual external-FX latency
offsets and deterministic MIDI timestamps, but does not yet emit an automated
calibration pulse. Callback duration is not round-trip latency.

## Display, touch, and GUI

```sh
./build/dev/neurogenic-diagnostics --display-test
./build/dev/neurogenic --gui --ui-scale 1.5
./build/dev/neurogenic --fullscreen --ui-scale 1.5
# Slint-enabled build:
./build/slint/neurogenic --frontend slint --fullscreen
```

The display command emits `neurogenic.display-test.v1` with native SDL metrics
and 1920x1200, 1024x800, and 1024x600 QA profiles. Verify touch/pointer alignment
at all corners, step-grid targets, live scale changes, fullscreen, Performance
view timing, and the on-screen keyboard. HDMI and USB HID touch remain host
services; touch is optional and keyboard/data-wheel navigation must work.
For Slint, validate the 1920x1200 reference mode and 1024x600 minimum: all
eight page labels, upper context fields, lower function labels and both rows of
eight performance slots must remain visible and touchable. Headless model tests
do not establish physical touch alignment, latency, readability or visual
quality.

## MIDI and controller inspection

Use `neurogenic --list-devices` for ALSA sequencer ports. Simulated tests cover
clock, SPP, dropout/recovery, and congestion; physical jitter needs loopback.

The event inspector accepts `CONTROL TYPE RAW [ACTION]` per line:

```sh
./build/dev/neurogenic-diagnostics --controller-inspect
pad.1 button 1 pad.trigger.v1
encoder.1 encoder -1 parameter.adjust.v1
fader.1 fader 0.62 parameter.set.v1
```

It emits `neurogenic.controller-event.v1` records with identity, raw and
normalized values, button transitions, and semantic action. This supports
bring-up of the planned encoders, contextual/performance buttons, faders,
crossfader, and data wheel without encoding that layout in the core.

The appliance accepts repeated exact CDC endpoints. Repeated `--controller`
and corresponding `--controller-id` options create independent sessions; no
unknown serial device is auto-selected. Third-party MIDI surfaces use
`--midi-controller-profile FILE`, whose `midi-device` selector is exact. Verify
reconnect, independent page/Shift state, full feedback refresh, and that
unmapped musical MIDI still reaches the instrument. CI coverage is simulated;
physical multi-device and feedback interoperability remain required.

## Startup, recovery, and shutdown checklist

1. Start with `--headless`; project inspection must work without audio hardware.
2. Start with `--project PATH`; playback remains stopped until commanded.
3. Disconnect/reconnect MIDI; verify logical-name matching and Panic cleanup.
4. Try invalid audio/MIDI selectors; no unrelated device may be substituted.
5. Exercise recording overflow and save/load failures using disposable data.
6. Stop and quit; MIDI notes, recording, and sample-loader workers must exit.

## Versioned report format

Stress output uses `neurogenic.hardware-test-report.v1`: application commit,
hardware/OS identity, audio configuration, scenario, status, limitations, and
an embedded `neurogenic.performance.v1` snapshot. Inventory uses
`neurogenic.hardware-diagnostics.v2` with negotiated device and Linux
scheduling data; controller and display records have their
own schemas. Save reports outside the source tree and compare like-for-like
rate, buffer, duration, and workload. `pass` means no callback deadline was
missed during that run; it does not assert absence of backend XRUNs, acceptable
audible quality, or acceptable round-trip latency.

## NixOS and appliance notes

The package retains ALSA in ELF RUNPATH and does not need global
`LD_LIBRARY_PATH`. Nix provides libraries; the host provides PipeWire/ALSA,
`/dev/snd` access, display/DRM ownership, and USB HID. Investigate a systemd
**user** service, fullscreen launch, stable udev names, realtime limits, CPU
governor, and restart policy separately. The project enables none of these and
does not edit NixOS configuration or services.

### Standalone launch and recovery

Preferences default to `$XDG_CONFIG_HOME/neurogenic/appliance.conf` (or
`~/.config/neurogenic/appliance.conf`) and are separate from projects. Launch
with `--reopen-last`, or use `--project PATH` to override it. Preferred named
devices are never replaced with another named port. GUI and interactive
sessions may continue in control-only degraded mode when audio cannot open.
Transport never starts automatically.

Shutdown defaults to `--shutdown-dirty cancel`. Alternatives are
`--shutdown-dirty save` (requires a project path and successful Save) and the
explicit `discard`. Shutdown cancels recording, stops transport, sends MIDI
panic, stops devices/dispatchers, joins workers, then persists preferences.
`--run-for SECONDS` exists only for bounded diagnostic lifecycle runs.

Optional examples are installed under `share/neurogenic/appliance/`:

- `neurogenic-appliance` is configured with `NEUROGENIC_BIN`,
  `NEUROGENIC_PREFERENCES`, `NEUROGENIC_PROJECT`, and optional
  `NEUROGENIC_EXTRA_ARGS`. It requests Save on shutdown; an untitled project
  without a Save-As path still refuses shutdown rather than being discarded.
- `neurogenic-appliance.service` is a systemd **user** unit with bounded
  restart, journal logging, SIGTERM, and a 20-second stop timeout.

Copy and edit these manually. The unit expects the launcher at
`~/.local/bin/neurogenic-appliance`; nothing installs or enables it. The
launcher requires Bash and assumes `neurogenic` is on `PATH` (or
`NEUROGENIC_BIN` is absolute) plus a working graphical session. It does not
force Wayland, X11, or DRM/KMS. Verify display/audio permissions and project
paths first.
If Save cannot complete, NEUROGENIC refuses graceful exit; systemd may still
terminate it after the example's 20-second timeout. Save-As untitled work before
service maintenance and do not treat this as power-loss recovery.

Troubleshooting:

- Unexpected Null audio: check runtime audio services, permissions, and ALSA.
- No MIDI: check `/dev/snd/seq`, permissions, and ALSA sequencer availability.
- Touch offset: compare window/drawable size and content scale, then compositor
  calibration.
- Non-48-kHz negotiation: configure/select a 48-kHz device profile.
- Deadline misses without audible failure: retain them separately from actual
  backend XRUN measurements.
