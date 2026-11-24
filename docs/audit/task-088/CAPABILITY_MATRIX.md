# Capability matrix

Legend: **C** complete in software, **P** partial, **B** backend-only, **M**
missing, **D** deferred, **NR** physical validation not run. Controller means
the built-in dedicated profile; MIDI means generic control-surface mapping.

| Capability | Engine | Semantic API | Slint | Controller | MIDI control | Persist | Automated evidence | Physical |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Transport/tempo | C | C | C | C | C | C | C | NR |
| Sample import/preview | C | C | C | P | P | C | C | NR |
| Regions/slices/pad assignment | C | C | C | P | P | C | C | NR |
| Offline sample processing/publication | C | C | C | P | P | C | C PCM | NR |
| External recording | C | C | C | P | P | C result | P simulated | NR |
| Internal resampling | C | C | C | P | P | C result | C audio | NR |
| Live-loop first pass/handoff | C | C | C | P | P | C clip | C audio/timing | NR |
| Live-loop overdub/replace | C | C | P | P | P | C version | C service | NR |
| Track-local step pattern | C | C | C | C | C | C | C playback | NR |
| Reusable multi-track pattern | C | C lifecycle | P select only | P launch | P launch | C | C engine | NR |
| Advanced step properties/locks | C | C | C track-local | C | C | C | C engine | NR |
| Arrangement pattern/audio playback | C | C | P read/select | P | P | C | C audio | NR |
| Arrangement authoring | C | C | B | B | B | C | C service | NR |
| Session slots/scenes/follows | C | C | P | P | P | C | C timing | NR |
| Hybrid per-track ownership | C | C | C status/actions | P | P | preference only | C | NR |
| Synth/sample instruments | C | C | C | P | C parameters | C | C audio | NR |
| Modulator creation/configuration | C | C | P | P | P | C | C engine | NR |
| Macro creation/assignment/capture | C | C | P adjust/inspect | P | P | C | C engine | NR |
| Mixer/groups/aux/master | C | C | C/P routing | P | C mappings | C | C audio | NR |
| Native FX chain | C | C | C/P type list | P | C parameters | C | C audio | NR |
| External FX | C | C | C config | P | P | C | C simulated | NR |
| Preset authoring/library | C | C | C supported scopes | P | P | C atomic | C round-trip | NR |
| Project save/open/relink | C | C | C/P multi-asset | P | P | C atomic | C audio restore | NR |
| Musical MIDI routes/clock | C | C | C basic | P | N/A | C | C MIDI | NR |
| Controller learn/profile | C | C | P | C profile | C learn | C | C protocol | NR |
| Manual mapping editor | C service | C service | P remove/learn | N/A | P | C | C service | NR |
| Production install/service | N/A | N/A | C CMake | N/A | N/A | C config | P package smoke | NR |

## Page implementation summary

| Page | Production operations | First important gap |
|---|---|---|
| Performance | transport, track/slot/scene launch, quantization, ownership/return, record entry | explicit 16-pad slot assignment and complete scene/macro authoring |
| Sequencer | long pages, track-local notes/properties/locks, track/pattern lifecycle, arrangement view | selected reusable pattern is not the step-edit authority; no clip editing |
| Sample | load, overview, regions, slices, analysis, preview, processing lifecycle | physical waveform interaction and full loop-overdub result workflow |
| Sound | instruments, descriptor parameters, envelopes, existing modulation, presets | create/configure full modulation and macro graphs |
| Mixer | channel bank, gain/pan/mute/solo, sends/groups/routes/meters/external FX | some group/master lifecycle and physical workflow depth |
| FX | owner/chain/parameters/modulation/macros/external/presets | descriptor-derived add list and full authoring of sources/macros |
| Browser | async paged files, projects/samples/presets, relink | 8192 scan ceiling and atomic multi-asset repair |
| Setup | audio preferences, MIDI routes/clock, controller list/learn/remove/save, diagnostics | complete manual mapping form and fully packaged Slint Nix output |
