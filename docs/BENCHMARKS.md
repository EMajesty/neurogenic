# Performance monitoring and benchmarks

Slint adapter refresh and generated software-renderer methodology/results are
reported in [SLINT_FRONTEND_PERFORMANCE.md](SLINT_FRONTEND_PERFORMANCE.md).
Those are desktop presentation measurements, not audio-callback or appliance
hardware claims.

Task 065 uses the bounded 16/64-slot preparation cases and integrated scene
transitions to exercise hybrid publication. Track-scoped activation adds a
fixed 32-ID payload and bounded cleanup, not callback graph work.
On the 2026-11-21 Ryzen 7 7800X3D x86-64 Debug host (GCC 15.3), the 16/64-slot
coordinator cases measured 1.57/3.16 microseconds preparation and 19/24 ns per
idle callback iteration. These measure launcher bookkeeping, not a physical
audio backend or a dense 16/32-track hybrid mix.

Task 064 adds stretched overlaps, crossfades, scenes/follows, capture, external
clock input, and gapless handoff to the bounded integration workload. The
optional `--soak-seconds` run is excluded from CI. Current workstation numbers
and hardware constraints are in `LIVE_PERFORMANCE_VALIDATION.md`.

Performance capture separates bounded activation acknowledgement handling from
control-side arrangement commit. Hardware callback timing still requires N150
and Raspberry Pi 5 measurements; CI results are not hardware claims.

Control-side preparation/cancellation workloads cover 16 and 64 slots and are
reported separately from the existing pattern/audio callback workloads.

Gapless handoff adds one bounded callback split without a PCM copy. Reserved PCM
is `frames * channels * sizeof(float)`, plus one sample-version slot and voice.
Live-loop callback cost otherwise follows the existing `internal_master_recording` path;
overdub preparation and clip publication are control-side. Eight sessions are
bounded, with one active capture.

Task 059 adds bounded overlap-envelope processing to the existing sample voice
path; clip scheduling remains two timeline edges per clip.

Task 058 extends the existing callback suite with 32 simultaneous stretched
voices and loop-aware stretched region workloads. The preparation case remains
separate from callback processing. Ratio rebasing, reverse reads and wrapped
loop reads use constant per-voice memory; results are host regressions until
measured on N150 or Raspberry Pi 5 hardware.

On the 2026-11-21 Ryzen 7 7800X3D x86-64 Debug host (GCC 15.3, Linux
7.3.0, 48 kHz, 256 frames, 20 iterations), 8/16/32 ordinary stretched voices
averaged 127/246/461 microseconds per buffer. Sixteen stretched region voices
averaged 292 microseconds and the same workload with wrapped loops/crossfades
averaged 428 microseconds. Resident memory was about 54.0 MB. These short host
runs report average processing cost, not hardware XRUNs or worst-case latency.

`audio_clips_8`, `audio_clips_32` and `audio_clips_128` separate control-side
plan preparation from per-buffer clip-edge scheduling. Long loops retain the
same two-edge plan representation.

`sample_processing_stereo_4s_three_operations` measures a
gain/fade/channel-conversion chain and `sample_processing_region_bounce`
measures region preparation plus trim.
Both are offline control-side costs and are separate from callback benchmarks.

`sample_analysis_stereo_equivalent_4_seconds` and
`sample_analysis_quiet_noisy_4_seconds` report control-side preparation time.
They do not report callback cost because analysis is never called by audio
processing. Temporary storage is reported by the result API.

Task 054 adds `region_fades_16_voices`, `region_loops_32_voices`, and
`region_stretch_16_voices`. They measure callback playback separately from
control-side editing and are host regressions, not Pi 5 or N150 claims.
On the 2026-11-21 x86-64 Debug host (Ryzen 7 7800X3D, GCC 15.3, 48 kHz,
256 frames, 20 iterations) they averaged 79.6, 190.8 and 218.2 microseconds
per buffer respectively, with approximately 54.0 MB resident memory.

