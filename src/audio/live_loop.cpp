#include "neurogenic/audio/live_loop.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
MusicalTick tick_at_frame(std::uint64_t frame, std::uint32_t rate,
                          const TransportMetadata &transport) noexcept {
    return static_cast<MusicalTick>(
        (static_cast<long double>(frame) * ticks_per_quarter * 1'000'000.0L) /
        (static_cast<long double>(rate) *
         transport.microseconds_per_quarter));
}
} // namespace

const char *live_loop_state_name(LiveLoopState state) noexcept {
    switch (state) {
    case LiveLoopState::idle: return "idle";
    case LiveLoopState::armed: return "armed";
    case LiveLoopState::waiting: return "waiting";
    case LiveLoopState::recording: return "recording";
    case LiveLoopState::finalizing: return "finalizing";
    case LiveLoopState::playing: return "playing";
    case LiveLoopState::overdubbing: return "overdubbing";
    case LiveLoopState::replacing: return "replacing";
    case LiveLoopState::canceled: return "canceled";
    case LiveLoopState::error: return "error";
    }
    return "unknown";
}

LiveLoopService::LiveLoopService(AudioEngine &engine, RecordingService &recording,
                                 SampleManager &samples, TimelineService &timeline)
    : engine_(engine), recording_(recording), samples_(samples), timeline_(timeline) {}

LiveLoopService::Session *LiveLoopService::mutable_find(
    LiveLoopSessionId id) noexcept {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
        [=](const auto &session) { return session.status.id == id; });
    return found == sessions_.end() ? nullptr : &*found;
}
const LiveLoopSessionStatus *LiveLoopService::find(
    LiveLoopSessionId id) const {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
        [=](const auto &session) { return session.status.id == id; });
    return found == sessions_.end() ? nullptr : &found->status;
}
std::vector<LiveLoopSessionStatus> LiveLoopService::sessions() const {
    std::vector<LiveLoopSessionStatus> result;
    result.reserve(sessions_.size());
    for (const auto &session : sessions_) result.push_back(session.status);
    return result;
}

MusicalTick LiveLoopService::quantum(const LiveLoopConfiguration &configuration,
                                     const Timeline &timeline) const noexcept {
    switch (configuration.quantization) {
    case LiveLoopQuantization::immediate: return 1;
    case LiveLoopQuantization::subdivision:
        return std::max<MusicalTick>(1, configuration.subdivision_ticks);
    case LiveLoopQuantization::beat: return ticks_per_quarter;
    case LiveLoopQuantization::bar:
        return std::max<MusicalTick>(1,
            static_cast<MusicalTick>(ticks_per_quarter * 4ULL *
                timeline.transport.time_signature_numerator /
                timeline.transport.time_signature_denominator));
    }
    return 1;
}
std::uint64_t LiveLoopService::frames_for_ticks(
    MusicalTick ticks, const Timeline &timeline) const noexcept {
    return static_cast<std::uint64_t>(std::ceil(
        static_cast<long double>(std::max<MusicalTick>(0, ticks)) *
        engine_.sample_rate() * timeline.transport.microseconds_per_quarter /
        (static_cast<long double>(ticks_per_quarter) * 1'000'000.0L)));
}

