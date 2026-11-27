#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
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
        {std::move(action), std::move(arguments), "event-recording-test", 0});
    require(result.accepted, result.error);
    return result;
}
ControllerInput button(std::string id, bool pressed, std::uint64_t sequence) {
    return {std::move(id), pressed ? 1.0 : 0.0, 0.0, pressed, sequence, 1};
}
const MusicalPattern &pattern(const Timeline &timeline, PatternId pattern_id,
                              TrackId track_id) {
    const auto definition = std::find_if(
        timeline.arrangement.patterns.begin(),
        timeline.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    require(definition != timeline.arrangement.patterns.end(),
            "pattern is missing");
    const auto track = std::find_if(definition->tracks.begin(),
        definition->tracks.end(),
        [=](const auto &value) { return value.track == track_id; });
    require(track != definition->tracks.end(), "pattern track is missing");
    return track->pattern;
}
void arm_and_start(ApplicationService &application, PatternId pattern_id,
                   TrackId track_id) {
    run(application, "event-recording.mode-set.v1",
        {{"mode", std::string("musical-events")}});
    run(application, "event-recording.arm.v1",
        {{"pattern_id", static_cast<std::int64_t>(pattern_id)},
         {"track_id", static_cast<std::int64_t>(track_id)}});
    run(application, "event-recording.start.v1");
}
}

