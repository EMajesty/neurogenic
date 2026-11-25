# NEUROGENIC hardware validation report

The intended primary x86 reference appliance is now the ordered G10; Raspberry
Pi 5 remains the secondary AArch64 reference target. Existing N150 criteria are
retained where platform-independent and may still be used for comparison. No
G10 component specifications are inferred. Reference Controller V2 PCB,
firmware and audio hardware are under development, and all physical rows remain
**NOT RUN**.

Copy this file per machine. Default status is **NOT RUN**. PASS requires the
listed physical evidence; simulation is not physical evidence.

## Identity

- Date/operator: NOT RUN
- Commit/build: NOT RUN
- Platform/CPU/kernel: NOT RUN
- Audio/MIDI/controller/display hardware: NOT RUN
- Diagnostic JSON attachment: NOT RUN

## Test matrix

| Area | Preconditions and steps | Evidence / pass criteria | Status |
|---|---|---|---|
| Audio startup | Exact duplex device, 48 kHz; supported 64/128/256/512 periods | Negotiated values; unsupported periods identified; clean start/stop | NOT RUN |
| Audio routing | Route every discovered input/output pair | Recorded map has no substitution/crosstalk | NOT RUN |
| Playback/record | Sample+synth playback and external capture | Expected PCM, no overflow; callback/XRUN evidence | NOT RUN |
| Resampling/FX | Track/group/master resample and available external loop | Expected route/latency; unavailable pairs rejected | NOT RUN |
| Live loop | Quantized capture, handoff, overdub | Frame error and rendered PCM | NOT RUN |
| Sustained load | Heavy project 30 min, then optional soak | Worst callback, XRUNs, CPU, memory, thermals | NOT RUN |
| MIDI input | Keyboard notes/velocity and musical routing | Notes, velocity and latency/jitter evidence | NOT RUN |
| MIDI output | Instrument, clock, transport and passthrough | Correct endpoint/channel; clock capture | NOT RUN |
| MIDI lifecycle | Multiple endpoints, disconnect/reconnect | No substitution; routes recover | NOT RUN |
| Controller | Handshake/profile, pads, encoders, functions/pages | Event and feedback log | NOT RUN |
| Controller recovery | Reconnect and invalid-profile maintenance | Full refresh; recovery succeeds | NOT RUN |
| Touch 1920x1200 | Eight pages, keyboard, picker, workflows | No essential clipping; calibration/readability accepted | NOT RUN |
| Touch 1024x600 | Repeat focused/minimum layouts | Essential actions reachable; scrolling works | NOT RUN |
| Storage | Save/reopen, relink, preset, removable-media removal | Prior valid file survives; expected PCM after recovery | NOT RUN |
| Low storage | Constrain a writable test volume | Explicit failure; no corruption/cleanup | NOT RUN |
| Persistence interruption | Interrupt a disposable save/export | Prior artifact opens; diagnostics retained | NOT RUN |
| Restart/shutdown | Crash burst, clean exit, unexpected shutdown | Bounded restart; dirty protection documented | NOT RUN |

## Results and limitations

- Measurements: NOT RUN
- Failures: NOT RUN
- Unavailable metrics: NOT RUN
- Follow-up issue IDs: NOT RUN
