# Slint frontend performance and rendered validation

Task 082 established incremental publication. Task 095 remeasured the current
frontend and added revision-cached immutable snapshot handles. See
[Task 095 validation](audit/task-088/TASK_095_VALIDATION.md) for current
measurements. They are development-host results, not N150 or Raspberry Pi 5
measurements.

## Reproduction

```sh
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic_slint_benchmark
ctest --test-dir build/slint -R 'slint_(rendered|incremental)' --output-on-failure
```

The deterministic workload has 16 tracks and 256 note events. The full phase
forces page-local structural changes; the incremental phase advances production
engine playback in 64-frame callbacks. Durations include application snapshot
and adapter refresh. CI uses structural assertions rather than timing gates.

| Refresh path | median | p95 | p99 | maximum |
|---|---:|---:|---:|---:|
| Idle/unchanged | 60.363 us | 66.584 us | 70.722 us | 111.618 us |
| Full reconstruction | 70.591 us | 77.405 us | 82.634 us | 89.327 us |
| Playback telemetry | 60.774 us | 69.249 us | 72.255 us | 73.106 us |

The measured full-to-playback median reduction is 13.9%. This run recorded 258
full rebuilds, 257 telemetry updates and 256 unchanged skips. Maximums are
observations, not latency guarantees.
`ApplicationService::snapshot_shared()` now reuses one immutable copied state
per application revision. The compatibility `snapshot()` API still returns a
value copy. Changed revisions still copy complete state and remain the
principal presentation-side cost.

## Publication architecture

The adapter fingerprints structural identity and separates full reconstruction
from transport, playhead, meters, recording/processing progress, parameter,
macro and contextual-control telemetry. Terminal states and acknowledgements
remain authoritative. The frontend publishes high-frequency rows only for the
active page. Slint `VectorModel` objects retain identity; equal-size data uses
row updates and structural size changes reset the vector. Publication remains on
the frontend thread.

## Generated and rendered coverage

`neurogenic_slint_rendered_tests` instantiates production `PerformanceWindow`,
installs production callbacks, invokes navigation and representative callbacks
for all eight pages, validates transport round trips and modal cancellation, and
renders with Slint's public software renderer at 1920x1200 and 1024x600. It
writes deterministic PPMs for every page and a recording modal; CI retains them
on failure.

Task 095 additionally asserts production shell-strip geometry. The pinned
Slint C++ testing API does not provide stable coordinate-level input
simulation with snapshot pixels in this configuration. Tests exercise generated
callbacks and bindings, not compositor pointer/touch delivery, focus traversal,
GPU rendering or physical touchscreen input. Those remain manual validation.

Native AArch64 development uses `cmake --preset slint` on an AArch64 host.
Cross-compiling additionally requires a host-executable pinned Slint compiler;
the normal AArch64 engine check is frontend-disabled and is not a rendered test.

## Task 097A regression investigation

The 096G full-rebuild regression was repeated assignment-target discovery added
after 096E, not incremental telemetry. Stable capability metadata is cached by
action/parameter identity. The identical development workload measured a
124,843 ns full median and 64,310 ns incremental median, with one discovery
rebuild. CI rejects unchanged page refreshes that repeat discovery.
