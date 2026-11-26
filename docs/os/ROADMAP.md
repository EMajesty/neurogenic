# NEUROGENIC OS workstream

The OS workstream is independent of the musical application roadmap. NixOS is
the reproducible baseline until measured hardware evidence supports another
choice.

1. **OS-001 — LinuxKMS packaging and feasibility.** Pinned direct-display
   package, build evidence and bring-up procedures.
2. **OS-002 — G10 display and touchscreen bring-up.** Identify the real GPU,
   connector, mode, orientation and input transform; compare software and
   FemtoVG on the reference display.
3. **OS-003 — Minimal NixOS appliance and latency baseline.** Non-root direct
   boot, persistence, maintenance recovery and standard-kernel measurements;
   then an experimental PREEMPT_RT configuration.
4. **OS-004 — Buildroot prototype.** Only after the application, host Slint
   compiler and target Rust/C++ dependency plan is reproducible.
5. **OS-005 — Yocto prototype.** Pin compatible OE, BSP, Rust and meta-slint
   revisions in a `meta-neurogenic` layer.
6. **OS-006 — Comparative evaluation.** Run the same workload and measurement
   protocol on NixOS, Buildroot and Yocto.
7. **OS-007 — Production image decision.** Choose from evidence, maintenance
   cost, BSP quality and recovery behavior.
8. **OS-008 — Immutable storage, updates and recovery.** Implement A/B images,
   boot acknowledgement and offline recovery using an established framework.

G10 is the primary x86_64 reference and Raspberry Pi 5 is the secondary
AArch64 reference. Neither physical target is validated by OS-001.
