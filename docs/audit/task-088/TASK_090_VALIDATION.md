# Task 090 runtime and package validation

## Root cause

The pre-fix command
`ctest --test-dir build/slint -R 'neurogenic_(diagnostics_schema|slint_rendered_primary)' --output-on-failure`
passed the rendered test but aborted diagnostics at libremidi 5.4.2
`backends/linux/udev.hpp:101`. The Nix development environment exposed
`libudev.h`, which enabled `LIBREMIDI_HAS_UDEV`; restricted netlink monitor
creation returned null and upstream asserted during `MidiInput` construction.
The ordinary build lacked that header and did not enter the path.

NEUROGENIC now selects libremidi's supported `LIBREMIDI_NO_UDEV` option. ALSA
sequencer enumeration, connections and announce-port monitoring remain; only
udev-derived metadata is omitted. No upstream source assertion was suppressed.

## Software evidence

Validation environment: x86_64 NixOS development host, Null audio, ALSA raw
MIDI backend with no discovered endpoints. Physical hardware tests were not
performed.

- Dependency-enabled development CTest: 32/32 passed.
- Slint-enabled CTest: 35/35 passed, including both deterministic software
  render sizes and the focused MIDI construction test. The same suite had
  aborted 8 processes during Task 088.
- ASan/UBSan CTest: 32/32 passed with LeakSanitizer disabled because the traced
  execution environment does not support it.
- The Task 089 `reusable-pattern-arrangement` scenario remains in
  `neurogenic_workflows`.
- `nix build path:.#production` completed in a sandbox using pinned Slint
  1.19.0, Corrosion 0.6.1 and the fixed Cargo vendor hash in `flake.nix`.
- Installed `neurogenic`, `neurogenic-slint`, diagnostics, discovery, launcher,
  preflight and service resources were present. `ldd` reported no unresolved
  dependencies and resolved Slint/xkbcommon/X11/Wayland/ALSA from the closure.
- Installed headless enumeration, diagnostic JSON, semantic discovery,
  preflight and missing-display rejection passed.
- A genuine installed frontend run passed under `xvfb-run` with
  `SLINT_BACKEND=winit-software`; lifecycle reached ready and stopped cleanly.
- `nix flake check path:.` passed the lightweight package tests, production
  package/install checks and AArch64 compilation-only check.

Final command counts and any environment limitation are recorded in the Task
090 completion report and `docs/STATUS.md`. Native Raspberry Pi 5 execution,
physical MIDI hotplug, touch and audio remain **NOT RUN**.