The preset regression reports `application_prepare_us`,
`activation_callback_us`, `package_export_us`, and `package_bytes` separately.
Preparation covers parsing, WAV preparation, validation, and ID remapping;
activation covers only the bounded callback operation. Package export is
filesystem work and never part of callback timing. Developer-machine results
are not Pi 5 or N150 performance claims.

Task 049 adds light (4 tracks), medium (16), heavy (32), and stress (32
tracks/128 routes) integration tiers. A 2026-11-20 Ryzen 7 7800X3D Debug run
measured average/worst callback times of 0.054/0.147, 0.177/0.332, 1.161/1.878
and 1.356/2.301 ms. It observed no deadline misses, MIDI drops or recording
overflows and about 47.4 MiB peak RSS. Callback sizes cycle through 256, 127, 65
and 191 frames; project preparation is excluded. These are regression results,
not Pi 5, N150 or XRUN claims. See
[INTEGRATION_VALIDATION.md](INTEGRATION_VALIDATION.md).

`arrangement_recording_4096_events` separates bounded capture cost from
control-side validation/commit cost for rapid launches and dense automation.
Callback acknowledgement remains a fixed queue push. Results are host
measurements, not Raspberry Pi 5 or N150 claims.

Arrangement scenarios compile and schedule 16 patterns/64 clips and 64
patterns/256 clips. `preparation_ns` contains control-side materialization and
compilation; buffer processing reports scheduler cost separately.

Task 045 adds `pattern_chain_64_entries`, including per-entry repetitions and
prepared loop traversal. It measures chain materialization separately from
steady-state scheduler buffers. Exact launch latency is also regression-tested
across variable callback partitions; physical audio/MIDI latency remains a
hardware measurement. On the development Ryzen 7 7800X3D Debug build (GCC
15.3, 48 kHz, 256 frames, 50 iterations), the chain prepared in 0.062 ms and
steady-state sparse scheduling averaged 10 ns/buffer with 14.0 MB process RSS.
This is not a Pi 5 or N150 measurement.

Debug GCC 15.3 measurements on the development Ryzen 7 7800X3D (48 kHz,
256-frame buffers, 50 iterations) were 0.466 ms preparation / 19 ns scheduler
work per sparse buffer for 16/64, and 5.90 ms / 19 ns for 64/256. Resident
memory observed was 13.4 and 14.3 MB. These are development-host measurements,
not Raspberry Pi 5 or N150 claims.

Task 042 adds `sample_voice_modulation_8v_4m`, `16v_4m`, `32v_6m` and
`64v_6m`. They isolate per-voice graph evaluation from PCM/stretch cost.
Preparation covers graph validation, voice allocation and deterministic reset.

Debug GCC 15.3 results on the development Ryzen 7 7800X3D at 48 kHz/256
frames (200 iterations) were 10.2, 20.9, 53.5 and 103.6 microseconds per
buffer respectively. Preparation was 3.6, 3.4, 7.6 and 13.2 microseconds;
process RSS was 77.0 MB. These are offline development-host measurements, not
Raspberry Pi 5 or N150 real-time claims.

Task 041 adds mixed native-generator workloads using LFO, step, Euclidean,
clocked random, function, logistic and follower instances with dense normal
destinations. On the 2026-11-20 x86-64 Debug host (Ryzen 7 7800X3D, GCC 15.3,
48 kHz, 256 frames, 200 iterations), 16/32, 32/64 and 64/128
instance/connection cases averaged 35.2, 87.4 and 269.4 microseconds per
buffer. Control-side preparation took 6.4, 9.7 and 21.3 microseconds;
resident process memory was about 77.0 MB. The 64-node deep graph averaged
121.0 microseconds. These are offline host regressions, not Pi 5 or N150
measurements.

Task 040 adds 16/32, 32/64 and 64/128 instance/connection graph workloads plus
a 64-node deep acyclic chain. On the 2026-11-20 x86-64 Debug host (Ryzen 7
7800X3D, GCC 15.3, 48 kHz, 256 frames, 200 iterations), callback averages were
28.7, 72.0, 208.2 and 88.4 microseconds per buffer. Graph preparation took
5.8, 7.9, 19.6 and 14.6 microseconds respectively; resident process memory was
76.6 MB. These are offline host regressions, not Pi 5 or N150 measurements.

