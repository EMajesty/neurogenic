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

Build the prototype with the dedicated preset:

```sh
cmake --preset slint
cmake --build --preset slint
./build/slint/neurogenic-slint --width 1920 --height 1200
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
- `ui/slint/performance.slint` composes the initial responsive performance
  page.
- `SlintFrontendAdapter` is renderer-independent and copies revisioned state,
  contextual mappings and pad status from production application services.
- `slint_frontend.cpp` marshals adapter snapshots onto Slint's event loop.

The page uses flexible layout at both 1920x1200 and the 1024x600 minimum rather
than scaling a fixed canvas. Longer labels elide, inactive assignments are
visibly unavailable, and touch/mouse hit handling is provided by Slint.

## Input and state

Contextual buttons invoke the action IDs supplied by the shared controller
mapping. Encoders use `ParameterBindingService`; pads use `PerformanceGrid` and
then semantic commands. Development keyboard input maps Space to transport,
Shift to the semantic modifier, and 1-8 to the first pad row. This is a
development convenience, not a physical-controller layout contract.

A 16 ms event-loop timer obtains copied revisioned snapshots. It does not touch
engine internals, perform audio callback work or establish another state
authority. Shutdown stops the timer before releasing the adapter.

## Current limits

This is a functional foundation, not a replacement-complete frontend. It shows
transport, tempo, selected track, discoverable active content, contextual
assignments and performance pad state. Some arrangement/clip activity lacks a
single authoritative active-object field and is therefore shown as
`Unavailable` rather than inferred. File dialogs, project workflows, full page
navigation, accessibility review and rendering screenshot tests remain future
work. Renderer tests require a usable Slint backend; model tests remain fully
headless.
