#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/io.hpp"
#include <cmath>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
AudioEngine *active_engine{};
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
void execute(ApplicationService &application, std::string action,
             std::map<std::string, ApplicationValue> arguments = {}) {
    const auto result = application.execute({std::move(action),
                                             std::move(arguments)});
    require(result.accepted, result.error);
    if (active_engine) {
        std::array<float, 128> left{}, right{};
        active_engine->process(left, right);
        (void)application.snapshot();
    }
}
double parameter(const ApplicationState &state, std::string_view id) {
    const auto found = std::find_if(state.parameters.begin(), state.parameters.end(),
                                    [&](const auto &value) { return value.id == id; });
    require(found != state.parameters.end(), "parameter was not published");
    if (const auto value = std::get_if<double>(&found->current_value)) return *value;
    if (const auto value = std::get_if<std::int64_t>(&found->current_value)) return *value;
    throw std::runtime_error("parameter was not numeric");
}
void settle(ApplicationService &application, AudioEngine &engine) {
    const auto results = application.dispatch_pending();
    for (const auto &result : results) require(result.accepted, result.error);
    std::array<float, 128> left{}, right{};
    engine.process(left, right);
    (void)application.snapshot();
}
ControllerInput button(std::string id, bool pressed, std::uint64_t sequence) {
    return {std::move(id), pressed ? 1.0 : 0.0, 0, pressed, sequence, 1};
}
ControllerInput turn(std::string id, double delta, std::uint64_t sequence) {
    return {std::move(id), delta, 0, true, sequence, 1};
}
Timeline fixture() {
    Timeline timeline;
    Track track;
    track.id = 1;
    track.name = "Synth";
    track.instrument_id = std::string(subtractive_instrument_id);
    timeline.tracks.push_back(track);
    PatternDefinition pattern;
    pattern.id = 11;
    pattern.name = "Performance pattern";
    pattern.tracks.push_back({1, {pattern.length, {{100, 0,
        MusicalEventType::note, 60, 0.8F, ticks_per_quarter / 4}}}});
    timeline.arrangement.patterns.push_back(pattern);
    LaunchSlot slot;
    slot.id = 501;
    slot.track = 1;
    slot.source_type = LaunchSourceType::pattern;
    slot.source_id = 11;
    slot.quantization = LaunchQuantization::immediate;
    timeline.arrangement.launch_slots.push_back(slot);
    timeline.arrangement.performance_mode = PerformancePlaybackMode::session;
    timeline.arrangement.playback_mode = TimelinePlaybackMode::song;
    timeline.arrangement.song_end = pattern.length;
    return timeline;
}
}

