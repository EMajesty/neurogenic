# Architecture, ownership and real-time audit

## Ownership

- `ApplicationService` remains the musical command/state authority. Slint,
  SDL3, controllers and MIDI submit semantic commands.
- `TimelineService` owns editable timeline/history and publishes prepared plans.
- `SampleManager` publishes immutable versions; voices retain old versions and
  control-side acknowledgement/reclamation updates active/pending metadata.
- Mixer, FX and modulation publish bounded prepared configurations.
- Controller sessions own modifiers, context, pickup and feedback; protocol
  adapters own transport translation.
- Slint local state owns only presentation choices such as subview, page,
  selection, scroll, keyboard and picker generation.

The main duplicate musical representation is `Track::pattern` versus reusable
`PatternDefinition::tracks`. It is intentional legacy compatibility, but the
current editor makes it a conflicting authority. Resolve it incrementally with
an explicit edit target and conversion/migration rules, not a rewrite.

## Callback audit

Inspected engine scheduling, sample voice/version, mixer/FX/modulation,
recording/live-loop, MIDI and publication paths. Existing designs use bounded
arrays/queues and prepared control-side objects. No new callback allocation,
mutex, filesystem or logging violation was confirmed. Reference-counted sample
handles can release ownership from voices, but version reclamation is explicitly
deferred to the non-audio side.

Important bounded failure behavior includes recording overflow counters,
launcher/voice capacity rejection, queue failure diagnostics and prepared
handoff rejection before capture. This is source/test evidence, not proof under
all allocators or physical backends; RT instrumentation/soak on both targets is
NOT RUN.

## Controller concurrency

Task 086's two-phase strategy is present: coordinator collections are copied as
`shared_ptr` under `mutex_`, then serial polling, MIDI observation and feedback
sending occur after release. `shutdown()` swaps endpoint vectors before closing
transports. Endpoint-local locks separate operation and diagnostic state.
Deterministic tests exercise failed/reentrant send and shutdown. Remaining risk:
a transport operation may itself block, delaying that poll/shutdown call, but it
does not hold the global coordinator lock.

## Frontend publication

Task 082 preserves Slint model identity and row-updates changed structures.
Telemetry can update without replacing unrelated models, and inactive pages are
not republished wholesale. However every `refresh()` still obtains a full
`ApplicationState` copy and structural fingerprints are computed from that copy;
encoder gestures call full discovery to recover one descriptor. On this host,
the Slint benchmark reported:

| path | median | p95 | p99 | max |
|---|---:|---:|---:|---:|
| idle refresh | 65.6 us | 103.3 us | 121.2 us | 326.8 us |
| full refresh | 92.8 us | 137.0 us | 151.5 us | 155.7 us |
| incremental | 63.8 us | 112.9 us | 117.7 us | 124.1 us |

Debug x86_64 Ryzen 7 7800X3D, simulated devices; not N150/Pi evidence.

## Filesystem/preset responsiveness

Browser enumeration is on one `jthread`, superseded by generation, capped at
8192 entries and paged in 128-row chunks. It parses preset metadata off the UI
thread. Cancellation is cooperative between entries. Destruction calls
`request_stop()` then `join()`; an uninterruptible filesystem/preset load can
therefore delay shutdown. Preset/project mutations are still synchronous
control-side filesystem operations and should not be invoked on the audio
thread; their UI latency is not benchmarked.

## Performance sample

`neurogenic_benchmark --iterations 200` on the same debug host reported no
deadline overruns in its integrated simulation. Representative 256-frame costs:
32 sample voices 112 us; dense timeline 394 us; integrated 8 sample + 4 synth +
modulation/aux/record/MIDI 239 us; 16 loop-stretched region voices 1.67 ms.
These are reproducible desktop measurements only.
