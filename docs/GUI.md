# Graphical interface

The display's upper and lower edges show the host assignments for eight upper
encoders and eight lower contextual buttons. They derive from the same
application snapshot as controller feedback; the GUI remains optional and is
never authoritative for those assignments.
Each upper label includes the formatted value and unit sent to 1.1 firmware.
Disabled assignments render muted. Changing GUI tabs updates the
host-authoritative context, and Shift exposes the same lower-button layer on
screen and hardware.

The MIDI view reports clock ownership, lock/acquiring/holdover/lost state,
measured tempo, phase error, SPP, connected logical ports, configured
outputs/routes and queue counters. Enter toggles internal/external
clock ownership and Shift+Enter invokes panic. Detailed port and routing-rule
editing currently uses semantic actions, project data, and native CLI port
selection.

NEUROGENIC retains SDL3's built-in 2D renderer behind a renderer-independent C++
UI model. SDL3 supplies X11/Wayland windows and keyboard, pointer and touch
events while leaving a future KMSDRM route. GLFW was considered, but would also
need a 2D renderer and separate touch handling; a retained desktop toolkit adds
policy and weight not needed by this instrument interface. Primary references:
[Wayland](https://wiki.libsdl.org/SDL3/README-wayland),
[high DPI](https://wiki.libsdl.org/SDL3/README-highdpi), and
[video/KMSDRM settings](https://wiki.libsdl.org/SDL3/EnvironmentVariables).
The preferred Slint shell is selected with `--frontend slint` in a Slint-enabled
build. `--frontend sdl` and `--gui` retain this legacy frontend, while
`--frontend headless` selects neither graphical event loop. Both clients use
one production `ApplicationService`; see `SLINT_UI.md`.

The Routing view shows two external FX loops, send/return pairs, wet/dry,
manual latency, return levels and scene rows. Up/Down selects a loop,
Left/Right adjusts wet/dry, Enter toggles enable, and pointer/touch can recall a
scene. Every operation submits the shared semantic action API.

Build normally, then run `./build/dev/neurogenic --gui --headless` for a window
with null audio, or use `--fullscreen` with normal audio/sample options.
`-DNEUROGENIC_ENABLE_GUI=OFF` preserves a headless build. If SDL3 is absent,
the application builds and `--gui` returns a useful unavailable diagnostic.

The primary visual target is an 8-inch 1920 × 1200 HDMI display with USB HID
touch, initially at 1.5× automatic scale. HDMI and touch remain host services;
neither is required. SDL window points, drawable pixels, logical workspace and
UI scale are tracked separately. Pointer coordinates are converted to drawable
pixels before the same scaled hit-test geometry used by touch and keyboard.

Automatic scale is 1.5× at 1920 × 1200, 1.25× on medium high-resolution
viewports, and 1.0× on compact displays. A manual 1.0–2.5× override and
fullscreen state are stored in `$XDG_CONFIG_HOME/neurogenic/gui.conf` (or
`$HOME/.config/neurogenic/gui.conf`). The Display view is encoder-accessible;
Up/Down changes scale and Enter toggles fullscreen. `+`/`-`, `0` (automatic),
and F11 are global shortcuts. CLI overrides are `--ui-scale SCALE`,
`--reset-ui-scale`, and `--fullscreen`. Removing the configuration file is the
last-resort reset path.

Responsive breakpoints use logical workspace size, not raw resolution. Wide
layouts keep the sequencer and inspector together and expand track/grid space;
1024 × 800 uses a compact layout; 1024 × 600 retains transport, navigation,
the 16-cell grid, and inspector without horizontal scrolling. Geometry is
rebuilt for drawable-size, scale, view, focus, and application revision changes.
Controls render directly at drawable resolution—there is no blurry whole-frame
upscale. The current integer-scaled SDL debug font is crisp and dependency-free,
but remains visually basic.

The shell has a persistent status bar and Sequencer, Samples, Parameters, X/Y,
Performance and Display views. Timeline and slot information
comes from application snapshots. The sequencer displays actual target-note
events, selection, playhead, pages, resolution and note target. Clicking or
tapping a valid cell toggles it through `step.toggle.v1`.
Filesystem browsing is outside the audio callback; a graphical file dialog and
virtual keyboard are future adapters to the existing path/text models.

In the Sequencer, Left/Right select steps, Up/Down select tracks, Enter toggles
the selected step, Shift+Left/Right changes pages, and Shift+Up/Down changes
the target note. Number keys toggle current-page cells; Shift selects only.
Elsewhere, defaults are arrows for adjustment, Enter to confirm, Escape or
Backspace to cancel, Tab to cycle focus, Space to play/stop, Shift as modifier,
and number keys for step selection. Mouse and touch select views and controls.
X/Y dragging is limited to 125 commands per second. All input passes through
`ApplicationService`; rendering never runs in or blocks the audio callback.

Shift+number or Shift+click selects an existing note without toggling it. The
step inspector shows event ID, velocity, duration, pitch, timing offset, and
the selected note's index when a cell contains several notes. Click a property
and use Up/Down to adjust it; Enter cycles properties in the encoder-only
workflow. Undo and Redo buttons reflect the shared timeline history.

The status bar shows compact callback load, process CPU, resident memory and
voice usage. The Performance view adds current/average/peak callback timing,
deadline and measured-overrun counts, sample rate/buffer size, queue counters,
CPU/memory availability, and the explicit absence of backend XRUN data. It is
refreshed from `ApplicationState` generations and never reads audio-owned state.
At compact resolutions it remains a separate view, avoiding sequencer clutter.

The Sequencer instrument field displays the selected track's stable routing ID
and toggles between `sample.main` and `synth.subtractive.v1`. The Parameters
view discovers the native synth and delay controls through `ParameterRegistry`;
it does not hold DSP pointers. The Performance view also reports synth
polyphony, voice steals, rejected DSP commands and bounded instrument/effect
processing measurements. Compact displays use the same semantic controls.

Steps containing parameter events show an `L` marker. The inspector lists locks
for the selected step separately from the live parameter value. In the
Parameters view, Shift+Enter adds or replaces a lock using the current value,
Shift+Up/Down edits the selected step's locked value, and Shift+Escape removes
it. Ordinary Up/Down continues to change the live parameter. These commands
remain available through keyboard/encoder navigation without touch.

CI covers 1920 × 1200 at 1.0×, 1.5× and 2.0×, both compact resolutions,
high-DPI coordinate conversion, resize, live scale/fullscreen requests,
persistence recovery, keyboard, pointer, touch-equivalent and encoder paths,
plus an SDL dummy-video smoke test. The AArch64 cross-check compiles the
portable model with the SDL adapter disabled; native AArch64 flake outputs
include SDL3. Physical touchscreen mapping, HDMI fullscreen/DPI transitions,
GPU combinations, 8-inch touch-target comfort and subjective stage readability
remain unverified.

## Waveform and slice editing

The Samples view draws cached peak data instead of rescanning PCM. Click/tap a
slice to select it, drag a start/end marker, or click empty waveform space to
create a short slice. Up/Down selects slices, Left/Right adjusts boundaries,
Shift+Up/Down zooms and Shift+Left/Right pans. Enter previews,
Shift+Enter creates 16 equal slices, Escape
stops preview, and Shift+Escape deletes. Number keys 1–16 assign the selected
slice to a pad; Shift+number selects sample slots 0–15. Hit tests use drawable coordinates and responsive scaling at
1920×1200, 1024×800 and 1024×600.

The contextual Samples inspector exposes the selected pad's assignment,
One-shot/Gate/Toggle mode, choke group, semitone and cent tuning, gain, reverse,
and ADSR. Pointer rows dispatch semantic actions. Non-touch clients focus
`pad-playback`, use relative navigation to adjust the selected property, and
press Enter to cycle properties; the GUI contains no playback implementation.
It also exposes Stretch Off/Realtime, effective time ratio, source beats/BPM,
quality and reported latency. Pointer rows and the same focus/data-wheel flow
dispatch shared actions. At compact sizes the denser inspector preserves the
waveform and essential navigation without requiring touch.

Edits publish at the next audio callback as complete validated plans. A final
partial step is available when its start tick lies inside a non-grid-aligned
pattern. Drag-painting and boundary-quantized publication are not implemented.

Expressive steps are marked with `*`. The inspector reports probability,
condition/cycle values, ratchet count, and global Fill state. Semantic actions
provide editing today; richer pointer widgets remain future work. All properties
remain reachable through non-touch controller navigation.
# Project status

The Display view also shows the current project name/path, dirty marker, and
missing-asset count. Shift+Enter saves to an established path. Open, Save As,
and relink use the same semantic C++ actions; a graphical embedded filesystem
browser remains a future integration point. No project operation requires
touch.

Enter in Display settings opens project-name entry. The overlay uses the shared
text-entry model and renders touch/pointer keys plus Backspace, Cancel and Done.
A minimal data wheel selects characters or those actions, Enter activates the
selection, and Up/Down moves the cursor. SDL keyboard text reaches the same
buffer. This is an initial on-screen keyboard, not a general file browser.

# Recording view

The Record tab shows source, destination, channel mode, state, captured frames,
buffer usage, peak levels, and overflow/dropped counts. Enter arms, starts, or
stops; Escape cancels. The same actions work via pointer and controllers, and
touch is optional. Responsive layouts retain essential status at 1920×1200,
1024×800, and 1024×600.

# Mixer view

The Mixer tab displays track strips, gain/pan, mute/solo, two sends, peak levels
and the master meter. Up/Down selects a strip, Left/Right adjusts gain, Enter
toggles mute and Shift+Enter toggles solo. Pointer selection invokes the same
semantic actions. Detailed input/aux/output setup currently uses the shared
action API rather than dedicated widgets.

# Modulation view

The `MOD` tab lists routes with source, destination, explicit scope, depth,
enabled state, and effective value. Up/Down selects, Left/Right adjusts bipolar
depth, and Enter toggles the route. The same workflow works with a minimal data
wheel at every display breakpoint; the GUI owns no parallel modulation graph.
The sequencer labels synth tracks with their instance number. MOD rows show
GLOBAL/TRACK/VOICE scope plus `ALL` or the concrete instance.
Sample, mixer, aux and external-FX destinations appear through the same route
rows and effective-value feedback; unsupported voice/sample combinations are
reported by the shared application action.

## Appliance close behavior

Window close, Ctrl+Q, and SIGTERM pass through the appliance shutdown request.
A dirty project remains open when the configured choice is `cancel`; a `save`
shutdown proceeds only after Save succeeds. Touch is not required for recovery:
the Project view and encoder/Enter/Back navigation expose Save and project text
entry. Explicit discard is a policy choice, never an implicit GUI side effect.
