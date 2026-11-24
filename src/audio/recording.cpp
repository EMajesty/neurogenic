#include "neurogenic/audio/recording.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
void update_peak(std::atomic<float> &target, float value) noexcept {
    value = std::fabs(value);
    auto current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(
                                  current, value, std::memory_order_relaxed)) {
    }
}
template <class T> void write_value(std::ofstream &output, T value) {
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
}
std::size_t recording_storage_size(std::uint32_t rate, std::uint64_t frames) {
    if (rate == 0 || frames == 0 ||
        frames > std::numeric_limits<std::size_t>::max() / 2)
        throw std::invalid_argument("invalid recording capacity");
    return static_cast<std::size_t>(frames) * 2;
}
} // namespace

const char *recording_source_name(RecordingSource source) noexcept {
    switch (source) {
    case RecordingSource::external_mono:
        return "External mono";
    case RecordingSource::external_stereo:
        return "External stereo";
    case RecordingSource::master_pre_effects:
        return "Master pre-effects";
    case RecordingSource::master_post_effects:
        return "Master post-effects";
    case RecordingSource::track_channel:
        return "Track channel";
    case RecordingSource::auxiliary_bus:
        return "Auxiliary bus";
    case RecordingSource::physical_input:
        return "Physical input";
    case RecordingSource::external_fx_return:
        return "External FX return";
    case RecordingSource::external_fx_mix:
        return "External FX wet/dry mix";
    case RecordingSource::group_pre_effects:
        return "Group pre-effects";
    case RecordingSource::group_post_effects:
        return "Group post-effects";
    case RecordingSource::group_post_fader:
        return "Group post-fader";
    }
    return "Unknown";
}
const char *recording_state_name(RecordingState state) noexcept {
    switch (state) {
    case RecordingState::idle:
        return "Idle";
    case RecordingState::armed:
        return "Armed";
    case RecordingState::recording:
        return "Recording";
    case RecordingState::completed:
        return "Completed";
    case RecordingState::publication_pending:
        return "Publication pending";
    case RecordingState::audio_active:
        return "Audio active";
    case RecordingState::canceled:
        return "Canceled";
    case RecordingState::error:
        return "Error";
    }
    return "Unknown";
}

RecordingService::RecordingService(std::uint32_t rate,
                                   std::uint64_t capacity_frames)
    : sample_rate_(rate), capacity_frames_(capacity_frames),
      storage_(recording_storage_size(rate, capacity_frames)) {}

bool RecordingService::arm(RecordingConfiguration configuration,
                           std::string &error) noexcept {
    if (configuration.destination == invalid_sample_slot ||
        (configuration.channels != 1 && configuration.channels != 2)) {
        error = "recording requires a valid slot and mono or stereo channels";
        return false;
    }
    if (configuration.fixed_frames >
        std::numeric_limits<std::uint64_t>::max() - configuration.start_frame) {
        error = "recording frame range overflows";
        return false;
    }
    const auto prepared_channels =
        configuration.source == RecordingSource::external_mono
            ? std::uint16_t{1}
            : configuration.source == RecordingSource::external_stereo
                  ? std::uint16_t{2}
                  : std::clamp<std::uint16_t>(configuration.channels, 1, 2);
    if (gapless_storage_ &&
        (configuration.fixed_frames == 0 ||
         gapless_storage_->sample_rate != sample_rate_ ||
         gapless_storage_->channels != prepared_channels ||
         gapless_storage_->frames() != configuration.fixed_frames)) {
        error = "prepared gapless buffer does not match recording";
        return false;
    }
    const auto current = state_.load(std::memory_order_acquire);
    if (current == RecordingState::recording) {
        error = "recording is already active";
        return false;
    }
    if (configuration.source == RecordingSource::external_mono)
        configuration.channels = 1;
    else if (configuration.source == RecordingSource::external_stereo)
        configuration.channels = 2;
    if (configuration.source != RecordingSource::external_mono &&
        configuration.source != RecordingSource::external_stereo)
        configuration.channels =
            std::clamp<std::uint16_t>(configuration.channels, 1, 2);
    configuration_ = configuration;
    captured_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    overflows_.store(0, std::memory_order_relaxed);
    peak_left_.store(0, std::memory_order_relaxed);
    peak_right_.store(0, std::memory_order_relaxed);
    finalization_us_.store(0, std::memory_order_relaxed);
    requested_start_.store(configuration.start_frame,
                           std::memory_order_relaxed);
    requested_stop_.store(configuration.fixed_frames
                              ? configuration.start_frame +
                                    configuration.fixed_frames
                              : std::numeric_limits<std::uint64_t>::max(),
                          std::memory_order_relaxed);
    error_.clear();
    start_enabled_.store(false, std::memory_order_relaxed);
    state_.store(RecordingState::armed, std::memory_order_release);
    error.clear();
    return true;
}

