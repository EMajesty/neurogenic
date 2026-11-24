# Hardware-first interaction matrix

The dedicated profile in `full_performance_profile()` maps 16 grid controls,
eight contextual encoders, eight contextual functions, page controls and six
navigation controls. Page buttons select controller-local contexts
(`sequencer`, `samples`, `parameters`, `mixer`, `modulation`, `routing`,
`recording`, `midi`), not Slint page navigation. Several edit/performance
cluster positions are intentionally unassigned.

| Page | Encoders | Functions/pads | Touch-only or weak areas |
|---|---|---|---|
| Performance | default transport/synth/sample/mixer parameters, not a performance-specific macro bank | pads launch first 16 presented slots; Play/Stop/Record available | scene selection, slot assignment, quantization and explicit return choices |
| Sequencer | generic tempo/synth/sample/mixer bank | Page-/+, Reset Step, Mute, Undo/Redo, Fill, Play; pads enter/select steps | advanced property/lock selection, pattern/track lifecycle confirmations |
| Sample | pitch/gain/pan and delay parameters | Preview/Stop Preview; pads use current global pad mode | marker drag, region/slice choice, analysis/process setup and destination selection |
| Sound | cutoff/resonance/ADSR/gain/delay mix | generic navigation/back/enter/play | instrument, modulation and preset authoring |
| Mixer | gain/pan/sends/returns/master/external FX | Mute/Solo plus generic navigation | banking, channel/group/output selection and detailed routing |
| FX | same `parameters` bank as Sound | generic navigation | chain ownership, effect selection/reorder, modulation/macro/preset workflows |
| Browser | falls back to sequencer/default bank | generic navigation but no dedicated parent/refresh/select bindings | file paging, category, picker confirmation and keyboard dominate touch |
| Setup | falls back to sequencer/default bank | generic navigation | device/route/mapping editor, learn and preference forms |

## Event path and argument safety

Dedicated events are decoded by `ControllerHost`, normalized into a
`ControllerSession`, resolved by `resolve_controller_context()` and submitted to
`ApplicationService`. Profile actions carry `ControlBinding::arguments` and are
validated against `ActionRegistry`. Context buttons also submit their resolved
arguments. Stable endpoint ownership is retained with `shared_ptr` snapshots in
`ControllerCoordinator`.

The Slint shared context copy stores label/value/parameter/action but not
`ContextControl::arguments`; `invoke_function()` dispatches only the ID. The
current built-in context functions are zero-argument, so no shipped label is
confirmed broken, but argument-bearing shared assignments cannot yet be safely
reused by Slint.

## Assessment and recommended model

Essential performance actions are physically reachable, but editing is not
hardware-first beyond step entry and common parameter changes. Subsequent work
should define page/subview-specific banks in the shared resolver, preserve bound
arguments, and reserve the eight function positions consistently for selection,
operation, undo/redo and confirmation. Pads should have explicit persisted
assignments in Performance, absolute steps in Sequencer, slice/sample triggers
in Sample, and remain inactive elsewhere unless a documented mode exists.
Administrative filesystem/text operations can remain touch-oriented. Physical
controller and touchscreen behavior is **NOT RUN**.
