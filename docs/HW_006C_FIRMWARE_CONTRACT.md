# HW-006C firmware contract

Approved implementation revision: `c163f6ea27a3bfbc6a15e19645a20305749f8688`.
Pin this exact revision for the header and fixtures, rather than a later
moving `main` or documentation-only commit.

HW-006C must vendor or otherwise consume the exact
`include/neurogenic/controller_v2_wire.h` from the approved HW-006B upstream
commit and verify its revision/hash. Do not maintain a divergent handwritten
copy.

Implement protocol major 1, maximum minor 4, with exact minor-1.3 fallback.
Capability bits are push 11 and RGB 12. Push IDs are `0x0160..0x0167`; RGB
destinations reuse `0x0120..0x0127`. Optional inventory records are eight
bytes, canonical push namespace 1 then RGB namespace 2. Counts are eight and
fingerprints are respectively `0x1936cd35` and `0x6b7c76c7`. The canonical
encoding and authoritative vectors are in
`protocol/fixtures/controller_v1_4.json`.

In 1.3 mode firmware must emit the exact six-byte baseline inventory and
13-byte input snapshot, suppress optional events, and reject optional feedback.
Push-enabled 1.4 appends one P1..P8 state byte to the snapshot and sends push
transitions through BUTTON with the host generation. A snapshot never creates
a press. Gaps, overflow, reset, reconnect and heartbeat loss require bounded
state reconciliation and cancellation of stale input.

Accept encoder LED_RENDER only after RGB capability/inventory negotiation.
The host payload is final perceptual RGB/brightness/mode/period. Firmware must
not add a semantic minimum or operational recolor; it owns gamma, calibration,
global/electrical caps, TLC channel mapping and bounded scheduling. RESET or a
new session discards stale RX, TX, render and animation work before the full
refresh.

Firmware conformance must consume both fixture files and cover 1.3 fallback,
push-only, RGB-only, both, unknown bits, wrong/missing/malformed records,
optional press/release, held-on-connect snapshot, invalid snapshot size,
duplicates, gaps, stale generations, overflow, reconnect and explicit off
convergence. Physical USB and illumination/electrical acceptance remain
separate HW-006C/hardware gates.
