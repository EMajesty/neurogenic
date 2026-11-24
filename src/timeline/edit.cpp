#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
MusicalTick wrap_tick(MusicalTick value, MusicalTick length) noexcept {
    value %= length;
    if (value < 0)
        value += length;
    return value;
}
MusicalTick shifted_tick(MusicalTick tick, MusicalTick offset,
                         MusicalTick length) noexcept {
    const auto normalized = offset % length;
    if (normalized >= 0)
        return tick >= length - normalized ? tick - (length - normalized)
                                           : tick + normalized;
    const auto magnitude = static_cast<MusicalTick>(
        static_cast<std::uint64_t>(-(normalized + 1)) + 1);
    return tick < magnitude ? length - (magnitude - tick) : tick - magnitude;
}
bool valid_note_values(MusicalTick tick, std::uint8_t pitch, float velocity,
                       MusicalTick duration, MusicalTick length) noexcept {
    return tick >= 0 && tick < length && pitch <= 127 &&
           std::isfinite(velocity) && velocity >= 0 && velocity <= 1 &&
           duration > 0 &&
           duration <= std::numeric_limits<MusicalTick>::max() - tick;
}
MusicalEvent *find_event(MusicalPattern &pattern, EventId id) noexcept {
    for (auto &event : pattern.events)
        if (event.id == id)
            return &event;
    return nullptr;
}
} // namespace