bool RecordingService::start(std::uint64_t callback_frame,
                             std::string &error) noexcept {
    if (state_.load(std::memory_order_acquire) != RecordingState::armed) {
        error = "recording is not armed";
        return false;
    }
    if (configuration_.quantization == RecordingQuantization::immediate) {
        configuration_.start_frame = callback_frame;
        requested_start_.store(callback_frame, std::memory_order_release);
        if (configuration_.fixed_frames)
            requested_stop_.store(callback_frame + configuration_.fixed_frames,
                                  std::memory_order_release);
    }
    start_enabled_.store(true, std::memory_order_release);
    error.clear();
    return true;
}

bool RecordingService::stop(std::string &error) noexcept {
    const auto current = state_.load(std::memory_order_acquire);
    if (current != RecordingState::recording &&
        current != RecordingState::armed) {
        error = "recording is not active";
        return false;
    }
    requested_stop_.store(0, std::memory_order_release);
    error.clear();
    return true;
}
bool RecordingService::stop_at(std::uint64_t callback_frame,
                               std::string &error) noexcept {
    const auto current = state_.load(std::memory_order_acquire);
    if (current != RecordingState::recording &&
        current != RecordingState::armed) {
        error = "recording is not active";
        return false;
    }
    requested_stop_.store(callback_frame, std::memory_order_release);
    error.clear();
    return true;
}
void RecordingService::cancel() noexcept {
    start_enabled_.store(false, std::memory_order_relaxed);
    state_.store(RecordingState::canceled, std::memory_order_release);
    captured_.store(0, std::memory_order_relaxed);
}
void RecordingService::device_stopped() noexcept {
    const auto state = state_.load(std::memory_order_acquire);
    if (state == RecordingState::armed || state == RecordingState::recording)
        state_.store(RecordingState::error, std::memory_order_release);
}
void RecordingService::reset() noexcept {
    if (state_.load(std::memory_order_acquire) != RecordingState::recording) {
        captured_.store(0, std::memory_order_relaxed);
        state_.store(RecordingState::idle, std::memory_order_release);
        error_.clear();
    }
}

void RecordingService::capture_internal(RecordingSource source,
                                        std::span<const float> left,
                                        std::span<const float> right,
                                        std::uint64_t callback_frame,
                                        std::uint64_t source_id) noexcept {
    capture(source, left, right, callback_frame, source_id);
}
void RecordingService::capture_external(std::span<const float> interleaved,
                                        std::uint16_t input_channels,
                                        std::uint64_t callback_frame) noexcept {
    const auto state = state_.load(std::memory_order_acquire);
    if (state != RecordingState::armed && state != RecordingState::recording)
        return;
    const auto source = configuration_.source;
    if (source != RecordingSource::external_mono &&
        source != RecordingSource::external_stereo &&
        source != RecordingSource::physical_input)
        return;
    const auto frames =
        input_channels ? interleaved.size() / input_channels : 0;
    // The device adapter provides bounded callback blocks. Deinterleave into
    // fixed stack chunks so no recording buffer allocation occurs here.
    std::array<float, 1024> left{}, right{};
    for (std::size_t done = 0; done < frames;) {
        const auto count = std::min<std::size_t>(left.size(), frames - done);
        for (std::size_t i = 0; i < count; ++i) {
            const auto selected =
                source == RecordingSource::physical_input
                    ? std::min<std::size_t>(configuration_.source_id,
                                            input_channels - 1)
                    : 0;
            left[i] = interleaved[(done + i) * input_channels + selected];
            right[i] = source == RecordingSource::physical_input
                           ? left[i]
                           : (input_channels > 1
                                  ? interleaved[(done + i) * input_channels + 1]
                                  : left[i]);
        }
        capture(source, {left.data(), count}, {right.data(), count},
                callback_frame + done, configuration_.source_id);
        done += count;
    }
}

