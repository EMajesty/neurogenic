# G10 bring-up checklist

All results are **NOT RUN** until recorded from the arriving hardware.

## Inventory

1. Record firmware/UEFI screens and updates; save `uname -a`, `cat /proc/cmdline`,
   `lscpu -e`, `free -h`, `lsblk -o NAME,MODEL,SIZE,FSTYPE,MOUNTPOINTS`,
   `lspci -nnk`, `lsusb -tv` and `sudo dmidecode` output.
2. Record `ls -l /dev/dri /dev/input /dev/snd`, `drm_info`,
   `/sys/class/drm/*/status`, `/sys/class/drm/*/modes`, `libinput list-devices`,
   `udevadm info` for the touchscreen, `aplay -lL`, `arecord -lL`,
   `aconnect -i -o` and controller `/dev/serial/by-id` entries.
3. Record user/group/ACL ownership. Do not change permissions globally before
   identifying the seat and udev policy.

## Baseline and display

1. Install a minimal pinned NixOS generation with a normal kernel and retain a
   bootable maintenance generation.
2. Run `nix build .#production`; smoke-test the desktop Slint backend.
3. Run `nix build .#kms`. With no compositor, use
   `SLINT_DRM_OUTPUT=list ./result/bin/neurogenic-kms`, then
   `SLINT_DRM_MODE=list` on the chosen connector.
4. Test `SLINT_BACKEND=linuxkms-software` first at the native advertised mode,
   including 1920x1200 when present. Then test `linuxkms-femtovg` and record the
   GPU/DRM/EGL driver and failure diagnostics.
5. Validate 1920x1200 and 1024x600 layout, physical orientation, refresh,
   touchscreen association, edges/corners, multi-touch behavior and any
   required libinput calibration. Never guess connector or input names.

## Audio and controller

1. Enumerate the real audio interface and verify duplex channel capabilities.
2. Confirm an actual 48 kHz negotiated stream; test 256/128/64/32 frame
   requests in order, logging selected period, callback timing, XRUNs and
   physical round-trip latency.
3. Enumerate MIDI and USB CDC, negotiate Controller V2 identity, run standalone
   diagnostics, feedback and reconnect tests.
4. Measure scheduling latency, callback load, temperature and throttling under
   the common stress workload before changing kernel/governor/IRQ policy.
