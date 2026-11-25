#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/discovery.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/instrument/instrument.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace neurogenic;

void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}

CommandResult execute(ApplicationService &application, std::string action,
                      std::map<std::string, ApplicationValue> arguments = {}) {
    auto result = application.execute(
        {std::move(action), std::move(arguments), "independent-client", 0});
    if (!result.accepted) throw std::runtime_error(result.error);
    return result;
}

bool audible(const std::array<float, 256> &left) {
    return std::any_of(left.begin(), left.end(),
                       [](float value) { return std::fabs(value) > 1e-6F; });
}
} // namespace

int main() {
    try {
        AudioEngine engine{48'000, 256};
        Timeline initial;
        Track track;
        track.id = 41;
        track.name = "Independent synth";
        track.instrument_id = std::string(subtractive_instrument_id);
        track.instrument_instance_id = 1;
        track.pattern.length = ticks_per_quarter * 4;
        initial.tracks.push_back(track);
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, initial);
        SampleManager samples(engine);
        ApplicationService application(engine, timeline, samples);
        std::array<float, 256> left{}, right{};
        const auto acknowledge = [&] {
            left.fill(0.0F);
            right.fill(0.0F);
            engine.process(left, right);
            (void)application.snapshot_shared();
        };

        // A non-graphical client discovers and drives the ordinary public API.
        const auto discovery = discover_application(application);
        std::string error;
        require(validate_discovery(discovery, error), error);
        require(std::any_of(discovery.objects.begin(), discovery.objects.end(),
                            [](const auto &object) {
                                return object.id == "track:41";
                            }),
                "stable track missing from discovery");
        const auto first_handle = application.snapshot_shared();
        const auto second_handle = application.snapshot_shared();
        require(first_handle == second_handle,
                "unchanged revision did not reuse immutable snapshot");
        execute(application, "track.instrument-set.v1",
                {{"instrument_id", std::string(subtractive_instrument_id)},
                 {"instance_id", std::int64_t{1}}});
        acknowledge();

        execute(application, "pattern.create.v1",
                {{"id", std::int64_t{91}}, {"name", std::string("Tracker A")},
                 {"length", std::int64_t{ticks_per_quarter * 4}}});
        acknowledge();
        execute(application, "pattern.select.v1",
                {{"pattern_id", std::int64_t{91}}});
        execute(application, "step.toggle.v1",
                {{"step", std::int64_t{1}}, {"velocity", 0.75},
                 {"duration_ticks", std::int64_t{ticks_per_quarter / 2}}});
        acknowledge();
        auto authored = application.snapshot_shared();
        require(authored != first_handle && authored->selected_pattern == 91,
                "structural edit did not publish a new snapshot revision");
        const auto pattern = std::find_if(
            authored->patterns.begin(), authored->patterns.end(),
            [](const auto &candidate) { return candidate.id == 91; });
        require(pattern != authored->patterns.end(), "authored pattern missing");
        execute(application, "arrangement.clip-create.v1",
                {{"id", std::int64_t{301}}, {"pattern", std::int64_t{91}},
                 {"start", std::int64_t{0}},
                 {"duration", std::int64_t{ticks_per_quarter * 4}},
                 {"loop", false}});
        acknowledge();
        execute(application, "arrangement.range-set.v1",
                {{"start", std::int64_t{0}},
                 {"end", std::int64_t{ticks_per_quarter * 4}}});
        acknowledge();
        execute(application, "arrangement.mode-set.v1",
                {{"mode", std::string("song")}});
        acknowledge();

        ParameterBindingService bindings(application);
        BindingReference gain{BindingTargetKind::explicit_object, "master:main",
                              "mixer.master.gain", {},
                              application.snapshot_shared()->revision};
        require(!bindings.apply(gain,
                    {BindingOperation::absolute_normalized, 0.75, false, 0}),
                "descriptor-backed parameter write failed");
        execute(application, "edit.undo.v1");
        acknowledge();
        execute(application, "edit.redo.v1");
        acknowledge();
        execute(application, "pattern.launch.v1",
                {{"id", std::int64_t{91}}, {"queued", false}});
        acknowledge();

        // Deliberately unlike the reference panel: three encoders, five
        // buttons, one fader, no pads and no page controls.
        ControllerDeviceDescriptor descriptor;
        descriptor.descriptor_id = "test.compact.v1";
        descriptor.device_id = "compact-six-control";
        descriptor.display_name = "Compact independent surface";
        descriptor.transport = ControllerTransport::simulated;
        descriptor.controls = {
            {"enc.a", "A", ControlType::relative_encoder},
            {"enc.b", "B", ControlType::relative_encoder},
            {"enc.c", "C", ControlType::relative_encoder},
            {"button.play", "Play", ControlType::momentary_button,
             {FeedbackType::single_led}},
            {"button.stop", "Stop", ControlType::momentary_button,
             {FeedbackType::single_led}},
            {"button.undo", "Undo", ControlType::momentary_button},
            {"button.redo", "Redo", ControlType::momentary_button},
            {"button.select", "Select", ControlType::momentary_button},
            {"fader", "Master", ControlType::fader,
             {FeedbackType::parameter_value}}};
        ControllerProfile profile;
        profile.profile_id = "compact-profile";
        profile.device_id = descriptor.device_id;
        profile.capabilities.controller_id = descriptor.device_id;
        profile.capabilities.name = descriptor.display_name;
        profile.capabilities.controls = descriptor.controls;
        const auto parameter_binding = [](std::string control,
                                          std::string parameter,
                                          bool absolute = false) {
            ControlBinding binding;
            binding.control_id = std::move(control);
            binding.parameter_id = std::move(parameter);
            binding.absolute = absolute;
            return binding;
        };
        const auto action_binding = [](std::string control,
                                       std::string action,
                                       std::string feedback = {}) {
            ControlBinding binding;
            binding.control_id = std::move(control);
            binding.action_id = std::move(action);
            binding.feedback_id = std::move(feedback);
            return binding;
        };
        profile.bindings = {
            parameter_binding("enc.a", "transport.tempo"),
            parameter_binding("enc.b", "synth.cutoff"),
            parameter_binding("enc.c", "mixer.master.gain"),
            action_binding("button.play", "transport.play.v1", "button.play"),
            action_binding("button.stop", "transport.stop.v1", "button.stop"),
            action_binding("button.undo", "edit.undo.v1"),
            action_binding("button.redo", "edit.redo.v1"),
            action_binding("button.select", "track.select.v1"),
            parameter_binding("fader", "mixer.master.gain", true)};
        profile.bindings[7].arguments["track_id"] = std::int64_t{41};
        profile.bindings[8].feedback_id = "fader";
        require(validate_device_descriptor(descriptor, error), error);
        require(validate_controller_configuration(descriptor, profile,
                                                   application, error), error);
        ControllerSession session(application, descriptor, profile, "compact", 32);
        require(session.valid(), "compact controller session invalid");
        require(session.input({"enc.a", 2.0, 0.0, true, 1}, error), error);
        require(session.input({"fader", 0.5, 0.0, true, 2}, error), error);
        require(session.input({"button.play", 1.0, 0.0, true, 3}, error), error);
        const auto results = application.dispatch_pending();
        require(std::all_of(results.begin(), results.end(),
                            [](const auto &result) { return result.accepted; }),
                "compact controller dispatch rejected");
        require(application.snapshot_shared()->playing,
                "compact controller did not start transport");
        session.observe(*application.snapshot_shared(), true);
        require(session.has_pending_feedback(), "compact feedback missing");
        session.reconnect();
        require(session.state().resync_required,
                "reconnect did not request authoritative resynchronization");

        left.fill(0.0F);
        right.fill(0.0F);
        for (int i = 0; i < 32 && !audible(left); ++i) engine.process(left, right);
        require(audible(left), "arranged synth pattern produced no audio; voices=" +
                                   std::to_string(engine.synth_state().active_voices));

        const auto directory = std::filesystem::temp_directory_path() /
                               "neurogenic-independent-client";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        execute(application, "project.save-as.v1",
                {{"path", directory.string()},
                 {"name", std::string("Independent client")}});

        AudioEngine restored_engine{48'000, 256};
        TimelineService restored_timeline(
            [&](const Timeline &value, std::string &message) {
                return restored_engine.queue_timeline(value, message);
            });
        SampleManager restored_samples(restored_engine);
        ApplicationService restored(restored_engine, restored_timeline,
                                    restored_samples);
        execute(restored, "project.open.v1",
                {{"path", directory.string()}, {"discard", true}});
        const auto reopened = restored.snapshot_shared();
        require(reopened->patterns.size() == 1 &&
                    reopened->patterns.front().id == 91 &&
                    reopened->clips.size() == 1,
                "alternative workflow did not survive project round trip");
        std::filesystem::remove_all(directory, ignored);
        restored_samples.shutdown();
        samples.shutdown();
        std::cout << "neurogenic.client-independence-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "client independence test failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
