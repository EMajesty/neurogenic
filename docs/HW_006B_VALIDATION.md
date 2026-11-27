# HW-006B validation

Starting application revision:
`baa87d2a6f0d25f8c1afa85cc929a8da8b29c299`. Consulted controller proposal:
`cc0cbc42a7b775a9de6964e2545192876437483b`. The controller repository was
read through its pinned Git object and was not modified.

The HW-006A proposal was approved without numeric changes: minor 1.4, bits
11/12, push IDs `0x0160..0x0167`, namespaces 1/2, eight-byte records,
fingerprints `0x1936cd35` and `0x6b7c76c7`, and the conditional one-byte push
snapshot suffix are normative. The existing LED_RENDER envelope is sufficient;
no magnitude message or third capability was added.

Production implementation is in the shared wire header, protocol codec/host,
Reference V2 descriptor/profile adaptation, reconciliation path, contextual
feedback projection and production byte-stream simulator. Optional pushes are
explicitly unassigned. Simulator personalities cover baseline, push-only,
RGB-only, both, unknown capability and three invalid inventory forms.

Deterministic evidence lives in
`neurogenic_controller_v2_protocol_14_tests`: all ten baseline frames and six
1.4 frames are exact, mutual fallback and independent capabilities negotiate,
invalid inventories fail, an unassigned push does not mutate musical state,
and real contextual descriptors emit encoder LED_RENDER including the assigned
minimum and reconnect refresh. `neurogenic_controller_v2_protocol_tests`
retains the original 1.3 production-path recovery and convergence suite.

## Validation results

- `cmake --preset dev -DNEUROGENIC_ENABLE_GUI=OFF && cmake --build --preset dev -j2 && ctest --preset dev`: 49/49 passed.
- `cmake --preset sanitize -DNEUROGENIC_ENABLE_GUI=OFF && cmake --build --preset sanitize -j2 && ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize`: 49/49 passed.
- `cmake --preset slint && cmake --build --preset slint -j2 && ctest --preset slint`: 52/52 passed, including both rendered resolutions.
- `cmake --preset production && cmake --build --preset production -j2 && ctest --preset production`: 52/52 passed.
- `build/production/neurogenic_slint_benchmark`: passed; full median 247,603 ns, incremental median 73,227 ns, assignment discovery rebuilds 1.
- `nix build path:. --no-write-lock-file`: passed.
- `nix build path:.#kms --no-write-lock-file`: passed.
- `nix flake check path:. --no-write-lock-file`: passed all three x86 checks, including the AArch64 cross derivation; native `aarch64-linux` evaluation was omitted on this x86 host as expected.
- `env -i PATH=/usr/bin:/bin result/bin/neurogenic --headless --list-devices`: passed with null audio and no discovered physical MIDI devices.
- `build/production/neurogenic-diagnostics --controller-protocol-test`: passed the existing production protocol diagnostic.

Physical USB controller interoperability, firmware, electrical illumination,
color appearance and latency are **NOT RUN**. The current parameter descriptor
does not carry an explicit logarithmic/perceptual curve tag; encoder feedback
therefore uses the authoritative normalized descriptor range. Adding such a
narrow metadata field remains appropriate when a production parameter exposes
that distinction. No physical controller can yet verify the 31/255 minimum.
