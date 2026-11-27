# Reference Controller V2 firmware implementation guide

Host semantic color policy is documented in
[`VISUAL_LANGUAGE.md`](VISUAL_LANGUAGE.md). Firmware renders bounded RGB
instructions and does not infer musical meaning from literal RGB.

This is the normative firmware-facing contract for the provisional locked
Reference Controller V2 panel. It specifies logical behavior, not PCB wiring.
GPIOs, switch matrix geometry, encoder pins, LED topology and electrical limits
belong to the board support package supplied with the PCB.

The dependency-free canonical constants are
[`controller_v2_wire.h`](../include/neurogenic/controller_v2_wire.h). Host-side
manifest validation is implemented by `reference_v2_wire_controls()`. Firmware
must not derive identity from array order or physical scan position.

## Version decision

Physical V2 uses **protocol 1.3**. “V2” names the panel design, not the protocol
major. The 1.3 extension preserves the 1.x frame, CRC, byte order and legacy
messages. It adds explicit V2 inventory, host-issued input generation, full
input-state snapshots and LED rendering modes. Devices using 1.0, 1.1 or 1.2
retain their existing descriptors and IDs. A 1.3 V2 host never interprets a
legacy ID as a V2 ID.

## Frame encoding

All integers are little-endian. Frames are packed byte-by-byte; native structure
layout is forbidden.

| Offset | Width | Field |
|---:|---:|---|
| 0 | 2 | magic `4e 47` (`NG`) |
| 2 | 1 | protocol major, `1` |
| 3 | 1 | message type |
| 4 | 2 | payload length, 0–256 |
| 6 | 4 | sequence number |
| 10 | N | payload |
| 10+N | 2 | CRC-16/CCITT-FALSE over bytes 0 through 9+N |

CRC parameters are polynomial `0x1021`, initial value `0xffff`, no reflection,
no final XOR. Maximum frame size is 268 bytes. A decoder retains partial frames,
accepts concatenated frames, discards bytes until the magic is recovered, and
rejects excessive lengths or invalid CRCs without allocating unbounded storage.

Message types remain 1 `HELLO`, 2 `HELLO_REPLY`, 3 `CAPABILITIES`, 4 `BUTTON`,
5 `ENCODER`, 6 `ANALOG`, 7 `LED`, 8 `CONTEXT`, 9 `STATE_SYNC`, 10 `HEARTBEAT`,
11 `ERROR`, 12 `RESET`; 1.3 adds 13 `INVENTORY`, 14 `INPUT_STATE`, 15
`LED_RENDER`, and 16 `DIAGNOSTIC`.

## Connection state machine

1. Firmware enumerates USB CDC but does not emit musical input.
2. Host sends `HELLO {major:u8, highest_minor:u8}`.
3. Firmware replies `HELLO_REPLY {major, minor, id_len, fw_len, id, firmware}`.
   Device identity is exactly `neurogenic.reference-controller-v2`.
4. Host requests `CAPABILITIES`; firmware returns a `u32` mask including
   buttons, relative encoders, RGB, contextual feedback, heartbeat, V2 inventory,
   input state, LED rendering and diagnostics as actually supported.
5. Host requests `INVENTORY`; firmware returns `{count:u16=67,
   fingerprint:u32=0x3256434e}`.
6. Host validates the descriptor/profile, chooses a nonzero input generation and
   sends `STATE_SYNC {generation:u32}`.
7. Firmware replies `INPUT_STATE {generation:u32, bitmap[9]}`. Bits correspond
   to canonical manifest positions and describe physical buttons only.
8. Only after the snapshot is accepted does the host enter connected state and
   accept musical input. It then sends a complete feedback refresh.

Wrong identity, unsupported version, missing capability, wrong count/fingerprint
or stale generation fails closed. Firmware reboot, USB reconnect and explicit
`RESET` restart this state machine.

## Input messages

`BUTTON` is `{generation:u32, wire_id:u16, pressed:u8}`. Firmware reports one
debounced transition for both press and release. Buttons already down during
state synchronization are quarantined by the host: the snapshot does not trigger
them, and their first release only clears quarantine. Firmware does not interpret
SHIFT, chords, tap/hold, page navigation or musical actions.

