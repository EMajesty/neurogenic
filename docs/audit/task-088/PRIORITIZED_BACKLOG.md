# Task 088 prioritized backlog

- **098E mandatory:** authoritative live sample-trigger, synth-note and external
  MIDI sequencer-event recording, including repeat capture and overdub.
- 098F: contextual loop, recording and overdub controls.
- 098G: quantization and swing.
- 099: temporary performance checkpoints.
- 100: consolidated implementation audit.

Task 098C implements bounded straight/triplet live repeat and matching-note
transport-aware live erase. Quantized persistent repeat recording remains open
because no live sequencer-event recording authority exists. Next capability is
the typed musical clipboard (Task 098D).

Task 098B implements additive track solo, single-owner track-channel record
arm, and unified panic. Remaining musical capability priority moves to bounded
note/triplet repeat, quantized repeated-event recording, and live erase.

Task 098A closes base metronome, recording count-in and tap tempo. Dedicated
cue routing, live-loop count-in, preference persistence and external-clock
hardware validation remain. Task 098B covers solo/arm and unified panic.

Severity and release relevance are independent. “First” means required for the
first usable standalone-instrument claim.

| ID | Sev | Release | Effort | Finding / evidence | Proposed resolution and acceptance |
|---|---|---|---:|---|---|
| AUD2-001 | Resolved | First | L | **Pattern edit authority split.** Resolved by Task 089: nonzero selected pattern/track is the reusable edit address; zero is explicit legacy compatibility. | Production workflow edits isolated A/B patterns and locks, renders distinct MIDI, arranges, saves/reopens and rejects unsafe deletion. |
| AUD2-002 | Resolved | First | L | **Arrangement was read-only in Slint.** Resolved for core authoring by Task 089. | Slint creates pattern/audio clips, moves, resizes, duplicates, confirms deletion, edits loops and seeks through timeline transactions. Full DAW gestures remain expansion work. |
| AUD2-003 | Resolved (core) | First | M | **Core modulation/macro authoring.** Task 091 adds a registry-driven source chooser and parameters, source lifecycle, compatible destination selection, route creation/editing, macro assignment/range authoring and deterministic engine-observable persistence tests. | Detailed graph cable visualization and general history are not required for the core standalone workflow. |
| AUD2-004 | Resolved | First | S-M | **Missing udev aborted production-process tests.** Task 090 reproduced libremidi 5.4.2 `udev.hpp:101`: header discovery enabled an optional helper whose denied netlink monitor asserted. | Supported `LIBREMIDI_NO_UDEV` disables only metadata enrichment. Full Slint CTest and missing-exact-endpoint regression pass; ALSA sequencer MIDI remains enabled. Physical reconnect is still NOT RUN. |
| AUD2-005 | Resolved (software) | First | M | **Live-loop overdub/replace workflow.** Task 092 adds stable session selection, pass/phase/progress presentation, overdub, replacement, cancellation recovery, confirmed clear and WAV export. | Production semantic test proves exact 100-frame PCM capture, feedback overdub, replacement, revision-specific acknowledgement, export, project reopen and playback. Physical gaplessness remains NOT RUN. |
| AUD2-006 | Partial (Task 097B) | First | S | **Manual controller mapping.** Generated Setup now covers supported manual MIDI sources, four descriptor-driven typed arguments, indexed contextual/Performance targets, pickup/invert/fine/acceleration, feedback enablement and structured conflict cancel/replace. | Add a coordinator-owned multi-profile create/rename/duplicate/select/remove/recovery authority; extend schema/presentation only for context/modifier and feedback modes actually supported by dispatch/protocol. |
| AUD2-007 | Partial (Task 097D software closure) | First | L | **Physical contextual coverage.** All sixteen destinations have shared definitions; controller-authored locks now have scheduled playback/save-reopen evidence, Sound notes have stable press/release, explicit sample/slice audition targets exist, and protocol feedback convergence is compared exactly. | Complete remaining graphical/physical-navigation cases and execute the physical V2 checklist; missing algorithms remain Task 098. Simulation is not hardware validation. |
| AUD2-008 | Resolved (software) | First | M | **Atomic project/preset dependency recovery.** Task 094 validates exact/prefix proposals as complete copies, publishes project samples in one acknowledged activation and persists preset repair by generation-atomic replacement. | Deterministic invalid-then-valid multi-asset tests preserve references on failure and restore active samples/library paths. Physical removable-media interruption remains NOT RUN. |
| AUD2-009 | Resolved (core) | First | S-M | **Preset activation history.** Acknowledged synth, track-sound and track FX activation records bounded before/after configuration in the one application history and restores through prepared activation. | FX activation Undo/Redo verifies effect order/state. Sample presets with unchanged stable slot sets are supported; slot-set-changing activation is explicitly non-undoable pending atomic slot removal. |
| AUD2-010 | Resolved (software) | First | M | **Default Nix package omitted Slint.** Task 090 makes pinned Slint 1.19.0 production the default with offline fixed-output Cargo dependencies. | x86_64 sandbox build, installed closure checks and virtual-X11 launch pass. Native AArch64 output/CI exists; Raspberry Pi 5 execution remains NOT RUN. |
| AUD2-011 | Partial (Task 095) | First | S-M | **Generated UI validation.** Production callbacks now cover representative history and accumulated workflows; actual shell geometry is asserted at both resolutions. | Complete modal/list reachability, accessibility and coordinate-driven coverage when the toolkit exposes a reliable input path; physical touch remains NOT RUN. |
| AUD2-012 | Partial (Task 095) | First | M | **Frontend snapshot/discovery costs.** Same-revision refresh reuses an immutable snapshot handle; benchmark separates snapshots, discovery and resolution. | Changed revisions still copy full state. Add narrower domains/invalidation only where continued measurements justify the complexity. |
| AUD2-013 | Partial (scalability resolved) | First | S | **Browser scan/shutdown bounds.** Task 094 removes the 8192 invisibility ceiling with fixed 1024-entry windows/128-row pages, bounded requests and stale-result rejection; an 8205-entry test reaches page 64. | One owned worker is safe and cancellation is checked between provider operations. Arbitrary uninterruptible kernel/network filesystem calls still have no portable strict join bound and remain documented. |
| AUD2-014 | Resolved (software) | First | S-M | **Explicit performance pad mapping.** Schema 11 persists transport-independent logical positions with unassigned/reassign/clear behavior and deterministic legacy migration. | Physical layout and ergonomics remain NOT RUN; reference Slint currently renders sixteen positions. |
| AUD2-015 | P2 | Expansion | M | **FX/preset scope gaps.** Hard-coded add types; group/aux/master capture unavailable. | Populate from descriptors and extend existing preset format only where already representable. |
| AUD2-016 | P2 | Expansion | M | **Sample waveform interaction remains limited.** | Correct polarity/detail, marker hit testing, zoom/pan and physical navigation tests; no new DSP required. |
| AUD2-017 | Resolved (authoring history) | Expansion | M | **Performance macro capture/recovery.** Task 091 provides authoring/persistence; Task 094 integrates assignment and scene authoring into acknowledged chronological application Undo/Redo. | Live macro gestures and scene recall intentionally remain performance state rather than flooding history. Temporary checkpoint implementation remains a separate RC2 milestone. |
| AUD2-018 | P2 | Physical | L | **Reference appliance unvalidated.** G10 is the intended primary x86 appliance and Pi 5 the secondary AArch64 target; all report rows remain NOT RUN. | Execute the G10/Pi 5 bring-up and validation checklists with actual audio/MIDI/controller/touch/storage; retain N150-compatible criteria where portable. |
| AUD2-019 | P3 | Experimental | XL | **Shared granular core deferred.** | After first musical workflow milestone, prototype one bounded core with sample, insert and rolling-buffer consumers; deterministic RT/audio tests. |

No confirmed irreversible data-loss path was found. Task 090 reproduced and
corrected the `AUD2-004` process abort in software; physical MIDI reconnect and
permission behavior remains part of `AUD2-018` rather than an unresolved crash.

## Release grouping

1. **First usable release:** AUD2-001–014, except optimizations may be accepted
   with measured limits; then physical validation AUD2-018.
2. **Musical expansion:** AUD2-015–017 plus deeper arrangement transforms.
3. **Refinement/experimental:** AUD2-019 and visual polish.
4. **Physical work:** AUD2-018 proceeds in parallel after crash/package fixes.

Task 097A closes the shared modal/focus foundation. Next is 097B graphical
assignment/profile lifecycle closure, then remaining contextual/pad coverage
and physical bring-up.
- Task 097F completed the software profile-library authority and dynamic
  mapping forms. Retain only dedicated USB runtime switching, refined
  mutually-exclusive source conflict analysis and physical navigation as
  controller-profile follow-up; do not reopen the frontend-owned library idea.
