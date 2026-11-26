# Appliance benchmark methodology

Compare minimal NixOS, Buildroot and Yocto with the same NEUROGENIC commit,
hardware, firmware, audio/MIDI/controller devices, 48 kHz rate, buffer size,
project, renderer, display mode, ambient conditions and measurement duration.
Record all deviations. Test standard and PREEMPT_RT kernels separately.

Measure cold power-on to usable instrument, idle CPU/RSS, image/root size,
background process count, cyclic scheduling latency and worst jitter, audio
callback distribution/deadline misses, ALSA XRUNs, physical round-trip latency,
GUI frame/input responsiveness, sustained DSP capacity, temperature,
throttling and measurable power.

The stress workload simultaneously plays a fixed deterministic project,
records multichannel audio, sends MIDI and controller traffic, renders
waveforms, changes pages and writes bounded project/recording data. Run idle,
playback and stress phases long enough to expose thermal steady state. Capture
kernel command line, scheduler policy, IRQ/CPU affinity, governor, USB topology,
driver versions and renderer.

Compare desktop, `linuxkms-software` and `linuxkms-femtovg`; compare 256, 128,
64 and 32 frame requests only when the device accepts them. Report failures and
XRUNs rather than excluding them. Distributions are selected from worst-case
measured behavior and maintainability, not average overhead assumptions.
