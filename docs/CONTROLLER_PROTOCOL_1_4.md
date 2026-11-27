# Controller protocol 1.4

Protocol 1.4 is the normative, negotiated Reference Controller V2 extension.
It was approved against NEUROGENIC starting revision
`baa87d2a6f0d25f8c1afa85cc929a8da8b29c299` after consulting the HW-006A
controller specification at
`cc0cbc42a7b775a9de6964e2545192876437483b`. Protocol 1.3 remains the exact
mandatory fallback.

## Negotiation and compatibility

The host sends `{major=1, highest_minor}`. The controller replies with major 1
and the highest minor supported by both endpoints. A maximum-1.3 endpoint and
a maximum-1.4 endpoint therefore select 1.3. Different majors are rejected.
Unknown capability bits are masked and do not imply an inventory namespace.
Optional behavior is enabled only after its capability and exact inventory
record both validate. A failed optional inventory makes that session
incompatible; it is not silently downgraded in place.

Minor 1.3 continues to use the exact six-byte inventory
`43 00 4e 43 56 32`, the 67-control fingerprint `0x3256434e`, the 13-byte
input snapshot, all existing wire IDs, framing and CRC.

## Capabilities and identities

| Capability | Bit | Meaning |
|---|---:|---|
| `CONTEXT_ENCODER_PUSH` | 11 | eight optional button inputs |
| `CONTEXT_ENCODER_RGB` | 12 | eight optional LED_RENDER destinations |

Push identities are `parameter.1.push` through `parameter.8.push`, wire IDs
`0x0160..0x0167`. They are independent buttons and unassigned in the shipped
profile. The RGB destinations reuse `parameter.1..8` IDs `0x0120..0x0127` in
the LED_RENDER destination namespace. DATA push remains the existing
navigation Enter input and is not duplicated.

## Conditional inventory

The six-byte baseline inventory is followed, in minor 1.4 only, by one
eight-byte record for every advertised known option:

`namespace:u8, count:u8, entry_size:u8=0, reserved:u8=0, fingerprint:u32-le`.

Canonical order is push namespace 1 followed by encoder-RGB namespace 2.
Duplicate, missing, unadvertised, reordered, wrong-length or wrong-fingerprint
records are rejected. Push has count 8 and fingerprint `0x1936cd35`; RGB has
count 8 and fingerprint `0x6b7c76c7`. These are CRC-32/ISO-HDLC values over
the canonical HW-006A bytes: UTF-8 identity, NUL, little-endian ID and kind
(button 1, RGB destination 2). The maximum both-option inventory payload is
22 bytes and its framed size is 34 bytes.

## Input and reconciliation

Optional pushes reuse BUTTON: generation `u32`, wire ID `u16`, pressed `u8`.
The baseline input snapshot remains generation plus nine canonical bitmap
bytes. Push-enabled sessions append one P1..P8 bitmap byte, producing 14
payload bytes (26 framed); RGB-only sessions retain 13 payload bytes.
Snapshots quarantine held controls and never synthesize presses. Duplicate
input is dropped, sequence gaps cancel ownership and request a new snapshot,
stale generations are rejected, and reconnect clears old gestures and queues.

## Encoder rendering

Encoder illumination reuses LED_RENDER unchanged. The host sends destination,
final perceptual RGB, final perceptual brightness, temporal mode and period.
The host owns descriptor normalization, functional/operational precedence,
assignment availability, the assigned minimum brightness (31/255, about 12%)
and explicit off. Firmware owns interpolation, calibration, gamma, global
brightness, electrical current limiting, physical channel mapping and output
scheduling. Neither side applies the semantic minimum or gamma twice.

Unipolar values scale minimum-to-maximum, bipolar values scale outward from
center, Boolean off retains assigned-minimum visibility while Boolean on is
bright, and enumerations use constant assigned brightness. Unassigned and
unavailable controls are off. Locked state uses the existing lock override;
other functional hues follow `VISUAL_LANGUAGE.md`.

Output remains bounded and latest-state convergent. Intermediate values may be
coalesced, but explicit off and a full reconnect refresh cannot be permanently
lost. Baseline sessions never receive optional destinations.

## Normative fixtures

- `protocol/fixtures/baseline_v1_3.json`: the ten imported HW-006A baseline
  frames, byte-for-byte unchanged.
- `protocol/fixtures/controller_v1_4.json`: negotiated inventory, push,
  extended snapshot and encoder LED_RENDER vectors.

`include/neurogenic/controller_v2_wire.h` is the dependency-free normative
contract. Physical USB interoperability, MCU firmware and illuminated hardware
remain **NOT RUN**.
