#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/recording_workflow.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
CommandResult run(ApplicationService &application, std::string action,
                  std::map<std::string, ApplicationValue> arguments = {}) {
    auto result = application.execute(
        {std::move(action), std::move(arguments), "loop-overdub-test", 0});
    require(result.accepted, result.error);
    return result;
}
ControllerInput press(std::string id, bool down, std::uint64_t sequence) {
    return {std::move(id), down ? 1.0 : 0.0, 0.0, down, sequence, 1};
}
const MusicalPattern &pattern(const Timeline &timeline, PatternId pattern_id,
                              TrackId track_id) {
    const auto definition = std::find_if(
        timeline.arrangement.patterns.begin(),
        timeline.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    require(definition != timeline.arrangement.patterns.end(),
            "reusable pattern is missing");
    const auto track = std::find_if(definition->tracks.begin(),
        definition->tracks.end(),
        [=](const auto &value) { return value.track == track_id; });
    require(track != definition->tracks.end(), "pattern track is missing");
    return track->pattern;
}
std::size_t note_count(const Timeline &timeline, PatternId pattern_id,
                       TrackId track_id) {
    return std::count_if(pattern(timeline, pattern_id, track_id).events.begin(),
                         pattern(timeline, pattern_id, track_id).events.end(),
                         [](const auto &event) {
                             return event.type == MusicalEventType::note;
                         });
}
}

