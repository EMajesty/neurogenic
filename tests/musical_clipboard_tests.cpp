#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
CommandResult execute(ApplicationService &application, std::string action,
                      std::map<std::string, ApplicationValue> arguments = {}) {
    auto result = application.execute({std::move(action), std::move(arguments)});
    require(result.accepted, result.error);
    return result;
}
const MusicalPattern &pattern(const Timeline &timeline, PatternId id,
                              TrackId track) {
    const auto definition = std::find_if(
        timeline.arrangement.patterns.begin(),
        timeline.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == id; });
    require(definition != timeline.arrangement.patterns.end(),
            "pattern is missing");
    const auto item = std::find_if(definition->tracks.begin(),
                                   definition->tracks.end(),
                                   [=](const auto &value) {
                                       return value.track == track;
                                   });
    require(item != definition->tracks.end(), "pattern track is missing");
    return item->pattern;
}
}

int main() try {
    AudioEngine engine(48'000, 64);
    Timeline initial;
    Track synth;
    synth.id = 1;
    synth.name = "Synth";
    synth.instrument_id = std::string(subtractive_instrument_id);
    synth.pattern.length = ticks_per_quarter * 4;
    Track sample;
    sample.id = 2;
    sample.name = "Sample";
    sample.instrument_id = std::string(sample_instrument_id);
    sample.pattern.length = synth.pattern.length;
    initial.tracks = {synth, sample};

    MusicalEvent note;
    note.id = 10;
    note.tick = 240;
    note.type = MusicalEventType::note;
    note.pitch = 67;
    note.velocity = 0.63F;
    note.duration = 180;
    note.trigger_probability = 0.75F;
    note.trigger_condition = TriggerCondition::every_nth_loop;
    note.condition_n = 2;
    note.condition_m = 3;
    note.ratchet_count = 3;
    MusicalEvent lock;
    lock.id = 11;
    lock.tick = note.tick;
    lock.type = MusicalEventType::parameter;
    lock.parameter_destination = "synth.cutoff";
    lock.value = 0.42F;
    MusicalEvent later = note;
    later.id = 12;
    later.tick = 1200;
    later.pitch = 72;
    MusicalPattern source;
    source.length = synth.pattern.length;
    source.events = {note, lock, later};
    MusicalPattern empty;
    empty.length = synth.pattern.length;
    initial.arrangement.patterns = {
        {10, "Source", source.length, 4, 4, {{1, source}}, {}},
        {20, "Destination", empty.length, 4, 4, {{1, empty}, {2, empty}}, {}}};
    initial.arrangement.playback_mode = TimelinePlaybackMode::song;
    initial.arrangement.song_end = synth.pattern.length;
    initial.arrangement.clips.push_back(
        {1, 20, 0, synth.pattern.length, true, 0, 1.0F, true});

    TimelineService timeline([&](const Timeline &, std::string &error) {
        error.clear();
        return true;
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);

    const auto initial_history = application.snapshot().undo_depth;
    execute(application, "clipboard.copy-selection.v1",
            {{"pattern_id", std::int64_t{10}}, {"track_id", std::int64_t{1}},
             {"step", std::int64_t{1}}});
    auto state = application.snapshot();
    require(state.clipboard.populated && state.clipboard.event_count == 2 &&
                state.clipboard.extent == 240,
            "single-step clipboard summary is incorrect");
    require(state.undo_depth == initial_history,
            "copy created a musical history entry");

    execute(application, "clipboard.paste.v1",
            {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{1}},
             {"anchor_tick", std::int64_t{480}},
             {"policy", std::string("merge")}});
    auto pasted = pattern(timeline.snapshot().timeline, 20, 1).events;
    require(pasted.size() == 2 && pasted[0].id != 10 && pasted[1].id != 11,
            "paste did not create fresh event identities");
    const auto pasted_note = std::find_if(pasted.begin(), pasted.end(),
        [](const auto &event) { return event.type == MusicalEventType::note; });
    const auto pasted_lock = std::find_if(pasted.begin(), pasted.end(),
        [](const auto &event) { return event.type == MusicalEventType::parameter; });
    require(pasted_note != pasted.end() && pasted_note->tick == 480 &&
                pasted_note->pitch == 67 && pasted_note->velocity == 0.63F &&
                pasted_note->duration == 180 &&
                pasted_note->trigger_probability == 0.75F &&
                pasted_note->trigger_condition == TriggerCondition::every_nth_loop &&
                pasted_note->condition_n == 2 && pasted_note->condition_m == 3 &&
                pasted_note->ratchet_count == 3,
            "paste lost a supported note property");
    require(pasted_lock != pasted.end() && pasted_lock->tick == 480 &&
                pasted_lock->parameter_destination == "synth.cutoff" &&
                pasted_lock->value == 0.42F,
            "paste lost an event-owned parameter lock");
    require(pattern(timeline.snapshot().timeline, 10, 1).events.size() == 3,
            "paste mutated source content");

    const auto history_before_conflict = application.snapshot().undo_depth;
    auto conflict = application.execute(
        {"clipboard.paste.v1",
         {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{1}},
          {"anchor_tick", std::int64_t{480}},
          {"policy", std::string("merge")}}});
    require(!conflict.accepted &&
                application.snapshot().undo_depth == history_before_conflict &&
                pattern(timeline.snapshot().timeline, 20, 1).events.size() == 2,
            "conflicting merge was not rejected atomically");
    auto incompatible = application.execute(
        {"clipboard.paste.v1",
         {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{2}},
          {"anchor_tick", std::int64_t{480}}}});
    require(!incompatible.accepted &&
                incompatible.error.find("instrument") != std::string::npos,
            "incompatible cross-track paste was not diagnosed");
    auto overflow = application.execute(
        {"clipboard.paste.v1",
         {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{1}},
          {"anchor_tick", std::int64_t{3800}}}});
    require(!overflow.accepted &&
                pattern(timeline.snapshot().timeline, 20, 1).events.size() == 2,
            "out-of-bounds paste partially mutated the timeline");

    execute(application, "edit.undo.v1");
    require(pattern(timeline.snapshot().timeline, 20, 1).events.empty(),
            "unified Undo did not remove one paste transaction");
    execute(application, "edit.redo.v1");
    require(pattern(timeline.snapshot().timeline, 20, 1).events.size() == 2,
            "unified Redo did not restore pasted content");

    execute(application, "step.selection-toggle.v1",
            {{"pattern_id", std::int64_t{10}}, {"track_id", std::int64_t{1}},
             {"step", std::int64_t{1}}});
    execute(application, "step.selection-toggle.v1",
            {{"pattern_id", std::int64_t{10}}, {"track_id", std::int64_t{1}},
             {"step", std::int64_t{5}}});
    execute(application, "clipboard.copy-selection.v1",
            {{"pattern_id", std::int64_t{10}}, {"track_id", std::int64_t{1}}});
    require(application.snapshot().clipboard.event_count == 3 &&
                application.snapshot().clipboard.extent == 1200,
            "sparse multi-step extent was not preserved");
    execute(application, "clipboard.paste.v1",
            {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{1}},
             {"anchor_tick", std::int64_t{1920}}});
    const auto sparse = pattern(timeline.snapshot().timeline, 20, 1).events;
    require(std::any_of(sparse.begin(), sparse.end(), [](const auto &event) {
                return event.type == MusicalEventType::note && event.tick == 1920;
            }) &&
            std::any_of(sparse.begin(), sparse.end(), [](const auto &event) {
                return event.type == MusicalEventType::note && event.tick == 2880;
            }),
            "sparse paste did not preserve relative gaps across pages");

    ControllerSession controller(application,
        reference_controller_v2_descriptor(), reference_controller_v2_profile(),
        "clipboard-controller");
    std::string controller_error;
    require(controller.configure_gestures(reference_controller_v2_gestures(),
                                           controller_error),
            controller_error);
    controller.set_context("sequencer:v2");
    execute(application, "pattern.select.v1", {{"pattern_id", std::int64_t{10}}});
    execute(application, "track.select.v1", {{"track_id", std::int64_t{1}}});
    execute(application, "step.page-select.v1",
            {{"page", std::int64_t{0}}, {"pattern_id", std::int64_t{10}},
             {"track_id", std::int64_t{1}}});
    std::uint64_t sequence{1};
    const auto button = [&](std::string id, bool pressed) {
        return ControllerInput{std::move(id), pressed ? 1.0 : 0.0, 0.0,
                               pressed, sequence++, 1};
    };
    require(controller.input(button("edit.copy", true), controller_error),
            controller_error);
    require(controller.input(button("pad.2", true), controller_error),
            controller_error);
    (void)application.dispatch_pending();
    require(controller.input(button("pad.2", false), controller_error),
            controller_error);
    require(controller.input(button("edit.copy", false), controller_error),
            controller_error);
    require(application.snapshot().clipboard.event_count == 2,
            "COPY+pad did not capture the stable step without triggering it");
    execute(application, "pattern.select.v1", {{"pattern_id", std::int64_t{20}}});
    execute(application, "step.select.v1", {{"step", std::int64_t{10}}});
    require(controller.input(button("edit.shift", true), controller_error),
            controller_error);
    require(controller.input(button("edit.copy", true), controller_error),
            controller_error);
    (void)application.dispatch_pending();
    require(controller.input(button("edit.copy", false), controller_error),
            controller_error);
    require(controller.input(button("edit.shift", false), controller_error),
            controller_error);
    const auto controller_paste = timeline.snapshot();
    const auto &controller_events = pattern(controller_paste.timeline, 20, 1).events;
    require(std::any_of(controller_events.begin(), controller_events.end(),
                        [](const auto &event) {
                            return event.type == MusicalEventType::note &&
                                   event.tick == 2400;
                        }),
            "SHIFT+COPY did not paste through ControllerSession");

    // The captured value remains valid after its source events are deleted.
    auto remove_source = timeline.edit();
    require(remove_source.set_pattern_track(10, 1, empty, conflict.error),
            conflict.error);
    require(timeline.commit(std::move(remove_source),
                            TimelineCommitPolicy::next_audio_callback,
                            conflict.error), conflict.error);
    execute(application, "clipboard.paste.v1",
            {{"pattern_id", std::int64_t{20}}, {"track_id", std::int64_t{1}},
             {"anchor_tick", std::int64_t{480}},
             {"policy", std::string("replace")}, {"confirmed", true}});
    require(pattern(timeline.snapshot().timeline, 20, 1).events.size() >= 5,
            "value-owned clipboard did not survive source deletion");

    // Complete reusable-pattern paste creates an independently editable copy
    // and does not redirect references to the original pattern.
    execute(application, "clipboard.copy-pattern.v1",
            {{"pattern_id", std::int64_t{20}}});
    execute(application, "clipboard.paste-pattern-new.v1",
            {{"pattern_id", std::int64_t{30}},
             {"name", std::string("Independent")}});
    const auto copied_count = pattern(timeline.snapshot().timeline, 30, 1).events.size();
    require(copied_count == pattern(timeline.snapshot().timeline, 20, 1).events.size(),
            "complete pattern paste lost content");
    auto independent = timeline.edit();
    require(independent.remove_event(30, 1,
                pattern(independent.timeline(), 30, 1).events.front().id,
                conflict.error), conflict.error);
    require(timeline.commit(std::move(independent),
                            TimelineCommitPolicy::next_audio_callback,
                            conflict.error), conflict.error);
    require(pattern(timeline.snapshot().timeline, 20, 1).events.size() !=
                pattern(timeline.snapshot().timeline, 30, 1).events.size(),
            "pasted pattern was not independently editable");

    const auto persistence_path = std::filesystem::temp_directory_path() /
        ("neurogenic-clipboard-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Project saved;
    saved.name = "Clipboard acceptance";
    saved.timeline = timeline.snapshot().timeline;
    save_project(saved, persistence_path);
    const auto reopened = load_project(persistence_path);
    require(pattern(reopened.timeline, 20, 1).events.size() ==
                pattern(saved.timeline, 20, 1).events.size(),
            "save/reopen lost pasted musical content");
    Timeline playback;
    std::string playback_error;
    require(materialize_arrangement(reopened.timeline, playback, playback_error),
            playback_error);
    AudioEngine fresh_engine(48'000, 64);
    require(fresh_engine.queue_timeline(playback, playback_error), playback_error);
    fresh_engine.restart();
    std::array<float, 256> left{}, right{};
    for (int block = 0; block < 80; ++block)
        fresh_engine.process(left, right);
    require(fresh_engine.timeline_activation_count() != 0 &&
                fresh_engine.applied_timeline_parameters() != 0,
            "fresh engine did not schedule reopened pasted notes and locks");
    std::error_code remove_error;
    std::filesystem::remove_all(persistence_path, remove_error);

    execute(application, "clipboard.clear.v1");
    require(!application.snapshot().clipboard.populated,
            "clipboard clear did not clear transient state");
    std::cout << "typed musical clipboard tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
