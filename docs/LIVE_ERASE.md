# Transport-aware live erase

Task 098C extends ERASE without changing stopped step erase. After an explicit
PLAY/RESTART, during running Sequencer playback ERASE + pad captures a stable reusable-pattern, track,
pitch, owner and connection generation. The application follows the
authoritative transport tick and removes only matching note events whose
occurrences are crossed while the gesture is held.

The callback never mutates project data. A bounded control-side transaction
collects stable event IDs (at most 256 per observation and eight loop crossings
per observation), then publishes one immutable prepared timeline when the
gesture ends. That produces one unified application-history entry. Parameter
locks attached to a removed note are removed with that owning event; unrelated
events, locks, empty steps and unvisited positions remain unchanged.

Live erase does not require recording to be armed. It operates on the captured
selected reusable pattern and track and supports note/pad pitch matching. It
does not erase automation, audio clips, scenes, launches, or entire tracks.
Arrangement-wide erase is unavailable. A transport rewind resets the encounter
cursor without deleting an unvisited event.

Normal release commits encountered removals. Modal/context/disconnect cleanup
ends and commits the already encountered bounded gesture; project replacement
discards transient ownership. PANIC cancels repeating sound but does not
silently discard already committed musical edits. `sequencer.live-erase-cancel`
is reserved for an explicit rollback before publication.

The controller wire inventory and firmware protocol remain unchanged.