int main() try {
    constexpr MusicalTick loop_ticks = ticks_per_quarter / 4;
    AudioEngine engine(48'000, 64);
    engine.set_sample(Sample{48'000, 1, std::vector<float>(96, 0.5F)});
    Timeline initial;
    Track track{1, "Loop track"};
    track.instrument_id = std::string(sample_instrument_id);
    Track unrelated{2, "Unrelated"};
    unrelated.instrument_id = std::string(sample_instrument_id);
    initial.tracks = {track, unrelated};
    MusicalPattern empty;
    empty.length = loop_ticks;
    initial.arrangement.patterns = {
        {10, "Overdub", loop_ticks, 1, 4, {{1, empty}, {2, empty}}, {}}};
    initial.arrangement.playback_mode = TimelinePlaybackMode::song;
    initial.arrangement.song_start = 0;
    initial.arrangement.song_end = loop_ticks;
    initial.arrangement.loop_enabled = true;
    initial.arrangement.loop_start = 0;
    initial.arrangement.loop_end = loop_ticks;
    initial.arrangement.song_end_behavior = SongEndBehavior::loop;
    initial.arrangement.clips.push_back(
        {1, 10, 0, loop_ticks, true, 0, 1.0F, true});
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    RecordingWorkflowCoordinator workflow(application);
    RecordingWorkflowConfiguration configuration;
    configuration.mode = RecordingWorkflowMode::musical_events;
    configuration.source = "live-input";
    configuration.destination_pattern = 10;
    configuration.destination_track = 1;
    configuration.range_start = 0;
    configuration.range_end = loop_ticks;
    configuration.loop_length_ticks = loop_ticks;
    configuration.capture_mode = "overdub";
    std::string error;
    require(workflow.configure(configuration, error), error);
    const auto undo_before = application.snapshot().undo_depth;
    require(workflow.arm(error) && workflow.start(error), error);

    std::array<float, 64> left{}, right{};
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{0}}, {"velocity", 0.65}});
    engine.process(left, right);
    run(application, "event-recording.poll.v1");
    auto state = application.snapshot();
    require(state.event_recording.active &&
                state.event_recording.published_events == 1 && engine.playing(),
            "first pass was not published while transport continued");

    // The live trigger has ended after 96 samples. Energy near the next
    // 6,000-frame cycle is therefore ordinary prepared-timeline playback.
    double second_pass_energy{};
    while (engine.callback_frame_position() < 6'200) {
        left.fill(0); right.fill(0);
        const auto before = engine.callback_frame_position();
        engine.process(left, right);
        if (before >= 5'900)
            for (const auto sample : left) second_pass_energy += std::abs(sample);
    }
    require(second_pass_energy > 1.0,
            "pass-one rhythm was not audible through scheduler playback on pass two");

    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{1}}, {"velocity", 0.8}});
    engine.process(left, right);
    run(application, "event-recording.poll.v1");
    require(application.snapshot().event_recording.published_events == 2,
            "second pass did not publish another bounded batch");

    double third_pass_energy{};
    while (engine.callback_frame_position() < 12'300) {
        left.fill(0); right.fill(0);
        const auto before = engine.callback_frame_position();
        engine.process(left, right);
        if (before >= 11'900)
            for (const auto sample : left) third_pass_energy += std::abs(sample);
    }
    require(third_pass_energy > second_pass_energy,
            "pass three did not contain both published rhythms");
    require(workflow.stop(error), error);
    engine.process(left, right);
    require(engine.playing(), "REC stop unnecessarily stopped transport");
    auto snapshot = timeline.snapshot();
    require(note_count(snapshot.timeline, 10, 1) == 2 &&
                note_count(snapshot.timeline, 10, 2) == 0,
            "continuous overdub did not preserve stable track ownership");
    require(application.snapshot().undo_depth == undo_before + 1,
            "multi-pass recording did not create one session history entry");
    run(application, "edit.undo.v1");
    engine.process(left, right);
    require(note_count(timeline.snapshot().timeline, 10, 1) == 0,
            "session-level undo did not remove the complete take");
    run(application, "edit.redo.v1");
    engine.process(left, right);
    require(note_count(timeline.snapshot().timeline, 10, 1) == 2,
            "session-level redo did not restore the complete take");

    // Replacement is explicit, range-scoped, and delayed until the first
    // meaningful captured batch. Empty replacement therefore preserves data.
    auto seed = timeline.edit();
    auto *seed_pattern = seed.find_edit_pattern(10, 1);
    require(seed_pattern, "replacement destination missing");
    seed_pattern->events.push_back(
        {seed.next_event_id(), 20, MusicalEventType::note, 45, 1.0F, 20});
    seed_pattern->events.push_back(
        {seed.next_event_id(), 100, MusicalEventType::note, 46, 1.0F, 20});
    require(timeline.commit(std::move(seed),
                            TimelineCommitPolicy::next_audio_callback, error),
            error);
    engine.process(left, right);
    configuration.range_start = 60;
    configuration.range_end = 120;
    configuration.capture_mode = "replace";
    require(workflow.configure(configuration, error) && workflow.arm(error) &&
                workflow.start(error), error);
    require(workflow.stop(error), error);
    require(std::any_of(pattern(timeline.snapshot().timeline, 10, 1).events.begin(),
                        pattern(timeline.snapshot().timeline, 10, 1).events.end(),
                        [](const auto &event) { return event.pitch == 46; }),
            "empty replacement take destroyed existing range content");

    require(workflow.arm(error) && workflow.start(error), error);
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{2}}, {"velocity", 0.7}});
    engine.process(left, right);
    run(application, "event-recording.poll.v1");
    require(workflow.stop(error), error);
    const auto replaced_snapshot = timeline.snapshot();
    const auto &replaced = pattern(replaced_snapshot.timeline, 10, 1);
    require(std::any_of(replaced.events.begin(), replaced.events.end(),
                       [](const auto &event) { return event.pitch == 45; }) &&
                std::none_of(replaced.events.begin(), replaced.events.end(),
                             [](const auto &event) { return event.pitch == 46; }) &&
                std::any_of(replaced.events.begin(), replaced.events.end(),
                            [](const auto &event) { return event.pitch == 38; }),
            "replacement did not remain confined to the captured range");

    // A published overdub can still be explicitly cancelled: the stable
    // session baseline is republished and no orphaned history entry remains.
    configuration.capture_mode = "overdub";
    require(workflow.configure(configuration, error) && workflow.arm(error) &&
                workflow.start(error), error);
    const auto before_cancel = timeline.snapshot().timeline;
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{3}}, {"velocity", 0.9}});
    engine.process(left, right);
    run(application, "event-recording.poll.v1");
    require(workflow.cancel(error), error);
    require(note_count(timeline.snapshot().timeline, 10, 1) ==
                note_count(before_cancel, 10, 1),
            "cancel did not roll back intermediate publications");

    const auto profile = reference_controller_v2_profile();
    const auto binding = [&](std::string_view control, std::string_view action,
                             bool shifted) {
        return std::any_of(profile.bindings.begin(), profile.bindings.end(),
            [&](const auto &item) {
                return item.control_id == control && item.action_id == action &&
                       item.shift == shifted;
            });
    };
    require(binding("transport.loop", "transport.loop-toggle.v1", false) &&
                binding("transport.loop", "event-recording.range-configure.v1", true) &&
                binding("transport.record", "event-recording.policy-configure.v1", true) &&
                binding("transport.record-mode", "event-recording.source-configure.v1", true),
            "reference profile lacks contextual LOOP/RANGE/OVERDUB/SOURCE bindings");
    run(application, "event-recording.range-configure.v1");
    require(application.snapshot().interaction.focus_identity == "recording.range",
            "RANGE did not enter shared recording focus");

    // Exercise the real reference profile and ControllerSession bridge rather
    // than treating the profile inventory as sufficient controller evidence.
    std::uint64_t now{1'000}, sequence{1};
    ControllerSession controller(application,
        reference_controller_v2_descriptor(), profile, "loop-overdub-v2", 128,
        [&] { return now; });
    require(controller.valid(), controller.state().error);
    require(controller.configure_gestures(reference_controller_v2_gestures(),
                                           error), error);
    const auto loop_before = timeline.snapshot().timeline.arrangement.loop_enabled;
    require(controller.input(press("transport.loop", true, sequence++), error) &&
                controller.input(press("transport.loop", false, sequence++), error),
            error);
    require(!application.dispatch_pending().empty(),
            "physical LOOP produced no semantic dispatch");
    require(timeline.snapshot().timeline.arrangement.loop_enabled != loop_before,
            "physical LOOP did not reach the authoritative transport action");
    require(controller.input(press("edit.shift", true, sequence++), error) &&
                controller.input(press("transport.loop", true, sequence++), error) &&
                controller.input(press("transport.loop", false, sequence++), error) &&
                controller.input(press("edit.shift", false, sequence++), error),
            error);
    require(!application.dispatch_pending().empty(),
            "physical SHIFT+LOOP produced no semantic dispatch");
    require(application.snapshot().interaction.focus_identity == "recording.range",
            "physical SHIFT+LOOP did not enter shared RANGE focus");

    std::cout << "loop recording overdub tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "loop recording overdub tests failed: " << error.what() << '\n';
    return 1;
}
