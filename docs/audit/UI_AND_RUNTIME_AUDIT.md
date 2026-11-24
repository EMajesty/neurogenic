# UI, runtime, controller and appliance audit

## UI architecture

The ownership boundary is sound: Slint receives copied `SlintUiState`, submits
semantic commands or parameter bindings, and has no engine pointers.
`apps/neurogenic.cpp` constructs one `ApplicationService`; SDL3, Slint and
headless modes are mutually selected. Independent `.slint` page components
share `performance.slint` as the shell.

### Confirmed behavior

- `SlintFrontendAdapter::refresh()` checks application and local revisions.
- Local page state (page/subview/selection/zoom/banking/staged preferences) uses
  `local_revision_`, so it need not wait for unrelated musical changes.
- The 16 launch positions are deterministically filled by ascending stable slot
  ID. This is tested, but it is an implicit ordering policy rather than a saved
  pad-placement model.
- Runtime setup data is copied through `SlintSetupServices`; mutable device and
  controller objects are not exposed to Slint.
- A 16 ms event-loop timer refreshes state; there is no UI work in the audio
  callback.

### Measured-by-inspection costs and risks

- Any relevant revision constructs a full `SlintUiState`, including inactive
  pages, waveform vectors, discovery-derived sound/FX data, browser/setup state,
  and mixer models (`slint_adapter.cpp::refresh`).
- Every publish allocates and replaces all Slint `VectorModel` objects,
  including inactive pages and waveform arrays (`slint_frontend.cpp::publish`).
- Transport frame and meters can change the application revision every poll,
  making this effectively frequent full reconstruction during playback.
- Context encoder turns call full `discover_application()` per gesture.
- Browser directory and preset enumeration are synchronous on the UI event
  loop, though bounded at 512 entries. Preference and controller-profile saves
  are also synchronous UI-thread filesystem operations.
- Waveform peaks are cached by sample/slice state in the application, but each
  UI publish recopies and transforms them; inactive Sample page updates pay the
  same cost.

No UI latency or allocation profile was recorded, so these are confirmed code
paths but only suspected user-visible performance risks. Task 082 should add
measurements before optimizing.

## Responsiveness and rendered UI

The Slint declarations use adaptive layouts, scrolling and a shared shell, and
the standalone supports 1920x1200 and 1024x600 dimensions. Tests do not render,
click, screenshot, measure touch targets, or detect clipping at either size.
`neurogenic_slint_missing_display` only proves graceful failure without a
display. Readability and touchscreen behavior therefore require validation.

There is no touchscreen keyboard. Several critical fields require paths,
preset IDs, logical control IDs, modes or profile paths, contradicting the goal
of operation without a physical keyboard.

## Controller architecture

The intended separation is mostly preserved:

- application: musical state/actions (`ApplicationService`);
- integration: profiles, sessions, pickup/context/feedback
  (`ControllerSession`, `ControllerCoordinator`);
- adapters: CDC protocol and MIDI translation;
- firmware: not present in this repository;
- Slint: independent semantic client.

Session context/modifiers/pickup are per device. Application selection remains
shared. A profile local-context page change does not drive Slint page selection,
and a Slint page change does not update controller-local pages; this is
consistent with independent contexts but may surprise users and is not
explicitly presented.

Confirmed concerns:

- `ControllerCoordinator::dispatch_midi_feedback()` holds the coordinator
  mutex while invoking the caller-provided sender. A blocking backend or
  reentrant callback can stall all coordinator operations; lock ordering is not
  documented.
- `midi_surface()` and `midi_profile()` return raw pointers after releasing the
  coordinator mutex. Current endpoints are not removed during normal running,
  but the API does not encode that lifetime guarantee.
- `ControllerCoordinator::poll()` holds its mutex across serial open/poll/write
  and application action dispatch. This is control-side, not audio-side, but
  increases shutdown/configuration contention.
- MIDI mapper registration is synchronized and endpoint destructors unregister;
  tests cover partial setup/lifetime. Physical CDC, third-party feedback,
  reconnect identity and multiple-device UX remain unvalidated.

No remaining GPIO, shift-register or LED-chain ordering was found in musical
application semantics. The dedicated profile is data, while protocol IDs remain
stable.

## Real-time and concurrency audit

No new Task 067–079 Slint path is callback-reachable. Browser, discovery,
preference and profile filesystem work is UI/control-side. Audio configuration
is staged rather than replaced live.

Existing engine publication paths use bounded queues and prepared immutable
objects. Sample reclamation occurs through control-side manager/engine methods;
active voices retain shared immutable versions. Mixer state reads lock a
control mutex, but `Mixer::process()` activates bounded publications without
that mutex.

Risks requiring targeted tooling/tests:

- Ordinary tests do not instrument allocator, lock, logging or filesystem calls
  on every callback-reachable path.
- `ApplicationService::snapshot()` holds its mutex while `synchronize_locked()`
  performs substantial control-side synchronization and state copying. It does
  not run in the audio callback, but frequent Slint/controller polling can
  contend with semantic commands.
- Coordinator sender callbacks under lock and raw endpoint access need a
  lifecycle/lock-order audit.
- Worker cancellation and shutdown are tested separately, not during combined
  sample analysis, processing, preset application, browser activity and GUI
  shutdown.

These are suspected risks, not confirmed audio-callback violations.

## Persistence and lifecycle

Project and preset writers use generation directories and rename-based commits
(`src/project/project.cpp`, `src/project/preset.cpp`). Tests cover round trips,
missing assets, failed application rollback and older formats. Controller
profiles and appliance preferences are deliberately outside musical projects.

Gaps:

- No one rendered-UI test covers dirty Save As → close → restart restoration.
- Browser does not expose project relinking, preset relinking/portable export,
  or recovery from media removal during an operation.
- Processing result save/publication and preset save are absent from Slint, so
  persistence code cannot make those UI workflows complete.
- Startup preference persistence is tested at service/model level, not through
  a real reboot/autostart cycle.
- Restart-required audio configuration is saved but cannot be reconciled with
  negotiated hardware until restart; Setup correctly labels this, but failure
  recovery UX is thin.

## Standalone appliance readiness

| Area | Software state | Remaining validation/blocker |
|---|---|---|
| Intel N150 | diagnostics/runbook exist | physical callback/XRUN/latency/thermal/soak data absent |
| Raspberry Pi 5 | portable/AArch64 builds and procedures | physical performance and device tests absent |
| 48 kHz | enforced and diagnosed | interface negotiation on target hardware |
| Multichannel interface | up to configured six channels and route validation | exact device capabilities, latency, external FX loop |
| RP2040 controller | protocol/profile/simulator | firmware and physical electrical/USB/feedback validation |
| MIDI fader surface | generic MIDI profiles/pickup | actual device profile, feedback and hotplug |
| Touchscreen | Slint mouse/touch semantics | calibration, gestures, text entry, both displays |
| Boot/autostart | run commands documented | supervised service, crash restart and boot integration not delivered |
| No-terminal configuration | partial Setup/Browser | on-screen keyboard; full MIDI/controller routing; startup recovery |
| Storage/removable media | project paths and bounded browser | mount/media lifecycle and relink UX |
| Shutdown | lifecycle and dirty guard | appliance power-button/OS shutdown integration intentionally absent |

The largest practical blockers are workflow/UI completeness and physical
validation, not lack of DSP breadth.
