#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
ControllerInput input(std::string id, bool pressed, std::uint64_t sequence,
                      std::uint64_t generation = 1) {
    return {std::move(id), pressed ? 1.0 : 0.0, 0.0, pressed,
            sequence,      generation};
}
} // namespace

int main() try {
    auto descriptor = reference_controller_v2_descriptor();
    auto profile = reference_controller_v2_profile();
    require(descriptor.controls.size() == 67,
            "V2 inventory is not 58 buttons plus nine encoders");
    std::set<std::string> identities;
    std::size_t buttons{}, encoders{}, pads{};
    for (const auto &control : descriptor.controls) {
        require(identities.insert(control.id).second,
                "duplicate V2 logical identity");
        if (control.type == ControlType::relative_encoder)
            ++encoders;
        else
            ++buttons;
        if (control.id.starts_with("pad."))
            ++pads;
    }
    require(buttons == 58 && encoders == 9 && pads == 16,
            "V2 logical control class counts differ from the locked inventory");

    AudioEngine engine(48'000, 256);
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    });
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    std::uint64_t now{1000};
    ControllerSession session(application, descriptor, profile, "reference-v2",
                              128, [&] { return now; });
    require(session.valid(), session.state().error);
    std::string error;
    require(
        session.configure_gestures(reference_controller_v2_gestures(), error),
        error);

    std::uint64_t sequence{1};
    for (std::size_t i = 0; i < 8; ++i) {
        require(session.input(
                    input("page." + std::to_string(i + 1), true, sequence++),
                    error),
                error);
        require(session.state().destination ==
                    reference_primary_destinations()[i].id,
                "primary page routing mismatch");
        require(session.input(
                    input("page." + std::to_string(i + 1), false, sequence++),
                    error),
                error);
    }
    require(session.input(input("edit.shift", true, sequence++), error), error);
    require(session.state().modifiers.shift,
            "held SHIFT did not become effective");
    for (std::size_t i = 0; i < 8; ++i) {
        require(session.input(
                    input("page." + std::to_string(i + 1), true, sequence++),
                    error),
                error);
        require(session.state().destination ==
                    reference_secondary_destinations()[i].id,
                "secondary page routing mismatch");
        require(session.state().modifiers.shift,
                "page context transition incorrectly cancelled global SHIFT");
        require(session.input(
                    input("page." + std::to_string(i + 1), false, sequence++),
                    error),
                error);
    }
    require(session.input(input("edit.shift", false, sequence++), error),
            error);
    require(session.state().destination == "setup" &&
                !session.state().modifiers.shift,
            "SHIFT release redirected the active secondary destination");

    for (int tap = 0; tap < 2; ++tap) {
        now += 60;
        require(session.input(input("edit.shift", true, sequence++), error),
                error);
        now += 20;
        require(session.input(input("edit.shift", false, sequence++), error),
                error);
    }
    require(session.state().modifiers.shift_latched,
            "double-tap SHIFT did not latch through ControllerSession");
    require(session.input(input("page.2", true, sequence++), error), error);
    require(session.state().destination == "song",
            "latched SHIFT did not select the secondary destination");
    require(session.input(input("page.2", false, sequence++), error), error);
    for (int tap = 0; tap < 2; ++tap) {
        now += 60;
        require(session.input(input("edit.shift", true, sequence++), error),
                error);
        now += 20;
        require(session.input(input("edit.shift", false, sequence++), error),
                error);
    }
    require(!session.state().modifiers.shift_latched,
            "double-tap SHIFT did not explicitly unlatch");

    const auto root = session.state().root_revision;
    require(session.input(input("page.8", true, sequence++), error), error);
    require(session.state().destination == "browser",
            "normal labels were not restored");
    require(session.input(input("page.8", false, sequence++), error), error);
    require(session.input(input("page.8", true, sequence++), error), error);
    require(session.state().root_revision > root,
            "active destination did not return to root");
    require(session.input(input("page.8", false, sequence++), error), error);

    session.set_modal_context("confirm-delete");
    require(!session.input(input("page.1", true, sequence++), error) &&
                session.state().destination == "browser",
            "page input bypassed modal ownership");
    session.set_modal_context({});

    require(session.input(input("edit.shift", true, sequence++), error), error);
    session.reconnect();
    require(!session.state().modifiers.shift &&
                session.state().interaction.effective_modifiers.empty(),
            "reconnect retained physical modifier ownership");
    const auto generation = session.state().interaction.connection_generation;
    require(!session.input(input("page.1", true, sequence++, generation - 1),
                           error),
            "stale-generation page input was accepted");
    require(session.input(input("page.1", true, sequence++, generation), error),
            error);
    require(session.state().destination == "performance",
            "new-generation navigation did not recover");

    std::cout << "neurogenic.reference-controller-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
