# Task 096G validation

## Outcome and version decision

Reference Controller V2 physical support uses dedicated protocol **1.3**, not
protocol major 2. The existing bounded 1.x frame, CRC and decoder remain wire
compatible. Minor 1.3 adds capabilities and messages that legacy firmware does
not send: exact V2 inventory, host-issued input generation, full input-state
snapshot, LED rendering and diagnostics. Protocols 1.0, 1.1 and 1.2 continue to
select their existing descriptors and payloads.

The normative firmware contract is
[`REFERENCE_CONTROLLER_V2_FIRMWARE.md`](../../REFERENCE_CONTROLLER_V2_FIRMWARE.md).
[`controller_v2_wire.h`](../../../include/neurogenic/controller_v2_wire.h) is
the dependency-free canonical constants file. Host manifest construction and
validation consume it; IDs are independent of vector order and PCB wiring.

## Implemented production path

- Negotiation requires exact `neurogenic.reference-controller-v2`, protocol
  1.3, inventory/input-state capabilities, 67 controls and fingerprint
  `0x3256434e`.
- Musical input is rejected until the host has issued a generation and received
  the matching nine-byte button snapshot.
- V2 button and encoder packets carry the generation. Per-sender sequences
  reject duplicates; gaps cancel ownership and request reconciliation.
- Snapshots release missing controls and quarantine initially held controls;
  they never synthesize musical presses.
- Disconnect, reset and heartbeat loss clear held/deferred/momentary ownership.
- Generic authoritative feedback is translated through logical manifest IDs to
  bounded `LED_RENDER` packets. Temporal modes are firmware-rendered and final
  state is recoverable by full refresh.
- `ReferenceV2FirmwareSimulator` emits and consumes encoded frames through the
  production decoder and host. It can negotiate, maintain button state, emit
  deltas, synchronize, heartbeat, reboot and retain received feedback.
- `neurogenic-diagnostics --controller-v2-sim-test` verifies the same host path
  without a project or audio device and clearly marks `physical_hardware=false`.

## Safety and ownership

All framing, gesture cancellation, discovery and diagnostics execute on the
control side. Protocol buffers, session queues, snapshots and diagnostic output
are bounded. No audio-callback allocation, locking, discovery, filesystem work
or protocol processing was introduced. Firmware reports physical facts;
application gesture and musical meaning remains host-owned.

## Software acceptance evidence

`neurogenic_controller_v2_protocol_tests` covers the exact manifest and an
independently calculated golden button frame/CRC, production byte-stream
negotiation, initial synchronization, primary and shifted page routing,
descriptor-backed encoder adjustment, Performance Fill press/lost-release
recovery, full feedback, disconnect/reconnect, stale-generation rejection and
heartbeat. Existing protocol tests retain legacy framing and negotiation.

The byte-stream fixture is not physical USB or firmware evidence. MCU firmware,
PCB electrical behavior, debounce, rollover/ghosting, encoder quality, RGB
visibility/current, USB reliability, latency and ergonomics are **NOT RUN**.

## Task 097 closure matrix

| Class | Remaining closure | Acceptance |
|---|---|---|
| Presentation completion | Shared visual-modal ENTER/BACK ownership; full Performance logical-position chooser; Setup typed-argument/profile lifecycle and conflict/feedback forms; focus reachability | Generated production callbacks and structured reachability at both resolutions, with authoritative round trips |
| Controller binding | Complete non-Sequencer contextual banks and primary/SHIFT pad modes; remaining supported navigation/transport bindings | Shared resolver matrix matches controller and frontend; unsupported controls explicitly unavailable |
| Stronger evidence | Controller-authored lock scheduled playback and save/reopen; stale/deletion/modal held-step cancellation; feedback overflow convergence | Production semantic/engine assertions through ControllerSession and generated components |
| Missing musical capability | Metronome/count-in, note repeat, live erase and other RC2-008 items | Separate capability tasks; never substitute labels or frontend-only state |
| Physical hardware | Firmware implementation, electrical bring-up, USB, debounce, rollover, encoders, LEDs, latency and ergonomics | Execute every NOT RUN row in Hardware testing on the identified PCB/firmware/appliance revisions |

Recommended Task 097 order: (A) shared modal/focus ownership and reachability,
(B) assignment/profile editor presentation closure, (C) contextual/pad matrix
closure, (D) stronger lock/feedback production evidence, then (E) physical
bring-up as soon as firmware and PCB exist. Optional musical algorithms remain
a later milestone rather than blockers disguised as bindings.

## Regression record

Executed on 2026-11-25:

| Command | Result |
|---|---|
| `cmake --build --preset dev -j2` | PASS |
| `ctest --preset dev --output-on-failure` | PASS, 39/39, 0 failed, 0 skipped |
| `cmake --build --preset slint -j2` | PASS, pinned Slint 1.19.0 |
| `ctest --preset slint --output-on-failure` | PASS, 42/42, 0 failed, 0 skipped |
| `cmake --build --preset production -j2` | PASS |
| `ctest --preset production --output-on-failure` | PASS, 42/42, 0 failed, 0 skipped |
| `cmake --build --preset sanitize -j2` | PASS |
| `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure` | PASS, 39/39, 0 failed, 0 skipped |
| `./build/slint/neurogenic_slint_benchmark` | PASS; full median 932901 ns, incremental median 65192 ns, `full_rebuilds=259`, `telemetry_updates=257`, `skipped=255` |
| `nix build path:. --no-write-lock-file` | PASS through system daemon; initial sandbox attempt was blocked by daemon-socket permissions |
| `nix flake check path:. --no-write-lock-file` | PASS, production/headless and configured AArch64 cross-compile checks; native AArch64 execution omitted as an incompatible host system |
| `./result/bin/neurogenic --headless --list-devices` | PASS; Null audio, ALSA raw MIDI, no endpoints discovered |
| `./result/bin/neurogenic-diagnostics --controller-v2-sim-test` | PASS; protocol 1.3, 67 controls, generation 1, input state valid |

The sanitizer initially exposed V2 context feedback indexing a raw wire ID as
a contextual slot. The implementation now translates logical wire IDs through
the bounded context-slot mapping; the targeted regression and full sanitizer
suite pass. No physical validation is included in these pass counts.