`ENCODER` is `{generation:u32, wire_id:u16, delta:i16}`. Positive is clockwise
and negative counter-clockwise when viewed from the panel. Zero is ignored.
Firmware may aggregate detents while preserving signed direction and clamping to
the `i16` range. Quadrature decoding belongs to firmware; acceleration, fine
adjustment and musical scaling belong to the host and must not be applied twice.
Protocol 1.3 and the normative 67-control fingerprint have no encoder-push
identities or dedicated encoder-RGB destinations. Hardware may wire eight
optional P-encoder switches and separate RGB channels, but firmware must not
report them under existing IDs. A future capability-negotiated inventory
extension will define eight stable push identities, encoder RGB feedback,
legacy behavior, and a new tested fingerprint. Protocol 1.3 firmware remains
compatible by omitting those capabilities.

Input frame sequences are monotonically increasing modulo `2^32`. Duplicate or
old-generation input is rejected. A gap cancels host gesture ownership and
requests a new input snapshot. A snapshot is reconciliation, not a series of
press events. Lost releases therefore cannot strand SHIFT, Fill, Performance
momentaries, held-step edits or navigation repeat.

## Complete logical wire inventory

Every range is inclusive and expands one-for-one; for example `pad.1` is
`0x0100` and `pad.16` is `0x010f`. All buttons accept `BUTTON` and `LED_RENDER`;
encoders accept `ENCODER` and contextual value feedback. Physical LED mapping is
not encoded here.

| Logical controls | Wire IDs | Type | Input | Feedback |
|---|---|---|---|---|
| `pad.1` … `pad.16` | `0x0100` … `0x010f` | button | BUTTON | LED_RENDER |
| `page.1` … `page.8` | `0x0110` … `0x0117` | button | BUTTON | LED_RENDER |
| `function.1` … `function.8` | `0x0118` … `0x011f` | button | BUTTON | LED_RENDER, CONTEXT |
| `parameter.1` … `parameter.8` | `0x0120` … `0x0127` | relative encoder | ENCODER | CONTEXT |
| `data` | `0x0128` | relative encoder | ENCODER | CONTEXT |
| `navigation.back/up/enter/left/down/right` | `0x0130` … `0x0135` | button | BUTTON | LED_RENDER |
| `edit.mute/solo/arm/track/page/erase/undo/copy/shift/select` | `0x0140` … `0x0149` | button | BUTTON | LED_RENDER |
| `transport.click/tap/quant/record-mode/fill/repeat/loop/record/stop/play` | `0x0150` … `0x0159` | button | BUTTON | LED_RENDER |

The individual named constants for all non-numbered controls and group endpoints
are in the canonical C header. Host validation proves 67 unique logical and wire
identities and rejects descriptor identities absent from the manifest.

## Heartbeat and recovery

The host sends `HEARTBEAT {monotonic_ms:u64}` every 500 ms while negotiating or
connected. Firmware echoes the payload. Two seconds without a valid heartbeat
is a confirmed loss: the host cancels ownership, marks disconnected and requires
renegotiation. These defaults are host policy and can be tuned after physical
USB testing. Firmware should enter its safe unowned feedback state after the
same loss window. Counters include received/sent frames, sequence gaps, stale and
duplicate input, state synchronizations, heartbeat count and queue overflow.

## LED rendering and semantic palette

`LED_RENDER` is `{wire_id:u16, rgb:u32, brightness:u8, mode:u8, period_ms:u16}`.
RGB is `0x00RRGGBB`; brightness is semantic 0–255 before caps. Modes are off 0,
steady 1, dim 2, blink 3 and pulse 4. A zero period selects the firmware default.
Firmware renders patterns locally and must fall back to steady for an unknown
mode. Host changes are revisioned/coalesced; queue overflow requests a full
refresh, and convergence to the newest authoritative state is more important
than intermediate animation states.

Reference semantics are dim neutral inactive, white selected, green active,
amber queued, red armed/recording, blue modifier and purple lock/special. The
host resolves precedence: recording > queued > modifier/lock > playing/enabled >
selected > available > off. Critical recording/waiting state also uses mode or
screen text, never color alone. Exact RGB and timing are presentation policy,
not musical state.

User brightness, host cap and semantic brightness are distinct. Firmware must
apply the board-specific electrical cap and map one logical control to any
number of physical LEDs. No current, LED count or chain ordering is specified.

## Diagnostics

