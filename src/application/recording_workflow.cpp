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
    const auto retain_session = live_loop_session_ &&
        configuration_.mode == RecordingWorkflowMode::live_loop &&
        value.mode == RecordingWorkflowMode::live_loop &&
        configuration_.destination == value.destination &&
        configuration_.destination_track == value.destination_track &&
        configuration_.loop_length_ticks == value.loop_length_ticks;
    if (retain_session) {
        if (!submit("live-loop.source-set.v1",
                    {{"id", static_cast<std::int64_t>(live_loop_session_)},
                     {"source", value.source},
                     {"source_id", static_cast<std::int64_t>(value.source_id)}},
                    error) ||
            !submit("live-loop.quantization-set.v1",
                    {{"id", static_cast<std::int64_t>(live_loop_session_)},
                     {"quantization", value.quantization}}, error))
            return false;
    } else
        live_loop_session_ = 0;
    configuration_ = std::move(value);
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

bool RecordingWorkflowCoordinator::select_live_loop(LiveLoopSessionId id,
                                                     std::string &error) {
    const auto state = application_.snapshot();
    const auto found = std::find_if(state.live_loops.begin(),
        state.live_loops.end(), [=](const auto &session) {
            return session.id == id;
        });
    if (found == state.live_loops.end()) {
        error = "live-loop session does not exist";
        return false;
    }
    const auto current = snapshot();
    if (!current.can_configure && live_loop_session_ != id) {
        error = "cannot change live-loop selection during an active pass";
        return false;
    }
    configuration_.mode = RecordingWorkflowMode::live_loop;
    configuration_.destination_track = found->configuration.destination_track;
    configuration_.destination = found->configuration.destination_slot;
    configuration_.source_id = found->configuration.source_id;
    configuration_.channels = found->configuration.channels;
    configuration_.loop_length_ticks = found->configuration.length_ticks;
    switch (found->configuration.source) {
    case RecordingSource::external_mono: configuration_.source = "external-mono"; break;
    case RecordingSource::external_stereo: configuration_.source = "external-stereo"; break;
    case RecordingSource::physical_input: configuration_.source = "physical-input"; break;
    case RecordingSource::track_channel: configuration_.source = "track"; break;
    case RecordingSource::group_pre_effects: configuration_.source = "group-pre"; break;
    case RecordingSource::group_post_effects: configuration_.source = "group-post"; break;
    case RecordingSource::group_post_fader: configuration_.source = "group-fader"; break;
    case RecordingSource::auxiliary_bus: configuration_.source = "aux"; break;
    case RecordingSource::master_pre_effects: configuration_.source = "master-pre"; break;
    case RecordingSource::master_post_effects: configuration_.source = "master-post"; break;
    case RecordingSource::external_fx_return: configuration_.source = "external-fx-return"; break;
    case RecordingSource::external_fx_mix: configuration_.source = "external-fx-mix"; break;
    }
    switch (found->configuration.quantization) {
    case LiveLoopQuantization::immediate: configuration_.quantization = "immediate"; break;
    case LiveLoopQuantization::subdivision: configuration_.quantization = "subdivision"; break;
    case LiveLoopQuantization::beat: configuration_.quantization = "beat"; break;
    case LiveLoopQuantization::bar: configuration_.quantization = "bar"; break;
    }
    live_loop_session_ = id;
    configured_ = true;
    error.clear();
    return true;
}

bool RecordingWorkflowCoordinator::clear_live_loop(std::string &error) {
    if (!live_loop_session_) {
        error = "select a live-loop session first";
        return false;
    }
    if (!submit("live-loop.clear.v1",
                {{"id", static_cast<std::int64_t>(live_loop_session_)}},
                error))
        return false;
    live_loop_session_ = 0;
    return true;
}

bool RecordingWorkflowCoordinator::save_live_loop(const std::string &path,
                                                   std::string &error) {
    if (!live_loop_session_ || path.empty()) {
        error = !live_loop_session_ ? "select a live-loop session first"
                                    : "live-loop export path is empty";
        return false;
    }
    return submit("live-loop.save.v1",
                  {{"id", static_cast<std::int64_t>(live_loop_session_)},
                   {"path", path}, {"format", std::string("float32")}},
                  error);
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
    out.live_loop_sessions = state.live_loops;
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
            out.loop_iteration = found->loop_iteration;
            out.requested_start_frame = found->requested_start_frame;
            out.requested_stop_frame = found->requested_stop_frame;
            out.capacity_frames = found->requested_stop_frame >
                                          found->requested_start_frame
                                      ? found->requested_stop_frame -
                                            found->requested_start_frame
                                      : 0;
            out.progress = out.capacity_frames
                               ? std::min(1.0F, static_cast<float>(
                                      found->captured_frames) /
                                      static_cast<float>(out.capacity_frames))
                               : 0.0F;
            out.live_loop_mode = found->mode == LiveLoopMode::overdub
                                     ? "overdub"
                                 : found->mode == LiveLoopMode::replace
                                     ? "replace"
                                     : "capture";
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
    out.can_overdub = configuration_.mode == RecordingWorkflowMode::live_loop &&
                      out.phase == RecordingWorkflowPhase::completed;
    out.can_replace = out.can_overdub;
    out.can_clear = out.can_overdub;
    out.can_save = out.can_overdub && out.result_audio_active;
    return out;
}

} // namespace neurogenic
