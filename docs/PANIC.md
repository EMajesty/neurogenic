# Unified panic

`transport.panic.v1` is the authoritative presentation-neutral emergency
silence operation. Reference Controller V2 invokes it with SHIFT+STOP; direct
clients and alternative mappings may invoke the same semantic action.

Panic differs from ordinary STOP. It is delivered by an atomic, coalescing
request observed at the next audio callback even when the ordinary transport
command queue is saturated. The callback stops transport, resets internal
sample and synth voices, clears queued pad/audition triggers, clears automation
and Fill ownership, cancels count-in click state, and invokes the existing MIDI
panic scheduler. A monotonic acknowledgement counter exposes completion. The
software response boundary is one active audio callback; physical latency is
NOT RUN.

MIDI panic queues note-off for all notes plus channel-mode All Notes Off, All
Sound Off, and sustain release on configured output ports. Hardware may ignore
standard MIDI channel-mode messages, so physical silence cannot be guaranteed
without device testing.

The application cancels an armed/count-in recording. An active audio capture
is stopped through the normal recorder, retaining captured material. Panic
does not delete projects, samples, patterns, live-loop material, mappings, or
history. Playback resumes only through an explicit Play/Restart request, which
prevents the running scheduler from immediately retriggering notes.

Panic remains a permitted modal safety operation. Controller gesture ownership
is generation-cancelled through the existing session recovery paths; stale
releases cannot create a new press.
