# Dedicated controller protocol

## Task 097D convergence evidence

The protocol-1.3 firmware fixture now constrains host output, forces a partial
refresh, drains backpressure, and compares the final logical LED array with an
unconstrained authoritative projection. The same comparison is repeated after
disconnect and renegotiation. This is byte-stream simulator evidence; physical
USB, MCU queues and LEDs remain **NOT RUN**.

Physical Reference Controller V2 uses protocol **1.3**. V2 is a panel revision,
not a protocol-major number: 1.3 preserves the 1.x frame and legacy behavior
while adding explicit inventory, a host-issued input generation, full input
state recovery and bounded LED rendering. The normative firmware contract and
golden vectors are in
[REFERENCE_CONTROLLER_V2_FIRMWARE.md](REFERENCE_CONTROLLER_V2_FIRMWARE.md);
portable constants are in
[`controller_v2_wire.h`](../include/neurogenic/controller_v2_wire.h).

Input connection generations and gesture ownership are host-side in Task 096A;
the wire format is unchanged. Firmware continues sending stable logical
identities and press/release events. See [Input gestures](INPUT_GESTURES.md).

Task 096B adds the host-side descriptor
`neurogenic.reference-controller-v2.v1`. Its 67 stable logical identities are
eight `page.N`, eight `function.N`, eight `parameter.N`, `data`, six
`navigation.*`, ten `edit.*`, ten `transport.*`, and sixteen `pad.N` controls.
The 1.3 manifest assigns separate 16-bit wire IDs to these names. GPIOs, matrix
positions and LED-chain offsets remain firmware board-abstraction details.
Existing protocol 1.x IDs are not reinterpreted.

## Transport decision

The first transport is USB CDC ACM serial at 115200 baud. CDC was selected
over USB HID because the controller needs bidirectional capability discovery,
version negotiation, variable contextual labels, diagnostics, and full-state
refreshes. CDC provides one ordered byte stream without encoding those
operations into a fixed HID report descriptor. Linux exposes it through the
standard TTY API, so the host adds no library dependency.

HID remains a possible later transport. Framing, protocol state, semantic
dispatch, and feedback live above `ControllerSerialTransport`; an HID adapter
can carry the same packets without changing the musical engine.

The application opens only the path supplied with `--controller PATH`. It does
not scan for or substitute another serial device. `--controller-id ID` also
requires an exact negotiated identity. Production should use both and a stable
`/dev/serial/by-id/...` path or an explicitly configured udev alias.

## Wire format

All integers are little-endian. Messages are bounded to 256 payload bytes and
268 total bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic `0x474e` (`NG` on the wire) |
| 2 | 1 | protocol major, currently 1 |
| 3 | 1 | message type |
| 4 | 2 | payload length, 0–256 |
| 6 | 4 | sender sequence number |
| 10 | N | payload |
| 10+N | 2 | CRC-16/CCITT-FALSE over all prior bytes |

The decoder resynchronizes after malformed bytes, rejects oversized and
CRC-invalid frames, and retains incomplete frames. A major-version mismatch is
fatal for that connection. Protocol 1.3 is current. A 1.0 device remains valid
and receives the original label-only `CONTEXT` payload; 1.1 retains its extended
context and historical inventory. Version 1.2 changes no framing or message
encoding. Protocol 1.3 adds V2 inventory/state/render messages and does not
change legacy packet interpretation.

## Messages and control IDs

Stable message IDs are `HELLO=1`, `HELLO_REPLY=2`, `CAPABILITIES=3`,
`BUTTON=4`, `ENCODER=5`, `ANALOG=6`, `LED=7`, `CONTEXT=8`, `STATE_SYNC=9`,
`HEARTBEAT=10`, `ERROR=11`, and `RESET=12`. Protocol 1.3 adds
`INVENTORY=13`, `INPUT_STATE=14`, `LED_RENDER=15`, and `DIAGNOSTIC=16`.

- `HELLO`: host major and minor (`u8`, `u8`).
- `HELLO_REPLY`: major, minor, device-ID length, firmware-string length, then
  UTF-8 identity and firmware bytes.
- `CAPABILITIES`: `u32` mask: buttons (bit 0), relative encoders (1), optional
  analog inputs (2), RGB (3), contextual feedback (4), heartbeat (5), and the
  Task 068 inventory (6). The current dedicated surface does not set bit 2.
- `BUTTON`: ID (`u16`) and pressed (`u8`). Both transitions are sent. See the
  stable control table below.
- `ENCODER`: ID (`u16`) and signed delta (`i16`). IDs 0–7 are contextual
  encoders; ID 8 is the main data encoder. None has a push switch.
- `ANALOG`: ID (`u16`) and raw `u16`, retained for optional/legacy controllers.
  The current dedicated surface advertises no analog capability.
- `LED`: ID (`u16`), RGB (`0x00RRGGBB`, `u32`), brightness (`u8`).
- `CONTEXT` 1.0: ID (`u16`), active (`u8`), label length (`u8`), label.
- `CONTEXT` 1.1: ID (`u16`), flags (`u8`: active, bipolar, pickup), signed
  pickup direction (`i8`: -1 down, 0 acquired, +1 up), feedback revision
  (`u64`), then five `u8 length + UTF-8 bytes` fields: name, formatted value,
  unit, minimum and maximum. Names are truncated to 24 bytes, values to 16,
  units to 8, and bounds use short numeric formatting.
