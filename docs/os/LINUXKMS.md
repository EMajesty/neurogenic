# Slint LinuxKMS

## Implemented build

Pinned Slint 1.19.0 is built with its exact CMake features:

- `SLINT_FEATURE_BACKEND_LINUXKMS=ON`
- `SLINT_FEATURE_BACKEND_LINUXKMS_LIBSEAT=ON`
- `SLINT_FEATURE_BACKEND_LINUXKMS_LIBINPUT=ON`
- `SLINT_FEATURE_RENDERER_SOFTWARE=ON`
- `SLINT_FEATURE_RENDERER_FEMTOVG=ON`

The normal winit desktop backend remains available in the separate production
package; the KMS package omits it so an explicit appliance build cannot hide a
KMS failure behind a compositor backend. Skia OpenGL/Vulkan and FemtoVG-wgpu are supported by the pinned
source but are not enabled in OS-001 because their larger closures and target
driver requirements have not been validated.

Build with `nix build .#kms` or CMake preset `kms`. Launch the installed Nix
package with:

```sh
SLINT_BACKEND=linuxkms-software ./result/bin/neurogenic-kms
SLINT_BACKEND=linuxkms-femtovg ./result/bin/neurogenic-kms
```

The wrapper defaults to `linuxkms-software`. The existing `.#production`,
`.#headless` and `.#sdl` packages are unchanged.

## Pinned 1.19 behavior

LinuxKMS renders one fullscreen window directly through DRM/KMS. Software uses
DRM dumb buffers and can fall back to legacy LinuxFB; FemtoVG requires OpenGL
ES/EGL/GBM. libinput supplies touch/mouse/keyboard events. libseat delegates
DRM and input ownership to logind or seatd so root is not required.

`SLINT_DRM_OUTPUT=list` prints connectors. Set it to the discovered connector.
`SLINT_DRM_MODE=list` prints modes; a value such as `1920x1200@60` must match an
advertised mode. `SLINT_KMS_ROTATION` accepts 0, 90, 180 or 270. Touch rotation
is separate and must use a hardware-specific libinput calibration matrix after
the real device is identified. Slint chooses the first connected connector and
preferred mode when these variables are absent. There is no verified
application-level DPI override in this integration; layout acceptance remains
1920x1200 and 1024x600.

## Dependencies and permissions

Build/runtime closure includes libdrm, GBM/Mesa, libinput, libudev,
libxkbcommon and libseat. Fontconfig/Freetype remain required. A service user
needs an active seat plus access delegated by logind/seatd; supplementary
`video`, `render`, `input`, `audio` and `dialout` groups are a bring-up fallback,
not the preferred long-term authorization model. Do not run as root merely to
avoid configuring the seat.

Common failures are explicit: no `/dev/dri/card*`, no connected connector,
invalid `SLINT_DRM_MODE`, denied seat/device access, missing GBM/EGL driver, or
renderer initialization failure. Use software first to separate DRM/mode/input
issues from GPU acceleration. The pinned selector does not fall back to winit
after an explicitly matched `linuxkms-*` backend fails initialization.

## AArch64 cross-build status

The renderer-independent AArch64 Nix check remains available. The OS-001
LinuxKMS software-renderer cross-build reaches Slint's Rust build but does not
yet pass: pinned Corrosion 0.6.1 falls back to the build-host
`x86_64-unknown-linux-gnu` target while Nix supplies the AArch64 linker, causing
Rust build scripts to pass `-m64` to that linker. Closing this requires an
explicit native host `slint-compiler`/Cargo build and an AArch64 Slint runtime
target. Host libraries must not be linked into the target package.

No DRM device or VKMS is available in the OS-001 environment, so actual KMS,
touch, rotation, refresh and GPU behavior are **NOT RUN**.
