# Designing a frontend for NEUROGENIC

The provisional functional palette and redundant state cues are specified in
[`VISUAL_LANGUAGE.md`](VISUAL_LANGUAGE.md); Task 098A does not broadly recolor
the frontend.

## Context descriptor agreement

Slint publication is keyed by semantic destination, not a reused page component.
It copies the same eight encoder, eight function and sixteen pad descriptors as
ControllerSession, so secondary destinations retain distinct banks.

## Shared assignment forms

An alternative frontend may consume `AssignmentTargetPicker` metadata and
retain its own bounded form/focus state. Confirmation must still call the
project Performance service or appliance controller coordinator appropriate to
the editor. The reference Slint form is an example, not a required UI runtime;
indexed roles and typed argument descriptors contain no Slint types.

Assignment editors should consume the shared copied target model and submit a
proposal to their owning service. The Performance editor commits project
transactions; the Setup mapping editor commits appliance profile candidates.
Neither requires Slint, and neither may infer stable identities from row order.

Alternative frontends can read `ApplicationState::performance_banks`, select a
bank through semantic actions and render any subset of its logical positions.
Current values still come from parameter/macro/launcher state. No Slint type or
Reference Controller V2 count is part of the project model. See
[Performance banks](PERFORMANCE_BANKS.md).

Task 096D adds copied contextual `bank_id`, `role`, and lock indication.
Alternative frontends may use these descriptors for held-step workflows or
ignore the reference grouping and invoke stable semantic operations directly.
They must not retain mutable event pointers or infer lockability from a label.

Reference frontends may reuse `reference_primary_destinations()` and
`reference_secondary_destinations()`, but alternative frontends need not. The
tables identify presentation destinations and existing subviews; they do not
own engine state. Slint's two-column encoder zone and label strips are shell
geometry, not application schema.

Frontends may publish a presentation-local modal identity through the shared
[input ownership contract](INPUT_GESTURES.md). Rendering remains frontend-local;
modal transitions invalidate stale held input.

NEUROGENIC does not require Slint, a window, or any particular page hierarchy.
A frontend is a control-side client of `ApplicationService`: it discovers
capabilities, reads copied revisioned state, invokes semantic operations, and
keeps navigation and rendering state locally. Slint is the production reference
frontend; SDL3 and headless operation demonstrate other clients of the same
engine boundary.

## Existing frontend contract

### Discover capabilities instead of assuming them

`discover_application(application)` returns `ApplicationDiscovery`, including
capabilities, stable objects, parameter descriptors, semantic actions with typed
arguments, state bindings, limits, and validation errors. The installed
`neurogenic-discovery --json` command exposes the same schema for inspection;
it is not a network API. See [Application Discovery](APPLICATION_DISCOVERY.md).

Use object IDs rather than row indices, honor availability and diagnostics, and
ignore unknown compatible fields. Discovery describes semantics and control
hints, not widgets, coordinates, colors, pages, or a declarative UI tree.

### Read authoritative copied state

`ApplicationService::snapshot()` returns detached `ApplicationState`.
`snapshot_shared()` returns an immutable handle reused for the same application
revision and avoids another value copy in refresh loops. Musical
objects remain owned by application services and immutable prepared engine
state. A frontend may copy IDs, descriptors, metadata, meters, and bounded
waveform summaries; it must not retain engine pointers, mutable DSP objects, or
sample PCM as an ordinary UI model.

Separate structural state, ordinary revisioned state, and bounded telemetry.
Refresh only affected presentation domains where practical. A dropped bounded
update means resynchronize from a fresh snapshot. Terminal failures and audio
publication acknowledgements must not be lost through coalescing.

Selection, focus, scrolling, open dialogs, navigation history, responsive
breakpoints, and temporary text buffers are frontend-local presentation state.
Track, pattern, sample, effect, route, and clip identity are authoritative
application state. Capture stable targets before confirmation so a later
selection change cannot redirect an operation.

### Invoke semantic actions and parameters

Actions use `ApplicationService::execute(ApplicationCommand)` for synchronous
control-side work or `submit()` plus `dispatch_pending()` for bounded queued
dispatch. Supply every required typed argument and display the returned error.
Do not mutate a snapshot and expect it to affect the engine.

`ParameterBindingService` resolves descriptor-backed parameters and supports
native, normalized, relative, toggle, and momentary operations with validation,
stale-reference detection, and pickup helpers. See
[Parameter Bindings](PARAMETER_BINDINGS.md). Use semantic actions for object
lifecycle and transactional edits; use parameter bindings for compatible value
control. Both ultimately pass through authoritative application services.

## Minimal C++ client

The following uses the existing in-process API. Construction of the engine,
timeline, and sample manager follows the application executable and is omitted
for clarity:

```cpp
#include "neurogenic/application/control.hpp"
#include "neurogenic/application/discovery.hpp"
#include "neurogenic/application/parameter_binding.hpp"

neurogenic::ApplicationService &application = obtain_application_service();
auto discovery = neurogenic::discover_application(application);
auto state = application.snapshot();

auto result = application.execute({
    "transport.play.v1", {}, "my-frontend", 1
});
if (!result.accepted)
    show_error(result.error);

neurogenic::ParameterBindingService bindings(application);
neurogenic::BindingReference master_gain{
    neurogenic::BindingTargetKind::explicit_object,
    "master:main", "mixer.master.gain", {}, state.revision
};
auto error = bindings.apply(master_gain, {
    neurogenic::BindingOperation::relative_steps, 1.0, false, 0
});
if (error)
    show_error(error.message);
```

The helper names `obtain_application_service()` and `show_error()` are supplied
by your host application; they are not NEUROGENIC APIs. Consult the current
descriptor before binding because not every historical parameter/action has the
same identifier or metadata.

## Layout, navigation, and toolkit choices

An alternative frontend may use a different page model, one screen, several
screens, a terminal, accessibility technology, or no local display. Map its
navigation to stable semantic selection and presentation-local focus rather
than copying the reference controller's eight pages or control counts. Handle
the intended resolution and input technology in that frontend; the engine has
no screen-size dependency.

The existing implementations illustrate different scopes:

- `SlintFrontendAdapter`, `SlintUiState`, and `src/ui/slint_frontend.cpp` show
  copied page models, incremental publication, asynchronous browsing, modals,
  and generated callbacks. See [Slint UI](SLINT_UI.md).
- `GuiModel` and the SDL3 adapter show logical draw commands, toolkit event
  translation, DPI scaling, and a smaller legacy interaction surface. See
  [SDL GUI](GUI.md).
- the executable's headless and interactive modes show that transport,
  discovery, diagnostics, MIDI, scripting, and semantic services do not require
  a graphical loop.

Slint is included by the default production Nix package, but it is not mandatory
for engine or alternative-frontend builds. The production executable selects a
single compiled frontend loop at runtime; it does not dynamically load arbitrary
UI runtimes.

## Ownership and real-time rules

Frontend code runs outside the audio callback. Never add rendering, toolkit
calls, discovery, JSON, filesystem access, allocation, logging, or frontend
locks to the callback. Expensive directory enumeration, preset parsing, waveform
generation, and device discovery need bounded worker/control-side execution.
Publish samples and prepared graphs through existing application services and
wait for their acknowledgement before presenting pending state as active.

Keep frontend dependencies behind their adapter and build target. A new toolkit
currently requires C++/build-system integration, a state adapter, event-loop
marshalling, semantic callback wiring, and tests. There is no stable binary
frontend plugin ABI, external RPC server, declarative renderer, or JavaScript
bridge. An out-of-process frontend would require a new explicitly designed
transport rather than treating discovery JSON as RPC.

## Verification expectations

Test semantic round trips through production services, stale identity handling,
errors, modal target capture, update overflow/resync, and shutdown. Distinguish
adapter tests from generated-component or rendered tests and from physical touch
validation. Alternative layouts should prove that they do not introduce a new
musical source of truth or block the frontend thread with unbounded work.

Current coupling still worth reducing includes the large copied
`ApplicationState`, some reference-oriented Slint presentation models, partial
descriptor metadata for historical actions, synchronous control-side operations
in a few services, and build-time rather than runtime selection of available UI
toolkits. These are architectural work items, not evidence of a plugin system.

`neurogenic_client_independence_tests` is the maintained compilable conformance
example. It has no Slint, SDL, reference-panel or page-hierarchy dependency and
covers discovery, copied state, typed actions, parameter binding, history,
audible rendering and project reopen. The shorter snippet above remains
illustrative because host construction and error display are host-specific.

An alternative frontend can opt into hardware navigation by publishing stable
destination/focus roles and consuming `ApplicationState::interaction` request
revisions once. Modal open captures a target; the frontend validates and
dispatches it, then closes the exact generation. Toolkit focus, geometry,
scrolling and text entry remain local. Direct semantic clients need not opt in.
## Setup profile library

Setup consumes copied profile-library entries and delegates every mutation to
the appliance service. Its scrollable library, dynamic typed arguments,
scope/modifier fields, feedback destination, learned-source preview and
structured conflict view use the shared interaction contract. The frontend is
not a persistence or activation authority.

Track presentation distinguishes mute, solo, effective audibility, arm, and
captured recording owner from frontend-local selection. The visual direction
is documented in [VISUAL_LANGUAGE.md](VISUAL_LANGUAGE.md); Task 098B does not
perform a palette or typography migration.
# Repeat and erase presentation

Frontends consume the presentation-neutral repeat division/active state and
live-erase status. They may invoke the registered semantic operations but do
not run musical timers or mutate timeline containers.