Task 039 adds bounded 8-, 32- and 64-instance scenarios; the last uses 128
dense routes. They measure callback work after control-side preparation and do
not constitute Raspberry Pi 5 measurements.

On the 2026-11-20 x86-64 Debug host (Ryzen 7 7800X3D, GCC 15.3, 48 kHz,
256 frames, 200 iterations), 8/16, 32/64 and 64/128 instance/route cases
averaged 19.1, 35.4 and 68.7 microseconds per buffer respectively. Resident
process memory was 76.4 MB. These short offline regression measurements are not
physical Raspberry Pi 5 or Intel N150 results.

Native-chain scenarios cover four tracks with two processors each, and sixteen
tracks with one processor each plus both aux chains and master. Results remain
offline measurements rather than Raspberry Pi 5 hardware claims.

Task 038 debug-build sample (Ryzen 7 7800X3D, Linux 7.3, GCC 15.3, 48 kHz,
256 frames, 50 iterations): four tracks/two effects averaged 24.9 us per
buffer; sixteen tracks/one effect plus two aux and master averaged 46.5 us.
Resident process memory was approximately 51.5 MB. Run the benchmark on target
hardware before assigning a device budget.

Device-backed bring-up uses `neurogenic-diagnostics --stress SECONDS
--realtime` and emits `neurogenic.hardware-test-report.v1`. Callback deadline
misses are not backend XRUN claims. See
[hardware testing](HARDWARE_TESTING.md).

Mixer scenarios cover 4, 8 and 16 channels, both stereo aux buses and simulated
six-input/six-output routing. These host measurements do not establish physical
Raspberry Pi 5 or Intel N150 performance.
`mixer_16_tracks_4_groups_fx_aux` and `mixer_16_tracks_8_groups_fx_aux`
measure prepared group summing, one native group effect per bus, group aux
sends, and master return. Preparation and publication are outside the timed
callback loop; run these scenarios on target hardware before setting budgets.
On the 2026-11-20 x86-64 Debug host (Ryzen 7 7800X3D, 256 frames, 50
iterations), four groups averaged 100.2 us/buffer (53.2x real-time; 1.57 us
preparation) and eight groups averaged 119.3 us/buffer (44.7x; 1.79 us
preparation). Resident process memory was about 77.4 MB. These are offline host
regression measurements, not Pi 5 or N150 claims.
`mixer_two_external_fx_latency` adds eight tracks, two stereo sends/returns,
simulated six-channel I/O, wet/dry mixing and 128-frame compensation. Routing
preparation and snapshot publication remain outside the measured callback path.
On the 2026-11-19 x86-64 Debug host it averaged about 40.7 us per 256-frame
buffer (roughly 131x offline real-time factor). This is an offline regression
measurement, not physical I/O, Raspberry Pi 5, or Intel N150 evidence.

NEUROGENIC targets comfortable full-workstation operation on Raspberry Pi 5,
while the personal reference build is an Intel N150 with 16 GB RAM. These are
design targets, not measured claims. Faster machines provide feature headroom;
they do not justify baseline overhead or an x86-specific implementation.

## Live metrics

`AudioEngine` records one bounded callback sample using a monotonic
`steady_clock`: current, exponentially smoothed average (1/64 update weight),
peak duration, frame/rate-derived deadline, count, measured deadline overruns,
and active voices. The callback performs two clock reads, bounded voice
counting, and relaxed atomic updates. Monitoring can be disabled through
`AudioEngine::set_performance_monitoring(false)`. It performs no allocation,
locks, logging, file access, or waits.

A deadline overrun means measured engine processing exceeded
`frames / sample_rate`; it is not proof that the audio backend experienced an
XRUN. Backend XRUN reporting is currently unavailable. Callback load is engine
processing time divided by its deadline. Linux process CPU is process CPU time
divided by wall time and can exceed 100% when multiple cores are busy; system
CPU is aggregate utilization across all cores.