Diagnostic mode is host-negotiated through message 16 and never dispatches its
inputs to the musical session. It should report firmware/device/protocol,
inventory, physical button bitmap and transition counters, encoder deltas and
decode errors, queue overflow, USB state and reset reason where available. It
should accept bounded commands for one logical LED, RGB channels, brightness and
an electrically capped all-LED test. Entry/exit is a protocol command; no boot
button or jumper is assumed.

Host-side simulated diagnostics:

```console
neurogenic-diagnostics --controller-v2-sim-test
```

This uses production framing, negotiation and host/session code without audio or
physical hardware. It is not physical validation.

## Golden vectors

Spaces may be inserted for readability; the bytes below are contiguous hex and
include CRC. They were independently calculated from the field definitions.

| Message | Complete frame hex |
|---|---|
| HELLO | `4e47010102000100000001037569` |
| INVENTORY | `4e47010d06000200000043004e435632001e` |
| page.1 press, generation 7 | `4e470104070003000000070000001001019f26` |
| page.1 release | `4e47010407000400000007000000100100031f` |
| P1 delta +3 | `4e470105080005000000070000002001030080af` |
| P1 delta -2 | `4e470105080006000000070000002001feff72d1` |
| empty INPUT_STATE generation 7 | `4e47010e0d000700000007000000000000000000000000a0fe` |
| green steady pad.1 | `4e47010f0a0008000000000100ff000080010000ffab` |
| HEARTBEAT 1000 ms | `4e47010a080009000000e8030000000000007ac2` |
| RESET | `4e47010c00000a000000b639` |

An invalid-CRC vector is produced by changing the last byte of any vector. An
invalid-length vector declares 257 bytes (`01 01`) and is rejected before
payload allocation. Unknown control IDs and stale generations are valid frames
with invalid semantics and must not dispatch.

## Firmware main-loop pseudocode

```text
initialize_board_abstraction()
state = waiting_for_hello
loop:
    scan_and_debounce_buttons()
    decode_encoder_detents()
    service_usb_rx_with_bounded_decoder()
    if negotiation_message: advance_state_or_fail_closed()
    if state == synchronized:
        enqueue_bounded_button_transitions_with_generation()
        enqueue_bounded_encoder_deltas_with_generation()
    if state_snapshot_requested: send_bitmap_without_synthesizing_presses()
    apply_latest_led_render_commands_through_board_abstraction()
    render_blink_and_pulse_locally()
    enforce_firmware_electrical_brightness_cap()
    service_usb_tx_without_blocking_scanning()
    if heartbeat_timeout: clear_host_ownership_and_show_safe_feedback()
```

Queues must be fixed-capacity or otherwise strictly bounded. Overflow increments
a diagnostic counter; input overflow prompts state synchronization, while output
overflow retains the newest final rendering state for later refresh.

## Conformance procedure

1. Compile the dependency-free header into firmware and validate all 67 IDs.
2. Run host golden-vector and malformed-frame tests.
3. Run the byte-stream firmware simulator acceptance test.
4. Run `neurogenic-diagnostics --controller-v2-sim-test`.
5. On hardware, execute the electrical, standalone, protocol, musical and
   physical checklist in [Hardware testing](HARDWARE_TESTING.md).

Simulator and pseudo-terminal results are software evidence only. MCU, USB,
debounce, rollover, encoder, illumination, latency and electrical validation
remain NOT RUN until measured on the manufactured controller.
# Task 098C host semantics

REPEAT and ERASE remain ordinary protocol-1.3 button transitions. Firmware must
not schedule repeats, infer triplets, follow the playhead, or erase events.
Those semantics and release recovery remain host-owned. No wire ID, inventory
fingerprint, or capability negotiation changes are introduced.
## HW-006B protocol 1.4 handoff

The normative host extension is now frozen in `controller_v2_wire.h`,
`CONTROLLER_PROTOCOL_1_4.md`, `HW_006C_FIRMWARE_CONTRACT.md` and the two JSON
fixture files under `protocol/fixtures`. Firmware must mutually select minor
1.3/1.4, validate optional records before use, suppress extensions in 1.3 and
retain baseline bytes exactly. Host rendering is perceptual; firmware owns
calibration, gamma and electrical/physical output policy. HW-006C and physical
USB/RGB validation remain pending.
