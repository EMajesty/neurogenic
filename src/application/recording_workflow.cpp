#include "neurogenic/application/recording_workflow.hpp"
#include <algorithm>

namespace neurogenic {
namespace {
bool audio_mode(RecordingWorkflowMode mode) noexcept {
    return mode == RecordingWorkflowMode::audio ||
           mode == RecordingWorkflowMode::resample;
}
} // namespace

const char *recording_workflow_mode_name(RecordingWorkflowMode value) noexcept {
    switch (value) {
    case RecordingWorkflowMode::audio: return "External audio";
    case RecordingWorkflowMode::resample: return "Internal resampling";
    case RecordingWorkflowMode::live_loop: return "Live loop";
    case RecordingWorkflowMode::arrangement: return "Arrangement capture";
    case RecordingWorkflowMode::performance: return "Performance capture";
    }
    return "Unknown";
}

const char *recording_workflow_phase_name(RecordingWorkflowPhase value) noexcept {
    switch (value) {
    case RecordingWorkflowPhase::idle: return "Idle";
    case RecordingWorkflowPhase::configuring: return "Configuring";
    case RecordingWorkflowPhase::armed: return "Armed";
    case RecordingWorkflowPhase::waiting: return "Waiting for boundary";
    case RecordingWorkflowPhase::recording: return "Recording";
    case RecordingWorkflowPhase::finalizing: return "Finalizing";
    case RecordingWorkflowPhase::publication_pending: return "Pending publication";
    case RecordingWorkflowPhase::completed: return "Completed";
    case RecordingWorkflowPhase::cancelled: return "Cancelled";
    case RecordingWorkflowPhase::failed: return "Failed";
    }
    return "Unknown";
}

bool RecordingWorkflowCoordinator::submit(
    std::string action, std::map<std::string, ApplicationValue> arguments,
    std::string &error) {
    const auto result = application_.execute(
        {std::move(action), std::move(arguments), "recording-workflow", 0});
    error = result.error;
    return result.accepted;
}

bool RecordingWorkflowCoordinator::configure(
    RecordingWorkflowConfiguration value, std::string &error) {
    const auto state = snapshot();
    if (!state.can_configure) {
        error = "recording configuration is locked while an operation is active";
        return false;
    }
    if (value.destination.value >= invalid_sample_slot.value) {
        error = "recording destination is invalid";
        return false;
    }
    if (value.channels != 1 && value.channels != 2) {
        error = "recording channels must be 1 or 2";
        return false;
    }
    if (value.mode == RecordingWorkflowMode::audio &&
        value.source != "external-mono" && value.source != "external-stereo" &&
        value.source != "physical-input") {
        error = "external recording requires an audio input source";
        return false;
    }
    if (value.mode == RecordingWorkflowMode::resample &&
        (value.source == "external-mono" || value.source == "external-stereo" ||
         value.source == "physical-input")) {
        error = "resampling requires an internal mixer source";
        return false;
    }
    if (value.mode == RecordingWorkflowMode::live_loop &&
        (!value.destination_track || value.loop_length_ticks <= 0)) {
        error = "live-loop destination track and length are required";
        return false;
    }
    configuration_ = std::move(value);
    live_loop_session_ = 0;
    configured_ = true;
    error.clear();
    return true;
}

bool RecordingWorkflowCoordinator::arm(std::string &error) {
    if (!configured_) {
        error = "select a recording mode, source and destination first";
        return false;
    }
    if (audio_mode(configuration_.mode)) {
        if (!submit("recording.source-select.v1",
                    {{"source", configuration_.source},
                     {"source_id", static_cast<std::int64_t>(configuration_.source_id)}},
                    error) ||
            !submit("recording.destination-select.v1",
                    {{"slot_id", static_cast<std::int64_t>(configuration_.destination.value)}},
                    error) ||
            !submit("recording.channels-set.v1",
                    {{"channels", static_cast<std::int64_t>(configuration_.channels)}}, error) ||
            !submit("recording.quantization-set.v1",
                    {{"quantization", configuration_.quantization}}, error) ||
            !submit("recording.fixed-bars-set.v1",
                    {{"bars", static_cast<std::int64_t>(configuration_.bars)}}, error))
            return false;
        return submit("recording.arm.v1", {}, error);
    }
    if (configuration_.mode == RecordingWorkflowMode::live_loop) {
        const auto before = application_.snapshot().live_loops;
        if (!submit("live-loop.create.v1",
                    {{"track", static_cast<std::int64_t>(configuration_.destination_track)},
                     {"slot", static_cast<std::int64_t>(configuration_.destination.value)},
                     {"source", configuration_.source},
                     {"source_id", static_cast<std::int64_t>(configuration_.source_id)},
                     {"channels", static_cast<std::int64_t>(configuration_.channels)},
                     {"quantization", configuration_.quantization},
                     {"length_ticks", static_cast<std::int64_t>(configuration_.loop_length_ticks)}},
                    error))
            return false;
        const auto after = application_.snapshot().live_loops;
        for (const auto &candidate : after)
            if (std::none_of(before.begin(), before.end(), [&](const auto &old) {
                    return old.id == candidate.id;
                }))
                live_loop_session_ = candidate.id;
        if (!live_loop_session_) {
            error = "created live-loop session was not observable";
            return false;
        }
        return submit("live-loop.arm.v1",
                      {{"id", static_cast<std::int64_t>(live_loop_session_)},
                       {"mode", std::string("capture")}}, error);
    }
    const std::string prefix =
        configuration_.mode == RecordingWorkflowMode::performance
            ? "performance.capture-" : "arrangement.record-";
    return submit(prefix + "arm.v1", {{"armed", true}}, error);
}

bool RecordingWorkflowCoordinator::start(std::string &error) {
    if (audio_mode(configuration_.mode))
        return submit("recording.start.v1", {}, error);
    if (configuration_.mode == RecordingWorkflowMode::live_loop)
        return submit("live-loop.start.v1",
                      {{"id", static_cast<std::int64_t>(live_loop_session_)}}, error);
    const auto action = configuration_.mode == RecordingWorkflowMode::performance
                            ? "performance.capture-start.v1"
                            : "arrangement.record-start.v1";
    std::map<std::string, ApplicationValue> args{
        {"mode", configuration_.capture_mode}};
    if (configuration_.mode == RecordingWorkflowMode::performance)
        args.emplace("quantization", configuration_.quantization);
    else
        args.emplace("count_in_bars", static_cast<std::int64_t>(0));
    return submit(action, std::move(args), error);
}

bool RecordingWorkflowCoordinator::stop(std::string &error) {
    if (audio_mode(configuration_.mode))
        return submit("recording.stop.v1", {}, error);
    if (configuration_.mode == RecordingWorkflowMode::live_loop)
        return submit("live-loop.stop.v1",
                      {{"id", static_cast<std::int64_t>(live_loop_session_)}}, error);
    const auto action = configuration_.mode == RecordingWorkflowMode::performance
                            ? "performance.capture-stop.v1"
                            : "arrangement.record-stop.v1";
    return submit(action, {}, error);
}

bool RecordingWorkflowCoordinator::overdub(bool replace, std::string &error) {
    if (configuration_.mode != RecordingWorkflowMode::live_loop ||
        !live_loop_session_) {
        error = "overdub requires a completed live-loop session";
        return false;
    }
    return submit(replace ? "live-loop.replace.v1" : "live-loop.overdub.v1",
                  {{"id", static_cast<std::int64_t>(live_loop_session_)}}, error);
}

bool RecordingWorkflowCoordinator::cancel(std::string &error) {
    if (audio_mode(configuration_.mode))
        return submit("recording.cancel.v1", {}, error);
    if (configuration_.mode == RecordingWorkflowMode::live_loop)
        return submit("live-loop.cancel.v1",
                      {{"id", static_cast<std::int64_t>(live_loop_session_)}}, error);
    const auto action = configuration_.mode == RecordingWorkflowMode::performance
                            ? "performance.capture-discard.v1"
                            : "arrangement.record-discard.v1";
    return submit(action, {}, error);
}

bool RecordingWorkflowCoordinator::commit(std::string &error) {
    if (audio_mode(configuration_.mode) ||
        configuration_.mode == RecordingWorkflowMode::live_loop) {
        error.clear();
        return snapshot().phase == RecordingWorkflowPhase::completed;
    }
    return submit(configuration_.mode == RecordingWorkflowMode::performance
                      ? "performance.capture-commit.v1"
                      : "arrangement.record-commit.v1", {}, error);
}

RecordingWorkflowSnapshot RecordingWorkflowCoordinator::snapshot() const {
    RecordingWorkflowSnapshot out;
    out.configuration = configuration_;
    out.live_loop_session = live_loop_session_;
    const auto state = application_.snapshot();
    if (!configured_) {
        out.phase = RecordingWorkflowPhase::idle;
    } else if (audio_mode(configuration_.mode)) {
        const auto &recording = state.recording;
        out.captured_frames = recording.captured_frames;
        out.capacity_frames = recording.capacity_frames;
        out.overflow_count = recording.overflow_count;
        out.progress = recording.capacity_frames
                           ? static_cast<float>(recording.captured_frames) /
                                 static_cast<float>(recording.capacity_frames)
                           : 0.0F;
        out.error = recording.error;
        switch (recording.state) {
        case RecordingState::idle: out.phase = RecordingWorkflowPhase::configuring; break;
        case RecordingState::armed:
            out.phase = recording.configuration.start_frame > state.transport_frame
                            ? RecordingWorkflowPhase::waiting
                            : RecordingWorkflowPhase::armed;
            break;
        case RecordingState::recording: out.phase = RecordingWorkflowPhase::recording; break;
        case RecordingState::completed: out.phase = RecordingWorkflowPhase::finalizing; break;
        case RecordingState::publication_pending: out.phase = RecordingWorkflowPhase::publication_pending; break;
        case RecordingState::audio_active: out.phase = RecordingWorkflowPhase::completed; break;
        case RecordingState::canceled: out.phase = RecordingWorkflowPhase::cancelled; break;
        case RecordingState::error: out.phase = RecordingWorkflowPhase::failed; break;
        }
        out.result_audio_active = recording.state == RecordingState::audio_active;
        out.result_pending = recording.state == RecordingState::publication_pending;
    } else if (configuration_.mode == RecordingWorkflowMode::live_loop) {
        const auto found = std::find_if(state.live_loops.begin(), state.live_loops.end(),
            [&](const auto &item) { return item.id == live_loop_session_; });
        if (found == state.live_loops.end()) out.phase = RecordingWorkflowPhase::configuring;
        else {
            out.captured_frames = found->captured_frames;
            out.overflow_count = found->overflow_count;
            out.error = found->error.empty() ? found->handoff_failure : found->error;
            switch (found->state) {
            case LiveLoopState::idle: out.phase = RecordingWorkflowPhase::configuring; break;
            case LiveLoopState::armed: out.phase = RecordingWorkflowPhase::armed; break;
            case LiveLoopState::waiting: out.phase = RecordingWorkflowPhase::waiting; break;
            case LiveLoopState::recording:
            case LiveLoopState::overdubbing:
            case LiveLoopState::replacing: out.phase = RecordingWorkflowPhase::recording; break;
            case LiveLoopState::finalizing: out.phase = RecordingWorkflowPhase::finalizing; break;
            case LiveLoopState::playing: out.phase = RecordingWorkflowPhase::completed; break;
            case LiveLoopState::canceled: out.phase = RecordingWorkflowPhase::cancelled; break;
            case LiveLoopState::error: out.phase = RecordingWorkflowPhase::failed; break;
            }
            out.result_audio_active = found->state == LiveLoopState::playing;
        }
    } else {
        const auto &capture = state.arrangement_recording;
        out.captured_events = capture.captured_patterns + capture.captured_automation +
                              capture.captured_performance_launches;
        out.overflow_count = capture.overflow_count;
        out.error = capture.error;
        switch (capture.phase) {
        case ArrangementRecordPhase::idle: out.phase = RecordingWorkflowPhase::configuring; break;
        case ArrangementRecordPhase::armed: out.phase = RecordingWorkflowPhase::armed; break;
        case ArrangementRecordPhase::recording: out.phase = RecordingWorkflowPhase::recording; break;
        case ArrangementRecordPhase::pending: out.phase = RecordingWorkflowPhase::finalizing; break;
        case ArrangementRecordPhase::committed: out.phase = RecordingWorkflowPhase::completed; break;
        case ArrangementRecordPhase::discarded: out.phase = RecordingWorkflowPhase::cancelled; break;
        case ArrangementRecordPhase::overflow:
        case ArrangementRecordPhase::conflict: out.phase = RecordingWorkflowPhase::failed; break;
        }
    }
    out.status = recording_workflow_phase_name(out.phase);
    out.can_configure = out.phase == RecordingWorkflowPhase::idle ||
                        out.phase == RecordingWorkflowPhase::configuring ||
                        out.phase == RecordingWorkflowPhase::completed ||
                        out.phase == RecordingWorkflowPhase::cancelled ||
                        out.phase == RecordingWorkflowPhase::failed;
    out.can_arm = configured_ && out.can_configure;
    out.can_start = out.phase == RecordingWorkflowPhase::armed ||
                    out.phase == RecordingWorkflowPhase::waiting;
    out.can_stop = out.phase == RecordingWorkflowPhase::recording;
    out.can_cancel = !out.can_configure || out.phase == RecordingWorkflowPhase::failed;
    return out;
}

} // namespace neurogenic
