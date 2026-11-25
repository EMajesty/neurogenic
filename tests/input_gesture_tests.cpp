#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
ControlCapability button(std::string id) {
    return {std::move(id), "Button", ControlType::momentary_button};
}
ControlCapability encoder(std::string id) {
    return {std::move(id), "Encoder", ControlType::relative_encoder};
}
}

int main() try {
    std::uint64_t now = 1000;
    InputGestureArbiter arbiter([&] { return now; });
    std::vector<ControlCapability> controls{
        button("play"), button("shift"), button("pad"), encoder("data"),
        button("repeat"), button("momentary")};
    GestureConfiguration configuration;
    configuration.maximum_active_controls = controls.size();
    configuration.controls = {
        {"play", {}, GestureDispatchPolicy::immediate},
        {"shift", "shift", GestureDispatchPolicy::deferred_tap, true},
        {"pad", {}, GestureDispatchPolicy::immediate},
        {"data", {}, GestureDispatchPolicy::immediate},
        {"repeat", {}, GestureDispatchPolicy::repeatable},
        {"momentary", {}, GestureDispatchPolicy::momentary, false, false, false, {},
         {"transport.play.v1", {}, {}}, {"transport.stop.v1", {}, {}}}};
    configuration.chords = {
        {"shift", "pad", {"transport.stop.v1", {}, {}}, true, false},
        {"shift", "data", {{}, "mixer.master.gain", {}}, true, false}};
    std::string error;
    require(arbiter.configure(configuration, controls, error), error);
    auto events = arbiter.process({"play", 1, true, false, 1, 1}, error);
    require(events.size() == 1 && events[0].kind == GestureEventKind::press,
            "immediate press was delayed");
    (void)arbiter.process({"play", 0, false, false, 2, 1}, error);
    require(arbiter.process({"shift", 1, true, false, 3, 1}, error).empty(),
            "deferred tap dispatched on press");
    events = arbiter.process({"shift", 0, false, false, 4, 1}, error);
    require(events.size() == 2 && events[0].kind == GestureEventKind::tap,
            "deferred tap did not dispatch exactly once");
    require(arbiter.process({"shift", 1, true, false, 5, 1}, error).empty(),
            "chord owner unexpectedly dispatched");
    events = arbiter.process({"pad", 1, true, false, 6, 1}, error);
    require(events.size() == 1 && events[0].kind == GestureEventKind::chord,
            "button chord was not recognized");
    (void)arbiter.process({"shift", 0, false, false, 7, 1}, error);
    events = arbiter.process({"pad", 0, false, false, 8, 1}, error);
    require(events.empty(), "consumed chord produced an ordinary tap");
    (void)arbiter.process({"shift", 1, true, false, 9, 1}, error);
    events = arbiter.process({"data", 2, true, true, 10, 1}, error);
    require(events.size() == 1 && events[0].target.parameter_id ==
                "mixer.master.gain", "encoder chord lost its stable target");
    const auto cancelled = arbiter.set_context("sequencer", "confirm-delete");
    require(!cancelled.empty() && arbiter.state().held_controls.empty(),
            "modal transition did not cancel held ownership");
    (void)arbiter.reconnect();
    require(arbiter.state().connection_generation == 2 &&
                arbiter.state().effective_modifiers.empty(),
            "reconnect did not clear ownership and advance generation");
    events = arbiter.process({"play", 1, true, false, 11, 1}, error);
    require(events.empty() && !error.empty(), "stale generation was accepted");
    (void)arbiter.set_context("sequencer");
    require(arbiter.process({"shift", 1, true, false, 12, 2}, error).empty(), error);
    now += 20;
    (void)arbiter.process({"shift", 0, false, false, 13, 2}, error);
    now += 80;
    require(arbiter.process({"shift", 1, true, false, 14, 2}, error).empty(), error);
    now += 20;
    (void)arbiter.process({"shift", 0, false, false, 15, 2}, error);
    require(arbiter.state().latched_modifiers.contains("shift"),
            "double-tap did not latch logical modifier");
    events = arbiter.process({"repeat", 1, true, false, 16, 2}, error);
    require(events.size() == 1 && events.front().kind == GestureEventKind::press,
            "repeatable control did not dispatch its immediate initial press");
    now += 2000;
    events = arbiter.update(error);
    require(events.size() == configuration.controls.back().timing.maximum_repeats_per_update,
            "held repetition exceeded or missed its bounded catch-up limit");
    (void)arbiter.process({"repeat", 0, false, false, 17, 2}, error);
    events = arbiter.process({"momentary", 1, true, false, 18, 2}, error);
    require(events.size() == 1 && events[0].target.action_id == "transport.play.v1",
            "momentary press target missing");
    events = arbiter.process({"momentary", 0, false, false, 19, 2}, error);
    require(events.size() == 1 && events[0].target.action_id == "transport.stop.v1",
            "momentary release target missing");

    AudioEngine engine(48'000, 256);
    TimelineService timeline([&](const Timeline &value, std::string &message) {
        return engine.queue_timeline(value, message);
    });
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    ControllerDeviceDescriptor descriptor;
    descriptor.descriptor_id = descriptor.device_id = "gesture-sim";
    descriptor.display_name = "Gesture simulation";
    descriptor.controls = controls;
    ControllerProfile profile;
    profile.profile_id = "gesture-profile";
    profile.device_id = descriptor.device_id;
    profile.capabilities.controller_id = descriptor.device_id;
    profile.capabilities.name = "Gesture simulation";
    profile.capabilities.controls = controls;
    profile.bindings = {{"play", "transport.play.v1"},
                        {"shift", "transport.stop.v1"},
                        {"pad", "transport.play.v1"},
                        {"data", {}, "mixer.master.gain"},
                        {"repeat", "transport.play.v1"},
                        {"momentary", "transport.play.v1"}};
    ControllerSession session(application, descriptor, profile, "gesture");
    require(session.valid(), session.state().error);
    require(session.configure_gestures(configuration, error), error);
    require(session.input({"play", 1, 0, true, 1}, error), error);
    auto results = application.dispatch_pending();
    require(results.size() == 1 && results[0].accepted &&
                application.snapshot_shared()->playing,
            "production session did not dispatch immediate action");
    require(session.input({"shift", 1, 0, true, 2}, error), error);
    require(session.input({"pad", 1, 0, true, 3}, error), error);
    results = application.dispatch_pending();
    require(results.size() == 1 && results[0].accepted,
            "production session did not dispatch exactly one chord target");
    require(session.state().interaction.held_controls.contains("shift") &&
                session.state().interaction.held_controls.contains("pad"),
            "production session lost chord ownership feedback");
    session.reconnect();
    require(session.state().interaction.held_controls.empty(),
            "production reconnect retained held ownership");
    require(!session.input({"play", 1, 0, true, 4, 1}, error) &&
                error == "stale controller input generation",
            "production session accepted an event from the old connection");
    std::cout << "neurogenic.input-gesture-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
