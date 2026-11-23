# Dedicated controller hardware boundary

## Confirmed host contract

Protocol 1.2 describes 16 performance buttons, eight contextual encoders, one
main data encoder, eight contextual function buttons, eight page buttons, ten
editing positions, ten transport/performance positions and six navigation
buttons. Encoders have no push switches. The dedicated profile has no faders,
crossfader or other analog controls. Unassigned cluster positions produce valid
events and no invented semantic action.

The host addresses controls and RGB feedback by logical IDs only. It applies
semantic actions through `ApplicationService`, publishes the same 8+8 context
used by SDL3 and Slint, caps brightness, suppresses unchanged feedback and sends
a full state after reset/reconnect.

## Current hardware assumptions

An RP2040/Pico scans modular daughterboards. Buttons and mechanical encoder
contacts use 74HC165 shift registers with shared clock and parallel-load signals
and one serial return per daughterboard; some boards chain two registers. Eight
contextual encoders consume sixteen quadrature contacts, and the main data
encoder is separate. Addressable RGB devices form a daisy chain across boards.
The RGB data path is level-shifted from 3.3 V to 5 V. USB powers the Pico, Pico
3.3 V powers shift-register logic, and a separate buck converter supplies LED
5 V from the global 12 V system.

Scan bits, GPIOs, connectors and LED-chain offsets are not protocol identities.
Firmware translates physical scanning into logical events and logical LED IDs
into its current chain order, permitting daughterboard and layout revisions.

## Not implemented

RP2040 firmware, GPIO allocation, scan timing, debounce parameters, quadrature
decoding, bootloader behavior and electrical protection are not implemented.
A future dockable MIDI fader controller is a separate MIDI device and is not
part of this dedicated-controller capability profile.

## Physical validation still required

Hardware testing must verify scan integrity under simultaneous transitions,
encoder loss at high speed, USB reconnect/reset, LED current and thermal limits,
level-shifter margins, and full-chain refresh timing. Qanba RGB assemblies and
SK6812MINI-E devices must not be assumed to share electrical timing until that
combination is measured on the physical chain.
