# OS-001 validation

## Outcome

OS-001 establishes a separate appliance OS workstream and a reproducible,
optional LinuxKMS package. It does not replace the production desktop package
or select a future distribution.

## Build investigation

The flake pins nixpkgs `nixos-26.05`, Slint 1.19.0, Corrosion 0.6.1,
miniaudio 0.11.21, libremidi 5.4.2, Lua 5.4.8 and sol2 3.5.0. Slint Cargo
dependencies are fixed by `fetchCargoVendor`; builds are offline after inputs
are available. The C++20 application consumes an ahead-of-time host
`slint-compiler` and links the target Slint runtime. Existing outputs are
`production` (Slint+winit+SDL), `headless`, and `sdl`.

Pinned source inspection, rather than current documentation alone, confirms
LinuxKMS, libseat, libinput, DRM dumb-buffer software, FemtoVG OpenGL/EGL/GBM,
Skia OpenGL/Vulkan/software and FemtoVG-wgpu features. OS-001 enables software
and FemtoVG only. Runtime names are `linuxkms-software` and
`linuxkms-femtovg`. Connector, mode and rotation variables are documented in
`LINUXKMS.md`.

## Implemented packaging

- `NEUROGENIC_ENABLE_SLINT_LINUXKMS`, optional GPU feature, and the `kms`
  CMake preset. The KMS variant disables winit; desktop remains a separate
  production package.
- `packages.<system>.kms` / `nix build .#kms`.
- Explicit libdrm, libgbm/Mesa, libinput/libudev, libseat and input/font
  dependencies without adding them to the headless package.
- `neurogenic-kms`, which defaults to `linuxkms-software` and launches the
  sibling installed binary with explicit Slint backend selection.
- AArch64 KMS cross-build investigation separated from native Raspberry Pi
  evidence. The attempted software-renderer build reaches Slint's Rust build,
  but pinned Corrosion 0.6.1 cannot infer the Rust target from Nix's CMake
  cross toolchain: it requests `x86_64-unknown-linux-gnu` while invoking the
  AArch64 linker. This exact host-tool/target-runtime split remains a blocker;
  the failing derivation is not exposed as a flake check.

The initial package attempt exposed two build defects and was not counted as a
pass: pinned Nixpkgs calls the libseat provider `seatd`, and GBM is a separate
`libgbm` output rather than supplied to the linker by `mesa` alone. Both are
now explicit.

## Runtime and appliance findings

The audio adapter uses miniaudio's ALSA backend directly, requests fixed 48 kHz,
supports one to six channels and optional 1–8192-frame periods, and rejects a
different negotiated rate. PipeWire/JACK/PulseAudio are not required. Portable
backend XRUN counters and verified realtime audio-thread policy remain gaps.

The proposed minimal NixOS baseline retains systemd, udev and logind/seatd,
runs a non-root service without a compositor, and preserves a maintenance VT.
PREEMPT_RT, affinity, governor, IRQ and autosuspend policies are experiments,
not defaults. Immutable A/B/RAUC storage is proposed, not implemented.

Buildroot and Yocto are feasibility studies only. No image was built. No
upstream in-tree Buildroot Slint package was confirmed. `meta-slint` exists,
but a release compatible with pinned 1.19 and NEUROGENIC's C++/host-tool split
must be pinned and tested.

## Validation record

| Command | Result |
|---|---|
| `cmake --preset dev && cmake --build --preset dev -j2 && ctest --preset dev --output-on-failure` | PASS, 42/42 |
| `cmake --preset production && cmake --build --preset production -j2 && ctest --preset production --output-on-failure` | PASS, 45/45; includes generated Slint renders at 1920x1200 and 1024x600 |
| `cmake --preset slint && cmake --build --preset slint -j2 && ctest --preset slint --output-on-failure` | PASS, 45/45 |
| `cmake --preset sanitize && cmake --build --preset sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | PASS, 42/42 |
| `./build/production/neurogenic_slint_benchmark --iterations 200` | PASS; full median 225,571 ns, incremental median 69,490 ns, discovery rebuilds 1 |
| `nix build path:.#kms --no-write-lock-file` | PASS, LinuxKMS package and its 45/45 CTest suite |
| `./result/bin/neurogenic-kms --run-for 0.05` with no DRM device | expected exit 69 and explicit `no DRM card found` diagnostic; no desktop fallback |
| `ldd ./result/bin/neurogenic` on the KMS result | PASS; libseat, libinput, libgbm and libdrm resolved from the closure; no `not found` entries |
| `nix build path:. --no-write-lock-file` | PASS, unchanged default production output |
| `nix flake check path:. --no-write-lock-file` | PASS on x86_64; package/check evaluation included production, KMS, headless, SDL and the existing AArch64 compilation-only check; native aarch64 outputs were omitted as incompatible on this host |
| `env -u LD_LIBRARY_PATH ./result/bin/neurogenic --headless --list-devices` | PASS; clean installed closure, null headless audio and ALSA MIDI discovery |
| `env -u LD_LIBRARY_PATH ./result/bin/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, 67 controls, synchronized generation 1 |
| `nix build path:.#checks.x86_64-linux.aarch64-kms-cross-compile --no-write-lock-file` (investigation-only output, subsequently removed) | FAIL as expected from investigation: Corrosion selected x86_64 Rust while the Nix cross environment supplied an AArch64 linker (`-m64` rejected); no host-library workaround retained |

The host kernel exposes `CONFIG_DRM_VKMS=m`, but `/dev/dri` is absent. Loading a
kernel module was deliberately not requested, so VKMS initialization is **NOT
RUN**. Physical DRM/KMS, display, touchscreen, GPU, audio hardware, boot time,
latency, thermals and power remain **NOT RUN**.

## Remaining blockers

- No `/dev/dri` or VKMS is available in this environment, so KMS initialization
  and input cannot be runtime-tested.
- G10 GPU/connector/mode/touch identities and driver support are unknown until
  bring-up.
- Native Raspberry Pi 5 runtime is not established by cross-compilation.
- LinuxKMS AArch64 cross-compilation is blocked by Corrosion's uninferred
  `Rust_CARGO_TARGET` and the required native build-script/target-runtime Rust
  tool separation. The existing renderer-independent AArch64 check remains.
- Skia/Vulkan and FemtoVG-wgpu are investigated but not packaged.
- Minimal NixOS module/image, PREEMPT_RT comparison, A/B updates, Buildroot and
  Yocto images belong to later OS milestones.
