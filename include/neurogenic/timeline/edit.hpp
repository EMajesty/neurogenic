#pragma once
#include "neurogenic/timeline/model.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
class DeterministicRandom {
  public:
    explicit DeterministicRandom(std::uint64_t seed) noexcept : state_(seed) {}
    [[nodiscard]] std::uint64_t next_u64() noexcept;
    [[nodiscard]] double unit() noexcept;
    [[nodiscard]] bool chance(double probability);
    [[nodiscard]] std::size_t index(std::size_t size);
    [[nodiscard]] std::uint64_t seed_state() const noexcept { return state_; }

  private:
    std::uint64_t state_{};
};

struct TimelineSnapshot {
    Timeline timeline;
    std::uint64_t revision{};
    std::uint64_t generation_seed{};
};
struct TimelineLimits {
    std::size_t max_tracks{256};
    std::size_t max_events{65'536};
};
struct TimelineHistoryStatus {
    bool can_undo{}, can_redo{};
    std::size_t undo_depth{}, redo_depth{}, capacity{};
};
enum class TimelineCommitPolicy : std::uint8_t { next_audio_callback };

class TimelineTransaction {
  public:
    TimelineTransaction() = default;
    [[nodiscard]] const Timeline &timeline() const noexcept {
        return timeline_;
    }
    [[nodiscard]] Timeline &timeline() noexcept { return timeline_; }
    [[nodiscard]] std::uint64_t base_revision() const noexcept {
        return base_revision_;
    }
    [[nodiscard]] std::uint64_t generation_seed() const noexcept {
        return generation_seed_;
    }
    void set_generation_seed(std::uint64_t seed) noexcept {
        generation_seed_ = seed;
    }
    [[nodiscard]] Track *find_track(TrackId) noexcept;
    [[nodiscard]] const Track *find_track(TrackId) const noexcept;
    bool create_track(TrackId, std::string, MusicalTick, std::string &error);
    bool delete_track(TrackId, std::string &error);
    bool set_track_name(TrackId, std::string, std::string &error);
    bool set_track_length(TrackId, MusicalTick, std::string &error);
    bool set_track_muted(TrackId, bool, std::string &error);
    bool set_track_instrument(
        TrackId, std::string, std::string &error,
        InstrumentInstanceId instance = default_synth_instance_id);
    bool set_track_midi_destination(TrackId, std::uint32_t port,
                                    std::uint8_t channel, std::string &error);
    bool add_note(TrackId, EventId, MusicalTick, std::uint8_t, float,
                  MusicalTick, std::string &error);
    bool modify_note(TrackId, EventId, MusicalTick, std::uint8_t, float,
                     MusicalTick, std::string &error);
    bool add_parameter(TrackId, EventId, MusicalTick, std::uint32_t, float,
                       std::string &error);
    bool modify_parameter(TrackId, EventId, MusicalTick, std::uint32_t, float,
                          std::string &error);
    bool add_parameter_destination(TrackId, EventId, MusicalTick, std::string,
                                   float, std::string &error);
    bool modify_parameter_destination(TrackId, EventId, MusicalTick,
                                      std::string, float, std::string &error);
    bool remove_event(TrackId, EventId, std::string &error);
    bool set_tempo_bpm(double, std::string &error);
    bool set_time_signature(std::uint16_t, std::uint16_t, std::string &error);
    bool create_pattern(PatternId, std::string, MusicalTick,
                        std::string &error);
    bool duplicate_pattern(PatternId source, PatternId destination,
                           std::string name, std::string &error);
    bool rename_pattern(PatternId, std::string, std::string &error);
    bool set_pattern_length(PatternId, MusicalTick, std::string &error);
    bool delete_pattern(PatternId, std::string &error);
    bool set_pattern_track(PatternId, TrackId, MusicalPattern,
                           std::string &error);
    bool create_clip(ArrangementClip, std::string &error);
    bool update_clip(ArrangementClip, std::string &error);
    bool delete_clip(ClipId, std::string &error);
    bool create_audio_clip(AudioClip, std::string &error);
    bool update_audio_clip(AudioClip, std::string &error);
    bool delete_audio_clip(AudioClipId, std::string &error);
    bool create_audio_crossfade(AudioClipCrossfade, std::string &error);
    bool update_audio_crossfade(AudioClipCrossfade, std::string &error);
    bool delete_audio_crossfade(AudioCrossfadeId, std::string &error);
    bool update_arrangement_automation(EventId, MusicalTick, float,
                                       std::string &error);
    bool delete_arrangement_automation(EventId, std::string &error);
    bool set_playback_mode(TimelinePlaybackMode, std::string &error);
    bool launch_pattern(PatternId, bool queue, std::string &error);
    bool set_song_range(MusicalTick start, MusicalTick end, std::string &error);
    bool set_loop_region(bool enabled, MusicalTick start, MusicalTick end,
                         std::string &error);
    bool set_pattern_chain(PatternChainId, std::vector<PatternChainEntry>,
                           bool loop, std::string &error);
    bool set_chain_running(bool running, std::size_t start_entry,
                           std::string &error);
    bool set_follow_action(PatternId, PatternFollowAction, std::string &error);
    bool set_launch_quantization(LaunchQuantization, std::string &error);
    bool set_voice_transition(VoiceTransitionPolicy, std::string &error);
    bool set_song_end_behavior(SongEndBehavior, PatternId return_pattern,
                               std::string &error);
    [[nodiscard]] PatternId next_pattern_id() const noexcept;
    [[nodiscard]] ClipId next_clip_id() const noexcept;
    [[nodiscard]] AudioClipId next_audio_clip_id() const noexcept;
    [[nodiscard]] AudioCrossfadeId next_audio_crossfade_id() const noexcept;
    [[nodiscard]] TrackId next_track_id() const noexcept;
    [[nodiscard]] EventId next_event_id() const noexcept;

