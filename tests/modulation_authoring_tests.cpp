#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/application/controller_session.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace neurogenic;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void execute(ApplicationService &application, std::string id,
             std::map<std::string, ApplicationValue> arguments = {}) {
    const auto result = application.execute(
        {std::move(id), std::move(arguments), "modulation-authoring-test", 0});
    if (!result.accepted) throw std::runtime_error(result.error);
}
void process(AudioEngine &engine, std::size_t frames = 128) {
    std::array<float, 128> left{}, right{};
    while (frames) {
        const auto count = std::min(frames, left.size());
        engine.process(std::span(left).first(count), std::span(right).first(count));
        frames -= count;
    }
}
} // namespace

int main() {
    using namespace neurogenic;
    try {
        AudioEngine engine;
        SampleManager samples(engine);
        Timeline initial;
        initial.tracks.push_back({1, "Modulated Synth"});
        initial.tracks[0].instrument_id = std::string(subtractive_instrument_id);
        initial.tracks[0].instrument_instance_id = 1;
        initial.tracks[0].pattern.length = ticks_per_quarter * 4;
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, initial);
        ApplicationService application(engine, timeline, samples);

        execute(application, "modulator.create.v1",
                {{"type", std::int64_t{1}}, {"owner", std::int64_t{3}},
                 {"owner_id", std::int64_t{0}}, {"name", std::string("Motion")}});
        process(engine);
        auto state = application.snapshot();
        check(state.modulation.modulator_count == 1, "semantic source creation");
        const auto source = state.modulation.modulators[0].id;
        execute(application, "modulator.parameter-set.v1",
                {{"id", std::int64_t{source}}, {"parameter", std::string("rate")},
                 {"value", 3.0}});
        process(engine);
        execute(application, "modulator.route-create.v1",
                {{"source_id", std::int64_t{source}},
                 {"destination", std::string("synth.cutoff")},
                 {"depth", 0.25}});
        process(engine);
        execute(application, "modulator.route-create.v1",
                {{"source_id", std::int64_t{source}},
                 {"destination", std::string("mixer.master.gain")},
                 {"depth", -0.2}});
        process(engine);
        state = application.snapshot();
        check(state.modulation.route_count == 2 && state.modulation_graph.valid,
              "descriptor destinations publish a valid graph");
        const auto context = resolve_controller_context(
            state, "modulation", state.modifiers);
        check(context[8].action_id == "modulation.route-depth.v1" &&
                  context[8].arguments.contains("route_id") &&
                  context[8].arguments.contains("depth") &&
                  context[10].arguments.contains("enabled"),
              "physical modulation context preserves typed route arguments");
        const auto first_runtime = engine.modulation_snapshot();
        process(engine, 512);
        const auto second_runtime = engine.modulation_snapshot();
        check(first_runtime.modulator_values[0] != second_runtime.modulator_values[0],
              "authored LFO produces engine-observable motion");

        const auto first_route = state.modulation.routes[0].id;
        execute(application, "modulation.route-depth.v1",
                {{"route_id", std::int64_t{first_route}}, {"depth", 0.75}});
        process(engine);
        check(std::abs(application.snapshot().modulation.routes[0].depth - .75F) <
                  1e-6F,
              "route depth authoring round trip");
        const auto rejected = application.execute(
            {"modulation.connection-create.v1",
             {{"source_id", std::int64_t{source}},
              {"destination_id", std::int64_t{source}},
              {"parameter", std::int64_t{1}}, {"depth", 1.0}},
             "modulation-authoring-test", 0});
        check(!rejected.accepted && application.snapshot().modulation.route_count == 2,
              "cycle rejection preserves active graph");

        execute(application, "macro.create.v1",
                {{"name", std::string("Expression")},
                 {"owner", std::string("project")}, {"value", 0.25}});
        process(engine);
        const auto macro = application.snapshot().performance_macros.macros[0].id;
        const auto invalid_assignment = application.execute(
            {"macro.assignment-set.v1",
             {{"macro_id", static_cast<std::int64_t>(macro)},
              {"destination", std::string("synth.gain")},
              {"minimum", -10.0}, {"maximum", 20.0}},
             "modulation-authoring-test", 0});
        check(!invalid_assignment.accepted &&
                  application.snapshot().performance_macros.macros[0]
                      .assignments.empty(),
              "descriptor range rejection preserves macro state");
        execute(application, "macro.assignment-set.v1",
                {{"macro_id", static_cast<std::int64_t>(macro)},
                 {"destination", std::string("synth.gain")},
                 {"minimum", 0.1}, {"maximum", 0.9},
                 {"curve", std::string("linear")},
                 {"polarity", std::string("unipolar")},
                 {"steps", std::int64_t{2}}});
        process(engine);
        const auto assignment = application.snapshot()
                                    .performance_macros.macros[0]
                                    .assignments[0]
                                    .id;
        execute(application, "macro.assignment-update.v1",
                {{"macro_id", static_cast<std::int64_t>(macro)},
                 {"assignment_id", static_cast<std::int64_t>(assignment)},
                 {"destination", std::string("synth.gain")},
                 {"minimum", 0.9}, {"maximum", 0.1},
                 {"curve", std::string("linear")},
                 {"polarity", std::string("unipolar")},
                 {"steps", std::int64_t{2}}});
        process(engine);
        check(application.snapshot().performance_macros.macros[0]
                      .assignments[0]
                      .minimum == .9F,
              "macro assignment update preserves stable identity");
        execute(application, "macro.assignment-set.v1",
                {{"macro_id", static_cast<std::int64_t>(macro)},
                 {"destination", std::string("mixer.master.gain")},
                 {"minimum", 0.25}, {"maximum", 1.0},
                 {"curve", std::string("linear")},
                 {"polarity", std::string("unipolar")},
                 {"steps", std::int64_t{2}}});
        process(engine);
        const auto low = engine.modulation_snapshot();
        execute(application, "macro.value-set.v1",
                {{"id", static_cast<std::int64_t>(macro)}, {"value", 0.75}});
        process(engine);
        const auto high = engine.modulation_snapshot();
        check(high.effective_values[2] < low.effective_values[2] &&
                  high.effective_values[3] > low.effective_values[3],
              "one macro applies independent destination direction and range");

        execute(application, "macro.scene-capture.v1",
                {{"name", std::string("Raised")}, {"duration", 0.0},
                 {"tempo_sync", false}, {"curve", std::string("linear")}});
        const auto scene = application.snapshot().performance_macros.scenes[0].id;
        execute(application, "macro.value-set.v1",
                {{"id", static_cast<std::int64_t>(macro)}, {"value", 0.1}});
        execute(application, "macro.scene-recall.v1",
                {{"id", static_cast<std::int64_t>(scene)}});
        process(engine);
        check(std::abs(application.snapshot().performance_macros.macros[0].value -
                       .75F) < 1e-6F,
              "scene recall restores captured macro value");

        execute(application, "modulation.source-configure.v1",
                {{"source", std::int64_t{0}}, {"waveform", std::int64_t{2}},
                 {"rate", 9.0}, {"tempo_sync", false}, {"beats", 1.0},
                 {"scope", std::int64_t{0}}});
        execute(application, "modulation.source-reset.v1",
                {{"source", std::int64_t{0}}});
        check(std::abs(application.snapshot().modulation.sources[0].rate_hz - 1.0F) <
                  1e-6F,
              "source reset restores defaults");

        const auto project_path = std::filesystem::temp_directory_path() /
                                  "neurogenic-modulation-authoring.ngp";
        std::filesystem::remove_all(project_path);
        execute(application, "project.save-as.v1",
                {{"path", project_path.string()},
                 {"name", std::string("Modulation Round Trip")}});
        AudioEngine restored_engine;
        SampleManager restored_samples(restored_engine);
        TimelineService restored_timeline(
            [&](const Timeline &value, std::string &error) {
                return restored_engine.queue_timeline(value, error);
            });
        ApplicationService restored(restored_engine, restored_timeline,
                                    restored_samples);
        execute(restored, "project.open.v1",
                {{"path", project_path.string()}, {"discard", false}});
        const auto reopened = restored.snapshot();
        check(reopened.modulation.modulator_count == 2 &&
                  reopened.modulation.route_count == 4 &&
                  reopened.performance_macros.macros.size() == 1 &&
                  reopened.performance_macros.macros[0].assignments.size() == 2 &&
                  reopened.performance_macros.scenes.size() == 1,
              "project restores graph, routes, macro assignments and scenes");
        process(restored_engine);
        check(restored_engine.modulation_snapshot().modulator_count >= 2,
              "restored macro and LFO become audio-active");
        std::filesystem::remove_all(project_path);
        std::cout << "neurogenic.modulation-authoring-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
