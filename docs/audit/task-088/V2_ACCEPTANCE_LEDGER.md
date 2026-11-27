# Reference Controller V2 acceptance ledger

| 098D typed clipboard | Application/Timeline transaction + COPY gestures | `neurogenic_musical_clipboard_tests`, Slint adapter/generated suites | Software PASS; physical NOT RUN |

Task 098E supplies authoritative live sequencer-event recording for accepted
sample, synth, MIDI-note, and repeat events. Contextual loop/replace workflows
remain 098F scope and physical evidence remains NOT RUN.

| 098C/098E / RC2-001, RC2-003, AUD2-007 | REPEAT+pad/DATA/triplet, persistent repeated events, and running ERASE+pad | fixed AudioEngine repeat slots, bounded live-event capture, stable live-erase transaction | `neurogenic_note_repeat_live_erase_tests`; `neurogenic_live_event_recording_tests` | production scheduler/MIDI/PCM/timeline/history | PASS software including recorded repeat insertion; simultaneous erase/recording and physical validation remain open |

| 098B / RC2-001, RC2-003, AUD2-007 | SOLO/ARM stable pad modes and SHIFT+STOP unified panic | transient ApplicationService track state, prepared mixer/engine audibility, atomic panic acknowledgement | `neurogenic_solo_arm_panic_tests`, `neurogenic_reference_controller_tests`, generated Slint fixture | production PCM + semantic + generated callback | PASS in software; physical controls/LEDs NOT RUN |

Date: 2026-11-26. Baseline: Task 097C
`91c7bb66f748d0373b674ab5c54a6095fdebc4a5`.

Evidence categories are deliberately distinct: **production** means actual
application/controller/engine code; **protocol simulation** means framed bytes
through the production decoder; **generated** means generated Slint callbacks
or structured layout; **physical** requires manufactured hardware.

