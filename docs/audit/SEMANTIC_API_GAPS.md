# Semantic API and frontend gap audit

The semantic layer is broad: `ActionRegistry` defines hundreds of typed actions
in `src/application/control.cpp`, discovery exports them, and
`ParameterBindingService` routes typed parameter writes. The principal problem
is coverage and composition at client boundaries, not absence of an action
registry.

## Prioritized gaps

| Priority | Gap | Exact missing layer | Evidence |
|---|---|---|---|
| P1 | Recording mode selection | Slint workflow/controller composition | `transport_record()` always rejects although `recording.*`, `arrangement.record-*`, `performance.capture-*` and `live-loop.*` actions exist. |
| P1 | Step page and advanced event editing | Slint callbacks/context | Registry exposes page, probability, condition, ratchet, timing and lock actions; `sequencer_page.slint` exposes only toggle, velocity and length-up. |
| P1 | Processing result activation | Slint callback/presentation model | `sample-processing.publish.v1` and save/cancel exist; Sample page only starts a job. |
| P1 | MIDI routing/external instrument configuration | Setup model and callbacks | `midi.route-set/delete`, clock output/forward and panic exist; Setup only toggles clock source and stages device selectors. |
| P1 | Controller mapping target editing | Setup semantic composition | Coordinator supports learn/save; UI requires typed control ID/mode and cannot bind an action/parameter or remove a binding. |
| P2 | Modulator lifecycle | FX/Sound UI | `modulator.create/configure/delete/duplicate/rename` are discoverable; pages only consume existing modulators. |
| P2 | Macro lifecycle and assignments | FX UI | `macro.create`, assignment set/delete, scenes and morph actions exist; page adjusts existing values only. |
| P2 | Preset save/rename/delete/enumeration composition | Missing application action for capture-current and missing Slint callbacks | `PresetLibrary::save` exists, but `preset_action("save")` explicitly rejects. Browser only validate/apply. |
| P2 | Arrangement editing | Sequencer UI | clip/audio-clip create/update/delete and loop actions exist; Arrangement subview only selects. |
| P2 | Missing-asset recovery | Browser UI | project/preset relink actions exist; no dependency/relink model or controls. |
| P2 | Track lifecycle | Slint pages | `track.create/delete` exist; no page can create/delete a track. |
| P3 | Generic descriptor-driven FX insertion | FX type model | Effect descriptors exist, but four processor types are hard-coded in `.slint`. |

## Actions exposed but incompletely usable

- `invoke_function()` submits an action ID with no arguments. It is valid only
  for zero-argument contextual actions; required arguments cannot be supplied
  by `SlintContextItem`. Context resolution must either provide bound arguments
  or restrict function assignments accordingly.
- Sample slice-to-pad uses `ApplicationState::selected_pad`, but the Sample page
  has no explicit destination-pad selector. The operation is semantic but its
  required context is hidden.
- `pattern.create.v1` is wrapped with a fixed default name and 16-step-derived
  length. Rename and length editing exist elsewhere but are not composed.
- FX route creation picks the currently selected modulator and effect parameter
  and a fixed depth; source output, range and ownership validation have no UI
  workflow.
- Browser preset Apply performs prepare then apply synchronously from one UI
  callback. Progress, cancellation and missing-asset relinking states are not
  surfaced despite backend transaction state.

## Backend without Slint access

Major families include arrangement recording/automation, performance capture,
live-loop sessions, audio-clip/crossfade editing, pattern chains/follow editing,
recording and resampling configuration, modulation instance management, macro
assignments/scenes/morphing, group lifecycle, mixer scenes, MIDI routes, project
relink, preset relink/export, sample-analysis candidate editing, sample
processing job management/publication, and edit undo/redo.

These are not necessarily missing engine features. They are missing
presentation models and semantic workflow adapters.

## Descriptor and binding findings

- Synth and native-FX parameter editors use authoritative descriptors and
  stable instance IDs. Dynamic stale selections are generally cleared during
  snapshot rebuilds (`SlintFrontendAdapter::refresh`).
- Context encoders call `discover_application()` on every turn to recover the
  descriptor. Correctness is centralized, but the full discovery enumeration
  is unnecessary per gesture and should be measured/cached by capability
  revision.
- Mixer controls are mostly hand-composed semantic actions instead of generic
  parameter bindings. This is not duplicate DSP authority, but it creates
  separate validation/display code paths.
- Discovery tests verify stable IDs, negotiation and stale references. They do
  not prove every documented action has an operable Slint control or complete
  numeric metadata.
- `ApplicationState::available_actions` represents registry availability, not
  object-specific precondition availability. Slint often discovers invalidity
  only after command rejection.

## SDL3 and CLI comparison

SDL3 remains a legacy client with its own `GuiModel` interactions; Slint does
not reuse SDL widgets. Both use application actions/state, so there is no second
musical authority, but client coverage differs. CLI startup still owns device
selectors, controller endpoints/profiles, headless mode and several operational
choices in `apps/neurogenic.cpp`. Setup preferences do not replace all of those
runtime arguments.

## Recommended acceptance target

A semantic-coverage test should enumerate required standalone workflows, verify
that each action is discoverable, object-valid, and reachable from at least one
presentation-neutral workflow adapter, then assert resulting state—not merely
command acceptance. This should not force every action onto every UI page.
