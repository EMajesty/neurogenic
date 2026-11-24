# Musical workflow audit

“Block” is the first point a standalone user cannot continue through the
intended workflow. Tests cited are host evidence, not hardware validation.

| Workflow | Production path and evidence | First block / workaround | Result |
|---|---|---|---|
| Create, save, reopen project | Browser/keyboard → `project.new/save-as/open.v1` → generation save in `project.cpp`; `workflow_scenarios` restores audible beat | none for basic project; multi-asset recovery is not atomic | Complete basic |
| Import drum samples and sequence | Browser → `sample.load.v1`; Slice/Pad; step actions mutate selected track; `scenario_beat_project` renders and restores audio | none for one pattern per track | Complete basic |
| Slice sample to pads | Sample analysis/slice actions → immutable slot/pad publication; sample tests | destination pad is contextual and explicit pad UX is limited | Partial UX |
| Edit beyond 16 steps | page actions + absolute step IDs; adapter and engine tests | reusable selected pattern is not edited | Complete only track-local |
| Synth sequence | track instrument action, descriptor parameters, step events; synth workflow renders audio | deep modulation creation is cumbersome/incomplete in UI | Complete basic |
| Add/configure effect | FX chain semantic actions and descriptors; engine audio tests | add list hard-coded; not every preset owner capturable | Complete basic |
| Create modulator/route | backend `modulator.*` and route actions | production pages largely inspect/adjust existing objects; full source/destination authoring is incomplete | Partial |
| Macro multiple parameters | backend `macro.*` supports assignments/scenes/morph | Slint mainly presents/adjusts existing macros; capture/assignment workflow incomplete | Partial |
| Record external audio | recording modal/coordinator → recording service → immutable publication | simulated input exists; physical channels/latency/playback NOT RUN | Implemented, unverified physically |
| Resample internally | source/destination workflow → `RecordingService`; `scenario_mixer_recording_external` measures non-silent source and publication | workflow test does not compare a complete expected waveform | Implemented, reasonably tested |
| Live loop | live-loop session, quantized boundary, gapless reserved handoff; `live_loop_tests` frame/PCM assertions | Slint overdub/replace/clear/save lifecycle is incomplete | Partial workflow |
| Launch clips/scenes | launcher plan → timeline scheduler → voices; performance tests | first-16 sorted slot mapping is implicit | Complete engine, partial UX |
| Hybrid arrangement/session | per-track ownership and return action; integration tests | arrangement cannot be authored in Slint | Complete playback, blocked authoring |
| Build full song | reusable patterns + arrangement clips backend | Sequencer edits legacy track pattern; arrangement view only selects | Blocked (P1) |
| External FX | Mixer/FX/Setup routes → validated device pairs | requires actual multichannel hardware | Software complete, physical NR |
| External MIDI instrument | track destination, routes, scheduler and clock; workflow verifies MIDI queue/persistence | full device identity/hotplug behavior needs hardware | Software complete basic |
| Map third-party controller | Setup endpoint → learn → binding/profile save | no complete descriptor-backed manual editor, scaling/inversion UI | Partial |
| Save/reuse preset | Sound/FX/Browser → capture/library/prepare/apply; preset tests cross destinations | activation is not an Undo/Redo history operation; dependency repair persistence limited | Substantially complete |
| Recover missing project asset | Browser inspector → explicit file → `project.relink-sample.v1`; tests replay PCM | one-at-a-time only; no atomic prefix transaction | Partial |
| Operate without terminal/keyboard | shell, keyboard, picker and Setup cover common software paths | initial install/service repair and some advanced mappings still maintenance tasks | Partial standalone |

## Arrangement versus sequencing

`Track` owns `MusicalPattern pattern` (`model.hpp:83`), while reusable
`PatternDefinition` owns a vector of `PatternTrack` (`model.hpp:165`). Step
actions around `control.cpp:5900–6350` edit the selected `Track::pattern`.
Pattern lifecycle and launches around `control.cpp:5205–5299` edit
`SongArrangement::patterns`. The Slint adapter displays `selected_pattern` but
constructs grid cells from `snapshot.tracks[*].events`. This is a user-visible
model conflict, not merely missing UI chrome.

## Performance recovery

Undo/Redo covers timeline edits, and rejected transactions preserve the prior
timeline. Live performance capacity failures are explicit. PCM overdub history
and preset activation are not integrated as coherent Undo/Redo operations, so
safe performance recovery is incomplete even though immutable versions retain
active audio safely.