std::uint64_t DeterministicRandom::next_u64() noexcept {
    state_ += 0x9e3779b97f4a7c15ULL;
    auto value = state_;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
double DeterministicRandom::unit() noexcept {
    return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
}
bool DeterministicRandom::chance(double probability) {
    if (!std::isfinite(probability) || probability < 0 || probability > 1)
        throw std::invalid_argument("probability must be between 0 and 1");
    return unit() < probability;
}
std::size_t DeterministicRandom::index(std::size_t size) {
    if (size == 0)
        throw std::invalid_argument("random selection pool must not be empty");
    return static_cast<std::size_t>(next_u64() % size);
}

Track *TimelineTransaction::find_track(TrackId id) noexcept {
    for (auto &track : timeline_.tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}
const Track *TimelineTransaction::find_track(TrackId id) const noexcept {
    for (const auto &track : timeline_.tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}
MusicalPattern *TimelineTransaction::find_edit_pattern(PatternId pattern_id,
                                                        TrackId track_id) noexcept {
    if (pattern_id == 0) {
        auto *track = find_track(track_id);
        return track ? &track->pattern : nullptr;
    }
    const auto definition = std::find_if(
        timeline_.arrangement.patterns.begin(),
        timeline_.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    if (definition == timeline_.arrangement.patterns.end()) return nullptr;
    const auto track = std::find_if(
        definition->tracks.begin(), definition->tracks.end(),
        [=](const auto &value) { return value.track == track_id; });
    return track == definition->tracks.end() ? nullptr : &track->pattern;
}
const MusicalPattern *TimelineTransaction::find_edit_pattern(
    PatternId pattern_id, TrackId track_id) const noexcept {
    if (pattern_id == 0) {
        const auto *track = find_track(track_id);
        return track ? &track->pattern : nullptr;
    }
    const auto definition = std::find_if(
        timeline_.arrangement.patterns.begin(),
        timeline_.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    if (definition == timeline_.arrangement.patterns.end()) return nullptr;
    const auto track = std::find_if(
        definition->tracks.begin(), definition->tracks.end(),
        [=](const auto &value) { return value.track == track_id; });
    return track == definition->tracks.end() ? nullptr : &track->pattern;
}
bool TimelineTransaction::ensure_pattern_track(PatternId pattern_id,
                                               TrackId track_id,
                                               std::string &error) {
    if (!find_track(track_id)) {
        error = "track does not exist";
        return false;
    }
    if (pattern_id == 0) {
        error.clear();
        return true;
    }
    auto definition = std::find_if(
        timeline_.arrangement.patterns.begin(),
        timeline_.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    if (definition == timeline_.arrangement.patterns.end()) {
        error = "pattern does not exist";
        return false;
    }
    if (!find_edit_pattern(pattern_id, track_id))
        definition->tracks.push_back({track_id, {definition->length, {}}});
    error.clear();
    return true;
}
bool TimelineTransaction::create_track(TrackId id, std::string name,
                                       MusicalTick length, std::string &error) {
    if (id == 0 || length <= 0) {
        error = "track ID and length must be positive";
        return false;
    }
    if (find_track(id)) {
        error = "track ID already exists";
        return false;
    }
    if (timeline_.tracks.size() >= limits_.max_tracks) {
        error = "timeline track limit exceeded";
        return false;
    }
    timeline_.tracks.push_back({id, std::move(name), false, {length, {}}});
    error.clear();
    return true;
}
bool TimelineTransaction::delete_track(TrackId id, std::string &error) {
    const auto found =
        std::find_if(timeline_.tracks.begin(), timeline_.tracks.end(),
                     [id](const Track &track) { return track.id == id; });
    if (found == timeline_.tracks.end()) {
        error = "track does not exist";
        return false;
    }
    const auto &arrangement = timeline_.arrangement;
    const bool referenced =
        std::any_of(arrangement.patterns.begin(), arrangement.patterns.end(),
                    [=](const auto &pattern) {
                        return std::any_of(pattern.tracks.begin(),
                            pattern.tracks.end(), [=](const auto &track) {
                                return track.track == id &&
                                       !track.pattern.events.empty();
                            });
                    }) ||
        std::any_of(arrangement.audio_clips.begin(), arrangement.audio_clips.end(),
                    [=](const auto &clip) { return clip.track == id; }) ||
        std::any_of(arrangement.launch_slots.begin(), arrangement.launch_slots.end(),
                    [=](const auto &slot) { return slot.track == id; }) ||
        std::any_of(arrangement.performance_scenes.begin(),
                    arrangement.performance_scenes.end(), [=](const auto &scene) {
                        return std::any_of(scene.entries.begin(), scene.entries.end(),
                            [=](const auto &entry) { return entry.track == id; });
                    });
    if (referenced) {
        error = "track is referenced by reusable musical material";
        return false;
    }
    for (auto &pattern : timeline_.arrangement.patterns)
        std::erase_if(pattern.tracks,
                      [=](const auto &track) { return track.track == id; });
    timeline_.tracks.erase(found);
    error.clear();
    return true;
}
bool TimelineTransaction::set_track_name(TrackId id, std::string name,
                                         std::string &error) {
    auto *track = find_track(id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    track->name = std::move(name);
    error.clear();
    return true;
}
bool TimelineTransaction::set_track_length(TrackId id, MusicalTick length,
                                           std::string &error) {
    auto *track = find_track(id);
    if (!track || length <= 0) {
        error =
            !track ? "track does not exist" : "track length must be positive";
        return false;
    }
    for (const auto &event : track->pattern.events)
        if (event.tick >= length) {
            error = "new track length excludes existing events";
            return false;
        }
    track->pattern.length = length;
    error.clear();
    return true;
}
bool TimelineTransaction::set_track_muted(TrackId id, bool muted,
                                          std::string &error) {
    auto *track = find_track(id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    track->muted = muted;
    error.clear();
    return true;
}
bool TimelineTransaction::set_track_instrument(TrackId id,
                                               std::string instrument,
                                               std::string &error,
                                               InstrumentInstanceId instance) {
    auto *track = find_track(id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    if (instrument != sample_instrument_id &&
        instrument != subtractive_instrument_id &&
        instrument != external_midi_instrument_id) {
        error = "instrument is unavailable";
        return false;
    }
    if (instrument == subtractive_instrument_id &&
        (instance == 0 || instance > maximum_instrument_instances)) {
        error = "instrument instance is out of range";
        return false;
    }
    track->instrument_id = std::move(instrument);
    track->instrument_instance_id = instance;
    error.clear();
    return true;
}
bool TimelineTransaction::set_track_midi_destination(TrackId id,
                                                     std::uint32_t port,
                                                     std::uint8_t channel,
                                                     std::string &error) {
    auto *track = find_track(id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    if (port == 0 || port > maximum_midi_ports || channel > 15) {
        error = "MIDI destination requires port 1..8 and channel 1..16";
        return false;
    }
    track->instrument_id = std::string(external_midi_instrument_id);
    track->midi_output_port = port;
    track->midi_channel = channel;
    error.clear();
    return true;
}
bool TimelineTransaction::add_note(TrackId track_id, EventId id,
                                   MusicalTick tick, std::uint8_t pitch,
                                   float velocity, MusicalTick duration,
                                   std::string &error) {
    return add_note(0, track_id, id, tick, pitch, velocity, duration, error);
}
bool TimelineTransaction::add_note(PatternId pattern_id, TrackId track_id,
                                   EventId id, MusicalTick tick,
                                   std::uint8_t pitch, float velocity,
                                   MusicalTick duration, std::string &error) {
    if (!ensure_pattern_track(pattern_id, track_id, error)) return false;
    auto *pattern = find_edit_pattern(pattern_id, track_id);
    std::size_t event_count{};
    for (const auto &item : timeline_.tracks)
        event_count += item.pattern.events.size();
    for (const auto &definition : timeline_.arrangement.patterns)
        for (const auto &item : definition.tracks)
            event_count += item.pattern.events.size();
    if (event_count >= limits_.max_events) {
        error = "timeline event limit exceeded";
        return false;
    }
    if (id == 0 || find_event(*pattern, id)) {
        error = "event ID must be nonzero and unique in its track";
        return false;
    }
    if (!valid_note_values(tick, pitch, velocity, duration,
                           pattern->length)) {
        error = "invalid note values";
        return false;
    }
    pattern->events.push_back({id,
                                     tick,
                                     MusicalEventType::note,
                                     pitch,
                                     velocity,
                                     duration,
                                     0,
                                     0,
                                     0,
                                     {}});
    error.clear();
    return true;
}
bool TimelineTransaction::modify_note(TrackId track_id, EventId id,
                                      MusicalTick tick, std::uint8_t pitch,
                                      float velocity, MusicalTick duration,
                                      std::string &error) {
    return modify_note(0, track_id, id, tick, pitch, velocity, duration, error);
}
bool TimelineTransaction::modify_note(PatternId pattern_id, TrackId track_id,
                                      EventId id, MusicalTick tick,
                                      std::uint8_t pitch, float velocity,
                                      MusicalTick duration,
                                      std::string &error) {
    auto *pattern = find_edit_pattern(pattern_id, track_id);
    auto *event = pattern ? find_event(*pattern, id) : nullptr;
    if (!event || event->type != MusicalEventType::note) {
        error = "note event does not exist";
        return false;
    }
    if (!valid_note_values(tick, pitch, velocity, duration,
                           pattern->length)) {
        error = "invalid note values";
        return false;
    }
    event->tick = tick;
    event->pitch = pitch;
    event->velocity = velocity;
    event->duration = duration;
    error.clear();
    return true;
}
bool TimelineTransaction::add_parameter(TrackId track_id, EventId id,
                                        MusicalTick tick,
                                        std::uint32_t parameter, float value,
                                        std::string &error) {
    auto *track = find_track(track_id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    std::size_t event_count{};
    for (const auto &item : timeline_.tracks)
        event_count += item.pattern.events.size();
    if (event_count >= limits_.max_events) {
        error = "timeline event limit exceeded";
        return false;
    }
    if (id == 0 || find_event(track->pattern, id)) {
        error = "event ID must be nonzero and unique in its track";
        return false;
    }
    if (tick < 0 || tick >= track->pattern.length || !std::isfinite(value)) {
        error = "invalid parameter event values";
        return false;
    }
    MusicalEvent event;
    event.id = id;
    event.tick = tick;
    event.type = MusicalEventType::parameter;
    event.parameter_id = parameter;
    event.value = value;
    track->pattern.events.push_back(event);
    error.clear();
    return true;
}
bool TimelineTransaction::modify_parameter(TrackId track_id, EventId id,
                                           MusicalTick tick,
                                           std::uint32_t parameter, float value,
                                           std::string &error) {
    auto *track = find_track(track_id);
    auto *event = track ? find_event(track->pattern, id) : nullptr;
    if (!event || event->type != MusicalEventType::parameter) {
        error = "parameter event does not exist";
        return false;
    }
    if (tick < 0 || tick >= track->pattern.length || !std::isfinite(value)) {
        error = "invalid parameter event values";
        return false;
    }
    event->tick = tick;
    event->parameter_id = parameter;
    event->value = value;
    error.clear();
    return true;
}
bool TimelineTransaction::add_parameter_destination(
    TrackId track_id, EventId id, MusicalTick tick, std::string destination,
    float value, std::string &error) {
    return add_parameter_destination(0, track_id, id, tick,
                                     std::move(destination), value, error);
}
bool TimelineTransaction::add_parameter_destination(
    PatternId pattern_id, TrackId track_id, EventId id, MusicalTick tick,
    std::string destination, float value, std::string &error) {
    if (destination.empty()) {
        error = "parameter destination is required";
        return false;
    }
    if (!ensure_pattern_track(pattern_id, track_id, error)) return false;
    auto *pattern = find_edit_pattern(pattern_id, track_id);
    std::size_t event_count{};
    for (const auto &item : timeline_.tracks)
        event_count += item.pattern.events.size();
    for (const auto &definition : timeline_.arrangement.patterns)
        for (const auto &item : definition.tracks)
            event_count += item.pattern.events.size();
    if (event_count >= limits_.max_events) {
        error = "timeline event limit exceeded";
        return false;
    }
    if (id == 0 || find_event(*pattern, id) || tick < 0 ||
        tick >= pattern->length || !std::isfinite(value)) {
        error = "invalid parameter event values";
        return false;
    }
    MusicalEvent parameter;
    parameter.id = id;
    parameter.tick = tick;
    parameter.type = MusicalEventType::parameter;
    parameter.value = value;
    pattern->events.push_back(std::move(parameter));
    auto *event = find_event(*pattern, id);
    event->parameter_destination = std::move(destination);
    error.clear();
    return true;
}
bool TimelineTransaction::modify_parameter_destination(
    TrackId track_id, EventId id, MusicalTick tick, std::string destination,
    float value, std::string &error) {
    return modify_parameter_destination(0, track_id, id, tick,
                                        std::move(destination), value, error);
}
bool TimelineTransaction::modify_parameter_destination(
    PatternId pattern_id, TrackId track_id, EventId id, MusicalTick tick,
    std::string destination, float value, std::string &error) {
    if (destination.empty()) {
        error = "parameter destination is required";
        return false;
    }
    auto *pattern = find_edit_pattern(pattern_id, track_id);
    auto *event = pattern ? find_event(*pattern, id) : nullptr;
    if (!event || event->type != MusicalEventType::parameter) {
        error = "parameter event does not exist";
        return false;
    }
    if (tick < 0 || tick >= pattern->length || !std::isfinite(value)) {
        error = "invalid parameter event values";
        return false;
    }
    event->tick = tick;
    event->parameter_id = 0;
    event->parameter_destination = std::move(destination);
    event->value = value;
    error.clear();
    return true;
}
bool TimelineTransaction::remove_event(TrackId track_id, EventId id,
                                       std::string &error) {
    return remove_event(0, track_id, id, error);
}
bool TimelineTransaction::remove_event(PatternId pattern_id, TrackId track_id,
                                       EventId id, std::string &error) {
    auto *pattern = find_edit_pattern(pattern_id, track_id);
    if (!pattern) {
        error = pattern_id ? "pattern track does not exist"
                           : "track does not exist";
        return false;
    }
    const auto found = std::find_if(
        pattern->events.begin(), pattern->events.end(),
        [id](const MusicalEvent &event) { return event.id == id; });
    if (found == pattern->events.end()) {
        error = "event does not exist";
        return false;
    }
    pattern->events.erase(found);
    error.clear();
    return true;
}
bool TimelineTransaction::set_tempo_bpm(double bpm, std::string &error) {
    if (!std::isfinite(bpm) || bpm <= 0 || bpm > 1000) {
        error = "tempo must be greater than 0 and at most 1000 BPM";
        return false;
    }
    const auto micros = std::llround(60'000'000.0 / bpm);
    if (micros <= 0 || micros > std::numeric_limits<std::uint32_t>::max()) {
        error = "tempo is outside the supported range";
        return false;
    }
    timeline_.transport.microseconds_per_quarter =
        static_cast<std::uint32_t>(micros);
    error.clear();
    return true;
}
bool TimelineTransaction::set_time_signature(std::uint16_t numerator,
                                             std::uint16_t denominator,
                                             std::string &error) {
    if (numerator == 0 || denominator == 0 ||
        (denominator & (denominator - 1)) != 0) {
        error = "time signature requires a positive numerator and power-of-two "
                "denominator";
        return false;
    }
    timeline_.transport.time_signature_numerator = numerator;
    timeline_.transport.time_signature_denominator = denominator;
    error.clear();
    return true;
}
bool TimelineTransaction::create_pattern(PatternId id, std::string name,
                                         MusicalTick length,
                                         std::string &error) {
    auto &patterns = timeline_.arrangement.patterns;
    if (!id || name.empty() || length <= 0 ||
        std::any_of(patterns.begin(), patterns.end(),
                    [=](const auto &p) { return p.id == id; })) {
        error = "invalid or duplicate pattern";
        return false;
    }
    patterns.push_back({id,
                        std::move(name),
                        length,
                        timeline_.transport.time_signature_numerator,
                        timeline_.transport.time_signature_denominator,
                        {},
                        {}});
    error.clear();
    return true;
}
bool TimelineTransaction::duplicate_pattern(PatternId source,
                                            PatternId destination,
                                            std::string name,
                                            std::string &error) {
    const auto found =
        std::find_if(timeline_.arrangement.patterns.begin(),
                     timeline_.arrangement.patterns.end(),
                     [=](const auto &p) { return p.id == source; });
    if (found == timeline_.arrangement.patterns.end()) {
        error = "source pattern does not exist";
        return false;
    }
    const auto source_copy = *found;
    if (!create_pattern(destination, std::move(name), source_copy.length,
                        error))
        return false;
    auto &copy = timeline_.arrangement.patterns.back();
    copy.time_signature_numerator = source_copy.time_signature_numerator;
    copy.time_signature_denominator = source_copy.time_signature_denominator;
    copy.tracks = source_copy.tracks;
    error.clear();
    return true;
}
bool TimelineTransaction::rename_pattern(PatternId id, std::string name,
                                          std::string &error) {
    const auto found = std::find_if(timeline_.arrangement.patterns.begin(),
                                    timeline_.arrangement.patterns.end(),
                                    [=](const auto &p) { return p.id == id; });
    if (found == timeline_.arrangement.patterns.end() || name.empty()) {
        error = "pattern does not exist or name is empty";
        return false;
    }
    found->name = std::move(name);
    error.clear();
    return true;
}
bool TimelineTransaction::set_pattern_length(PatternId id, MusicalTick length,
                                             std::string &error) {
    const auto pattern = std::find_if(timeline_.arrangement.patterns.begin(),
        timeline_.arrangement.patterns.end(),
        [=](const auto &candidate) { return candidate.id == id; });
    if (pattern == timeline_.arrangement.patterns.end() || length <= 0) {
        error = pattern == timeline_.arrangement.patterns.end()
                    ? "pattern does not exist"
                    : "pattern length must be positive";
        return false;
    }
    for (const auto &track : pattern->tracks)
        for (const auto &event : track.pattern.events)
            if (event.tick >= length) {
                error = "new pattern length excludes existing events";
                return false;
            }
    pattern->length = length;
    for (auto &track : pattern->tracks)
        track.pattern.length = length;
    error.clear();
    return true;
}
bool TimelineTransaction::delete_pattern(PatternId id, std::string &error) {
    auto &a = timeline_.arrangement;
    if (std::any_of(a.clips.begin(), a.clips.end(),
                    [=](const auto &c) { return c.pattern == id; }) ||
        std::find(a.pattern_chain.begin(), a.pattern_chain.end(), id) !=
            a.pattern_chain.end() ||
        std::any_of(a.chain_entries.begin(), a.chain_entries.end(),
                    [=](const auto &entry) { return entry.pattern == id; }) ||
        std::any_of(a.launch_slots.begin(), a.launch_slots.end(),
                    [=](const auto &slot) {
                        return slot.source_type == LaunchSourceType::pattern &&
                               slot.source_id == id;
                    }) ||
        std::any_of(a.patterns.begin(), a.patterns.end(),
                    [=](const auto &pattern) {
                        return pattern.follow.type ==
                                   FollowActionType::specific &&
                               pattern.follow.target == id;
                    }) ||
        a.active_pattern == id || a.queued_pattern == id ||
        a.return_live_pattern == id) {
        error = "pattern is referenced by playback state";
        return false;
    }
    const auto before = a.patterns.size();
    std::erase_if(a.patterns, [=](const auto &p) { return p.id == id; });
    if (a.patterns.size() == before) {
        error = "pattern does not exist";
        return false;
    }
    error.clear();
    return true;
}
bool TimelineTransaction::set_pattern_track(PatternId pattern_id,
                                            TrackId track_id,
                                            MusicalPattern pattern_value,
                                            std::string &error) {
    auto found =
        std::find_if(timeline_.arrangement.patterns.begin(),
                     timeline_.arrangement.patterns.end(),
                     [=](const auto &p) { return p.id == pattern_id; });
    if (found == timeline_.arrangement.patterns.end() ||
        !find_track(track_id) || pattern_value.length <= 0) {
        error = "pattern or track does not exist";
        return false;
    }
    auto target =
        std::find_if(found->tracks.begin(), found->tracks.end(),
                     [=](const auto &p) { return p.track == track_id; });
    if (target == found->tracks.end())
        found->tracks.push_back({track_id, std::move(pattern_value)});
    else
        target->pattern = std::move(pattern_value);
    error.clear();
    return true;
}
bool TimelineTransaction::create_clip(ArrangementClip clip,
                                      std::string &error) {
    if (!clip.id ||
        std::any_of(timeline_.arrangement.clips.begin(),
                    timeline_.arrangement.clips.end(),
                    [&](const auto &c) { return c.id == clip.id; })) {
        error = "invalid or duplicate clip";
        return false;
    }
    timeline_.arrangement.clips.push_back(std::move(clip));
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement.clips.pop_back();
        return false;
    }
    return true;
}
bool TimelineTransaction::update_clip(ArrangementClip clip,
                                      std::string &error) {
    auto found = std::find_if(timeline_.arrangement.clips.begin(),
                              timeline_.arrangement.clips.end(),
                              [&](const auto &c) { return c.id == clip.id; });
    if (found == timeline_.arrangement.clips.end()) {
        error = "clip does not exist";
        return false;
    }
    const auto previous = *found;
    *found = std::move(clip);
    if (!validate_arrangement(timeline_, error)) {
        *found = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::delete_clip(ClipId id, std::string &error) {
    const auto before = timeline_.arrangement.clips.size();
    std::erase_if(timeline_.arrangement.clips,
                  [=](const auto &c) { return c.id == id; });
    if (timeline_.arrangement.clips.size() == before) {
        error = "clip does not exist";
        return false;
    }
    error.clear();
    return true;
}
bool TimelineTransaction::create_audio_clip(AudioClip clip,
                                            std::string &error) {
    if (!clip.id || std::any_of(timeline_.arrangement.audio_clips.begin(),
                                timeline_.arrangement.audio_clips.end(),
                                [&](const auto &value) {
                                    return value.id == clip.id;
                                })) {
        error = "invalid or duplicate audio clip";
        return false;
    }
    timeline_.arrangement.audio_clips.push_back(std::move(clip));
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement.audio_clips.pop_back();
        return false;
    }
    return true;
}
bool TimelineTransaction::update_audio_clip(AudioClip clip,
                                            std::string &error) {
    auto found = std::find_if(timeline_.arrangement.audio_clips.begin(),
                              timeline_.arrangement.audio_clips.end(),
                              [&](const auto &value) {
                                  return value.id == clip.id;
                              });
    if (found == timeline_.arrangement.audio_clips.end()) {
        error = "audio clip does not exist";
        return false;
    }
    const auto previous = *found;
    *found = std::move(clip);
    if (!validate_arrangement(timeline_, error)) {
        *found = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::delete_audio_clip(AudioClipId id,
                                            std::string &error) {
    const auto before = timeline_.arrangement.audio_clips.size();
    std::erase_if(timeline_.arrangement.audio_clips,
                  [=](const auto &clip) { return clip.id == id; });
    if (timeline_.arrangement.audio_clips.size() == before) {
        error = "audio clip does not exist";
        return false;
    }
    std::erase_if(timeline_.arrangement.audio_crossfades,
                  [=](const auto &crossfade) {
                      return crossfade.outgoing_clip == id ||
                             crossfade.incoming_clip == id;
                  });
    error.clear();
    return true;
}
bool TimelineTransaction::create_audio_crossfade(AudioClipCrossfade crossfade,
                                                 std::string &error) {
    if (!crossfade.id || std::any_of(timeline_.arrangement.audio_crossfades.begin(),
                                     timeline_.arrangement.audio_crossfades.end(),
                                     [&](const auto &value) { return value.id == crossfade.id; })) {
        error = "invalid or duplicate audio crossfade";
        return false;
    }
    timeline_.arrangement.audio_crossfades.push_back(crossfade);
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement.audio_crossfades.pop_back();
        return false;
    }
    return true;
}
bool TimelineTransaction::update_audio_crossfade(AudioClipCrossfade crossfade,
                                                 std::string &error) {
    auto found = std::find_if(timeline_.arrangement.audio_crossfades.begin(),
                              timeline_.arrangement.audio_crossfades.end(),
                              [&](const auto &value) { return value.id == crossfade.id; });
    if (found == timeline_.arrangement.audio_crossfades.end()) {
        error = "audio crossfade does not exist";
        return false;
    }
    const auto previous = *found;
    *found = crossfade;
    if (!validate_arrangement(timeline_, error)) {
        *found = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::delete_audio_crossfade(AudioCrossfadeId id,
                                                 std::string &error) {
    const auto before = timeline_.arrangement.audio_crossfades.size();
    std::erase_if(timeline_.arrangement.audio_crossfades,
                  [=](const auto &value) { return value.id == id; });
    if (before == timeline_.arrangement.audio_crossfades.size()) {
        error = "audio crossfade does not exist";
        return false;
    }
    error.clear();
    return true;
}
bool TimelineTransaction::update_arrangement_automation(EventId id,
                                                        MusicalTick tick,
                                                        float value,
                                                        std::string &error) {
    auto &events = timeline_.arrangement.automation;
    const auto found =
        std::find_if(events.begin(), events.end(),
                     [=](const auto &event) { return event.id == id; });
    if (found == events.end() || tick < 0 || !std::isfinite(value)) {
        error = found == events.end() ? "arrangement automation does not exist"
                                      : "invalid arrangement automation value";
        return false;
    }
    found->tick = tick;
    found->value = value;
    error.clear();
    return true;
}
bool TimelineTransaction::delete_arrangement_automation(EventId id,
                                                        std::string &error) {
    auto &events = timeline_.arrangement.automation;
    const auto found =
        std::find_if(events.begin(), events.end(),
                     [=](const auto &event) { return event.id == id; });
    if (found == events.end()) {
        error = "arrangement automation does not exist";
        return false;
    }
    events.erase(found);
    error.clear();
    return true;
}
bool TimelineTransaction::set_playback_mode(TimelinePlaybackMode mode,
                                            std::string &error) {
    const auto previous = timeline_.arrangement.playback_mode;
    timeline_.arrangement.playback_mode = mode;
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement.playback_mode = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::launch_pattern(PatternId id, bool queue,
                                         std::string &error) {
    if (std::none_of(timeline_.arrangement.patterns.begin(),
                     timeline_.arrangement.patterns.end(),
                     [=](const auto &p) { return p.id == id; })) {
        error = "pattern does not exist";
        return false;
    }
    if (queue)
        timeline_.arrangement.queued_pattern = id;
    else {
        timeline_.arrangement.active_pattern = id;
        timeline_.arrangement.queued_pattern = 0;
        timeline_.arrangement.playback_mode =
            TimelinePlaybackMode::live_pattern;
    }
    error.clear();
    return true;
}
bool TimelineTransaction::set_song_range(MusicalTick start, MusicalTick end,
                                         std::string &error) {
    if (start < 0 || end <= start) {
        error = "invalid song range";
        return false;
    }
    timeline_.arrangement.song_start = start;
    timeline_.arrangement.song_end = end;
    error.clear();
    return true;
}
bool TimelineTransaction::set_loop_region(bool enabled, MusicalTick start,
                                          MusicalTick end, std::string &error) {
    if (enabled && (start < timeline_.arrangement.song_start || end <= start ||
                    end > timeline_.arrangement.song_end)) {
        error = "invalid arrangement loop region";
        return false;
    }
    timeline_.arrangement.loop_enabled = enabled;
    timeline_.arrangement.loop_start = start;
    timeline_.arrangement.loop_end = end;
    error.clear();
    return true;
}
bool TimelineTransaction::set_pattern_chain(
    PatternChainId id, std::vector<PatternChainEntry> entries, bool loop,
    std::string &error) {
    if (!id || entries.empty() || entries.size() > 4096) {
        error = "pattern chain ID and entries are required";
        return false;
    }
    const auto previous = timeline_.arrangement;
    timeline_.arrangement.pattern_chain_id = id;
    timeline_.arrangement.chain_entries = std::move(entries);
    timeline_.arrangement.chain_loop = loop;
    timeline_.arrangement.pattern_chain_position = 0;
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::set_chain_running(bool running,
                                            std::size_t start_entry,
                                            std::string &error) {
    auto &a = timeline_.arrangement;
    if (running &&
        (a.chain_entries.empty() || start_entry >= a.chain_entries.size())) {
        error = "pattern chain start entry is invalid";
        return false;
    }
    a.chain_running = running;
    a.pattern_chain_position = running ? start_entry : 0;
    if (running) {
        a.playback_mode = TimelinePlaybackMode::live_pattern;
        a.active_pattern = a.chain_entries[start_entry].pattern;
    }
    error.clear();
    return true;
}
bool TimelineTransaction::set_follow_action(PatternId id,
                                            PatternFollowAction action,
                                            std::string &error) {
    const auto found = std::find_if(timeline_.arrangement.patterns.begin(),
                                    timeline_.arrangement.patterns.end(),
                                    [=](const auto &p) { return p.id == id; });
    if (found == timeline_.arrangement.patterns.end()) {
        error = "pattern does not exist";
        return false;
    }
    const auto previous = found->follow;
    found->follow = action;
    if (!validate_arrangement(timeline_, error)) {
        found->follow = previous;
        return false;
    }
    return true;
}
bool TimelineTransaction::set_launch_quantization(LaunchQuantization value,
                                                  std::string &error) {
    timeline_.arrangement.launch_quantization = value;
    error.clear();
    return true;
}
bool TimelineTransaction::set_voice_transition(VoiceTransitionPolicy value,
                                               std::string &error) {
    timeline_.arrangement.voice_transition = value;
    error.clear();
    return true;
}
bool TimelineTransaction::set_song_end_behavior(SongEndBehavior behavior,
                                                PatternId return_pattern,
                                                std::string &error) {
    const auto previous_behavior = timeline_.arrangement.song_end_behavior;
    const auto previous_pattern = timeline_.arrangement.return_live_pattern;
    timeline_.arrangement.song_end_behavior = behavior;
    timeline_.arrangement.return_live_pattern = return_pattern;
    if (!validate_arrangement(timeline_, error)) {
        timeline_.arrangement.song_end_behavior = previous_behavior;
        timeline_.arrangement.return_live_pattern = previous_pattern;
        return false;
    }
    return true;
}
PatternId TimelineTransaction::next_pattern_id() const noexcept {
    PatternId next = 1;
    for (const auto &pattern : timeline_.arrangement.patterns)
        if (pattern.id >= next &&
            pattern.id < std::numeric_limits<PatternId>::max())
            next = pattern.id + 1;
    return next;
}
ClipId TimelineTransaction::next_clip_id() const noexcept {
    ClipId next = 1;
    for (const auto &clip : timeline_.arrangement.clips)
        if (clip.id >= next && clip.id < std::numeric_limits<ClipId>::max())
            next = clip.id + 1;
    return next;
}
AudioClipId TimelineTransaction::next_audio_clip_id() const noexcept {
    AudioClipId next = 1;
    for (const auto &clip : timeline_.arrangement.audio_clips)
        if (clip.id >= next &&
            clip.id < std::numeric_limits<AudioClipId>::max())
            next = clip.id + 1;
    return next;
}
AudioCrossfadeId TimelineTransaction::next_audio_crossfade_id() const noexcept {
    AudioCrossfadeId next{1};
    for (const auto &crossfade : timeline_.arrangement.audio_crossfades)
        next = std::max(next, crossfade.id + 1);
    return next;
}
TrackId TimelineTransaction::next_track_id() const noexcept {
    TrackId next = 1;
    for (const auto &track : timeline_.tracks)
        if (track.id >= next && track.id < std::numeric_limits<TrackId>::max())
            next = track.id + 1;
    return next;
}
EventId TimelineTransaction::next_event_id() const noexcept {
    EventId next = 1;
    for (const auto &track : timeline_.tracks)
        for (const auto &event : track.pattern.events)
            if (event.id >= next &&
                event.id < std::numeric_limits<EventId>::max())
                next = event.id + 1;
    for (const auto &pattern : timeline_.arrangement.patterns)
        for (const auto &track : pattern.tracks)
            for (const auto &event : track.pattern.events)
                if (event.id >= next &&
                    event.id < std::numeric_limits<EventId>::max())
                    next = event.id + 1;
    return next;
}

TimelineService::TimelineService(Publish publish, Timeline initial,
                                 TimelineLimits limits,
                                 std::size_t history_capacity)
    : publish_(std::move(publish)), limits_(limits),
      history_capacity_(history_capacity), current_(std::move(initial)) {
    generation_seed_ = current_.generation_seed;
    if (!publish_)
        throw std::invalid_argument(
            "timeline publication callback is required");
    if (limits_.max_tracks == 0 || limits_.max_events == 0)
        throw std::invalid_argument("timeline limits must be positive");
    PlaybackPlan plan;
    std::string error;
    if (!compile_timeline(current_, plan, error))
        throw std::invalid_argument("invalid initial timeline: " + error);
}
TimelineSnapshot TimelineService::snapshot() const {
    std::lock_guard lock(mutex_);
    return {current_, revision_, generation_seed_};
}
TimelineTransaction TimelineService::edit() const {
    std::lock_guard lock(mutex_);
    TimelineTransaction transaction;
    transaction.timeline_ = current_;
    transaction.limits_ = limits_;
    transaction.base_revision_ = revision_;
    transaction.generation_seed_ = generation_seed_;
    return transaction;
}
bool TimelineService::validate(const TimelineTransaction &transaction,
                               std::string &error) const {
    if (transaction.timeline_.tracks.size() > limits_.max_tracks) {
        error = "timeline track limit exceeded";
        return false;
    }
    std::size_t events{};
    for (const auto &track : transaction.timeline_.tracks) {
        if (track.pattern.events.size() > limits_.max_events - events) {
            error = "timeline event limit exceeded";
            return false;
        }
        events += track.pattern.events.size();
    }
    for (const auto &pattern : transaction.timeline_.arrangement.patterns)
        for (const auto &track : pattern.tracks) {
            if (track.pattern.events.size() > limits_.max_events - events) {
                error = "timeline event limit exceeded";
                return false;
            }
            events += track.pattern.events.size();
        }
    PlaybackPlan plan;
    if (!compile_timeline(transaction.timeline_, plan, error))
        return false;
    error.clear();
    return true;
}
void TimelineService::push_bounded(std::vector<HistoryEntry> &history,
                                   HistoryEntry entry) {
    if (history_capacity_ == 0)
        return;
    if (history.size() == history_capacity_)
        history.erase(history.begin());
    history.push_back(std::move(entry));
}
bool TimelineService::commit(TimelineTransaction transaction,
                             TimelineCommitPolicy policy, std::string &error) {
    return commit_with(std::move(transaction), policy, publish_, error);
}
bool TimelineService::commit_with(TimelineTransaction transaction,
                                  TimelineCommitPolicy policy,
                                  Publish publisher, std::string &error) {
    if (policy != TimelineCommitPolicy::next_audio_callback) {
        error = "unsupported timeline commit policy";
        return false;
    }
    transaction.timeline_.generation_seed = transaction.generation_seed_;
    if (!validate(transaction, error))
        return false;
    std::lock_guard commit_lock(commit_mutex_);
    HistoryEntry previous;
    {
        std::lock_guard state_lock(mutex_);
        if (transaction.base_revision_ != revision_) {
            error = "timeline revision conflict";
            return false;
        }
        previous = {current_, generation_seed_};
    }
    if (!publisher || !publisher(transaction.timeline_, error))
        return false;
    {
        std::lock_guard state_lock(mutex_);
        push_bounded(undo_, std::move(previous));
        redo_.clear();
        current_ = std::move(transaction.timeline_);
        generation_seed_ = transaction.generation_seed_;
        ++revision_;
    }
    error.clear();
    return true;
}
bool TimelineService::undo(TimelineCommitPolicy policy, std::string &error) {
    if (policy != TimelineCommitPolicy::next_audio_callback) {
        error = "unsupported timeline commit policy";
        return false;
    }
    std::lock_guard commit_lock(commit_mutex_);
    HistoryEntry candidate;
    {
        std::lock_guard state_lock(mutex_);
        if (undo_.empty()) {
            error = "nothing to undo";
            return false;
        }
        candidate = undo_.back();
    }
    if (!publish_(candidate.timeline, error))
        return false;
    {
        std::lock_guard state_lock(mutex_);
        push_bounded(redo_, {std::move(current_), generation_seed_});
        current_ = std::move(candidate.timeline);
        generation_seed_ = candidate.generation_seed;
        current_.generation_seed = generation_seed_;
        undo_.pop_back();
        ++revision_;
    }
    error.clear();
    return true;
}
bool TimelineService::redo(TimelineCommitPolicy policy, std::string &error) {
    if (policy != TimelineCommitPolicy::next_audio_callback) {
        error = "unsupported timeline commit policy";
        return false;
    }
    std::lock_guard commit_lock(commit_mutex_);
    HistoryEntry candidate;
    {
        std::lock_guard state_lock(mutex_);
        if (redo_.empty()) {
            error = "nothing to redo";
            return false;
        }
        candidate = redo_.back();
    }
    if (!publish_(candidate.timeline, error))
        return false;
    {
        std::lock_guard state_lock(mutex_);
        push_bounded(undo_, {std::move(current_), generation_seed_});
        current_ = std::move(candidate.timeline);
        generation_seed_ = candidate.generation_seed;
        current_.generation_seed = generation_seed_;
        redo_.pop_back();
        ++revision_;
    }
    error.clear();
    return true;
}
TimelineHistoryStatus TimelineService::history_status() const {
    std::lock_guard lock(mutex_);
    return {!undo_.empty(), !redo_.empty(), undo_.size(), redo_.size(),
            history_capacity_};
}

bool transpose(MusicalPattern &pattern, int semitones, std::string &error) {
    for (const auto &event : pattern.events)
        if (event.type == MusicalEventType::note ||
            event.type == MusicalEventType::note_on ||
            event.type == MusicalEventType::note_off) {
            const auto pitch =
                static_cast<std::int64_t>(event.pitch) + semitones;
            if (pitch < 0 || pitch > 127) {
                error = "transpose would exceed MIDI pitch range";
                return false;
            }
        }
    for (auto &event : pattern.events)
        if (event.type == MusicalEventType::note ||
            event.type == MusicalEventType::note_on ||
            event.type == MusicalEventType::note_off)
            event.pitch = static_cast<std::uint8_t>(
                static_cast<std::int64_t>(event.pitch) + semitones);
    error.clear();
    return true;
}
bool shift(MusicalPattern &pattern, MusicalTick ticks, std::string &error) {
    if (pattern.length <= 0) {
        error = "pattern length must be positive";
        return false;
    }
    for (auto &event : pattern.events)
        event.tick = shifted_tick(event.tick, ticks, pattern.length);
    error.clear();
    return true;
}
bool scale_velocities(MusicalPattern &pattern, double factor,
                      std::string &error) {
    if (!std::isfinite(factor) || factor < 0) {
        error = "velocity factor must be finite and nonnegative";
        return false;
    }
    for (auto &event : pattern.events)
        if (event.type != MusicalEventType::parameter)
            event.velocity = static_cast<float>(
                std::clamp(event.velocity * factor, 0.0, 1.0));
    error.clear();
    return true;
}
bool quantize(MusicalPattern &pattern, MusicalTick grid, std::string &error) {
    if (pattern.length <= 0 || grid <= 0) {
        error = "pattern length and quantize grid must be positive";
        return false;
    }
    for (auto &event : pattern.events) {
        const auto lower = (event.tick / grid) * grid;
        const auto upper =
            lower > std::numeric_limits<MusicalTick>::max() - grid
                ? lower
                : lower + grid;
        event.tick =
            wrap_tick(event.tick - lower < upper - event.tick ? lower : upper,
                      pattern.length);
    }
    error.clear();
    return true;
}
bool humanize(MusicalPattern &pattern, DeterministicRandom &random,
              MusicalTick max_tick_delta, double max_velocity_delta,
              std::string &error) {
    if (pattern.length <= 0 || max_tick_delta < 0 ||
        static_cast<std::uint64_t>(max_tick_delta) >
            std::numeric_limits<std::uint64_t>::max() / 2 - 1 ||
        !std::isfinite(max_velocity_delta) || max_velocity_delta < 0) {
        error = "invalid humanize range";
        return false;
    }
    for (auto &event : pattern.events) {
        const auto span = static_cast<std::uint64_t>(max_tick_delta) * 2 + 1;
        const auto delta =
            static_cast<MusicalTick>(random.next_u64() % span) - max_tick_delta;
        event.tick = shifted_tick(event.tick, delta, pattern.length);
        if (event.type != MusicalEventType::parameter) {
            const auto velocity_delta =
                (random.unit() * 2 - 1) * max_velocity_delta;
            event.velocity = static_cast<float>(
                std::clamp(event.velocity + velocity_delta, 0.0, 1.0));
        }
    }
    error.clear();
    return true;
}
bool reverse(MusicalPattern &pattern, std::string &error) {
    if (pattern.length <= 0) {
        error = "pattern length must be positive";
        return false;
    }
    for (auto &event : pattern.events)
        event.tick = wrap_tick(pattern.length - event.tick, pattern.length);
    error.clear();
    return true;
}
bool rotate(MusicalPattern &pattern, MusicalTick ticks, std::string &error) {
    return shift(pattern, ticks, error);
}
bool duplicate(MusicalPattern &pattern, std::span<const MusicalTick> offsets,
               EventId &next_id, std::string &error) {
    if (pattern.length <= 0 || next_id == 0) {
        error = "pattern length and next event ID must be valid";
        return false;
    }
    const auto originals = pattern.events;
    if (!originals.empty() &&
        offsets.size() > (std::numeric_limits<EventId>::max() - next_id) /
                             originals.size()) {
        error = "event ID space exhausted";
        return false;
    }
    for (const auto &event : originals)
        if (event.type == MusicalEventType::note_off &&
            std::none_of(originals.begin(), originals.end(),
                         [&](const MusicalEvent &target) {
                             return target.id == event.target_event_id;
                         })) {
            error = "cannot duplicate a note-off without its target";
            return false;
        }
    for (const auto offset : offsets) {
        std::vector<std::pair<EventId, EventId>> ids;
        ids.reserve(originals.size());
        for (const auto &original : originals)
            ids.emplace_back(original.id, next_id++);
        for (std::size_t index = 0; index < originals.size(); ++index) {
            auto event = originals[index];
            event.id = ids[index].second;
            event.tick = shifted_tick(event.tick, offset, pattern.length);
            if (event.type == MusicalEventType::note_off) {
                const auto target = std::find_if(
                    ids.begin(), ids.end(), [&](const auto &mapping) {
                        return mapping.first == event.target_event_id;
                    });
                event.target_event_id = target->second;
            }
            pattern.events.push_back(event);
        }
    }
    error.clear();
    return true;
}
std::vector<bool> euclidean_rhythm(std::size_t steps, std::size_t pulses,
                                   std::size_t rotation_value,
                                   std::string &error) {
    if (steps == 0 || steps > 4096 || pulses > steps) {
        error = "Euclidean rhythm requires 1..4096 steps and pulses no greater "
                "than steps";
        return {};
    }
    std::vector<bool> result(steps);
    for (std::size_t step = 0; step < steps; ++step)
        result[(step + rotation_value) % steps] =
            (step * pulses) % steps < pulses;
    error.clear();
    return result;
}
std::uint8_t random_note(DeterministicRandom &random,
                         std::span<const std::uint8_t> notes) {
    return notes[random.index(notes.size())];
}
bool generate_euclidean_notes(TimelineTransaction &transaction,
                              TrackId track_id, std::size_t steps,
                              std::size_t pulses, std::size_t rotation_value,
                              std::uint8_t pitch, float velocity,
                              MusicalTick duration, std::string &error) {
    auto *track = transaction.find_track(track_id);
    if (!track) {
        error = "track does not exist";
        return false;
    }
    auto hits = euclidean_rhythm(steps, pulses, rotation_value, error);
    if (hits.empty() && pulses != 0)
        return false;
    const auto length = track->pattern.length;
    EventId next = transaction.next_event_id();
    for (std::size_t step = 0; step < steps; ++step)
        if (hits[step]) {
            const auto tick = static_cast<MusicalTick>(
                (static_cast<long double>(step) * length) / steps);
            if (!transaction.add_note(track_id, next++, tick, pitch, velocity,
                                      duration, error))
                return false;
        }
    error.clear();
    return true;
}
} // namespace neurogenic
