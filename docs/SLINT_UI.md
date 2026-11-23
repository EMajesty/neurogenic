# Slint UI foundation

Slint 1.19.0 is the preferred frontend foundation. The existing SDL3 frontend
remains available while the Slint pages gain coverage. Neither frontend owns
musical state: both consume copied `ApplicationState` snapshots and invoke the
same semantic application actions.

## Build and run

Slint is deliberately opt-in. A normal dependency-enabled or headless build
does not resolve or compile Slint:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Build the frontend with the dedicated preset. The production executable uses
the same appliance lifecycle as SDL and headless operation:

```sh
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic --frontend slint --headless
./build/slint/neurogenic --frontend slint --fullscreen
# Development-only isolated adapter executable:
./build/slint/neurogenic-slint --width 1024 --height 600
```

The CMake integration first accepts an installed `Slint` 1.19 package and
otherwise fetches the pinned upstream `v1.19.0` source tag. Building from source
requires the Rust toolchain supported by that Slint release. Linux x86_64 and
AArch64 are supported by Slint and by NEUROGENIC's portable C++ baseline.
The source build disables jemalloc only for Slint's build-time compiler because
its configure probe is incompatible with Nix's wrapped GCC; this does not alter
the frontend runtime allocator.

For live preview, open `ui/slint/performance.slint` in `slint-viewer` or use the
official Slint editor extension. Preview shows presentation structure only;
real application state and semantic callbacks require `neurogenic-slint`.

## Structure

- `ui/slint/theme.slint` contains temporary presentation tokens.
- `ui/slint/components.slint` contains navigation, contextual controls,
  transport, track/content, parameter, notification and modal components.
- `ui/slint/performance.slint` composes the shared application shell, page
  registry, responsive Performance page and honest placeholder destinations.
- `SlintFrontendAdapter` is renderer-independent and copies revisioned state,
  contextual mappings and pad status from production application services.
- `slint_frontend.cpp` marshals adapter snapshots onto Slint's event loop.

The page uses flexible layout at both 1920x1200 and the 1024x600 minimum rather
than scaling a fixed canvas. Longer labels elide, inactive assignments are
visibly unavailable, and touch/mouse hit handling is provided by Slint.

## Input and state

The shell exposes Performance, Sequencer, Sample, Sound, Mixer, FX, Browser and
Setup. Only Performance is implemented; selecting another destination keeps
navigation operational and labels it as a placeholder. Arrangement belongs
under Sequencer, modulation under Sound/FX, and MIDI/controller/system work
under Setup.

Contextual buttons invoke the action IDs supplied by the shared controller
mapping. Encoders use `ParameterBindingService`; pads use `PerformanceGrid` and
then semantic commands when no performance launch slot is assigned. Assigned
performance slots dispatch `performance.slot-launch.v1`. Track navigation,
transport play/stop, and Return to Arrangement likewise use semantic actions.
Launch states and per-track arrangement/session ownership are copied from the
authoritative performance-launching snapshot. Record is visibly rejected
because there is not yet one unambiguous transport-record semantic action.

Development keyboard input maps Space to transport, Shift to the semantic
modifier, 1-8 to the first pad row and F1-F8 to registered pages. This is a
development convenience, not a physical-controller layout contract.

A 16 ms event-loop timer obtains copied revisioned snapshots. It does not touch
engine internals, perform audio callback work or establish another state
authority. Shutdown stops the timer before releasing the adapter.

## Current limits

This is a functional shell, not a replacement-complete frontend. Scene launch
and macro services are represented by the adapter but do not yet have dedicated
widgets. Quantization is displayed; editing it needs a future selector.
Confirmation infrastructure exists, while project save/open dialogs and the
seven non-Performance pages remain deferred. Some arrangement/clip activity
lacks a single authoritative active-object field and is shown as `Unavailable`
rather than inferred. Physical touch quality, accessibility and screenshot
validation still require display hardware; model tests remain headless.