void RecordingService::capture(RecordingSource source,
                               std::span<const float> left,
                               std::span<const float> right,
                               std::uint64_t callback_frame,
                               std::uint64_t source_id) noexcept {
    auto state = state_.load(std::memory_order_acquire);
    if ((state != RecordingState::armed &&
         state != RecordingState::recording) ||
        configuration_.source != source ||
        ((source == RecordingSource::track_channel ||
          source == RecordingSource::auxiliary_bus ||
          source == RecordingSource::group_pre_effects ||
          source == RecordingSource::group_post_effects ||
          source == RecordingSource::group_post_fader ||
          source == RecordingSource::physical_input ||
          source == RecordingSource::external_fx_return ||
          source == RecordingSource::external_fx_mix) &&
         configuration_.source_id != source_id) ||
        !start_enabled_.load(std::memory_order_acquire))
        return;
    const auto frames = std::min(left.size(), right.size());
    auto first = std::size_t{};
    const auto start = requested_start_.load(std::memory_order_acquire);
    if (state == RecordingState::armed) {
        if (callback_frame + frames <= start)
            return;
        first = start > callback_frame
                    ? static_cast<std::size_t>(start - callback_frame)
                    : 0;
        state_.store(RecordingState::recording, std::memory_order_release);
        state = RecordingState::recording;
    }
    auto stop = requested_stop_.load(std::memory_order_acquire);
    if (stop == 0) {
        state_.store(RecordingState::completed, std::memory_order_release);
        return;
    }
    auto count = frames - first;
    if (stop != std::numeric_limits<std::uint64_t>::max() &&
        callback_frame + first + count > stop)
        count = stop > callback_frame + first
                    ? static_cast<std::size_t>(stop - callback_frame - first)
                    : 0;
    auto captured = captured_.load(std::memory_order_relaxed);
    const auto available =
        capacity_frames_ - std::min(captured, capacity_frames_);
    const auto accepted =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, available));
    const auto channels = configuration_.channels;
    for (std::size_t i = 0; i < accepted; ++i) {
        const auto l = std::isfinite(left[first + i]) ? left[first + i] : 0.0F;
        const auto r =
            std::isfinite(right[first + i]) ? right[first + i] : 0.0F;
        auto *gapless = gapless_pcm_.load(std::memory_order_acquire);
        auto &target = gapless ? gapless->interleaved : storage_;
        if (channels == 1)
            target[static_cast<std::size_t>(captured + i)] =
                source == RecordingSource::external_mono ? l : (l + r) * 0.5F;
        else {
            target[static_cast<std::size_t>(captured + i) * 2] = l;
            target[static_cast<std::size_t>(captured + i) * 2 + 1] = r;
        }
        update_peak(peak_left_, l);
        update_peak(peak_right_, r);
    }
    captured += accepted;
    captured_.store(captured, std::memory_order_release);
    if (accepted < count) {
        dropped_.fetch_add(count - accepted, std::memory_order_relaxed);
        overflows_.fetch_add(1, std::memory_order_relaxed);
        state_.store(RecordingState::completed, std::memory_order_release);
        return;
    }
    if ((stop != std::numeric_limits<std::uint64_t>::max() &&
         callback_frame + frames >= stop) ||
        captured >= capacity_frames_)
        state_.store(RecordingState::completed, std::memory_order_release);
}

bool RecordingService::prepare_gapless_buffer(std::shared_ptr<Sample> sample,
                                               std::string &error) noexcept {
    if (!sample || sample->sample_rate != sample_rate_ ||
        (sample->channels != 1 && sample->channels != 2) ||
        sample->interleaved.empty() ||
        sample->interleaved.size() % sample->channels != 0 ||
        sample->frames() > capacity_frames_ ||
        state_.load(std::memory_order_acquire) == RecordingState::recording) {
        error = "invalid prepared gapless recording buffer";
        return false;
    }
    gapless_storage_ = std::move(sample);
    gapless_pcm_.store(gapless_storage_.get(), std::memory_order_release);
    error.clear();
    return true;
}
std::shared_ptr<Sample> RecordingService::gapless_buffer() const noexcept {
    return gapless_storage_;
}
void RecordingService::release_gapless_buffer() noexcept {
    gapless_pcm_.store(nullptr, std::memory_order_release);
    gapless_storage_.reset();
}
std::uint64_t RecordingService::next_stop_boundary(
    std::uint64_t callback_frame, std::size_t frames) const noexcept {
    if (!gapless_pcm_.load(std::memory_order_acquire) ||
        !start_enabled_.load(std::memory_order_acquire))
        return 0;
    const auto state = state_.load(std::memory_order_acquire);
    if (state != RecordingState::armed && state != RecordingState::recording)
        return 0;
    const auto stop = requested_stop_.load(std::memory_order_acquire);
    return stop > callback_frame && stop <= callback_frame + frames ? stop : 0;
}

