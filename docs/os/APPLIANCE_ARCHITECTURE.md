# Minimal appliance architecture

## NixOS prototype

Use a normal kernel baseline first, systemd/udev and logind (or seatd), without
X11, a Wayland compositor or desktop environment. Start `neurogenic-kms` as a
dedicated user on a real VT after udev settles. Grant only the device and
resource privileges needed: realtime scheduling, bounded memory locking,
DRM/input/audio/MIDI/USB-CDC access and writable persistent paths. Restart only
on failure with a bounded delay and start limit; repeated renderer failure must
leave a maintenance VT and journal/SSH access available.

ALSA is direct through miniaudio's ALSA backend. The application requests
48 kHz, 1–6 input/output channels and an optional 1–8192 frame period with the
low-latency profile; it rejects a negotiated non-48-kHz stream. Device
selection accepts index or name substring. PipeWire, JACK and PulseAudio are
not required. Backend XRUN counters, realtime thread-policy verification and
round-trip latency still require hardware work.

## Real-time experiment

Keep a standard preemptible kernel as the recovery baseline. Evaluate pinned
Nixpkgs PREEMPT_RT availability on the actual G10 kernel branch before adding a
separate boot generation. Permit `SCHED_FIFO` and `memlock` narrowly through
systemd limits/capabilities. Record governor, thermals, IRQ topology and USB
layout before testing changes. CPU/IRQ affinity, isolation, performance
governor and USB autosuspend changes are experiments applied one at a time;
none is a default in OS-001.

## Storage and updates

Future images should use a read-only root with a separately mounted persistent
data filesystem. Keep projects, samples, recordings, user presets, controller
profiles, appliance preferences and bounded diagnostics there; keep binaries,
system configuration and shipped presets in the image. Task 097F profile files
and the manifest are individually atomic but not a multi-file transaction, so
power-loss recovery needs generation directories or an outer commit marker.

Use an established A/B updater such as RAUC rather than a custom updater:
signed inactive-slot install, boot-attempt counter, userspace success
acknowledgement, rollback and an offline USB/recovery path. Partition sizes and
bootloader integration wait for actual G10/RPi storage and firmware evidence.

## Distribution boundary

The application remains CMake/C++20 and does not require Nix at runtime. Nix
store RUNPATHs are packaging output, not application logic. Distributions must
provide assets/fonts, ALSA, device discovery and writable XDG configuration,
data, state and cache roots. Project/controller paths remain explicit or XDG
derived; temporary atomic files must reside on the same filesystem as their
destination. Service supervision belongs to the OS.
