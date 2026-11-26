#include "neurogenic/application/controller_coordinator.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, std::string message) {
    if (!value) throw std::runtime_error(std::move(message));
}
double parameter(const ApplicationState &state, std::string_view id) {
    const auto found = std::find_if(state.parameters.begin(), state.parameters.end(),
        [&](const auto &value) { return value.id == id; });
    require(found != state.parameters.end(), "missing parameter");
    return std::get<double>(found->current_value);
}
MidiControllerProfile profile(std::string id, std::string parameter_id,
                              std::uint8_t cc) {
    MidiControllerProfile result;
    result.controller = generic_midi_profile();
    result.controller.profile_id = std::move(id);
    result.controller.device_id = "profile-library-fixture";
    result.controller.capabilities.controller_id = "profile-library-fixture";
    result.controller.bindings[0] = {};
    result.controller.bindings[0].control_id = "knob.1";
    result.controller.bindings[0].parameter_id = std::move(parameter_id);
    result.controller.bindings[0].absolute = true;
    result.midi.push_back({1, 0, cc, MidiControlMode::cc_absolute,
                           "knob.1", true});
    return result;
}
ControllerDeviceDescriptor descriptor(const MidiControllerProfile &profile) {
    ControllerDeviceDescriptor result;
    result.descriptor_id = "profile-library.fixture";
    result.device_id = profile.controller.device_id;
    result.display_name = "Profile library fixture";
    result.transport = ControllerTransport::midi;
    result.controls = profile.controller.capabilities.controls;
    return result;
}
} // namespace

int main() try {
    AudioEngine engine(48'000, 128);
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    });
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    ControllerCoordinator coordinator(application);
    auto original = profile("original", "performance.xy.x", 10);
    auto alternate = profile("alternate", "performance.xy.y", 11);
    std::string error;
    require(coordinator.profile_library().register_descriptor(
                descriptor(original), error), error);
    require(coordinator.profile_library().create(
                "original", "Original", "profile-library.fixture",
                original, false, error), error);
    require(coordinator.profile_library().create(
                "alternate", "Alternate", "profile-library.fixture",
                alternate, false, error), error);
    require(coordinator.profile_library().duplicate(
                "original", "copy", "Copy", error), error);
    auto changed_copy = coordinator.profile_library().find("copy")->profile;
    changed_copy.controller.bindings[0].parameter_id = "synth.cutoff";
    require(coordinator.profile_library().replace("copy", changed_copy, error), error);
    require(coordinator.profile_library().find("original")->profile.controller
                .bindings[0].parameter_id == "performance.xy.x",
            "duplicate is independent");
    require(coordinator.profile_library().rename("copy", "Stage Copy", error), error);

    MidiMapper first_mapper, second_mapper;
    require(coordinator.add_midi("first", original, first_mapper, error), error);
    require(coordinator.add_midi("second", original, second_mapper, error), error);
    require(coordinator.activate_midi_profile("first", "original", error), error);
    require(coordinator.activate_midi_profile("second", "alternate", error), error);
    require(first_mapper.receive({0xb0, 10, 100, 0, 1, 3}) &&
                first_mapper.dispatch_pending() == 1,
            "first endpoint dispatches original profile");
    require(second_mapper.receive({0xb0, 11, 90, 0, 1, 3}) &&
                second_mapper.dispatch_pending() == 1,
            "second endpoint dispatches alternate profile");
    auto dispatched = application.dispatch_pending();
    require(dispatched.size() == 2 && dispatched[0].accepted && dispatched[1].accepted,
            "independent endpoint commands accepted");
    const auto snapshot = application.snapshot();
    require(parameter(snapshot, "performance.xy.x") > 0.7 &&
                parameter(snapshot, "performance.xy.y") > 0.6,
            "independent profiles address independent parameters");
    const auto first_generation = coordinator.states()[0].session.local_revision;
    require(coordinator.activate_midi_profile("first", "alternate", error), error);
    const auto states = coordinator.states();
    const auto first = std::find_if(states.begin(), states.end(),
        [](const auto &value) { return value.id == "first"; });
    require(first != states.end() &&
                first->session.local_revision > first_generation,
            "profile activation advances endpoint generation");

    const auto directory = std::filesystem::temp_directory_path() /
                           "neurogenic-profile-library-test";
    std::filesystem::remove_all(directory);
    require(coordinator.profile_library().save(directory, error), error);
    ControllerProfileLibrary restored(application);
    require(restored.register_descriptor(descriptor(original), error), error);
    require(restored.load(directory, error), error);
    require(restored.entries().size() == 3 &&
                restored.associated_profile("first") == "alternate" &&
                restored.associated_profile("second") == "alternate",
            "fresh library restores identities and endpoint associations");
    { std::ofstream corrupt(directory / "copy.ngc", std::ios::trunc);
      corrupt << "corrupt\n"; }
    ControllerProfileLibrary recovered(application);
    require(recovered.register_descriptor(descriptor(original), error), error);
    require(recovered.load(directory, error) && recovered.find("copy") &&
                recovered.find("copy")->valid,
            "fresh load recovers a corrupt profile from known-valid data");
    auto corrupt_candidate = restored.find("copy")->profile;
    corrupt_candidate.controller.bindings[0].control_id = "unknown";
    require(!restored.replace("copy", corrupt_candidate, error) &&
                restored.find("copy")->valid,
            "invalid replacement preserves known-valid in-memory profile");
    require(restored.restore_known_valid("copy", directory, error), error);
    require(!restored.remove("alternate", false, error),
            "active profile removal requires explicit handling");
    require(restored.remove("alternate", true, error) &&
                !restored.associated_profile("first") &&
                restored.find("original"),
            "explicit active removal clears associations without fallback");
    require(!restored.remove("missing", true, error),
            "missing profile removal is rejected");
    std::filesystem::remove_all(directory);

    std::cout << "neurogenic.controller-profile-library-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
