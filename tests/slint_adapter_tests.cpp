#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    using namespace neurogenic;
    try {
        AudioEngine engine;
        SampleManager samples(engine);
        Timeline initial;
        initial.tracks.push_back({1, "UI Track"});
        initial.tracks.push_back({2, "Second Track"});
        PatternDefinition pattern;
        pattern.id = 1;
        pattern.name = "UI Pattern";
        pattern.tracks.push_back({1, {pattern.length, {}}});
        initial.arrangement.patterns.push_back(std::move(pattern));
        initial.arrangement.launch_slots.push_back(
            {11, 1, LaunchSourceType::pattern, 1, LaunchBehavior::loop,
             LaunchQuantization::bar});
        initial.arrangement.performance_scenes.push_back(
            {21, "UI Scene", {{1, 11}}, LaunchQuantization::bar});
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, initial);
        ApplicationService application(engine, timeline, samples);
        SlintFrontendAdapter adapter(application);
        require(adapter.active(), "adapter lifecycle starts active");
        require(adapter.state().selected_track == "UI Track",
                "selected track copied from application state");
        require(adapter.state().encoders.size() == 8 &&
                    adapter.state().functions.size() == 8 &&
                    adapter.state().pads.size() == 16,
                "fixed presentation model dimensions");
        require(adapter.state().pages.size() == 8 &&
                    adapter.state().page_id == "performance" &&
                    adapter.state().pages.front().implemented,
                "page registry exposes eight default destinations");
        require(adapter.state().pads[0].available &&
                    adapter.state().pads[0].slot_id == 11,
                "performance slots use authoritative launcher state");
        require(adapter.state().scenes.size() == 1 &&
                    adapter.state().scenes[0].label == "UI Scene",
                "performance scenes use authoritative launcher state");
        require(adapter.state().encoders[0].label != "",
                "contextual descriptor copied");
        require(adapter.keyboard_action("space", true) &&
                    application.snapshot().playing,
                "keyboard simulation uses transport semantic action");
        require(adapter.invoke_function(7) && application.snapshot().playing,
                "contextual function dispatch");
        require(adapter.select_track(1) &&
                    application.snapshot().selected_track == 2,
                "track selection uses semantic actions");
        require(adapter.select_page(1) &&
                    adapter.state().page_id == "sequencer" &&
                    !adapter.state().notification.empty(),
                "placeholder page selection is explicit");
        require(adapter.select_page(0) &&
                    adapter.state().page_id == "performance",
                "performance navigation restores working page");
        require(adapter.cycle_launch_quantization() &&
                    application.snapshot().launch_quantization ==
                        LaunchQuantization::two_bars,
                "launch quantization uses semantic arrangement action");
        require(!adapter.transport_record() &&
                    adapter.last_error().find("semantic action") !=
                        std::string::npos,
                "unsupported record action is honest");
        adapter.dismiss_notification();
        require(adapter.state().notification.empty(),
                "notifications can be dismissed without application mutation");
        require(adapter.turn_encoder(0, 1),
                "contextual encoder uses parameter binding");
        require(!adapter.turn_encoder(99, 1) && !adapter.last_error().empty(),
                "unavailable binding is visible");
        require(!adapter.press_pad(16), "invalid pad rejected");
        const auto revision = adapter.state().revision;
        require(!adapter.refresh() || adapter.state().revision >= revision,
                "revisioned refresh");
        adapter.shutdown();
        require(!adapter.active() && !adapter.refresh() &&
                    !adapter.invoke_action("transport.stop.v1"),
                "shutdown prevents dispatch");
        samples.shutdown();
        std::cout << "neurogenic.slint-adapter-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