- `STATE_SYNC`: requests a complete feedback refresh.
- `HEARTBEAT`: opaque bounded payload echoed by the host.
- `ERROR`: bounded diagnostic data; never audio-callback logging.
- `RESET`: device reboot indication; negotiation restarts.
- `INVENTORY`: control count (`u16`) and manifest fingerprint (`u32`). V2 must
  report 67 and `0x3256434e`.
- `INPUT_STATE`: generation (`u32`) and a nine-byte bitmap for the 58 V2
  buttons. It reconciles ownership but never synthesizes presses.
- V2 `BUTTON`: generation (`u32`), wire ID (`u16`), pressed (`u8`). V2
  `ENCODER`: generation, wire ID and signed detent delta (`i16`).
- `LED_RENDER`: wire ID, RGB, brightness, rendering mode and period. Supported
  modes are off, steady, dim, blink and pulse; temporal rendering is local.

## Host behavior

`ControllerHost` negotiates and validates packets, converts wire IDs to logical
input events, and forwards them to a `ControllerSession`. The session profile,
not the protocol decoder, selects semantic actions or parameters. Generic
feedback is translated back to existing 1.x packets. It never owns audio-engine
objects. Input and output queues are bounded and expose drop counters. Serial
I/O runs on a controller worker, never the audio callback. The full controller
architecture and MIDI adapter are in
[CONTROLLER_INTEGRATION.md](CONTROLLER_INTEGRATION.md).

The negotiated protocol selects a descriptor before the mapping profile is
validated. Protocol 1.3 requires the V2 inventory/input-state capabilities,
validates the exact identity/count/fingerprint, installs the V2 profile, issues
a connection generation, and requires an input snapshot before accepting
musical input. Protocol 1.2 retains the Task 068 descriptor; 1.0/1.1 select
explicit legacy descriptors. Profile controls never
replace or extend that inventory. Unknown physical IDs are rejected; known but
unassigned controls remain harmless.

Performance modes remain host-authoritative. RGB updates show selection and
playhead state, and state revisions suppress duplicate feedback. Reconnection
negotiates before a full refresh. Raw analog values stay separate from
normalized values. Soft takeover compares calibrated physical position to the
effective parameter target. Direction is observable; acquisition occurs within
the calibrated dead zone. Scene/automation changes invalidate acquisition when
the control is no longer near the new target. Firmware reports raw detents;
Shift selects 0.1× fine steps and deltas of four or more receive bounded 2×
host acceleration for contextual and data encoders.

Named contexts select explicit parameters for sequencer, sample, instrument,
mixer, routing and modulation pages; they never depend on registry order. Every
control has an explicit target or is visibly inactive. Shift maps the first
lower buttons to Undo/Redo. GUI and serial use `build_controller_context()`.

## Stable control ID table

| IDs | Control |
|---:|---|
| 0–15 | performance grid, physical 2×8 |
| 16–23 | contextual lower buttons 1–8 |
| 24–35 | legacy 1.1 Play, Stop, Record, Shift, Select, data-push, modes and page pair |
| 36–43 | page-selection buttons 1–8 |
| 44–53 | editing-cluster buttons 1–10 (unassigned positions remain inert) |
| 54–63 | transport/performance-cluster buttons 1–10 (unassigned positions remain inert) |
| 64–69 | navigation Up, Down, Left, Right, Confirm, Back |

Encoder IDs 0–7 are contextual controls and 8 is the main data encoder. The
1.2 controller sends no encoder-button event. IDs 24–35 are not reinterpreted:
they remain available to 1.1 devices, while new firmware uses the 1.2 ranges.
Protocol 1.0 retains its historical 24–27 mode-button interpretation.

Grid LEDs show programmed steps, playhead, selection, pad assignment and
velocity/note modes. Dedicated LEDs show mode, Play, Record, Shift and Select.
Only changes are sent, brightness has a configurable host cap (224 default),
and queue overflow forces a later full refresh rather than accepting a partial
state. Cache revisions advance only after queue acceptance; transport
backpressure retains pending session feedback. Local context and modifier
revisions trigger updates even without an application revision. LED IDs are
logical control IDs; firmware maps them to chain offsets.

## Simulator and diagnostics

`SimulatedController` uses the same session and application dispatch as serial.
Tests negotiate legacy and current versions, exercise every 1.2 control group,
reject absent analog controls, receive RGB/context packets, reconnect and
saturate feedback queues.

```sh
./build/dev/neurogenic-diagnostics --controller-protocol-test
./build/dev/neurogenic --gui \
  --controller /dev/serial/by-id/usb-NEUROGENIC_Controller
```

`--controller` is repeatable. Repeated `--controller-id` values correspond by
index and require exact identity; no unknown serial device is auto-selected.

## Firmware requirements

Firmware must implement framing/CRC, bounded receive storage, negotiation,
stable identity, capabilities, press/release events, signed encoder deltas,
RGB/brightness output, heartbeat, reset notification and full output refresh.
Analog reports are optional. Firmware owns switch debouncing, quadrature
decoding and logical-ID-to-scan/LED mapping, but must not infer musical behavior.

USB descriptors, pin mapping, electrical LED limits, bootloader and firmware
update behavior are outside this task. Current limitations: no HID adapter,
calibration is configured through the host API rather than persisted, hotplug
uses bounded path polling rather than udev notifications, and physical USB
latency/reliability is unmeasured.
## Profile activation boundary

The appliance profile library does not alter protocol 1.3 negotiation.
Dedicated USB descriptors are still selected only after identity/inventory
validation. Task 097F's live profile activation operation applies to MIDI
endpoints; dedicated USB runtime switching remains an explicit follow-up and
must preserve connection epochs and full-state synchronization.