| Original task / finding | Acceptance requirement | Production implementation | Exact evidence | Category | Status / blocker |
|---|---|---|---|---|---|
| 096A / RC2-001 | One owner; correlated press/release; consumed chord | `InputGestureArbiter`, `ControllerSession` | `neurogenic_input_gesture_tests`, `neurogenic_reference_controller_tests` | production | PASS |
| 096A / RC2-001 | Interrupted SHIFT double tap | non-modifier input clears pending latch sequence | `neurogenic_input_gesture_tests`: intervening Play assertion | production, injected clock | PASS (fixed 097D) |
| 096A / RC2-001 | Modal/context/disconnect cancellation | existing generation transition and captured release | reference-controller modal rollback and Sound cancellation assertions | production | PASS for representative held-step/note; exhaustive modal matrix remains partial |
| 096B / RC2-002 | 67 controls and 16 destinations | V2 descriptor/profile and destination table | `neurogenic_reference_controller_tests`; `neurogenic_contextual_coverage_tests` | production | PASS software; physical NOT RUN |
| 096C / RC2-003 | Track/Page/Mute/Erase chords, Fill, transport | registered actions and stable chord targets | `neurogenic_reference_controller_tests` | production | PASS for supported bindings; missing algorithms stay Task 098 |
| 096C / RC2-003 | Sound note press/release | captured synth instance, note and release action | reference-controller PCM energy/release assertions | production/engine | PASS (sample-pad misrouting fixed 097D); external MIDI interruption matrix not complete |
| 096D / RC2-004 | Held-step property edit and coalesced history | one existing timeline edit group per gesture | velocity, Undo-depth and Undo/Redo assertions in reference-controller test | production | PASS |
| 096D / RC2-004 | Cancel edit group without orphaned mutation | cancel drops uncommitted `TimelineTransaction` | modal cancellation checks event value, group flag, Undo depth and stale release | production | PASS (strengthened 097D) |
| 096D / RC2-004 | Controller-authored lock affects playback | existing lock scheduler and prepared plan | prepared-plan event plus exact `AudioEngine::last_timeline_parameter_value()` assertion | production/engine | PASS (097D) |
| 096D / RC2-004 | Lock save/reopen and playback | project format + existing timeline loader | stable IDs/value/base plus fresh-engine exact applied value | persistence/engine | PASS (097D) |
| 096D / RC2-004 | Other pattern/base isolation | stable PatternId/TrackId/step target | reference-controller other-pattern and registry-base assertions | production | PASS |
| 096D / RC2-004 | Multi-step grouped edit | bounded stable selections (64) | two-step selected velocity assertion | production | PASS representative; cross-page lock removal/pruning matrix remains insufficiently tested |
| 097C / AUD2-007 | Explicit sample/slice audition target | optional stable `slot_id` / `slice_id` semantic arguments | contextual coverage descriptor assertions | production descriptor | PASS binding; engine audition cancellation matrix remains partial |
| 097C / AUD2-007 | Macro/Scene select without mutation | `macro.select.v1`, `macro.scene-select.v1` | contextual descriptor/action validation; action registry suite | production | PASS binding; full generated scenario remains partial |
| 096E / RC2-005 | Performance bank/layers/pads persistence | project-owned bank service | `neurogenic_performance_banks_tests`, Task 097B generated fixture | production/generated | PASS supported model |
| 096E / RC2-005 | Momentary captured release | existing active assignment ownership | performance-bank session tests | production | PASS for Fill; arbitrary actions remain intentionally unavailable |
| 096F/097B / RC2-006, AUD2-006 | Mapping proposal/conflict replacement | candidate-first mapping authoring | `neurogenic_controller_mapping_authoring_tests`, generated Setup fixture | production/generated | PASS supported active-profile workflow; profile library and extended fields OPEN |
| 096G / RC2-007 | Protocol negotiation/input recovery | protocol 1.3 host and firmware simulator | `neurogenic_controller_v2_protocol_tests` | protocol simulation | PASS software; USB/MCU physical NOT RUN |
| 096G / RC2-007 | Feedback overflow final convergence | bounded host output + forced full refresh | exact constrained/unconstrained logical LED equality before/after reconnect | protocol simulation | PASS (strengthened 097D) |
| 097A | Shared modal/navigation bridge | presentation-neutral interaction generation/focus | `neurogenic_interaction_navigation_tests`; generated Slint tests | production/generated | PASS representative; every historical modal not exhaustively bridged |
| 097B | All 48 Performance graphical positions | generated chooser/forms | generated Slint fixture enumerates 48 positions | generated | PASS callbacks/layout; full physical traversal NOT RUN |
| 097C | Sixteen-destination P/F/pad accounting | shared contextual/pad resolvers | `neurogenic_contextual_coverage_tests` | production descriptor | PASS supported/unavailable matrix |
| 095 / independent clients | No V2/Slint dependency | reusable application services | `neurogenic_client_independence_tests`, generic MIDI regressions | production | PASS |
| 097D | Generated reachability for every new reliability path | existing generated suite | Slint CTest at both resolutions | generated | PARTIAL: no new coordinate-input test; physical touch NOT RUN |
| 096G/097D | Palette on manufactured LEDs | logical palette and render packets | protocol tests only | physical | NOT RUN: PCB/firmware/electrical mapping unavailable |

## Missing capability ledger (not failed integration)

Typed musical clipboard, persistent repeated-event recording, generalized loop
configuration, remaining recording
mode/overdub and swing controls belong to Task 098. No unrelated action is used
as a substitute.

## Physical dependency ledger

G10, display/touchscreen, controller PCB/MCU, USB CDC reliability, electrical
RGB limits, debounce/rollover/ghosting, encoder quality, audio-interface latency
and sustained load are **NOT RUN**. Simulator and software-render evidence do
not close these rows.
| 097F | Appliance profile library and mapping configuration | RC2-006 / AUD2-006 | `ControllerProfileLibrary`, coordinator MIDI activation, dynamic Setup/Performance argument forms | `neurogenic_controller_profile_library_tests`, Slint adapter/rendered tests | production service + generated callback | PASS (software); dedicated USB switch and physical input NOT RUN | conservative layered-source conflict analysis |