LiveLoopSessionId LiveLoopService::create(LiveLoopConfiguration configuration,
                                          std::string &error) {
    const auto snapshot = timeline_.snapshot().timeline;
    if (sessions_.size() >= maximum_sessions || !configuration.destination_track ||
        configuration.destination_slot == invalid_sample_slot ||
        configuration.length_ticks <= 0 || configuration.channels < 1 ||
        configuration.channels > 2 || !std::isfinite(configuration.overdub_feedback) ||
        configuration.overdub_feedback < 0 || configuration.overdub_feedback > 1 ||
        (configuration.quantization == LiveLoopQuantization::subdivision &&
         configuration.subdivision_ticks <= 0) ||
        std::none_of(snapshot.tracks.begin(), snapshot.tracks.end(),
                     [&](const auto &track) {
                         return track.id == configuration.destination_track;
                     })) {
        error = sessions_.size() >= maximum_sessions
                    ? "live-loop session capacity exhausted"
                    : "invalid live-loop configuration";
        return 0;
    }
    Session session;
    session.status.id = next_id_++;
    session.status.configuration = configuration;
    sessions_.push_back(std::move(session));
    error.clear();
    return sessions_.back().status.id;
}
bool LiveLoopService::configure(LiveLoopSessionId id,
                                LiveLoopConfiguration configuration,
                                std::string &error) {
    auto *session = mutable_find(id);
    if (!session || active_id_ == id) {
        error = !session ? "live-loop session does not exist"
                         : "cannot configure an active live-loop session";
        return false;
    }
    const auto old = session->status.configuration;
    session->status.configuration = configuration;
    const auto snapshot = timeline_.snapshot().timeline;
    if (!configuration.destination_track ||
        configuration.destination_slot == invalid_sample_slot ||
        configuration.length_ticks <= 0 || configuration.channels < 1 ||
        configuration.channels > 2 || !std::isfinite(configuration.overdub_feedback) ||
        configuration.overdub_feedback < 0 || configuration.overdub_feedback > 1 ||
        (configuration.quantization == LiveLoopQuantization::subdivision &&
         configuration.subdivision_ticks <= 0) ||
        std::none_of(snapshot.tracks.begin(), snapshot.tracks.end(),
                     [&](const auto &track) { return track.id == configuration.destination_track; })) {
        session->status.configuration = old;
        error = "invalid live-loop configuration";
        return false;
    }
    error.clear();
    return true;
}

bool LiveLoopService::arm(LiveLoopSessionId id, LiveLoopMode mode,
                          std::string &error) {
    auto *session = mutable_find(id);
    if (!session || active_id_ ||
        (mode != LiveLoopMode::capture && !session->status.clip_id)) {
        error = !session ? "live-loop session does not exist"
                         : active_id_ ? "another live-loop capture is active"
                                      : "overdub/replace requires an active loop";
        return false;
    }
    const auto timeline = timeline_.snapshot().timeline;
    if (session->status.configuration.quantization !=
            LiveLoopQuantization::immediate && !engine_.playing()) {
        error = "quantized live-loop recording requires running transport";
        return false;
    }
    if (mode != LiveLoopMode::overdub &&
        session->status.configuration.source == RecordingSource::track_channel &&
        session->status.configuration.source_id ==
            session->status.configuration.destination_track) {
        error = "recording a destination track into itself requires overdub mode";
        return false;
    }
    const auto transport_frame = engine_.frame_position();
    const auto now_tick = tick_at_frame(transport_frame, engine_.sample_rate(),
                                        timeline.transport);
    const auto q = quantum(session->status.configuration, timeline);
    auto start_tick = now_tick;
    if (session->status.configuration.quantization !=
        LiveLoopQuantization::immediate)
        start_tick = ((now_tick / q) + 1) * q;
    const auto delta = frames_for_ticks(start_tick - now_tick, timeline);
    auto start_frame = engine_.callback_frame_position() + delta;
    if (mode != LiveLoopMode::capture && session->status.captured_frames) {
        start_frame = session->status.actual_stop_frame;
        while (start_frame <= engine_.callback_frame_position())
            start_frame += session->status.captured_frames;
        const auto loop_delta = start_frame - engine_.callback_frame_position();
        start_tick = now_tick + static_cast<MusicalTick>(
            (static_cast<long double>(loop_delta) * ticks_per_quarter *
             1'000'000.0L) /
            (static_cast<long double>(engine_.sample_rate()) *
             timeline.transport.microseconds_per_quarter));
    }
    const auto compensation = session->status.configuration.latency_compensation_frames;
    if (compensation < 0)
        start_frame -= std::min(start_frame, static_cast<std::uint64_t>(-compensation));
    else if (compensation > 0)
        start_frame += static_cast<std::uint64_t>(compensation);
    const auto length_frames = frames_for_ticks(
        session->status.configuration.length_ticks, timeline);
    if (mode == LiveLoopMode::capture) {
        if (!session->status.clip_id)
            session->status.clip_id = timeline_.edit().next_audio_clip_id();
        try {
            session->handoff_sample = std::make_shared<Sample>();
            session->handoff_sample->sample_rate = engine_.sample_rate();
            session->handoff_sample->channels =
                session->status.configuration.source == RecordingSource::external_mono
                    ? 1 : session->status.configuration.channels;
            session->handoff_sample->interleaved.resize(
                static_cast<std::size_t>(length_frames) *
                session->handoff_sample->channels);
        } catch (...) {
            session->handoff_sample.reset();
            error = "unable to reserve gapless capture buffer";
            session->status.handoff_failure = error;
            return false;
        }
        if (!recording_.prepare_gapless_buffer(session->handoff_sample, error) ||
            !engine_.prepare_gapless_handoff(
                session->status.configuration.destination_slot,
                session->handoff_sample,
                session->status.configuration.destination_track,
                session->status.clip_id, length_frames, error)) {
            recording_.release_gapless_buffer();
            session->handoff_sample.reset();
            session->status.handoff_failure = error;
            return false;
        }
        session->status.buffer_reserved = true;
        session->status.voice_reserved = true;
        session->status.handoff_ready = true;
        session->status.handoff_failure.clear();
    }
    RecordingConfiguration recording;
    recording.source = session->status.configuration.source;
    recording.source_id = session->status.configuration.source_id;
    recording.destination = session->status.configuration.destination_slot;
    recording.channels = session->status.configuration.channels;
    recording.monitoring = session->status.configuration.monitoring;
    recording.quantization = start_frame == engine_.callback_frame_position()
                                 ? RecordingQuantization::immediate
                                 : RecordingQuantization::beat;
    recording.start_frame = start_frame;
    recording.fixed_frames = length_frames;
    if (!recording_.arm(recording, error)) {
        if (mode == LiveLoopMode::capture) {
            engine_.cancel_gapless_handoff();
            recording_.release_gapless_buffer();
            session->handoff_sample.reset();
        }
        return false;
    }
    session->status.mode = mode;
    session->status.requested_start_tick = start_tick;
    session->status.requested_stop_tick =
        start_tick + session->status.configuration.length_ticks;
    session->status.requested_start_frame = start_frame;
    session->status.requested_stop_frame = start_frame + length_frames;
    session->status.actual_start_frame = session->status.actual_stop_frame = 0;
    session->status.state = LiveLoopState::armed;
    session->status.error.clear();
    session->previous_sample = samples_.active_sample(
        session->status.configuration.destination_slot);
    session->previous_captured_frames = session->status.captured_frames;
    session->previous_actual_start_frame = session->status.actual_start_frame;
    session->previous_actual_stop_frame = session->status.actual_stop_frame;
    active_id_ = id;
    error.clear();
    return true;
}

