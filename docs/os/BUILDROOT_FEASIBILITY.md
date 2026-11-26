# Buildroot feasibility

Buildroot is feasible but no maintained in-tree Slint package was found in the
OS-001 source review. A future `br2-external-neurogenic` should contain the
application package, defconfigs, kernel fragments, rootfs overlay and service
integration without forking Buildroot.

Required target packages are glibc, C++20 runtime, ALSA, libdrm, Mesa/GBM/EGL,
libinput/libudev, libseat, libxkbcommon, fontconfig/Freetype, USB/udev support
and the selected init/supervisor. x86_64 needs a UEFI-capable defconfig; RPi 5
needs a maintained board/BSP and GPU firmware/DRM stack. PREEMPT_RT should be a
kernel fragment variant, not the only recovery kernel.

The difficult boundary is mixed-language cross-building. Slint's compiler is a
host executable while its C++ runtime/Rust crates and native libraries target
the appliance. Corrosion, Cargo's fully vendored graph, Rust target support and
pkg-config sysroot separation must be explicit. The smallest strategy is one
external CMake package using the repository's pinned source/vendor inputs and a
host-built `slint-compiler`, rather than packaging every Slint crate.

Persistent storage, update/rollback and board boot integration are outside an
application package. Buildroot should be prototyped only after OS-002/003 yield
real driver and latency requirements.