`SystemMonitor` reads `/proc` on a control-side thread at most twice per second.
It publishes availability-tagged process CPU, aggregate CPU, resident memory,
and total memory atomically. Unsupported platforms report unavailable values.
`ApplicationState::performance` combines these metrics with sample rate,
buffer size, voice capacity, transport and existing bounded-queue counters.

Run one headless machine-readable snapshot with:

```sh
./build/dev/neurogenic --stats
```

The schema is `neurogenic.performance.v1`. A one-shot snapshot normally reports
CPU unavailable because two periodic samples are required.

## Deterministic benchmark workloads

```sh
cmake --preset dev
cmake --build --preset dev --target neurogenic_benchmark
./build/dev/neurogenic_benchmark > benchmark.jsonl
```

The executable uses 2,000 iterations per timed scenario by default. Pass
`--iterations COUNT` to choose a different workload. CTest deliberately uses
100 iterations: its `neurogenic_benchmark_schema` test exercises every scenario
(including under ASan/UBSan) and validates the JSONL summary, but is a bounded
correctness smoke test rather than a performance measurement. Run the command
above without an override when collecting comparable benchmark results.

Each JSON Lines record uses `neurogenic.benchmark.v1` and reports scenario,
CPU model, architecture, OS/kernel, compiler/build type, sample rate, buffer
size, preparation time, processing nanoseconds per buffer, real-time factor,
resident memory, and iteration count. Workloads cover idle, 8/16/32 voices,
44.1/48 kHz playback, dense scheduling, rapid publication, 44.1-to-48 kHz
preparation, native synth at 1/8/16/32 voices, filter and delay enabled/bypassed,
mixed sample/synth playback, and GUI frame construction at 1920×1200, 1024×800
and 1024×600.
Task 019 adds sparse and dense parameter-lock scheduling, multiple automated
destinations, mixed sample/synth automation, and rapid automation publication.
Compilation/preparation remains reported separately from callback processing.
On the 2026-11-19 x86-64 Debug host, these scenarios averaged approximately
12.0 µs per 256-frame buffer for both sparse and dense locks, 11.9 µs for the
mixed automation case, and 9.5 µs for rapid publication. Dense-plan compilation
was 18.6 µs. These offline figures are not Pi 5/N150 or hardware real-time
claims.
Preparation is excluded from audio processing measurements. GUI measurements
cover snapshot synchronization, layout and draw-command construction; GPU
execution and presentation latency are not measured.

An optional dedicated benchmark machine can enforce a per-buffer ceiling:

```sh
NEUROGENIC_BENCHMARK_BUDGET_NS=100000 ./build/dev/neurogenic_benchmark
```

No budget is enabled in ordinary CI because shared-runner timings are not
comparable. Retain JSONL from both commits and use the same machine, power mode,
compiler, build type, and background load. Compare matching scenario records.

N150 physical profiles request 64, 128, 256 and 512 frames at 48 kHz through
`neurogenic-diagnostics --realtime`; reports retain requested/negotiated
periods, scheduling policy, callback timing and XRUN capability. See
[N150_BRINGUP.md](N150_BRINGUP.md). No N150 measurements have been collected;
Null/offline results are not hardware performance.
The checksum/schema test verifies deterministic workload execution and output,
not identical elapsed time.

Example Debug output from the 2026-11-19 development host included roughly
8.8 µs idle, 9.6/14.0/23.1 µs for 8/16/32 voices, and 3.2–3.7 µs for GUI frame
construction. These offline averages are neither worst-case analysis nor
evidence of Pi 5, N150, physical audio, GPU, XRUN, or thermal performance.