bool LiveLoopService::start(LiveLoopSessionId id, std::string &error) {
    auto *session = mutable_find(id);
    if (!session || active_id_ != id || session->status.state != LiveLoopState::armed) {
        error = "live-loop session is not armed";
        return false;
    }
    if (!recording_.start(engine_.callback_frame_position(), error)) return false;
    session->status.state = LiveLoopState::waiting;
    return true;
}
bool LiveLoopService::stop(LiveLoopSessionId id, std::string &error) {
    auto *session = mutable_find(id);
    if (!session || active_id_ != id) {
        error = "live-loop session is not active";
        return false;
    }
    const auto timeline = timeline_.snapshot().timeline;
    const auto q = quantum(session->status.configuration, timeline);
    if (session->status.configuration.quantization == LiveLoopQuantization::immediate)
        return recording_.stop(error);
    const auto now_tick = tick_at_frame(engine_.frame_position(),
                                        engine_.sample_rate(), timeline.transport);
    const auto stop_tick = ((now_tick / q) + 1) * q;
    session->status.requested_stop_tick = stop_tick;
    session->status.requested_stop_frame = engine_.callback_frame_position() +
                                           frames_for_ticks(stop_tick - now_tick, timeline);
    return recording_.stop_at(session->status.requested_stop_frame, error);
}
bool LiveLoopService::cancel(LiveLoopSessionId id, std::string &error) {
    auto *session = mutable_find(id);
    if (!session) { error = "live-loop session does not exist"; return false; }
    if (active_id_ == id) {
        recording_.cancel();
        engine_.cancel_gapless_handoff();
        recording_.release_gapless_buffer();
        session->handoff_sample.reset();
        active_id_ = 0;
    }
    if (session->status.mode != LiveLoopMode::capture &&
        session->status.clip_id && session->previous_sample) {
        session->status.state = LiveLoopState::playing;
        session->status.captured_frames = session->previous_captured_frames;
        session->status.actual_start_frame = session->previous_actual_start_frame;
        session->status.actual_stop_frame = session->previous_actual_stop_frame;
        session->previous_sample.reset();
    } else {
        session->status.state = LiveLoopState::canceled;
        session->status.captured_frames = 0;
    }
    session->status.error.clear();
    error.clear();
    return true;
}

