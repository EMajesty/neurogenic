#include "neurogenic/application/assignment_picker.hpp"
#include "neurogenic/application/controller_coordinator.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <filesystem>
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
    const auto result = application.execute({std::move(action), std::move(arguments)});
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
    require(found != state.parameters.end(), "parameter is absent");
    if (const auto *value = std::get_if<double>(&found->current_value)) return *value;
    throw std::runtime_error("parameter is not real");
}
}

int main() try {
    AudioEngine engine(48'000, 128);
    active_engine = &engine;
    Timeline initial;
    Track track;
    track.id = 1;
    track.instrument_id = std::string(subtractive_instrument_id);
    initial.tracks.push_back(track);
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);

    AssignmentTargetPicker picker(application);
    const auto targets = picker.targets();
    require(std::any_of(targets.begin(), targets.end(), [](const auto &target) {
        return target.kind == AssignmentTargetKind::parameter &&
               target.id == "transport.tempo";
    }), "shared picker publishes parameters");
    const auto fill = std::find_if(targets.begin(), targets.end(), [](const auto &target) {
        return target.id == "performance.fill-set.v1";
    });
    require(fill != targets.end() && fill->performance_safe,
            "shared picker publishes safe typed actions");
    require(std::count_if(targets.begin(), targets.end(), [](const auto &target) {
                return target.kind == AssignmentTargetKind::performance_role &&
                       target.id.starts_with("performance.parameter.");
            }) == 8 &&
                std::count_if(targets.begin(), targets.end(), [](const auto &target) {
                    return target.kind == AssignmentTargetKind::performance_role &&
                           target.id.starts_with("performance.action.");
                }) == 8 &&
                std::count_if(targets.begin(), targets.end(), [](const auto &target) {
                    return target.kind == AssignmentTargetKind::performance_role &&
                           target.id.starts_with("performance.pad.");
                }) == 16,
            "shared picker publishes indexed Performance roles without V2 geometry in the project model");
    AssignmentProposal proposal{AssignmentTargetKind::action,
                                "performance.fill-set.v1", {}};
    std::string error;
    const auto safe = [](const auto &action) {
        return performance_safe_assignment_action(action);
    };
    require(!picker.validate(proposal, error, safe) && !error.empty(),
            "missing typed action arguments rejected");
    proposal.arguments["active"] = true;
    require(picker.validate(proposal, error, safe),
            "typed action validates through shared picker");

    execute(application, "performance.bank-create.v1",
            {{"bank", std::int64_t{1}}, {"name", std::string("Direct")}});
    execute(application, "performance.bank-create.v1",
            {{"bank", std::int64_t{2}}, {"name", std::string("Alternate")}});
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{1}}, {"assignment", std::int64_t{11}},
             {"position", std::int64_t{0}}, {"secondary", false},
             {"parameter_id", std::string("synth.cutoff")}});
    execute(application, "performance.parameter-assign.v1",
            {{"bank", std::int64_t{2}}, {"assignment", std::int64_t{12}},
             {"position", std::int64_t{0}}, {"secondary", false},
             {"parameter_id", std::string("synth.resonance")}});

    MidiControllerProfile profile;
    profile.controller = generic_midi_profile();
    profile.controller.profile_id = "manual-authoring";
    profile.controller.device_id = "disconnected-surface";
    profile.port_selector = "Future USB MIDI surface";
    auto &context_button = profile.controller.bindings[0];
    context_button = {};
    context_button.control_id = "button.1";
    context_button.semantic = "local-context";
    context_button.context = "performance";
    auto &role = profile.controller.bindings[3];
    role = {};
    role.control_id = "encoder.1";
    role.semantic = "context-encoder";
    role.index = 0;

    ControllerDeviceDescriptor descriptor;
    descriptor.descriptor_id = "manual.fixture";
    descriptor.device_id = profile.controller.device_id;
    descriptor.display_name = "Disconnected fixture";
    descriptor.transport = ControllerTransport::midi;
    descriptor.controls = profile.controller.capabilities.controls;
    MidiControlBinding absolute{1, 0, 10, MidiControlMode::cc_absolute,
                                "knob.1", true};
    MidiControlBinding relative{1, 0, 11,
        MidiControlMode::cc_relative_twos_complement, "encoder.1", false};
    MidiControlBinding context{1, 0, 60, MidiControlMode::note_momentary,
                               "button.1", false};
    require(validate_midi_control_binding(absolute, descriptor, error) &&
            validate_midi_control_binding(relative, descriptor, error) &&
            validate_midi_control_binding(context, descriptor, error),
            "disconnected-device source validation");
    require(find_midi_binding_conflicts(absolute, {absolute}).size() == 1,
            "manual conflict detection identifies source collision");
    require(!validate_midi_control_binding(
                {1, 16, 1, MidiControlMode::cc_absolute, "knob.1"},
                descriptor, error), "invalid MIDI channel rejected");

    MidiMapper mapper;
    ControllerCoordinator coordinator(application);
    require(coordinator.add_midi("manual", profile, mapper, error), error);
    require(coordinator.set_midi_binding("manual", 0, absolute, error) &&
            coordinator.set_midi_binding("manual", 1, relative, error) &&
            coordinator.set_midi_binding("manual", 2, context, error), error);
    require(mapper.receive({0xb0, 10, 100, 0, 1, 3}) &&
            mapper.dispatch_pending() == 1, "absolute production mapping dispatch");
    auto results = application.dispatch_pending();
    require(results.size() == 1 && results.front().accepted &&
            parameter(application.snapshot(), "performance.xy.x") > 0.7,
            "direct mapping changes authoritative parameter");

    require(mapper.receive({0x90, 60, 127, 0, 1, 3}) &&
            mapper.dispatch_pending() == 1, "context selection dispatch");
    require(mapper.receive({0xb0, 11, 1, 0, 1, 3}) &&
            mapper.dispatch_pending() == 1, "Performance role dispatch");
    results = application.dispatch_pending();
    require(!results.empty() && results.back().accepted,
            "Performance role reaches current bank target");
    const auto cutoff = parameter(application.snapshot(), "synth.cutoff");
    execute(application, "performance.bank-select.v1", {{"bank", std::int64_t{2}}});
    require(mapper.receive({0xb0, 11, 1, 0, 1, 3}) &&
            mapper.dispatch_pending() == 1, "bank-following role dispatch");
    results = application.dispatch_pending();
    require(!results.empty() && results.back().accepted &&
            parameter(application.snapshot(), "synth.resonance") > 0.0 &&
            parameter(application.snapshot(), "synth.cutoff") == cutoff,
            "Performance role follows bank without redirecting direct target");

    const auto before = coordinator.midi_profile("manual");
    require(before && before->midi.size() == 3, "candidate profile observable");
    require(!coordinator.set_midi_binding("manual", before->midi.size(),
                                         absolute, error) &&
            coordinator.midi_profile("manual")->midi == before->midi,
            "conflicting proposal leaves valid profile intact");
    const auto generation_revision = coordinator.states().front().session.local_revision;
    auto replacement = absolute;
    replacement.number = 12;
    require(coordinator.set_midi_binding("manual", 0, replacement, error) &&
            coordinator.midi_profile("manual")->midi[0] == replacement,
            "explicit valid replacement commits atomically");
    require(coordinator.states().front().session.local_revision >
                generation_revision,
            "active source replacement invalidates old input ownership");

    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-task096f.ngc";
    require(coordinator.save_midi_profile("manual", path, error), error);
    MidiControllerProfile restored;
    require(load_midi_controller_profile(path, restored, error) &&
            restored.midi == coordinator.midi_profile("manual")->midi,
            "profile save/reload preserves manual mappings");
    std::filesystem::remove(path);
    coordinator.disconnect_midi();
    coordinator.reconnect_midi();
    coordinator.poll(application.snapshot());

    std::cout << "neurogenic.controller-mapping-authoring-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