int main() try {
    AudioEngine engine(48'000, 128);
    active_engine = &engine;
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, fixture());
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);

    execute(application, "performance.bank-create.v1",
            {{"bank", std::int64_t{100}}, {"name", std::string("Live")}});
    execute(application, "performance.bank-create.v1",
            {{"bank", std::int64_t{200}}, {"name", std::string("Breakdown")}});
    execute(application, "macro.create.v1",
            {{"name", std::string("Energy")},
             {"owner", std::string("project")}, {"value", 0.25}});
    const auto macro_id = application.snapshot().performance_macros.macros.front().id;
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1001}},
             {"position", std::int64_t{0}}, {"secondary", false},
             {"parameter_id", std::string("synth.cutoff")},
             {"label", std::string("Tone")}});
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1002}},
             {"position", std::int64_t{0}}, {"secondary", true},
             {"parameter_id", std::string("synth.resonance")},
             {"label", std::string("Reso")}});
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1003}},
             {"position", std::int64_t{1}}, {"secondary", false},
             {"parameter_id", std::string("macro.") + std::to_string(macro_id) +
                                  ".value"},
             {"label", std::string("Energy")}});
    execute(application, "performance.action-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1101}},
             {"position", std::int64_t{0}}, {"secondary", false},
             {"action_id", std::string("performance.fill-set.v1")},
             {"behavior", std::string("momentary")},
             {"label", std::string("Fill")}});
    execute(application, "performance.action-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1102}},
             {"position", std::int64_t{0}}, {"secondary", true},
             {"action_id", std::string("transport.play.v1")},
             {"behavior", std::string("one-shot")},
             {"label", std::string("Play")}});
    execute(application, "performance.bank-pad-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1201}},
             {"position", std::int64_t{0}}, {"slot", std::int64_t{501}},
             {"label", std::string("Pattern A")}});
    execute(application, "performance.bank-select.v1",
            {{"bank", std::int64_t{100}}});

    auto descriptor = reference_controller_v2_descriptor();
    ControllerSession session(application, descriptor,
                              reference_controller_v2_profile(), "v2");
    std::string error;
    require(session.configure_gestures(reference_controller_v2_gestures(), error),
            error);
    session.set_context("performance:v2");
    session.observe(application.snapshot(), true);
    std::uint64_t sequence{1};
    const auto cutoff_before = parameter(application.snapshot(), "synth.cutoff");
    require(session.input(turn("parameter.1", 1, sequence++), error), error);
    settle(application, engine);
    const auto cutoff_after = parameter(application.snapshot(), "synth.cutoff");
    require(cutoff_after > cutoff_before, "primary Performance encoder did not adjust cutoff");
    const auto macro_before = application.snapshot().performance_macros.macros.front().value;
    require(session.input(turn("parameter.2", 2, sequence++), error), error);
    settle(application, engine);
    require(application.snapshot().performance_macros.macros.front().value > macro_before,
            "Performance macro encoder did not use the macro service");
    const auto resonance_before = parameter(application.snapshot(), "synth.resonance");
    require(session.input(button("edit.shift", true, sequence++), error), error);
    require(session.input(turn("parameter.1", 1, sequence++), error), error);
    settle(application, engine);
    require(parameter(application.snapshot(), "synth.resonance") > resonance_before,
            "secondary Performance encoder did not adjust resonance");
    require(parameter(application.snapshot(), "synth.cutoff") == cutoff_after,
            "secondary Performance encoder redirected to primary target");

    require(session.input(button("edit.shift", false, sequence++), error), error);
    require(session.input(button("function.1", true, sequence++), error), error);
    settle(application, engine);
    require(application.snapshot().fill_active, "momentary Performance action did not press");
    require(session.input(button("function.1", false, sequence++), error), error);
    settle(application, engine);
    require(!application.snapshot().fill_active, "momentary Performance action did not release");
    require(session.input(button("function.1", true, sequence++), error), error);
    settle(application, engine);
    execute(application, "performance.bank-select.v1",
            {{"bank", std::int64_t{200}}});
    session.observe(application.snapshot());
    settle(application, engine);
    require(!application.snapshot().fill_active,
            "bank change did not cancel the original momentary target");
    execute(application, "performance.bank-select.v1",
            {{"bank", std::int64_t{100}}});
    session.observe(application.snapshot());
    require(session.input(button("pad.1", true, sequence++), error), error);
    require(session.input(button("pad.1", false, sequence++), error), error);
    settle(application, engine);
    require(!application.snapshot().performance_launching.pending.empty(),
            "Performance pad did not use the banked launch target");
    const auto pending_before_select =
        application.snapshot().performance_launching.pending.size();
    require(session.input(button("edit.shift", true, sequence++), error), error);
    require(session.input(button("pad.1", true, sequence++), error), error);
    require(session.input(button("pad.1", false, sequence++), error), error);
    settle(application, engine);
    const auto selected = application.snapshot();
    require(selected.performance_banks.selected_assignment == 1201,
            "SHIFT+pad did not select the stable assignment");
    require(selected.performance_launching.pending.size() == pending_before_select,
            "SHIFT+pad triggered musical material");
    require(session.input(button("edit.shift", false, sequence++), error), error);

    execute(application, "performance.slot-clear.v1",
            {{"slot", std::int64_t{501}}});
    const auto unavailable = application.snapshot();
    require(unavailable.performance_banks.banks.front().pads.front().launch_slot == 501,
            "target deletion silently removed the user-authored assignment");
    execute(application, "performance.slot-assign.v1",
            {{"slot", std::int64_t{501}}, {"track", std::int64_t{1}},
             {"source_type", std::string("pattern")},
             {"source", std::int64_t{11}}, {"behavior", std::string("loop")}});

    const auto depth = application.snapshot().undo_depth;
    execute(application, "performance.bank-rename.v1",
            {{"bank", std::int64_t{100}}, {"name", std::string("Live Set")}});
    require(application.snapshot().undo_depth == depth + 1,
            "bank authoring did not enter unified history");
    execute(application, "edit.undo.v1");
    require(application.snapshot().performance_banks.banks.front().name == "Live",
            "bank rename Undo failed");
    execute(application, "edit.redo.v1");
    require(application.snapshot().performance_banks.banks.front().name == "Live Set",
            "bank rename Redo failed");
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{100}}, {"assignment", std::int64_t{1001}},
             {"position", std::int64_t{0}}, {"secondary", false},
             {"parameter_id", std::string("synth.gain")},
             {"label", std::string("Level")}});
    auto assignment_parameter = [&] {
        const auto state = application.snapshot();
        const auto &parameters = state.performance_banks.banks.front().parameters;
        const auto found = std::find_if(parameters.begin(), parameters.end(),
                                        [](const auto &value) {
                                            return value.id == 1001;
                                        });
        require(found != parameters.end(), "stable assignment disappeared");
        return found->parameter_id;
    };
    require(assignment_parameter() == "synth.gain",
            "assignment replacement did not retain stable assignment identity");
    execute(application, "edit.undo.v1");
    require(assignment_parameter() == "synth.cutoff",
            "assignment replacement Undo failed");
    execute(application, "edit.redo.v1");

    const auto temporary = std::filesystem::temp_directory_path() /
        "neurogenic-performance-banks-test";
    std::filesystem::remove_all(temporary);
    Project project;
    project.name = "Performance banks";
    project.timeline = timeline.snapshot().timeline;
    save_project(project, temporary);
    const auto loaded = load_project(temporary);
    require(loaded.timeline.arrangement.performance_banks.size() == 2,
            "Performance banks did not survive project persistence");
    const auto &restored = loaded.timeline.arrangement.performance_banks.front();
    require(restored.name == "Live Set" && restored.parameters.size() == 3 &&
                restored.actions.size() == 2 && restored.pads.size() == 1,
            "Performance assignment layers did not survive persistence");
    auto legacy = fixture();
    legacy.arrangement.launch_slots.front().logical_position = 3;
    legacy.arrangement.performance_banks.clear();
    const auto legacy_path = temporary / "legacy.ngt";
    save_timeline(legacy, legacy_path);
    std::ifstream legacy_input(legacy_path);
    std::string legacy_text((std::istreambuf_iterator<char>(legacy_input)), {});
    legacy_input.close();
    constexpr std::string_view legacy_header{"NEUROGENIC_TIMELINE "};
    const auto header = legacy_text.find("NEUROGENIC_TIMELINE 12");
    require(header == 0, "unexpected timeline persistence header");
    legacy_text.replace(legacy_header.size(), 2, "11");
    std::ofstream legacy_output(legacy_path, std::ios::trunc);
    legacy_output << legacy_text;
    legacy_output.close();
    const auto migrated = load_timeline(legacy_path);
    require(migrated.arrangement.performance_banks.size() == 1 &&
                migrated.arrangement.performance_banks.front().pads.size() == 1 &&
                migrated.arrangement.performance_banks.front().pads.front().position == 3,
            "legacy logical position did not migrate deterministically");
    std::filesystem::remove_all(temporary);

    // A deliberately non-reference controller addresses logical contextual
    // roles without pads, page buttons, or V2 geometry.
    ControllerDeviceDescriptor alternative;
    alternative.descriptor_id = "test.performance-mini";
    alternative.device_id = "test.performance-mini";
    alternative.display_name = "Performance Mini";
    alternative.controls = {{"knob.a", "A", ControlType::relative_encoder,
                             {FeedbackType::parameter_value}}};
    ControllerProfile alternative_profile;
    alternative_profile.profile_id = "test.performance-mini";
    alternative_profile.device_id = alternative.device_id;
    alternative_profile.capabilities = {alternative.device_id,
                                        alternative.display_name,
                                        alternative.controls, false};
    alternative_profile.bindings.push_back(
        {"knob.a", {}, {}, {}, {}, false, false, false,
         "context-encoder", "performance", 0});
    ControllerSession mini(application, alternative, alternative_profile, "mini");
    require(mini.valid(), mini.state().error);
    mini.set_context("performance");
    const auto restored_gain = parameter(application.snapshot(), "synth.gain");
    require(mini.input(turn("knob.a", 1, 1), error), error);
    settle(application, engine);
    require(parameter(application.snapshot(), "synth.gain") > restored_gain,
            "non-reference controller could not operate Performance assignment");

    std::cout << "neurogenic.performance-banks-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