bool LiveLoopService::remove(LiveLoopSessionId id, std::string &error) {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
        [=](const auto &session) { return session.status.id == id; });
    if (found == sessions_.end()) {
        error = "live-loop session does not exist";
        return false;
    }
    if (active_id_ == id) {
        error = "cannot clear an active live-loop pass";
        return false;
    }
    if (found->status.clip_id) {
        auto transaction = timeline_.edit();
        if (!transaction.delete_audio_clip(found->status.clip_id, error) ||
            !timeline_.commit(std::move(transaction),
                              TimelineCommitPolicy::next_audio_callback,
                              error))
            return false;
    }
    // Retain the immutable sample slot: other instruments may still own it and
    // the user may export or reassign it after clearing the live session.
    sessions_.erase(found);
    error.clear();
    return true;
}

bool LiveLoopService::save(LiveLoopSessionId id,
                           const std::filesystem::path &path,
                           RecordingWavFormat format, std::string &error) {
    const auto *session = find(id);
    if (!session || session->state != LiveLoopState::playing) {
        error = !session ? "live-loop session does not exist"
                         : "live-loop audio is not active";
        return false;
    }
    const auto sample = samples_.active_sample(
        session->configuration.destination_slot);
    if (!sample) {
        error = "live-loop sample is not audio-active";
        return false;
    }
    if (!RecordingService::save_sample_wav(*sample, path, format, error))
        return false;
    if (!samples_.set_source_path(session->configuration.destination_slot,
                                  path)) {
        error = "saved live-loop sample is not active";
        return false;
    }
    return true;
}
void LiveLoopService::clear() noexcept {
    if (active_id_) recording_.cancel();
    engine_.cancel_gapless_handoff();
    recording_.release_gapless_buffer();
    active_id_ = 0;
    sessions_.clear();
}