Task 018 Debug measurements on the same x86-64 development host were about
7.7/30.7/54.7/104.9 µs for 1/8/16/32 filtered synth voices, 49.4 µs for 16
voices with the filter bypassed, 1.1 µs for enabled delay processing, and
110.6 µs for mixed sample/synth playback. These are scenario examples, not
portable thresholds or ARM measurements.
Task 020 adds `probability_conditions` and `maximum_ratchets_fill` scheduler
scenarios. Their reported preparation time is timeline compilation; processing
time measures bounded scheduling only. Offline figures are regression data, not
claims about Pi 5 or N150 real-time hardware behavior.

Task 021 adds `slice_triggers_16`, `slice_voices_32_overlapping`,
`slice_rapid_retrigger`, `waveform_peak_generation`, and
`transient_detection`. Playback reports callback work; analysis reports
preparation time separately. These host averages establish neither Pi 5/N150
limits nor physical-device latency.
On the 2026-11-19 Ryzen 7 7800X3D Debug run, the three slice playback cases
averaged about 21.1, 29.7 and 48.9 µs per 256-frame buffer; four-second waveform
peaks and transient analysis took about 0.14 and 0.74 ms respectively.

Task 022 adds `pitched_slice_voices_16`, `pitched_slice_voices_32`,
`reverse_slice_playback`, `choke_heavy_sample_playback` and
`gate_release_envelopes`. They measure callback playback only; setup and
callback-queue publication are outside the timed region. Results are offline
regression measurements, not physical Pi 5, N150 or XRUN claims.
On the 2026-11-19 Ryzen 7 7800X3D Debug run, these averaged about 19.2, 26.6,
19.7, 13.5 and 23.3 µs per 256-frame buffer respectively. The comparable
16/32-voice legacy slice cases measured 25.1/36.1 µs in the same run; workload
lifetimes differ, so this is regression context rather than a claimed speedup.

Task 023 adds mono/stereo realtime stretch at 0.5×, 1.5× and 2×, 8/16
simultaneous voices, mixed ordinary/stretched playback and voice-state setup.
On the 2026-11-19 Ryzen 7 7800X3D Debug host, 1.5× mono/stereo measured about
24.3/26.1 µs, while 8/16 voices at 2× measured about 138.7/264.2 µs per
256-frame buffer (real-time factors 38.5×/20.2×). The mixed 8+8 case measured
140.7 µs. Voice setup was below useful timer resolution and allocates no cache.
These are average offline callback costs, not Pi 5/N150, worst-case or XRUN
claims; ratio-dependent voice lifetime also affects averages.
# Project metadata I/O

`neurogenic_benchmark` reports `project_save_metadata` and
`project_load_metadata` preparation time separately from callback processing.
These deterministic cases cover metadata only; sample decode/resampling time is
already measured by the existing preparation benchmarks and depends on asset
size and storage. The figures do not establish crash durability or hardware
real-time performance.

`internal_master_recording` measures post-effect callback capture separately
from the control-side immutable-sample copy reported as preparation time. It
uses deterministic silence and a preallocated buffer; WAV filesystem throughput
and physical-device latency are excluded.

Task 026 adds mixer scenarios for 4 channels, 8/16 channels with two active
sends, simulated six-channel I/O and rapid routing publication. On the
2026-11-19 Ryzen 7 7800X3D Debug host these averaged approximately 9.4, 29.8,
50.1 and 62.5 µs per 256-frame buffer. Rapid publication preparation averaged
1.2 µs and its idle 16-channel processing pass 12.5 µs. Resident process memory
was about 35.8 MiB. These are deterministic offline averages, not worst-case,
physical-device, Pi 5, N150 or XRUN claims.

Task 028 adds bounded scenarios for four active LFOs with 16 routes, 32 routes,
and tempo synchronization. Processing covers callback modulation and numeric
destination delivery; configuration preparation remains outside the timed
region. These are offline regression measurements, not Pi 5/N150 or XRUN data.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, the 16-route, 32-route, and
tempo-sync scenarios averaged approximately 13.5, 15.9, and 13.9 µs per
256-frame buffer. They are averages, not worst-case or hardware claims.

