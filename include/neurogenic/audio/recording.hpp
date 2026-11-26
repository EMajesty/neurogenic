#pragma once
#include "neurogenic/audio/sample.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace neurogenic {
class SampleManager;

enum class RecordingSource : std::uint8_t {
    external_mono,
    external_stereo,
    master_pre_effects,
    master_post_effects,
    track_channel,
    auxiliary_bus,
    physical_input,
    external_fx_return,
    external_fx_mix,
    group_pre_effects,
    group_post_effects,
    group_post_fader
};
enum class RecordingState : std::uint8_t {
    idle,
    armed,
    recording,
    completed,
    publication_pending,
    audio_active,
    canceled,
    error
};
enum class RecordingMonitorMode : std::uint8_t { off, on, automatic };
enum class RecordingQuantization : std::uint8_t { immediate, beat, bar };
enum class RecordingWavFormat : std::uint8_t { pcm16, float32 };

struct RecordingConfiguration {
    RecordingSource source{RecordingSource::master_post_effects};
    std::uint64_t source_id{};
    SampleSlotId destination{};
    std::uint16_t channels{2};
    RecordingMonitorMode monitoring{RecordingMonitorMode::off};
    RecordingQuantization quantization{RecordingQuantization::immediate};
    std::uint64_t start_frame{};
    std::uint64_t fixed_frames{};
};

struct RecordingStatus {
    RecordingState state{RecordingState::idle};
    RecordingConfiguration configuration;
    std::uint32_t sample_rate{};
    std::uint64_t captured_frames{}, capacity_frames{}, dropped_frames{},
        overflow_count{}, finalization_microseconds{}, memory_bytes{};
    float peak_left{}, peak_right{};
    std::string error;
};

class RecordingService {
  public:
    explicit RecordingService(std::uint32_t sample_rate,
                              std::uint64_t capacity_frames = 48000 * 60);
    RecordingService(const RecordingService &) = delete;
    RecordingService &operator=(const RecordingService &) = delete;

    bool arm(RecordingConfiguration, std::string &error) noexcept;
    bool start(std::uint64_t callback_frame, std::string &error) noexcept;
    bool start_scheduled(std::string &error) noexcept;
    bool stop(std::string &error) noexcept;
    bool stop_at(std::uint64_t callback_frame, std::string &error) noexcept;
    void cancel() noexcept;
    void device_stopped() noexcept;
    void reset() noexcept;

    // Audio-callback producer entry points. Both are bounded and noexcept.
    void capture_internal(RecordingSource, std::span<const float> left,
                          std::span<const float> right,
                          std::uint64_t callback_frame,
                          std::uint64_t source_id = 0) noexcept;
    void capture_external(std::span<const float> interleaved,
                          std::uint16_t input_channels,
                          std::uint64_t callback_frame) noexcept;

    [[nodiscard]] RecordingStatus status() const;
    [[nodiscard]] RecordingState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool monitoring_active() const noexcept;
    [[nodiscard]] bool finalize_to_slot(SampleManager &, std::string &error);
    [[nodiscard]] bool finalize_sample_to_slot(SampleManager &, Sample,
                                               std::string &error);
    void acknowledge_audio_active() noexcept;
    void mark_publication_pending() noexcept {
        state_.store(RecordingState::publication_pending,
                     std::memory_order_release);
    }
    [[nodiscard]] bool save_wav(const std::filesystem::path &,
                                RecordingWavFormat, std::string &error) const;
    [[nodiscard]] static bool save_sample_wav(const Sample &,
                                              const std::filesystem::path &,
                                              RecordingWavFormat,
                                              std::string &error);
    [[nodiscard]] Sample completed_sample() const;
    [[nodiscard]] bool prepare_gapless_buffer(std::shared_ptr<Sample>,
                                              std::string &error) noexcept;
    [[nodiscard]] std::shared_ptr<Sample> gapless_buffer() const noexcept;
    void release_gapless_buffer() noexcept;
    [[nodiscard]] std::uint64_t next_stop_boundary(std::uint64_t,
                                                   std::size_t) const noexcept;
    [[nodiscard]] std::uint64_t requested_stop_frame() const noexcept {
        return requested_stop_.load(std::memory_order_acquire);
    }

  private:
    void capture(RecordingSource, std::span<const float>,
                 std::span<const float>, std::uint64_t,
                 std::uint64_t source_id = 0) noexcept;
    std::uint32_t sample_rate_{};
    std::uint64_t capacity_frames_{};
    std::vector<float> storage_;
    std::shared_ptr<Sample> gapless_storage_;
    std::atomic<Sample *> gapless_pcm_{};
    RecordingConfiguration configuration_;
    std::atomic<RecordingState> state_{RecordingState::idle};
    std::atomic<std::uint64_t> captured_{}, dropped_{}, overflows_{},
        requested_start_{}, requested_stop_{}, finalization_us_{};
    std::atomic<float> peak_left_{}, peak_right_{};
    std::atomic<bool> start_enabled_{};
    std::string error_;
};

[[nodiscard]] const char *recording_source_name(RecordingSource) noexcept;
[[nodiscard]] const char *recording_state_name(RecordingState) noexcept;
} // namespace neurogenic
