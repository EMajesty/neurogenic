# End-to-end workflow matrix

Status uses the classifications defined in `IMPLEMENTATION_AUDIT.md`. “Test”
means the cited test executed a meaningful result; none of these workflows was
performed on physical hardware during Task 080.

| # | Workflow and required path | Existing path and evidence | Blocking gap / workaround | Status |
|---|---|---|---|---|
| 1 | Empty project: New, name, create tracks, choose instruments | `project.new.v1`, `track.create.v1`, Browser New; project tests | Slint has no create/delete track UI and New uses a fixed “Untitled” name. Use semantic/CLI path. | B/P |
| 2 | Drum pattern: import WAV, assign pad, enter notes | Browser/Sample load, slice/pad actions, Sequencer step toggle; `workflow_scenarios.cpp::scenario_beat_project` | Slint cannot create tracks and only first 16 steps are reachable. | P |
| 3 | Slice and assign: load, inspect, add/move slices, assign/preview | Sample page and sample tests | Add/delete/assign work; marker move/drag and pad chooser are absent (assignment uses currently selected pad). | P |
| 4 | Pattern >16 steps: change/page through length, enter/edit | Backend page-next/page-previous and pattern model | No Slint page controls or pattern-length edit. Controller profile may map page actions, but touch-only workflow is blocked. | B |
| 5 | Synth: create track, select synth, sequence, edit patch | Sound typed parameters; `scenario_synth_modulation` | Slint cannot create the track; detailed voice controls depend on descriptor coverage. | P |
| 6 | Add/configure effect: owner, add, parameter, reorder/bypass | FX page; adapter and native-effect tests | Four add buttons are hard-coded; no preset workflow on FX page. | IT/P |
| 7 | Modulator: create type, configure, route to parameter | Full semantic registry and modulation tests | FX/Sound can edit existing routes but cannot create/configure the source. Pre-create via semantic API. | B |
| 8 | Macro controlling several parameters | Macro create/assignment/actions and tests | FX adjusts existing macro value only; create and assignment UI absent. | B |
| 9 | External audio record: source, destination, arm/start/stop/save | Recording actions and numerical tests | No Slint recording configuration page; Performance Record intentionally rejects. | B |
| 10 | Internal resample: choose mixer tap, record, publish/assign | Recording source actions; `scenario_mixer_recording_external` | No Slint controls for source/destination/finalization. | B |
| 11 | Live loop: session, source, quantized capture, handoff, overdub | Live-loop semantic actions and `live_loop_tests.cpp` | No Slint live-loop session UI or mode selector. | B |
| 12 | Launch clips/scenes | Performance pads/scenes; performance launching tests | Existing slots/scenes launch. No slot/scene create/edit UI; pad mapping is ascending slot ID, not an authored 2x8 placement. | P/IT |
| 13 | Hybrid arrangement/session playback | Slot takeover, owner display, Return; hybrid tests | Per-track behavior exists; Slint arrangement editing is read-only and capture controls absent. | P |
| 14 | External FX: choose physical pairs, levels, latency, monitor | Mixer/FX controls and route validator tests | Physical availability requires hardware. Pair cycling UI is terse and Setup has no dedicated configuration. | P/H |
| 15 | External MIDI instrument: device, route, channel, clock | MIDI routing actions and tests | Setup only selects preference device and toggles clock source; route/channel/output UI absent. | B |
| 16 | Map third-party MIDI controller | `.ngc`, coordinator, MIDI learn, profile save; controller tests | Learn requires manually typed control ID/mode; cannot choose semantic target, remove mapping or load profile from Slint. | P/H |
| 17 | Save reusable instrument/FX preset | `PresetLibrary` supports save; preset tests | No save-current semantic action in Slint adapter and Browser has no mutation controls. Use library API/tooling. | B |
| 18 | Save, close, reopen complete project | Browser save/open plus transactional project tests and `scenario_beat_project` | Close is not exposed; Slint path entry requires keyboard. Full UI-driven round trip is not tested. | IT backend/P UI |
| 19 | Missing assets/disconnected hardware recovery | Project/preset missing-asset models, relink actions, reconnect services | Slint does not expose project asset relink; hardware reconnect is automatic but only simulated in tests. | P/H |
| 20 | Operate without terminal/typing keyboard | Touch controls cover existing objects and basic edits | No on-screen keyboard/picker; startup device/profile config remains CLI/prefs-centric; several creation workflows are absent. | M as complete workflow |

## Detailed traces

### Drum pattern

Required: Browser selects WAV → `sample.load.v1` asynchronously publishes an
immutable version → Sample creates/assigns slice → Sequencer selects track and
toggles steps → transport plays → project saves. The backend workflow test
asserts non-silent output and restored state. Slint covers the middle operations
but lacks track creation and pagination, so this is not a demonstrated Slint
end-to-end workflow.

### Offline processing

Sample page calls `sample-processing.create.v1`,
`sample-processing.operation-add.v1`, then `sample-processing.start.v1`.
Completion produces a result owned by `SampleProcessingService`, but Slint does
not call save or `sample-processing.publish.v1`. A user cannot complete
“process then use the result” from the page.

### Project lifecycle

Browser confirmation correctly supplies `discard=true` for New/Open only after
the user accepts. Save and Save As use semantic actions. Atomic generation
writes and failed-load rollback have backend tests. However, closing, relinking
missing assets, startup restoration choices and a rendered dirty-shutdown flow
are not exercised together through Slint.

### Physical validation boundaries

External audio/MIDI latency, XRUN reporting, multichannel pair availability,
USB hotplug, RP2040 feedback, third-party MIDI feedback, touchscreen gestures,
display readability, suspend/power behavior and long-duration thermal stability
all require physical validation. Host simulations cannot promote these rows to
IT on the appliance.