Task 029 adds 4/8/16-instance synth workloads and 8/16-instance per-voice
LFO/envelope workloads. On the 2026-11-20 Ryzen 7 7800X3D Debug host, plain
instances averaged about 50.3/104.8/201.7 µs and the 8/16-instance voice
modulation cases averaged 105.1/206.1 µs per 256-frame buffer. Idle instances
are skipped. These are offline averages, not Pi 5/N150 or hardware guarantees.

Task 030 broadens the 16/32-route scenarios across synth, delay, sample, mixer
and external-FX numeric destinations. Timed work is callback dispatch and
effective-value application; configuration preparation remains excluded. A
20-iteration Debug run on the 2026-11-20 Ryzen 7 7800X3D host measured about
16.0/18.1/16.2 µs for 16 routes, 32 routes and 16 tempo-synchronized routes,
with approximately 38.8 MiB process RSS. These short offline averages are not
worst-case, Pi 5/N150 or physical-audio results.

Task 031 adds `midi_four_ports_dense_clock`: four explicit routes forward 16
note messages per iteration while four-port 24-PPQN clock generation runs and
the outbound queue is drained. It measures control/scheduling and queue
dispatch overhead only; libremidi, kernel scheduling, USB/DIN transmission and
physical jitter are excluded.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, a 100-iteration run averaged
approximately 0.95 microseconds per 256-frame iteration (about 0.05% of one
5.33 ms buffer deadline). This is an offline average, not a wire-latency claim.

Task 032 adds `midi_external_clock_tracking`, which feeds deterministic jittered
pulses through the median/frequency/phase follower and advances one 1000-frame
interval per iteration. It measures only bounded tracking arithmetic, not
libremidi or physical transmission.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, a 100-iteration run measured
approximately 27 ns per follower update; the combined four-port dense routing
and internal-clock case measured approximately 1.03 microseconds per 256-frame
iteration. These short offline averages are not worst-case hardware results.

Task 033 adds `integrated_8_sample_4_synth_mod_aux_record_midi` and
`integrated_gui_state_sync_1920x1200`. The first times only callback processing
for eight sample tracks, four synth instances, active modulation, two aux sends,
internal master recording and MIDI clock; MIDI dispatch and GUI work stay
outside that timer. The second reports remaining per-iteration GUI/state,
MIDI-dispatch and harness cost. A 100-iteration Debug run on the 2026-11-20
Ryzen 7 7800X3D host measured 218.8 microseconds per 256-frame callback
(24.4x realtime, approximately 4.1% of one callback deadline), a 373.9
microsecond observed callback peak, zero deadline overruns, MIDI drops and
recording overflows, about 15.0 microseconds of GUI/state overhead, and about
40.4 MiB RSS. This is a short offline regression
run, not N150, Raspberry Pi 5, GPU-presentation or hardware-XRUN evidence.

Task 047 adds `performance_macros_8_32` and `performance_macros_16_64`.
Preparation time covers macro-to-graph compilation; processing time covers the
bounded control-rate graph evaluation for a 256-frame buffer. Results are host
measurements only and must not be treated as Raspberry Pi 5 or N150 claims.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, a 20-iteration run measured
approximately 12.4 microseconds per buffer for 8/32 and 35.9 microseconds for
16/64, with 3.8/6.5 microseconds of graph preparation and about 74.2 MiB RSS.

Task 048 adds callback-executed morph scenarios for 8 and 16 simultaneous
morphs, rapid scene recall, and 128 active macro assignments. A 20-iteration
Debug run on the 2026-11-20 Ryzen 7 7800X3D host measured 4.22 microseconds per
256-frame buffer for 8 morphs, 4.91 microseconds for 16 morphs and 4.24
microseconds for rapid recall. The 16-macro/128-assignment graph measured 95.05
microseconds per buffer with 11.58 microseconds preparation; process RSS was
about 74.3 MiB. Morph command preparation measured below 0.5 microseconds in
these cases. These are short deterministic host averages, not worst-case,
Raspberry Pi 5, N150, physical-audio or XRUN claims. Scheduling accuracy is
bounded by the 32-frame control interval, independently of callback size.
