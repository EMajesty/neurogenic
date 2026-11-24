#pragma once
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace neurogenic {
class AudioEngine;
class SampleManager;

using LiveLoopSessionId = std::uint64_t;
enum class LiveLoopMode : std::uint8_t { capture, overdub, replace };
enum class LiveLoopState : std::uint8_t {
    idle, armed, waiting, recording, finalizing, playing, overdubbing,
    replacing, canceled, error
};
enum class LiveLoopQuantization : std::uint8_t {
    immediate, subdivision, beat, bar
};
struct LiveLoopConfiguration {
    TrackId destination_track{};
    SampleSlotId destination_slot{};
    RecordingSource source{RecordingSource::master_post_effects};
    std::uint64_t source_id{};
    std::uint16_t channels{2};
    LiveLoopQuantization quantization{LiveLoopQuantization::bar};
    MusicalTick subdivision_ticks{ticks_per_quarter / 4};
    MusicalTick length_ticks{ticks_per_quarter * 4};
    float overdub_feedback{1.0F};
    std::int64_t latency_compensation_frames{};
    RecordingMonitorMode monitoring{RecordingMonitorMode::off};
};
struct LiveLoopSessionStatus {
    LiveLoopSessionId id{};
    LiveLoopConfiguration configuration;
    LiveLoopMode mode{LiveLoopMode::capture};
    LiveLoopState state{LiveLoopState::idle};
    MusicalTick requested_start_tick{}, requested_stop_tick{};
    std::uint64_t requested_start_frame{}, requested_stop_frame{};
    std::uint64_t actual_start_frame{}, actual_stop_frame{};
    std::uint64_t loop_iteration{}, captured_frames{}, overflow_count{};
    AudioClipId clip_id{};
    bool handoff_ready{}, buffer_reserved{}, voice_reserved{},
        handoff_completed{};
    std::uint64_t first_playback_frame{};
    std::string handoff_failure;
    std::string error;
};

class LiveLoopService {
  public:
    static constexpr std::size_t maximum_sessions = 8;
    LiveLoopService(AudioEngine &, RecordingService &, SampleManager &,
                    TimelineService &);
    [[nodiscard]] LiveLoopSessionId create(LiveLoopConfiguration,
                                           std::string &error);
    [[nodiscard]] bool arm(LiveLoopSessionId, LiveLoopMode,
                           std::string &error);
    [[nodiscard]] bool configure(LiveLoopSessionId, LiveLoopConfiguration,
                                 std::string &error);
    [[nodiscard]] bool start(LiveLoopSessionId, std::string &error);
    [[nodiscard]] bool stop(LiveLoopSessionId, std::string &error);
    [[nodiscard]] bool cancel(LiveLoopSessionId, std::string &error);
    [[nodiscard]] bool remove(LiveLoopSessionId, std::string &error);
    [[nodiscard]] bool save(LiveLoopSessionId, const std::filesystem::path &,
                            RecordingWavFormat, std::string &error);
    void clear() noexcept;
    void update();
    [[nodiscard]] std::vector<LiveLoopSessionStatus> sessions() const;
    [[nodiscard]] const LiveLoopSessionStatus *find(LiveLoopSessionId) const;

  private:
    struct Session {
        LiveLoopSessionStatus status;
        std::shared_ptr<const Sample> previous_sample;
        std::shared_ptr<Sample> handoff_sample;
        std::uint64_t expected_publication{};
        std::uint64_t previous_captured_frames{}, previous_actual_start_frame{},
            previous_actual_stop_frame{};
    };
    [[nodiscard]] MusicalTick quantum(const LiveLoopConfiguration &,
                                      const Timeline &) const noexcept;
    [[nodiscard]] std::uint64_t frames_for_ticks(MusicalTick,
                                                 const Timeline &) const noexcept;
    Session *mutable_find(LiveLoopSessionId) noexcept;
    AudioEngine &engine_;
    RecordingService &recording_;
    SampleManager &samples_;
    TimelineService &timeline_;
    std::vector<Session> sessions_;
    LiveLoopSessionId next_id_{1}, active_id_{};
};
[[nodiscard]] const char *live_loop_state_name(LiveLoopState) noexcept;
} // namespace neurogenic