  private:
    friend class TimelineService;
    Timeline timeline_;
    TimelineLimits limits_;
    std::uint64_t base_revision_{}, generation_seed_{};
};

class TimelineService {
  public:
    using Publish = std::function<bool(const Timeline &, std::string &)>;
    static constexpr std::size_t default_history_capacity = 64;
    explicit TimelineService(
        Publish, Timeline initial = {}, TimelineLimits limits = {},
        std::size_t history_capacity = default_history_capacity);
    [[nodiscard]] TimelineSnapshot snapshot() const;
    [[nodiscard]] TimelineTransaction edit() const;
    bool validate(const TimelineTransaction &, std::string &error) const;
    bool commit(TimelineTransaction, TimelineCommitPolicy, std::string &error);
    bool commit_with(TimelineTransaction, TimelineCommitPolicy, Publish,
                     std::string &error);
    bool undo(TimelineCommitPolicy, std::string &error);
    bool redo(TimelineCommitPolicy, std::string &error);
    [[nodiscard]] TimelineHistoryStatus history_status() const;
    [[nodiscard]] TimelineLimits limits() const noexcept { return limits_; }

  private:
    struct HistoryEntry {
        Timeline timeline;
        std::uint64_t generation_seed{};
    };
    void push_bounded(std::vector<HistoryEntry> &, HistoryEntry);
    Publish publish_;
    TimelineLimits limits_;
    const std::size_t history_capacity_;
    mutable std::mutex mutex_;
    std::mutex commit_mutex_;
    Timeline current_;
    std::uint64_t revision_{1}, generation_seed_{};
    std::vector<HistoryEntry> undo_, redo_;
};

bool transpose(MusicalPattern &, int semitones, std::string &error);
bool shift(MusicalPattern &, MusicalTick ticks, std::string &error);
bool scale_velocities(MusicalPattern &, double factor, std::string &error);
bool quantize(MusicalPattern &, MusicalTick grid, std::string &error);
bool humanize(MusicalPattern &, DeterministicRandom &,
              MusicalTick max_tick_delta, double max_velocity_delta,
              std::string &error);
bool reverse(MusicalPattern &, std::string &error);
bool rotate(MusicalPattern &, MusicalTick ticks, std::string &error);
bool duplicate(MusicalPattern &, std::span<const MusicalTick> offsets,
               EventId &next_id, std::string &error);
[[nodiscard]] std::vector<bool> euclidean_rhythm(std::size_t steps,
                                                 std::size_t pulses,
                                                 std::size_t rotation,
                                                 std::string &error);
[[nodiscard]] std::uint8_t random_note(DeterministicRandom &,
                                       std::span<const std::uint8_t> notes);
bool generate_euclidean_notes(TimelineTransaction &, TrackId, std::size_t steps,
                              std::size_t pulses, std::size_t rotation,
                              std::uint8_t pitch, float velocity,
                              MusicalTick duration, std::string &error);
} // namespace neurogenic
