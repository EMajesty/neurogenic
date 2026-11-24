# Task 088 prioritized backlog

Severity and release relevance are independent. “First” means required for the
first usable standalone-instrument claim.

| ID | Sev | Release | Effort | Finding / evidence | Proposed resolution and acceptance |
|---|---|---|---:|---|---|
| AUD2-001 | P1 | First | L | **Pattern edit authority split.** Step actions edit `Track::pattern`; selected/launched reusable patterns are `PatternDefinition`. | Add explicit reusable-pattern edit target, conversion/legacy policy and production UI. Tests must edit two patterns on one track, launch/arrange/save/reopen and render distinct audio. |
| AUD2-002 | P1 | First | L | **Arrangement is read-only in Slint.** Component exposes only toggle/select although semantic clip actions exist. | Add bounded pattern/audio clip create/move/resize/duplicate/delete/loop UI with transactions, confirmations and reference tests. |
| AUD2-003 | P1 | First | M | **Core modulation/macro authoring is incomplete.** Backend actions exist; Sound/FX mainly inspect/adjust. | Descriptor-driven create/source/destination/depth/remove and macro assignment/capture workflows; audible/state round trips and persistence. |
| AUD2-004 | P0/P1 | First | S-M | **Missing udev can abort production-process tests.** Direct Slint CTest: 8 aborts at libremidi `udev.hpp:101`, not graceful degradation. | Reproduce in clean environment; avoid assertion path or select safe backend; missing hardware must produce diagnostics and all affected tests pass without udev monitor. |
| AUD2-005 | P1 | First | M | **Live-loop overdub/replace surface incomplete.** Service and numeric tests exist; modal does not expose full session result lifecycle. | Complete Slint session selection, overdub/replace/clear/save with phase/PCM and failure tests. |
| AUD2-006 | P2 | First | M | **Manual controller mapping is incomplete.** Setup supports learn/remove/save, not descriptor-backed target, scaling or inversion editing. | Add compatible target chooser and implemented mapping fields with conflict/reconnect/persistence tests. |
| AUD2-007 | P2 | First | M | **Physical contextual coverage is sparse/generic.** Resolver has only a few broad contexts; Browser/Setup fall back to defaults. | Define shared page/subview banks, argument-preserving Slint copies and simulated dedicated-controller workflow tests. |
| AUD2-008 | P2 | First | M | **Project/preset dependency recovery is not atomic across multiple assets.** | Prepared multi-relink transaction and persistent preset repair; failure must leave every reference and active version unchanged. |
| AUD2-009 | P2 | First | S-M | **Preset activation lacks coherent Undo/Redo.** | Capture before/after instrument/FX state in application history; undo after acknowledged activation restores valid audio/IDs. |
| AUD2-010 | P2 | First | M | **Default Nix package omits Slint.** `flake.nix` sets `NEUROGENIC_ENABLE_SLINT=OFF`. | Add pinned sandboxed production Slint package and clean runtime test on x86_64; native AArch64 build evidence. |
| AUD2-011 | P2 | First | S-M | **Generated UI validation is shallow.** PPM size/dimensions only. | Add geometry/visibility/scroll assertions and a reviewed golden or structured layout policy for representative states; retain both resolutions. |
| AUD2-012 | P2 | First | M | **Frontend retains full snapshot/discovery costs.** | Measure representative projects, add domain snapshots/cached descriptors only where evidence warrants; preserve terminal acknowledgements. |
| AUD2-013 | P2 | First | S | **Browser scan/shutdown bounds.** 8192 cap hides later entries; worker join can wait on blocking I/O. | Cursor/chunk enumeration without fixed invisibility; documented bounded shutdown/cancellable provider and tests. |
| AUD2-014 | P2 | First | S-M | **Implicit performance pad mapping.** First sorted 16 slots become pads. | Persist explicit pad/slot presentation assignment independent of collection order; migration and launch tests. |
| AUD2-015 | P2 | Expansion | M | **FX/preset scope gaps.** Hard-coded add types; group/aux/master capture unavailable. | Populate from descriptors and extend existing preset format only where already representable. |
| AUD2-016 | P2 | Expansion | M | **Sample waveform interaction remains limited.** | Correct polarity/detail, marker hit testing, zoom/pan and physical navigation tests; no new DSP required. |
| AUD2-017 | P2 | Expansion | M | **Performance macro capture/recovery incomplete.** | Capture assignments/scenes through production UI and integrate safe history where feasible. |
| AUD2-018 | P2 | Physical | L | **Reference appliance unvalidated.** All report rows NOT RUN. | Execute report on N150 and Pi 5 with actual audio/MIDI/controller/touch/storage; attach measurements. |
| AUD2-019 | P3 | Experimental | XL | **Shared granular core deferred.** | After first musical workflow milestone, prototype one bounded core with sample, insert and rolling-buffer consumers; deterministic RT/audio tests. |

No confirmed irreversible data-loss path was found. `AUD2-004` is marked P0/P1
pending reproduction because it is a process abort; it was observed in tests but
not yet shown on the documented Nix production runtime.

## Release grouping

1. **First usable release:** AUD2-001–014, except optimizations may be accepted
   with measured limits; then physical validation AUD2-018.
2. **Musical expansion:** AUD2-015–017 plus deeper arrangement transforms.
3. **Refinement/experimental:** AUD2-019 and visual polish.
4. **Physical work:** AUD2-018 proceeds in parallel after crash/package fixes.
