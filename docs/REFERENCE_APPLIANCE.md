# Reference appliance deployment

This guide prepares software for Intel N150-class x86_64 and Raspberry Pi
5-class AArch64 Linux systems. It is not a hardware qualification report. All
physical audio, MIDI, controller and touchscreen checks remain **NOT RUN** until
recorded on the target machine.

## Build and install

Dependencies are pinned by `flake.lock`, `cmake/Dependencies.cmake` and Slint
`v1.19.0`. The default flake package is the production Slint appliance:

```sh
nix build
./result/bin/neurogenic-preflight --prepare
nix run
```

`packages.<system>.headless` is the lightweight engine/test package and
`packages.<system>.sdl` retains the SDL-compatible package. A production source
build remains available:

```sh
nix develop --command cmake --preset production
nix develop --command cmake --build --preset production
nix develop --command ctest --preset production
cmake --install build/production --prefix "$HOME/.local"
install -m644 build/appliance-stage/share/neurogenic/appliance/neurogenic-appliance.service "$HOME/.config/systemd/user/"
```

`production` is Release, C++20, Slint-enabled, SDL-compatible and includes the
diagnostic/discovery programs, launcher, preflight, user-service template and
runtime resources. Runtime operation needs neither a compiler nor a source
checkout. Slint and Corrosion sources plus the complete Cargo vendor closure
are fixed by `flake.lock` and the Cargo hash in `flake.nix`; derivation build
phases run offline. To update Slint, change the input tag, refresh `flake.lock`,
replace the Cargo vendor hash from Nix's mismatch report, review the narrowly
scoped jemalloc patch and run every production check.

Native AArch64 exposes the same production package output. Hosted native ARM CI
is the intended validation path; the x86_64 AArch64 check remains
compilation-only and does not establish Raspberry Pi 5 runtime behavior.

## Storage and configuration

Run `neurogenic-preflight --prepare` once as the appliance user. It creates:

| XDG root | Content |
|---|---|
| config | `neurogenic/appliance.conf`, `appliance.env` |
| data | projects, samples, presets, controllers, recordings, exports |
| state | logs and diagnostic reports |
| cache | disposable temporary files only |

Content roots remain configurable in Setup and removable storage is selected
explicitly. Preflight checks the nearest existing parent for read/write access;
it never deletes content or changes a device. Back up configuration and data
roots together. Reports omit project content and PCM by default.

Example `~/.config/neurogenic/appliance.env`:

```sh
NEUROGENIC_BIN=/home/instrument/.local/bin/neurogenic
# Optional overrides; installed launchers discover sibling binaries by default.
NEUROGENIC_FRONTEND=slint
```

## First startup and supervision

Run `neurogenic-preflight`, inspect its JSON device enumeration, then run
`neurogenic-appliance` interactively. Device names are exact: a missing named
device is never replaced. After interactive Save/Shutdown succeeds:

```sh
systemctl --user daemon-reload
systemctl --user enable --now neurogenic-appliance.service
journalctl --user -u neurogenic-appliance.service
```

The user service belongs to the graphical session, reads an optional environment
file, runs preflight, limits crash restarts to three per minute, and shuts down
with SIGTERM. It does not require root. The host controls PipeWire/ALSA, seat,
serial and MIDI permissions. Grant only needed device access. Real-time priority
and memory-lock limits are reported, never changed automatically.

## Device preflight

`neurogenic-diagnostics --json` reports build/commit, architecture, kernel,
audio formats and channels, MIDI endpoints, display, scheduling and memory-lock
limits. Select an exact device-advertised 48 kHz format. Enumeration does not
prove duplex operation, latency or XRUN-free performance. A physical run is:

```sh
neurogenic-diagnostics --realtime --stress 60 --buffer 128 \
  --audio 'EXACT OUTPUT' --input 'EXACT INPUT' \
  --input-channels 2 --output-channels 2
```

Configured MIDI/controller identities remain unavailable until the same endpoint
returns. Inspect handshake/profile status in Setup. Recover a bad profile by
stopping the service and running the launcher with
`NEUROGENIC_MAINTENANCE=1`. Maintenance uses Null/headless operation, no last
project, and a separate temporary preference file; normal configuration remains
untouched.

Libremidi 5.4.2's optional udev metadata helper is disabled deliberately. In a
restricted service it asserts when a udev netlink monitor cannot be created.
ALSA sequencer enumeration, exact endpoint matching and sequencer announce-port
hotplug remain enabled. Missing `/dev/snd/seq`, permissions or endpoints are
reported as unavailable; no configured endpoint is redirected. This is a
software hardening measure, not physical MIDI validation.

## Failure and recovery

| Condition | Runtime behavior | Operator action |
|---|---|---|
| No audio hardware | Explicit headless/maintenance uses Null; normal exact selection degrades with an issue | Connect/configure an exact device |
| No ALSA sequencer or permission | MIDI backend reports compiled-but-unavailable; audio/UI may continue | Restore `/dev/snd/seq` access, then reconnect |
| No MIDI endpoints | Empty available list; configured names remain unavailable | Connect the same endpoint; no substitution |
| No udev monitor | No dependency on the optional monitor | None; udev metadata is intentionally absent |
| Missing dedicated controller | Session remains reconnecting for the exact path | Reconnect or enter maintenance |
| Invalid controller profile | Profile validation fails without replacing the active mapping | Select a valid profile in maintenance/Setup |
| Device disconnect | Active connection becomes unavailable; bounded reconnect retains identity | Restore the same device or explicitly reconfigure |

- Missing devices: use degraded Setup, correct the exact binding, reconnect.
- Invalid configuration/profile: retain the bad file for diagnosis and recover
  in maintenance mode.
- Failed project load/missing assets: use transactional Browser relink and Save
  As; failed relink does not partially replace active samples.
- Unavailable storage: restore writable storage and retry explicitly. There is
  no destructive cleanup.
- Crash/power loss: atomic project/preset writes protect the prior completed
  file, but in-flight recording and unsaved edits may be lost.
- Repeated startup failure: the service stops after its bounded burst; use the
  interactive maintenance path rather than increasing the loop.

## Validation

Copy [HARDWARE_VALIDATION_REPORT.md](HARDWARE_VALIDATION_REPORT.md) per machine.
Attach diagnostic JSON, journal excerpts and the commit. Leave every unperformed
row `NOT RUN`. Software tests and offline stress are not physical appliance
performance measurements.
