#include "neurogenic/timeline/performance_launching.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace neurogenic {
namespace {
MusicalTick bar_ticks(const TransportMetadata &t) noexcept {
    return static_cast<MusicalTick>(ticks_per_quarter) *
           t.time_signature_numerator * 4 / t.time_signature_denominator;
}
std::uint64_t random_word(std::uint64_t value) noexcept {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
}
bool PerformanceLauncher::configure(const Timeline &timeline, std::string &error,
                                    bool preserve_pending) {
    const auto &a = timeline.arrangement;
    if (a.launch_slots.size() > maximum_launch_slots ||
        a.performance_scenes.size() > maximum_performance_scenes) {
        error = "performance launcher capacity exceeded";
        return false;
    }
    std::set<LaunchSlotId> slots;
    std::set<std::int32_t> logical_positions;
    for (const auto &value : a.launch_slots) {
        const bool track_exists = std::any_of(timeline.tracks.begin(), timeline.tracks.end(),
            [&](const auto &track) { return track.id == value.track; });
        bool source_exists{};
        if (value.source_type == LaunchSourceType::pattern)
            source_exists = std::any_of(a.patterns.begin(), a.patterns.end(),
                [&](const auto &p) { return p.id == value.source_id; });
        else
            source_exists = std::any_of(a.audio_clips.begin(), a.audio_clips.end(),
                [&](const auto &clip) { return clip.id == value.source_id &&
                    (value.source_type == LaunchSourceType::live_loop || clip.track == value.track); });
        if (!value.id || !value.track || !value.source_id || !track_exists ||
            !source_exists || !slots.insert(value.id).second ||
            (value.logical_position >= 0 &&
             !logical_positions.insert(value.logical_position).second) ||
            !value.follow_repetitions || value.subdivision_ticks <= 0) {
            error = "invalid performance launch slot";
            return false;
        }
    }
    std::set<PerformanceSceneId> scenes;
    for (const auto &scene : a.performance_scenes) {
        std::set<TrackId> tracks;
        if (!scene.id || scene.name.empty() ||
            scene.entries.size() > maximum_scene_entries ||
            !scenes.insert(scene.id).second || scene.subdivision_ticks <= 0) {
            error = "invalid performance scene";
            return false;
        }
        for (const auto &entry : scene.entries) {
            const auto found = std::find_if(a.launch_slots.begin(), a.launch_slots.end(),
                [&](const auto &candidate) { return candidate.id == entry.slot; });
            if (found == a.launch_slots.end() || found->track != entry.track ||
                !tracks.insert(entry.track).second) {
                error = "performance scene references an invalid or duplicate track slot";
                return false;
            }
        }
    }
    timeline_ = timeline;
    if (!preserve_pending) {
        pending_.clear();
        repetitions_.clear();
        active_.clear();
    } else {
        const auto valid = [&](const PreparedLaunch &launch) {
            const auto *definition = slot(launch.slot);
            return definition && definition->track == launch.track &&
                   definition->source_type == launch.source_type &&
                   definition->source_id == launch.source_id;
        };
        std::erase_if(pending_, [&](const auto &launch) {
            return !valid(launch);
        });
        std::erase_if(active_, [&](const auto &entry) {
            return !valid(entry.second);
        });
    }
    diagnostics_.used_slots = a.launch_slots.size();
    diagnostics_.used_scenes = a.performance_scenes.size();
    diagnostics_.pending = pending_.size();
    error.clear();
    return true;
}
const LaunchSlot *PerformanceLauncher::slot(LaunchSlotId id) const noexcept {
    const auto &values = timeline_.arrangement.launch_slots;
    const auto found = std::find_if(values.begin(), values.end(),
        [&](const auto &value) { return value.id == id; });
    return found == values.end() ? nullptr : &*found;
}
MusicalTick PerformanceLauncher::boundary(const LaunchSlot &value,
                                           MusicalTick now) const noexcept {
    auto quantization = value.quantization_override ? value.quantization
        : timeline_.arrangement.launch_quantization;
    if (quantization == LaunchQuantization::subdivision) {
        const auto q = value.subdivision_ticks;
        return ((now + q - 1) / q) * q;
    }
    if (quantization == LaunchQuantization::four_bars ||
        quantization == LaunchQuantization::eight_bars) {
        auto q = bar_ticks(timeline_.transport) *
            (quantization == LaunchQuantization::four_bars ? 4 : 8);
        return ((now + q - 1) / q) * q;
    }
    MusicalTick length = ticks_per_quarter * 4;
    if (value.source_type == LaunchSourceType::pattern)
        for (const auto &pattern : timeline_.arrangement.patterns)
            if (pattern.id == value.source_id) length = pattern.length;
    return launch_boundary(now, quantization, length, timeline_.transport);
}
std::uint64_t PerformanceLauncher::frame_at(MusicalTick target,
                                            MusicalTick now,
                                            std::uint64_t now_frame) const noexcept {
    if (target <= now) return now_frame;
    const auto frames = (static_cast<long double>(target - now) * sample_rate_ *
        timeline_.transport.microseconds_per_quarter) /
        (static_cast<long double>(ticks_per_quarter) * 1'000'000.0L);
    return now_frame + static_cast<std::uint64_t>(std::llround(frames));
}
bool PerformanceLauncher::prepare_slot(LaunchSlotId id, PreparedLaunchOperation op,
                                       MusicalTick now, std::uint64_t now_frame,
                                       PreparedLaunch &result, std::string &error) {
    const auto *value = slot(id);
    if (!value) { ++diagnostics_.rejected; error = "launch slot does not exist"; return false; }
    if (pending_.size() >= maximum_pending_launches) {
        ++diagnostics_.rejected; error = "pending launch capacity exceeded"; return false;
    }
    const auto tick = boundary(*value, now);
    result = {id, value->track, op, tick, frame_at(tick, now, now_frame),
              value->source_type, value->source_id, value->behavior,
              value->behavior == LaunchBehavior::legato};
    pending_.push_back(result);
    diagnostics_.pending = pending_.size();
    error.clear();
    return true;
}
bool PerformanceLauncher::prepare_scene(PerformanceSceneId id, MusicalTick now,
    std::uint64_t now_frame, std::vector<PreparedLaunch> &result, std::string &error) {
    const auto &scenes = timeline_.arrangement.performance_scenes;
    const auto found = std::find_if(scenes.begin(), scenes.end(),
        [&](const auto &scene) { return scene.id == id; });
    if (found == scenes.end()) { ++diagnostics_.rejected; error = "performance scene does not exist"; return false; }
    if (pending_.size() + found->entries.size() > maximum_pending_launches) {
        ++diagnostics_.rejected; error = "scene exceeds pending launch capacity"; return false;
    }
    auto q = found->quantization == LaunchQuantization::subdivision
        ? found->subdivision_ticks : (found->quantization == LaunchQuantization::four_bars
            ? bar_ticks(timeline_.transport) * 4 : found->quantization == LaunchQuantization::eight_bars
                ? bar_ticks(timeline_.transport) * 8 : found->quantization == LaunchQuantization::two_bars
                    ? bar_ticks(timeline_.transport) * 2 : found->quantization == LaunchQuantization::bar
                        ? bar_ticks(timeline_.transport) : found->quantization == LaunchQuantization::quarter
                            ? MusicalTick(ticks_per_quarter) : MusicalTick(1));
    const auto tick = found->quantization == LaunchQuantization::immediate ? now : ((now + q - 1) / q) * q;
    const auto frame = frame_at(tick, now, now_frame);
    result.clear();
    for (const auto &entry : found->entries) {
        const auto *value = slot(entry.slot);
        result.push_back({value->id, value->track, PreparedLaunchOperation::start,
            tick, frame, value->source_type, value->source_id, value->behavior,
            value->behavior == LaunchBehavior::legato, id});
    }
    pending_.insert(pending_.end(), result.begin(), result.end());
    diagnostics_.pending = pending_.size();
    error.clear();
    return true;
}
bool PerformanceLauncher::cancel(LaunchSlotId id) noexcept {
    const auto before = pending_.size();
    std::erase_if(pending_, [&](const auto &value) { return value.slot == id; });
    diagnostics_.pending = pending_.size();
    return before != pending_.size();
}
void PerformanceLauncher::acknowledge(
    std::uint64_t activation_frame, std::vector<PreparedLaunch> *activated) noexcept {
    for (const auto &value : pending_)
        if (value.activation_frame <= activation_frame) {
            if (activated) activated->push_back(value);
            if (value.operation == PreparedLaunchOperation::stop)
                repetitions_.erase(value.slot), active_.erase(value.track);
            else {
                ++repetitions_[value.slot];
                active_[value.track] = value;
            }
        }
    std::erase_if(pending_, [&](const auto &value) { return value.activation_frame <= activation_frame; });
    diagnostics_.pending = pending_.size();
}
std::vector<PreparedLaunch> PerformanceLauncher::compose_session(
    std::span<const PreparedLaunch> changes) const {
    auto next = active_;
    for (const auto &change : changes) {
        if (change.operation == PreparedLaunchOperation::stop)
            next.erase(change.track);
        else
            next[change.track] = change;
    }
    std::vector<PreparedLaunch> result;
    result.reserve(next.size());
    for (const auto &[track, launch] : next) {
        (void)track;
        result.push_back(launch);
    }
    if (!changes.empty())
        for (auto &launch : result)
            launch.activation_frame = changes.front().activation_frame,
            launch.activation_tick = changes.front().activation_tick;
    // Keep a stop marker when the last session-owned track is stopped.  It
    // carries the authoritative activation boundary used to publish silence.
    if (result.empty() && !changes.empty())
        result.push_back(changes.front());
    return result;
}
bool PerformanceLauncher::prepare_follow(const PreparedLaunch &active,
                                         PreparedLaunch &result,
                                         std::string &error) {
    const auto *definition = slot(active.slot);
    if (!definition || definition->follow == SlotFollowAction::none) {
        error.clear();
        return false;
    }
    std::vector<LaunchSlotId> eligible;
    for (const auto &candidate : timeline_.arrangement.launch_slots)
        if (candidate.track == active.track) eligible.push_back(candidate.id);
    std::sort(eligible.begin(), eligible.end());
    const auto repetition = repetitions_[active.slot];
    if (!repetition || repetition % definition->follow_repetitions) {
        error.clear();
        return false;
    }
    const auto target = follow(active.slot, repetition, eligible);
    MusicalTick duration = ticks_per_quarter * 4;
    if (active.source_type == LaunchSourceType::pattern) {
        const auto found = std::find_if(timeline_.arrangement.patterns.begin(),
            timeline_.arrangement.patterns.end(), [&](const auto &pattern) {
                return pattern.id == active.source_id;
            });
        if (found != timeline_.arrangement.patterns.end()) duration = found->length;
    } else {
        const auto found = std::find_if(timeline_.arrangement.audio_clips.begin(),
            timeline_.arrangement.audio_clips.end(), [&](const auto &clip) {
                return clip.id == active.source_id;
            });
        if (found != timeline_.arrangement.audio_clips.end()) duration = found->duration;
    }
    if (pending_.size() >= maximum_pending_launches) {
        ++diagnostics_.rejected;
        error = "pending follow launch capacity exceeded";
        return false;
    }
    if (!target) {
        result = active;
        result.operation = PreparedLaunchOperation::stop;
    } else {
        const auto *next = slot(target);
        result = {next->id, next->track, PreparedLaunchOperation::start, 0, 0,
                  next->source_type, next->source_id, next->behavior,
                  next->behavior == LaunchBehavior::legato};
    }
    result.activation_tick = active.activation_tick + duration;
    result.activation_frame = active.activation_frame +
        frame_at(result.activation_tick, active.activation_tick, 0);
    result.follow_transition = true;
    pending_.push_back(result);
    diagnostics_.pending = pending_.size();
    error.clear();
    return true;
}
bool PerformanceLauncher::prepare_return(
    TrackId track, MusicalTick now, std::uint64_t now_frame,
    LaunchQuantization quantization, std::vector<PreparedLaunch> &result,
    std::string &error) {
    result.clear();
    std::vector<TrackId> tracks;
    for (const auto &[active_track, launch] : active_)
        if (track == 0 || active_track == track)
            tracks.push_back(active_track);
    if (tracks.empty()) {
        ++diagnostics_.rejected;
        error = "track is not session-owned";
        return false;
    }
    if (pending_.size() + tracks.size() > maximum_pending_launches) {
        ++diagnostics_.rejected;
        error = "pending return capacity exceeded";
        return false;
    }
    MusicalTick quantum = 1;
    if (quantization == LaunchQuantization::quarter)
        quantum = ticks_per_quarter;
    else if (quantization == LaunchQuantization::bar ||
             quantization == LaunchQuantization::two_bars ||
             quantization == LaunchQuantization::four_bars ||
             quantization == LaunchQuantization::eight_bars) {
        quantum = bar_ticks(timeline_.transport);
        if (quantization == LaunchQuantization::two_bars) quantum *= 2;
        else if (quantization == LaunchQuantization::four_bars) quantum *= 4;
        else if (quantization == LaunchQuantization::eight_bars) quantum *= 8;
    }
    const auto tick = quantization == LaunchQuantization::immediate
                          ? now
                          : ((now + quantum - 1) / quantum) * quantum;
    const auto frame = frame_at(tick, now, now_frame);
    for (const auto active_track : tracks) {
        std::erase_if(pending_, [&](const auto &pending) {
            return pending.track == active_track;
        });
        auto value = active_.at(active_track);
        value.operation = PreparedLaunchOperation::stop;
        value.activation_tick = tick;
        value.activation_frame = frame;
        value.follow_transition = false;
        result.push_back(value);
    }
    pending_.insert(pending_.end(), result.begin(), result.end());
    diagnostics_.pending = pending_.size();
    error.clear();
    return true;
}

std::vector<TrackPlaybackOwnership> PerformanceLauncher::ownership() const {
    std::map<TrackId, TrackPlaybackOwnership> result;
    for (const auto &track : timeline_.tracks)
        result.emplace(track.id, TrackPlaybackOwnership{track.id});
    for (const auto &[track, launch] : active_)
        result[track] = {track, TrackPlaybackOwner::session, 0, launch.slot};
    for (const auto &launch : pending_) {
        auto &value = result[launch.track];
        value.track = launch.track;
        value.owner = launch.operation == PreparedLaunchOperation::stop
                          ? TrackPlaybackOwner::transitioning_to_arrangement
                          : TrackPlaybackOwner::transitioning_to_session;
        value.transition_frame = launch.activation_frame;
        value.slot = launch.slot;
    }
    std::vector<TrackPlaybackOwnership> values;
    values.reserve(result.size());
    for (const auto &[track, value] : result) {
        (void)track;
        values.push_back(value);
    }
    return values;
}
LaunchSlotId PerformanceLauncher::follow(LaunchSlotId id, std::uint64_t repetition,
                                         std::span<const LaunchSlotId> eligible) const noexcept {
    const auto *value = slot(id);
    if (!value || repetition % value->follow_repetitions) return id;
    if (value->follow == SlotFollowAction::stop) return 0;
    if (value->follow == SlotFollowAction::repeat || value->follow == SlotFollowAction::none) return id;
    if (value->follow == SlotFollowAction::specified) return slot(value->follow_target) ? value->follow_target : 0;
    const auto position = std::find(eligible.begin(), eligible.end(), id);
    if (eligible.empty() || position == eligible.end()) return 0;
    if (value->follow == SlotFollowAction::random_eligible)
        return eligible[random_word(value->random_seed ^ repetition) % eligible.size()];
    const auto index = static_cast<std::size_t>(position - eligible.begin());
    if (value->follow == SlotFollowAction::previous)
        return eligible[(index + eligible.size() - 1) % eligible.size()];
    return eligible[(index + 1) % eligible.size()];
}
float PerformanceLauncher::velocity(float input, bool velocity_capable,
                                    bool full_level) const noexcept {
    if (full_level || timeline_.arrangement.velocity_policy == VelocityPolicy::full_level) return 1.0F;
    if (timeline_.arrangement.velocity_policy == VelocityPolicy::fixed || !velocity_capable)
        return std::clamp(timeline_.arrangement.fixed_velocity, 0.0F, 1.0F);
    return std::clamp(input, 0.0F, 1.0F);
}
} // namespace neurogenic
