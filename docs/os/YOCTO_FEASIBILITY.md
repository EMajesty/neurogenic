# Yocto feasibility

Upstream `slint-ui/meta-slint` is an active MIT-licensed layer with Slint
recipes and LinuxKMS demo-image integrations. Its current main branch is not
evidence of compatibility with NEUROGENIC's pinned Slint 1.19.0; a future build
must select a matching release/commit and verify its C++ recipe, host compiler,
Cargo vendoring and LinuxKMS feature set.

A future `meta-neurogenic` should contain an application recipe, image/service
configuration, persistence mount, udev/seat policy and optional RT kernel
fragment. It should depend on compatible OE-Core plus meta-rust/meta-clang as
required by the selected meta-slint revision, and the actual BSP layers:
generic x86-64/UEFI for G10 only after its GPU is known, and a maintained
Raspberry Pi 5 layer for AArch64. Graphics dependencies remain libdrm,
Mesa/GBM/EGL, libinput, libseat and font/input libraries.

Yocto offers stronger BSP/layer and image-composition machinery than a small
Buildroot external tree, at higher metadata and maintenance cost. OS-005 must
pin every layer, build the same application revision and run the common
benchmark before it influences the production decision. No Yocto image was
built in OS-001.

References: <https://github.com/slint-ui/meta-slint> and the pinned application
source at Slint v1.19.0.
