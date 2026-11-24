# Reference appliance deployment

This guide prepares software for Intel N150-class x86_64 and Raspberry Pi
5-class AArch64 Linux systems. It is not a hardware qualification report. All
physical audio, MIDI, controller and touchscreen checks remain **NOT RUN** until
recorded on the target machine.

## Build and install

Dependencies are pinned by `flake.lock`, `cmake/Dependencies.cmake` and Slint
`v1.19.0`. A production source build is:

```sh
nix develop --command cmake --preset production
nix develop --command cmake --build --preset production
nix develop --command ctest --preset production
cmake --install build/production --prefix "$HOME/.local"
install -m755 build/appliance-stage/share/neurogenic/appliance/neurogenic-appliance "$HOME/.local/bin/"
install -m755 build/appliance-stage/share/neurogenic/appliance/neurogenic-preflight "$HOME/.local/bin/"
install -m644 build/appliance-stage/share/neurogenic/appliance/neurogenic-appliance.service "$HOME/.config/systemd/user/"
```

`production` is Release, C++20, Slint-enabled, SDL-compatible and includes the
diagnostic programs and runtime resources. Runtime operation needs neither a
compiler nor a source checkout. The default Nix package remains the bounded
headless/SDL test package; Slint's Rust/Cargo source build is verified through
the pinned production preset and CI. A fully sandboxed Slint flake package is a
remaining packaging limitation. Native AArch64 uses the same commands. The
x86_64 cross check proves portable compilation, not Pi 5 runtime behavior.

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
NEUROGENIC_DIAGNOSTICS_BIN=/home/instrument/.local/bin/neurogenic-diagnostics
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

## Failure and recovery

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
