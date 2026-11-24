#include "neurogenic/audio/live_loop.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/application/controller_protocol.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace neurogenic;
    AudioEngine engine(48'000, 8);
    SampleManager samples(engine);
    Timeline timeline;
    timeline.tracks.push_back({1, "Loop", false, {960, {}},
                               std::string(sample_instrument_id)});
    timeline.arrangement.playback_mode = TimelinePlaybackMode::song;
    timeline.arrangement.song_start = 0;
    timeline.arrangement.song_end = 20'000;
    TimelineService timelines(
        [&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, timeline);
    RecordingService recorder(48'000, 1024);
    engine.set_recording_service(&recorder);
    LiveLoopService loops(engine, recorder, samples, timelines);

    LiveLoopConfiguration configuration;
    configuration.destination_track = 1;
    configuration.destination_slot = SampleSlotId{3};
    configuration.source = RecordingSource::master_post_effects;
    configuration.quantization = LiveLoopQuantization::immediate;
    configuration.length_ticks = 4; // 100 frames at 120 BPM.
    std::string error;
    const auto id = loops.create(configuration, error);
    check(id && loops.arm(id, LiveLoopMode::capture, error) &&
              loops.start(id, error),
          "immediate live-loop capture did not start");
    std::array<float, 100> left{}, right{};
    left.fill(.25F);
    right.fill(.25F);
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              engine.callback_frame_position());
    loops.update();
    check(loops.find(id)->state == LiveLoopState::finalizing,
          "completed loop did not enter finalization");
    std::array<float, 1> ack_left{}, ack_right{};
    engine.process(ack_left, ack_right);
    loops.update();
    const auto first = loops.find(id);
    check(first && first->state == LiveLoopState::playing && first->clip_id &&
              first->actual_stop_frame - first->actual_start_frame == 100,
          "first pass did not publish and create a clip");
    const auto snapshot = timelines.snapshot().timeline;
    check(snapshot.arrangement.audio_clips.size() == 1 &&
              snapshot.arrangement.audio_clips.front().loop &&
              snapshot.arrangement.audio_clips.front().duration == 4,
          "live-loop clip metadata is incorrect");

    configuration.overdub_feedback = .5F;
    const auto before_canceled_overdub = samples.active_sample(SampleSlotId{3});
    check(loops.configure(id, configuration, error) &&
              loops.arm(id, LiveLoopMode::overdub, error) &&
              loops.cancel(id, error) &&
              loops.find(id)->state == LiveLoopState::playing &&
              loops.find(id)->captured_frames == 100 &&
              samples.active_sample(SampleSlotId{3}) == before_canceled_overdub,
          "canceled overdub did not preserve the active loop");
    check(loops.arm(id, LiveLoopMode::overdub, error) &&
              loops.start(id, error),
          "overdub did not start");
    left.fill(.5F);
    right.fill(.5F);
    const auto overdub_start = loops.find(id)->requested_start_frame;
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              overdub_start);
    loops.update();
    check(loops.find(id)->state == LiveLoopState::finalizing,
          "overdub acknowledged an older active sample publication");
    engine.process(ack_left, ack_right);
    loops.update();
    const auto overdubbed = samples.active_sample(SampleSlotId{3});
    check(overdubbed && std::abs(overdubbed->interleaved.front() - .625F) < 1e-6F &&
              timelines.snapshot().timeline.arrangement.audio_clips.front().id ==
                  first->clip_id,
          "overdub was not phase-aligned immutable replacement");

    check(loops.arm(id, LiveLoopMode::replace, error) && loops.start(id, error),
          "replace pass did not start");
    left.fill(.1F);
    right.fill(.1F);
    const auto replace_start = loops.find(id)->requested_start_frame;
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              replace_start);
    loops.update();
    engine.process(ack_left, ack_right);
    loops.update();
    const auto replaced = samples.active_sample(SampleSlotId{3});
    check(replaced && std::abs(replaced->interleaved.front() - .1F) < 1e-6F,
          "replace pass did not preserve clip identity with new audio");

    const auto export_path = std::filesystem::temp_directory_path() /
        "neurogenic-live-loop-result.wav";
    std::error_code ignored;
    std::filesystem::remove(export_path, ignored);
    check(loops.save(id, export_path, RecordingWavFormat::float32, error) &&
              std::filesystem::file_size(export_path) == 44 + 100 * 2 * 4 &&
              samples.status(SampleSlotId{3}).source_path == export_path,
          "audio-active loop export did not preserve the reusable asset path");
    std::filesystem::remove(export_path, ignored);

    engine.restart();
    engine.process(ack_left, ack_right);
    LiveLoopConfiguration beat = configuration;
    beat.destination_slot = SampleSlotId{4};
    beat.quantization = LiveLoopQuantization::beat;
    const auto beat_id = loops.create(beat, error);
    check(beat_id && loops.arm(beat_id, LiveLoopMode::capture, error),
          "beat-quantized loop did not arm");
    const auto beat_status = loops.find(beat_id);
    check(beat_status->requested_start_frame == engine.callback_frame_position() +
              24'000 && beat_status->requested_start_tick == 960,
          "beat boundary was not derived from authoritative transport");
    check(!loops.remove(beat_id, error) &&
              error.find("active") != std::string::npos,
          "active live-loop session removal was not rejected");
    check(loops.cancel(beat_id, error), "armed loop cancellation failed");

    LiveLoopConfiguration unsafe = configuration;
    unsafe.source = RecordingSource::track_channel;
    unsafe.source_id = 1;
    unsafe.destination_slot = SampleSlotId{5};
    const auto unsafe_id = loops.create(unsafe, error);
    check(unsafe_id && !loops.arm(unsafe_id, LiveLoopMode::capture, error) &&
              error.find("itself") != std::string::npos,
          "same-track feedback capture was not rejected");

    for (std::size_t index = loops.sessions().size();
         index < LiveLoopService::maximum_sessions; ++index) {
        unsafe.destination_slot = SampleSlotId{
            static_cast<std::uint32_t>(10 + index)};
        check(loops.create(unsafe, error) != 0,
              "bounded live-loop session creation failed early");
    }
    check(loops.create(unsafe, error) == 0 &&
              error.find("capacity") != std::string::npos,
          "live-loop session capacity exhaustion was not reported");
    check(loops.remove(id, error) &&
              timelines.snapshot().timeline.arrangement.audio_clips.empty() &&
              samples.active_sample(SampleSlotId{3}),
          "clearing a session did not remove its clip while retaining PCM");

    ApplicationState controller_state;
    LiveLoopSessionStatus controller_loop;
    controller_loop.id = 42;
    controller_loop.state = LiveLoopState::playing;
    controller_state.live_loops.push_back(controller_loop);
    controller_state.available_actions = {
        "live-loop.arm.v1", "live-loop.start.v1", "live-loop.stop.v1",
        "live-loop.overdub.v1", "live-loop.replace.v1",
        "live-loop.cancel.v1"};
    const auto controls = resolve_controller_context(
        controller_state, "performance", {});
    check(controls[10].label == "OVERDUB" && controls[10].active &&
              std::get<std::int64_t>(controls[10].arguments.at("id")) == 42 &&
              controls[11].label == "REPLACE" && controls[11].active &&
              !controls[9].active,
          "performance context did not preserve the stable live-loop target");

    // The duplex callback deliberately spans the stop boundary. The first
    // captured sample must be rendered at that exact frame, with neither a
    // callback-sized silent gap nor a duplicated boundary sample.
    {
        AudioEngine handoff_engine(48'000, 4);
        SampleManager handoff_samples(handoff_engine);
        Timeline handoff_timeline;
        handoff_timeline.tracks.push_back(
            {1, "Gapless", false, {960, {}},
             std::string(sample_instrument_id)});
        handoff_timeline.arrangement.playback_mode =
            TimelinePlaybackMode::song;
        handoff_timeline.arrangement.song_end = 20'000;
        TimelineService handoff_timelines(
            [&](const Timeline &value, std::string &publish_error) {
                return handoff_engine.queue_timeline(value, publish_error);
            }, handoff_timeline);
        RecordingService handoff_recorder(48'000, 256);
        handoff_engine.set_recording_service(&handoff_recorder);
        LiveLoopService handoff_loops(handoff_engine, handoff_recorder,
                                      handoff_samples, handoff_timelines);
        LiveLoopConfiguration handoff_configuration;
        handoff_configuration.destination_track = 1;
        handoff_configuration.destination_slot = SampleSlotId{9};
        handoff_configuration.source = RecordingSource::external_mono;
        handoff_configuration.channels = 1;
        handoff_configuration.quantization = LiveLoopQuantization::immediate;
        handoff_configuration.length_ticks = 4;
        const auto handoff_id = handoff_loops.create(handoff_configuration,
                                                     error);
        check(handoff_id && handoff_loops.arm(handoff_id, LiveLoopMode::capture,
                                              error) &&
                  handoff_loops.start(handoff_id, error),
              "gapless handoff did not prepare before capture");
        std::array<float, 150> input{}, output_left{}, output_right{};
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = .2F + static_cast<float>(i) * .001F;
        handoff_engine.process_duplex(output_left, output_right, input, 1);
        const auto handoff = handoff_loops.find(handoff_id);
        check(handoff && handoff_engine.gapless_handoff_completed() &&
                  handoff_engine.gapless_first_playback_frame() == 100,
              "handoff was not activated at the exact recording boundary");
        check(std::abs(output_left[99]) < 1e-6F &&
                  std::abs(output_left[100] - .16F) < 1e-5F &&
                  std::abs(output_left[101] - .1608F) < 1e-5F,
              "first repetition contained a gap or duplicated first frame");
        handoff_loops.update();
        handoff_engine.set_recording_service(nullptr);
    }

    engine.set_recording_service(nullptr);
    std::cout << "neurogenic.live-loop-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
