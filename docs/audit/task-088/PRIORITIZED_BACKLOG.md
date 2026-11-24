# Task 088 prioritized backlog

Severity and release relevance are independent. “First” means required for the
first usable standalone-instrument claim.

| ID | Sev | Release | Effort | Finding / evidence | Proposed resolution and acceptance |
|---|---|---|---:|---|---|
| AUD2-001 | Resolved | First | L | **Pattern edit authority split.** Resolved by Task 089: nonzero selected pattern/track is the reusable edit address; zero is explicit legacy compatibility. | Production workflow edits isolated A/B patterns and locks, renders distinct MIDI, arranges, saves/reopens and rejects unsafe deletion. |
| AUD2-002 | Resolved | First | L | **Arrangement was read-only in Slint.** Resolved for core authoring by Task 089. | Slint creates pattern/audio clips, moves, resizes, duplicates, confirms deletion, edits loops and seeks through timeline transactions. Full DAW gestures remain expansion work. |
| AUD2-003 | Resolved (core) | First | M | **Core modulation/macro authoring.** Task 091 adds a registry-driven source chooser and parameters, source lifecycle, compatible destination selection, route creation/editing, macro assignment/range authoring and deterministic engine-observable persistence tests. | Detailed graph cable visualization and general history are not required for the core standalone workflow. |
| AUD2-004 | Resolved | First | S-M | **Missing udev aborted production-process tests.** Task 090 reproduced libremidi 5.4.2 `udev.hpp:101`: header discovery enabled an optional helper whose denied netlink monitor asserted. | Supported `LIBREMIDI_NO_UDEV` disables only metadata enrichment. Full Slint CTest and missing-exact-endpoint regression pass; ALSA sequencer MIDI remains enabled. Physical reconnect is still NOT RUN. |
| AUD2-005 | Resolved (software) | First | M | **Live-loop overdub/replace workflow.** Task 092 adds stable session selection, pass/phase/progress presentation, overdub, replacement, cancellation recovery, confirmed clear and WAV export. | Production semantic test proves exact 100-frame PCM capture, feedback overdub, replacement, revision-specific acknowledgement, export, project reopen and playback. Physical gaplessness remains NOT RUN. |
| AUD2-006 | Partial (Task 093) | First | M | **Manual controller mapping.** Setup now selects discovered action/parameter targets; profiles persist pickup, inversion, fine scale and acceleration alongside MIDI source modes. | Manual source creation still begins with MIDI learn; complex bound-argument authoring and conflict replacement need a focused follow-up. |
| AUD2-007 | Partial (Task 093) | First | M | **Physical contextual coverage.** Shared `page:bank` identities, Browser/Setup contexts, bank UI, stable arguments and physical launch simulation replace the generic fallback. | Complete final page-bank assignments and physical modal focus after panel layout freezes. |
| AUD2-008 | P2 | First | M | **Project/preset dependency recovery is not atomic across multiple assets.** | Prepared multi-relink transaction and persistent preset repair; failure must leave every reference and active version unchanged. |
| AUD2-009 | P2 | First | S-M | **Preset activation lacks coherent Undo/Redo.** | Capture before/after instrument/FX state in application history; undo after acknowledged activation restores valid audio/IDs. |
| AUD2-010 | Resolved (software) | First | M | **Default Nix package omitted Slint.** Task 090 makes pinned Slint 1.19.0 production the default with offline fixed-output Cargo dependencies. | x86_64 sandbox build, installed closure checks and virtual-X11 launch pass. Native AArch64 output/CI exists; Raspberry Pi 5 execution remains NOT RUN. |
| AUD2-011 | P2 | First | S-M | **Generated UI validation is shallow.** PPM size/dimensions only. | Add geometry/visibility/scroll assertions and a reviewed golden or structured layout policy for representative states; retain both resolutions. |
| AUD2-012 | P2 | First | M | **Frontend retains full snapshot/discovery costs.** | Measure representative projects, add domain snapshots/cached descriptors only where evidence warrants; preserve terminal acknowledgements. |
| AUD2-013 | P2 | First | S | **Browser scan/shutdown bounds.** 8192 cap hides later entries; worker join can wait on blocking I/O. | Cursor/chunk enumeration without fixed invisibility; documented bounded shutdown/cancellable provider and tests. |
| AUD2-014 | Resolved (software) | First | S-M | **Explicit performance pad mapping.** Schema 11 persists transport-independent logical positions with unassigned/reassign/clear behavior and deterministic legacy migration. | Physical layout and ergonomics remain NOT RUN; reference Slint currently renders sixteen positions. |
| AUD2-015 | P2 | Expansion | M | **FX/preset scope gaps.** Hard-coded add types; group/aux/master capture unavailable. | Populate from descriptors and extend existing preset format only where already representable. |
| AUD2-016 | P2 | Expansion | M | **Sample waveform interaction remains limited.** | Correct polarity/detail, marker hit testing, zoom/pan and physical navigation tests; no new DSP required. |
| AUD2-017 | Partial | Expansion | M | **Performance macro capture/recovery.** Task 091 adds standalone create/name, multi-destination assignment, inversion/removal, scene capture/update/recall/delete and project restoration. | General project-object Undo/Redo remains unresolved because current history is timeline-scoped; do not create a competing history stack. |
| AUD2-018 | P2 | Physical | L | **Reference appliance unvalidated.** All report rows NOT RUN. | Execute report on N150 and Pi 5 with actual audio/MIDI/controller/touch/storage; attach measurements. |
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