void LiveLoopService::update() {
    for (auto &item : sessions_)
        if (item.status.state == LiveLoopState::playing &&
            item.status.captured_frames &&
            engine_.callback_frame_position() > item.status.actual_stop_frame)
            item.status.loop_iteration =
                (engine_.callback_frame_position() -
                 item.status.actual_stop_frame) /
                item.status.captured_frames;
    auto *session = mutable_find(active_id_);
    if (!session) return;
    auto recording = recording_.status();
    session->status.handoff_completed = engine_.gapless_handoff_completed();
    session->status.first_playback_frame =
        engine_.gapless_first_playback_frame();
    session->status.captured_frames = recording.captured_frames;
    session->status.overflow_count = recording.overflow_count;
    if (recording.state == RecordingState::armed) return;
    if (recording.state == RecordingState::recording) {
        session->status.actual_start_frame = recording.configuration.start_frame;
        session->status.state = session->status.mode == LiveLoopMode::overdub
            ? LiveLoopState::overdubbing
            : session->status.mode == LiveLoopMode::replace
                ? LiveLoopState::replacing : LiveLoopState::recording;
        return;
    }
    if (recording.state == RecordingState::error) {
        session->status.error = recording.error;
        if (session->status.mode != LiveLoopMode::capture &&
            session->previous_sample) {
            session->status.state = LiveLoopState::playing;
            session->status.captured_frames = session->previous_captured_frames;
            session->status.actual_start_frame = session->previous_actual_start_frame;
            session->status.actual_stop_frame = session->previous_actual_stop_frame;
            session->previous_sample.reset();
        } else
            session->status.state = LiveLoopState::error;
        active_id_ = 0;
        return;
    }
    if (recording.state == RecordingState::completed) {
        session->status.actual_start_frame = recording.configuration.start_frame;
        session->status.actual_stop_frame = recording.configuration.start_frame +
                                            recording.captured_frames;
        try {
            if (session->status.mode == LiveLoopMode::capture) {
                if (!session->handoff_sample ||
                    !samples_.publish_prepared(
                        session->status.configuration.destination_slot,
                        session->handoff_sample, {}))
                    throw std::runtime_error("gapless sample publication failed");
                session->expected_publication = samples_.status(
                    session->status.configuration.destination_slot).revision;
                recording_.mark_publication_pending();
                session->status.state = LiveLoopState::finalizing;
                return;
            }
            auto result = recording_.completed_sample();
            if (session->status.mode == LiveLoopMode::overdub) {
                if (!session->previous_sample ||
                    session->previous_sample->sample_rate != result.sample_rate ||
                    session->previous_sample->channels != result.channels ||
                    session->previous_sample->frames() != result.frames())
                    throw std::runtime_error("overdub source does not match loop format and length");
                const auto feedback = session->status.configuration.overdub_feedback;
                for (std::size_t i = 0; i < result.interleaved.size(); ++i)
                    result.interleaved[i] =
                        session->previous_sample->interleaved[i] * feedback +
                        result.interleaved[i];
            }
            std::string error;
            if (!recording_.finalize_sample_to_slot(samples_, std::move(result), error))
                throw std::runtime_error(error);
            session->expected_publication = samples_.status(
                session->status.configuration.destination_slot).revision;
            session->status.state = LiveLoopState::finalizing;
        } catch (const std::exception &exception) {
            session->status.error = exception.what();
            session->status.handoff_failure = exception.what();
            if (session->status.mode != LiveLoopMode::capture &&
                session->previous_sample) {
                session->status.state = LiveLoopState::playing;
                session->status.captured_frames = session->previous_captured_frames;
                session->status.actual_start_frame = session->previous_actual_start_frame;
                session->status.actual_stop_frame = session->previous_actual_stop_frame;
                session->previous_sample.reset();
            } else
                session->status.state = LiveLoopState::error;
            active_id_ = 0;
        }
        return;
    }
    if (recording.state != RecordingState::publication_pending) return;
    const auto slot = samples_.status(session->status.configuration.destination_slot);
    if (slot.state != SampleSlotLoadState::loaded || !slot.has_sample ||
        slot.active_revision != session->expected_publication)
        return;
    recording_.acknowledge_audio_active();
    auto transaction = timeline_.edit();
    auto activation_tick = session->status.requested_stop_tick;
    std::string error;
    const auto existing = std::find_if(
        transaction.timeline().arrangement.audio_clips.begin(),
        transaction.timeline().arrangement.audio_clips.end(),
        [&](const auto &clip) { return clip.id == session->status.clip_id; });
    if (existing == transaction.timeline().arrangement.audio_clips.end() &&
        session->status.mode == LiveLoopMode::capture) {
        AudioClip clip;
        clip.id = session->status.clip_id
                      ? session->status.clip_id
                      : transaction.next_audio_clip_id();
        clip.track = session->status.configuration.destination_track;
        clip.sample_slot = session->status.configuration.destination_slot.value;
        clip.sample_publication = slot.active_publication;
        clip.start = activation_tick;
        clip.duration = session->status.configuration.length_ticks;
        clip.source_end_frame = slot.frames;
        clip.loop_start_frame = 0;
        clip.loop_end_frame = slot.frames;
        clip.loop = true;
        if (transaction.timeline().arrangement.playback_mode ==
                TimelinePlaybackMode::song &&
            transaction.timeline().arrangement.song_end < clip.start + clip.duration)
            transaction.set_song_range(transaction.timeline().arrangement.song_start,
                                       clip.start + clip.duration, error);
        if (error.empty() && transaction.create_audio_clip(clip, error))
            session->status.clip_id = clip.id;
    } else {
        if (existing == transaction.timeline().arrangement.audio_clips.end())
            error = "live-loop audio clip no longer exists";
        else {
            auto clip = *existing;
            clip.sample_publication = slot.active_publication;
            clip.source_end_frame = slot.frames;
            clip.loop_end_frame = slot.frames;
            transaction.update_audio_clip(clip, error);
        }
    }
    if (error.empty() && timeline_.commit(std::move(transaction),
            TimelineCommitPolicy::next_audio_callback, error)) {
        session->status.state = LiveLoopState::playing;
        session->status.loop_iteration = 0;
        session->previous_sample.reset();
        engine_.cancel_gapless_handoff();
        recording_.release_gapless_buffer();
        session->handoff_sample.reset();
        active_id_ = 0;
        recording_.reset();
    } else {
        session->status.state = LiveLoopState::error;
        session->status.error = error.empty() ? "live-loop activation failed" : error;
        active_id_ = 0;
    }
}
} // namespace neurogenic