int main() try {
    AudioEngine engine(48'000, 64);
    engine.set_sample(Sample{48'000, 1, std::vector<float>(12'000, 0.25F)});
    Timeline initial;
    Track sample{1, "Sample"};
    sample.instrument_id = std::string(sample_instrument_id);
    Track synth{2, "Synth"};
    synth.instrument_id = std::string(subtractive_instrument_id);
    synth.instrument_instance_id = 1;
    Track midi{3, "MIDI"};
    midi.instrument_id = std::string(external_midi_instrument_id);
    midi.midi_output_port = 1;
    midi.midi_channel = 2;
    initial.tracks = {sample, synth, midi};
    MusicalPattern empty;
    empty.length = ticks_per_quarter * 4;
    initial.arrangement.patterns = {
        {10, "Recorded", empty.length, 4, 4,
         {{1, empty}, {2, empty}, {3, empty}}, {}},
        {20, "Unrelated", empty.length, 4, 4, {{1, empty}}, {}}};
    initial.arrangement.playback_mode = TimelinePlaybackMode::song;
    initial.arrangement.song_end = empty.length;
    initial.arrangement.clips.push_back(
        {1, 10, 0, empty.length, true, 0, 1.0F, true});
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    engine.play();
    std::array<float, 257> left{}, right{};
    engine.process(left, right);

    // Accepted sample-pad input is captured at the production audio dispatch
    // point, while selection-only or modal gestures never reach this path.
    arm_and_start(application, 10, 1);
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{0}}, {"velocity", 0.61}});
    engine.process(left, right);
    run(application, "event-recording.stop.v1");
    auto snapshot = timeline.snapshot();
    const auto &sample_pattern = pattern(snapshot.timeline, 10, 1);
    require(sample_pattern.events.size() == 1 &&
                sample_pattern.events[0].pitch == 36 &&
                std::abs(sample_pattern.events[0].velocity - 0.61F) < 0.001F &&
                sample_pattern.events[0].duration > 0,
            "sample performance was not recorded faithfully");
    require(pattern(snapshot.timeline, 20, 1).events.empty(),
            "recording mutated an unrelated reusable pattern");

    // Polyphonic synth ownership correlates note-off by captured instrument and
    // FIFO same-pitch ownership, not by later selection state.
    arm_and_start(application, 10, 2);
    run(application, "instrument.note-on.v1",
        {{"note", std::int64_t{60}}, {"velocity", 0.7},
         {"instance_id", std::int64_t{1}}});
    run(application, "instrument.note-on.v1",
        {{"note", std::int64_t{64}}, {"velocity", 0.8},
         {"instance_id", std::int64_t{1}}});
    engine.process(left, right);
    for (int i = 0; i < 20; ++i) engine.process(left, right);
    run(application, "instrument.note-off.v1",
        {{"note", std::int64_t{60}}, {"instance_id", std::int64_t{1}}});
    engine.process(left, right);
    for (int i = 0; i < 8; ++i) engine.process(left, right);
    run(application, "instrument.note-off.v1",
        {{"note", std::int64_t{64}}, {"instance_id", std::int64_t{1}}});
    engine.process(left, right);
    run(application, "event-recording.stop.v1");
    snapshot = timeline.snapshot();
    const auto &synth_pattern = pattern(snapshot.timeline, 10, 2);
    require(synth_pattern.events.size() == 2 &&
                synth_pattern.events[0].duration > 0 &&
                synth_pattern.events[1].duration > synth_pattern.events[0].duration,
            "polyphonic synth note durations were not correlated");

    // Actual MIDI input parsing handles zero-velocity note-on as note-off and
    // filters the captured endpoint/channel against the stable destination.
    arm_and_start(application, 10, 3);
    MidiPacket midi_on;
    midi_on.port = 1;
    midi_on.bytes = {0x92, 67, 100};
    midi_on.size = 3;
    require(application.receive_midi(midi_on),
            "MIDI note-on queue rejected input");
    engine.process(left, right);
    for (int i = 0; i < 12; ++i) engine.process(left, right);
    auto midi_off = midi_on;
    midi_off.bytes[2] = 0;
    require(application.receive_midi(midi_off),
            "MIDI note-off queue rejected input");
    engine.process(left, right);
    run(application, "event-recording.stop.v1");
    snapshot = timeline.snapshot();
    const auto &midi_pattern = pattern(snapshot.timeline, 10, 3);
    require(midi_pattern.events.size() == 1 && midi_pattern.events[0].pitch == 67 &&
                midi_pattern.events[0].duration > 0,
            "external MIDI note was not recorded through production routing");

    // Scheduler-generated repeat events use the same capture queue exactly
    // once per audible repeat, including the established half-division gate.
    auto clear = timeline.edit();
    std::string error;
    require(clear.set_pattern_track(10, 1, empty, error),
            "could not clear repeat destination");
    require(timeline.commit(std::move(clear),
                            TimelineCommitPolicy::next_audio_callback, error),
            error);
    engine.process(left, right);
    arm_and_start(application, 10, 1);
    run(application, "performance.note-repeat-begin.v1",
        {{"target", std::string("sample")}, {"note", std::int64_t{36}},
         {"velocity", 0.75}, {"owner", std::int64_t{1}},
         {"generation", std::int64_t{1}}});
    for (int i = 0; i < 80; ++i) engine.process(left, right);
    run(application, "performance.note-repeat-end.v1",
        {{"owner", std::int64_t{1}}, {"generation", std::int64_t{1}}});
    engine.process(left, right);
    run(application, "event-recording.stop.v1");
    snapshot = timeline.snapshot();
    const auto &repeated = pattern(snapshot.timeline, 10, 1);
    require(repeated.events.size() >= 3 && repeated.events[0].duration == 120 &&
                repeated.events[1].tick - repeated.events[0].tick == 240,
            "straight repeat did not become editable half-gate events: count=" +
                std::to_string(repeated.events.size()) +
                (repeated.events.empty() ? std::string{} :
                    " first=" + std::to_string(repeated.events[0].tick) +
                    "/" + std::to_string(repeated.events[0].duration)));

    // The existing count-in owns the future activation boundary; accepted live
    // input before it remains audible but is not inserted into the pattern.
    run(application, "transport.count-in-set.v1", {{"bars", std::int64_t{1}}});
    arm_and_start(application, 20, 1);
    require(application.snapshot().event_recording.phase ==
                EventRecordingPhase::count_in,
            "event recording did not enter count-in");
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{2}}, {"velocity", 0.4}});
    for (int i = 0; i < 380; ++i) engine.process(left, right);
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{3}}, {"velocity", 0.9}});
    engine.process(left, right);
    run(application, "event-recording.stop.v1");
    snapshot = timeline.snapshot();
    require(pattern(snapshot.timeline, 20, 1).events.size() == 1 &&
                pattern(snapshot.timeline, 20, 1).events[0].pitch == 39,
            "count-in captured pre-boundary input or lost active input");
    run(application, "transport.count-in-set.v1", {{"bars", std::int64_t{0}}});

    // One recording stop is one history operation; copy remains independently
    // available and ordinary project data survives undo/redo.
    const auto undo_depth = application.snapshot().undo_depth;
    require(undo_depth > 0, "recording did not enter unified history");
    run(application, "edit.undo.v1");
    engine.process(left, right);
    (void)application.snapshot();
    run(application, "edit.redo.v1");
    engine.process(left, right);
    (void)application.snapshot();
    run(application, "clipboard.copy-pattern-track.v1",
        {{"pattern_id", std::int64_t{10}}, {"track_id", std::int64_t{1}}});
    require(application.snapshot().clipboard.populated,
            "recorded events are not usable by the typed clipboard");

    // Dedicated REC MODE and REC controls enter the same authoritative
    // service; controller ownership never creates a second recording state.
    run(application, "event-recording.mode-set.v1",
        {{"mode", std::string("audio")}});
    const auto controller_profile = reference_controller_v2_profile();
    const auto mode_binding = std::find_if(controller_profile.bindings.begin(),
        controller_profile.bindings.end(), [](const auto &binding) {
            return binding.control_id == "transport.record-mode";
        });
    require(mode_binding != controller_profile.bindings.end() &&
                mode_binding->action_id == "event-recording.mode-toggle.v1",
            "reference profile lacks REC MODE binding");
    ControllerSession controller(application, reference_controller_v2_descriptor(),
                                 controller_profile, "v2-test");
    require(controller.valid(), controller.state().error);
    std::uint64_t sequence{1};
    require(controller.input(button("transport.record-mode", true, sequence++),
                             error), error);
    require(controller.input(button("transport.record-mode", false, sequence++),
                             error), error);
    (void)application.dispatch_pending();
    require(application.snapshot().event_recording.mode == "musical-events",
            "REC MODE did not select musical event recording: " +
                application.snapshot().event_recording.mode);
    require(controller.input(button("transport.record", true, sequence++), error),
            error);
    require(controller.input(button("transport.record", false, sequence++), error),
            error);
    (void)application.dispatch_pending();
    require(application.snapshot().event_recording.active,
            "REC did not arm and start captured musical event recording");
    run(application, "pad.trigger.v1",
        {{"pad", std::int64_t{1}}, {"velocity", 0.5}});
    engine.process(left, right);
    require(controller.input(button("transport.record", true, sequence++), error),
            error);
    require(controller.input(button("transport.record", false, sequence++), error),
            error);
    (void)application.dispatch_pending();
    require(application.snapshot().event_recording.phase ==
                EventRecordingPhase::completed,
            "REC did not finalize the musical event session");

    const auto persistence_path = std::filesystem::temp_directory_path() /
        ("neurogenic-event-recording-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Project saved;
    saved.name = "Live event recording acceptance";
    saved.timeline = timeline.snapshot().timeline;
    save_project(saved, persistence_path);
    const auto reopened = load_project(persistence_path);
    require(pattern(reopened.timeline, 10, 1).events.size() ==
                pattern(saved.timeline, 10, 1).events.size(),
            "save/reopen lost recorded musical events");
    Timeline playback;
    require(materialize_arrangement(reopened.timeline, playback, error), error);
    AudioEngine fresh_engine(48'000, 64);
    require(fresh_engine.queue_timeline(playback, error), error);
    fresh_engine.restart();
    std::array<float, 256> fresh_left{}, fresh_right{};
    double fresh_energy{};
    for (int block = 0; block < 180; ++block) {
        fresh_engine.process(fresh_left, fresh_right);
        for (const auto value : fresh_left) fresh_energy += std::abs(value);
    }
    require(fresh_engine.timeline_activation_count() != 0 && fresh_energy > 0.1,
            "fresh engine did not play reopened recorded synth events");
    std::error_code remove_error;
    std::filesystem::remove_all(persistence_path, remove_error);

    std::cout << "live event recording tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "live event recording tests failed: " << error.what() << '\n';
    return 1;
}