RecordingStatus RecordingService::status() const {
    RecordingStatus result;
    result.state = state_.load(std::memory_order_acquire);
    result.configuration = configuration_;
    result.sample_rate = sample_rate_;
    result.captured_frames = captured_.load(std::memory_order_acquire);
    result.capacity_frames = capacity_frames_;
    result.dropped_frames = dropped_.load(std::memory_order_relaxed);
    result.overflow_count = overflows_.load(std::memory_order_relaxed);
    result.finalization_microseconds =
        finalization_us_.load(std::memory_order_relaxed);
    result.memory_bytes = storage_.size() * sizeof(float);
    result.peak_left = peak_left_.load(std::memory_order_relaxed);
    result.peak_right = peak_right_.load(std::memory_order_relaxed);
    result.error = error_;
    if (result.state == RecordingState::error && result.error.empty())
        result.error = "audio device stopped during recording";
    return result;
}
bool RecordingService::monitoring_active() const noexcept {
    const auto state = state_.load(std::memory_order_acquire);
    if (configuration_.source != RecordingSource::external_mono &&
        configuration_.source != RecordingSource::external_stereo &&
        configuration_.source != RecordingSource::physical_input)
        return false;
    return configuration_.monitoring == RecordingMonitorMode::on ||
           (configuration_.monitoring == RecordingMonitorMode::automatic &&
            (state == RecordingState::armed ||
             state == RecordingState::recording));
}

Sample RecordingService::completed_sample() const {
    const auto frames = captured_.load(std::memory_order_acquire);
    if (frames == 0 ||
        (state_.load(std::memory_order_acquire) != RecordingState::completed &&
         state_.load(std::memory_order_acquire) !=
             RecordingState::publication_pending &&
         state_.load(std::memory_order_acquire) !=
             RecordingState::audio_active))
        throw std::runtime_error("recording is not complete");
    Sample sample{sample_rate_, configuration_.channels, {}};
    sample.interleaved.assign(storage_.begin(),
                              storage_.begin() + frames * sample.channels);
    return sample;
}

bool RecordingService::finalize_to_slot(SampleManager &samples,
                                        std::string &error) {
    try {
        return finalize_sample_to_slot(samples, completed_sample(), error);
    } catch (const std::exception &exception) {
        error_ = exception.what();
        error = error_;
        state_.store(RecordingState::error, std::memory_order_release);
        return false;
    }
}
bool RecordingService::finalize_sample_to_slot(SampleManager &samples,
                                               Sample sample,
                                               std::string &error) {
    if (state_.load(std::memory_order_acquire) ==
            RecordingState::publication_pending ||
        state_.load(std::memory_order_acquire) == RecordingState::audio_active)
        return true;
    const auto begin = std::chrono::steady_clock::now();
    try {
        auto prepared = std::make_shared<const Sample>(std::move(sample));
        if (!samples.publish_prepared(configuration_.destination, prepared, {})) {
            error = "recorded sample publication capacity is exhausted";
            return false;
        }
        finalization_us_.store(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin)
                .count()));
        state_.store(RecordingState::publication_pending,
                     std::memory_order_release);
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error_ = exception.what();
        error = error_;
        state_.store(RecordingState::error, std::memory_order_release);
        return false;
    }
}
void RecordingService::acknowledge_audio_active() noexcept {
    auto expected = RecordingState::publication_pending;
    state_.compare_exchange_strong(expected, RecordingState::audio_active,
                                   std::memory_order_acq_rel);
}

bool RecordingService::save_wav(const std::filesystem::path &path,
                                RecordingWavFormat format,
                                std::string &error) const {
    try {
        const auto sample = completed_sample();
        return save_sample_wav(sample, path, format, error);
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}

bool RecordingService::save_sample_wav(const Sample &sample,
                                       const std::filesystem::path &path,
                                       RecordingWavFormat format,
                                       std::string &error) {
    try {
        if (sample.sample_rate == 0 || sample.frames() == 0 ||
            (sample.channels != 1 && sample.channels != 2))
            throw std::runtime_error("sample is not exportable");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("cannot open recording output");
        const auto bits = format == RecordingWavFormat::pcm16 ? 16U : 32U;
        const auto bytes = bits / 8;
        const auto data_bytes =
            static_cast<std::uint32_t>(sample.interleaved.size() * bytes);
        output.write("RIFF", 4);
        write_value(output, std::uint32_t{36 + data_bytes});
        output.write("WAVEfmt ", 8);
        write_value(output, std::uint32_t{16});
        write_value(output, static_cast<std::uint16_t>(
                                format == RecordingWavFormat::pcm16 ? 1 : 3));
        write_value(output, sample.channels);
        write_value(output, sample.sample_rate);
        write_value(output, sample.sample_rate * sample.channels * bytes);
        write_value(output,
                    static_cast<std::uint16_t>(sample.channels * bytes));
        write_value(output, static_cast<std::uint16_t>(bits));
        output.write("data", 4);
        write_value(output, data_bytes);
        for (const auto value : sample.interleaved)
            if (format == RecordingWavFormat::pcm16)
                write_value(output,
                            static_cast<std::int16_t>(
                                std::clamp(value, -1.0F, 1.0F) * 32767.0F));
            else
                write_value(output, value);
        if (!output || !(output.flush()))
            throw std::runtime_error("cannot write recording output");
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}
} // namespace neurogenic
