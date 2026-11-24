#pragma once

#include "neurogenic/application/control.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace neurogenic {

enum class RecordingWorkflowMode : std::uint8_t {
    audio,
    resample,
    live_loop,
    arrangement,
    performance
};

enum class RecordingWorkflowPhase : std::uint8_t {
    idle,
    configuring,
    armed,
    waiting,
    recording,
    finalizing,
    publication_pending,
    completed,
    cancelled,
    failed
};

struct RecordingWorkflowConfiguration {
    RecordingWorkflowMode mode{RecordingWorkflowMode::audio};
    std::string source{"external-stereo"};
    std::uint64_t source_id{};
    SampleSlotId destination{};
    TrackId destination_track{};
    std::uint16_t channels{2};
    std::string quantization{"immediate"};
    std::uint64_t bars{};
    MusicalTick loop_length_ticks{ticks_per_quarter * 4};
    std::string capture_mode{"overdub"};
};

struct RecordingWorkflowSnapshot {
    RecordingWorkflowConfiguration configuration;
    RecordingWorkflowPhase phase{RecordingWorkflowPhase::idle};
    LiveLoopSessionId live_loop_session{};
    std::uint64_t captured_frames{}, capacity_frames{}, overflow_count{};
    std::size_t captured_events{};
    float progress{};
    bool can_configure{true}, can_arm{}, can_start{}, can_stop{}, can_cancel{};
    bool result_audio_active{}, result_pending{};
    std::string status{"Idle"}, error;
};

// Presentation-neutral composition over the existing recording services. It
// owns only a pending user configuration; all recording state remains in
// ApplicationService and its existing bounded state machines.
class RecordingWorkflowCoordinator {
  public:
    explicit RecordingWorkflowCoordinator(ApplicationService &application)
        : application_(application) {}

    [[nodiscard]] bool configure(RecordingWorkflowConfiguration,
                                 std::string &error);
    [[nodiscard]] bool arm(std::string &error);
    [[nodiscard]] bool start(std::string &error);
    [[nodiscard]] bool stop(std::string &error);
    [[nodiscard]] bool overdub(bool replace, std::string &error);
    [[nodiscard]] bool cancel(std::string &error);
    [[nodiscard]] bool commit(std::string &error);
    [[nodiscard]] RecordingWorkflowSnapshot snapshot() const;
    [[nodiscard]] const RecordingWorkflowConfiguration &configuration() const
        noexcept { return configuration_; }

  private:
    [[nodiscard]] bool submit(std::string,
                              std::map<std::string, ApplicationValue>,
                              std::string &);
    ApplicationService &application_;
    RecordingWorkflowConfiguration configuration_;
    LiveLoopSessionId live_loop_session_{};
    bool configured_{};
};

[[nodiscard]] const char *recording_workflow_mode_name(
    RecordingWorkflowMode) noexcept;
[[nodiscard]] const char *recording_workflow_phase_name(
    RecordingWorkflowPhase) noexcept;

} // namespace neurogenic
