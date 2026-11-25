# Performance macros

Task 096E allows a banked continuous assignment to address
`macro.<stable-id>.value`. Relative movement is routed through the macro
service, so its existing multi-destination ranges and modulation publication
remain authoritative; Performance banks do not copy macro assignments or
values. See [Performance banks](PERFORMANCE_BANKS.md).

Performance macros are persistent, normalized control sources compiled into the
existing modulation graph. A macro has a stable 64-bit identity, a distinct
stable modulator-instance identity, a project/track/group/master owner, a base
and default value, descriptive metadata, and bounded parameter assignments.
The default project budget is 32 macros, 128 assignments and 32 scenes.

## Value composition and mappings

Each assignment names an existing numeric parameter destination and maps the
macro's 0–1 value into a native-unit minimum/maximum range. Linear,
exponential (`x²`), logarithmic (`sqrt(x)`), smooth S-curve, stepped and
inverted ranges are supported. Assignment output is expressed as a delta from
the macro default, so the existing rule remains:

`effective = clamp(automation-or-base + sum(modulation and macro deltas))`

Multiple macros targeting one destination sum in stable route order. They do
not overwrite base values, parameter locks or automation. Topology, files,
processor types and resource-owning parameters are not valid destinations.
Track-local assignments require an unambiguous instance-aware destination.

Macros are also discoverable `native.performance-macro.v1` modulator instances.
Consequently they can feed DSP or modulator parameters and can receive
descriptor-approved modulator-to-modulator input through the same acyclic
graph. Timeline automation addresses a macro as
`macro.<source-instance-id>.value`; only semantic macro values are recorded,
not every downstream assignment.

## Scenes and morphs

A performance scene has a stable ID, name, captured macro values, and a default
morph preference. Recall prepares fixed-size commands on the control thread;
the audio callback executes them against absolute transport frames. Durations
may be seconds or musical quarter-note units. Linear, exponential, logarithmic
and S-curve interpolation are supported. Up to 32 independent macro morphs may
run concurrently. Recalling a macro already in flight replaces that macro's
morph; Stop leaves it at its current effective value. Missing targets are
reported and skipped while valid targets continue, and a recall with no valid
targets fails.

Timed values update at a fixed 32-frame control interval. A scheduled start is
therefore observed no later than 31 transport frames after its exact boundary
(about 0.65 ms at 48 kHz); completion and progress use the authoritative frame
position rather than callback boundaries. Seconds durations count transported
frames. Beat durations integrate the current authoritative tempo, so fractional
BPM and mid-morph tempo changes are followed without resetting progress.
External synchronization uses the externally followed frame range and tempo.
Timed morphs freeze on Stop and resume on Continue. Backward seeking preserves
the completed fraction and rebases the remaining morph; seeking beyond a
scheduled start catches up from the requested boundary. Immediate recalls apply
at the first processed sample even while transport is stopped.

Manual macro changes and recorded macro automation share the ordered DSP command
queue with morph commands. The most recently processed semantic value interrupts
the active morph for that macro; a later scene recall replaces it. Automation is
stored only for the macro value, never expanded into downstream assignments.
The callback queue has 128 usable command slots and exposes overflow counts.

Scene definitions are separate from existing routing scenes, so loading an old
routing scene does not silently change macro values. Morph runtime progress,
oscillator phases and command-queue state are transient and are not persisted.

## Discovery and persistence

`ApplicationState::performance_macros` is a versioned copied snapshot containing
identities, owners, names, current/default values, assignments, curves, scenes,
morph progress, capacities and validation diagnostics. This is presentation
neutral: UI Studio may render the same macro as a slider, knob, button, XY axis
or custom SVG widget.

Project format 5 adds `macros.ngm` to each committed generation. Macro metadata
version 2 persists each scene's interpolation curve; version 1 loads with the
linear default. Active morph progress, scheduled frames and smoothing state are
transient. Older projects load an empty macro collection without changing their
sound. Invalid owners, destinations, duplicate IDs and over-budget data are
rejected before audio publication.

## Standalone authoring

The FX Macros subview creates and names macros through the shared touchscreen
keyboard, adjusts their normalized performance value, and assigns the selected
descriptor-approved FX parameter without exposing internal IDs. Assignments
retain stable IDs and display destination and native range; their range can be
inverted or the assignment removed. One macro may address multiple supported
synth, sample, mixer, external-FX, native-FX or modulator destinations through
the shared destination resolver.

The scene list captures current macro values, recalls them through prepared
callback commands, explicitly updates a capture, and deletes it. Scene capture
does not include clips, patterns, routing or complete project state. Macro and
scene authoring is independent of controller mapping; profiles bind physical
controls to the same semantic objects.

Current limitations: scene capture stores macro values (the optional numeric
parameter list is reserved), assignment-specific smoothing metadata is
persisted but DSP destinations currently use their existing smoothing, recorded
automation segments remain discrete. Macro assignment and scene authoring uses
the shared application history; live macro values and scene recall remain
performance state. The reusable callback-safe
`ParameterSmoother` supports bypass, linear ramps and exponential smoothing for
future destinations; it is not stacked on processors that already smooth their
inputs.
