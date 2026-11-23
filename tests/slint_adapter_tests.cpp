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
        require(adapter.state().encoders[0].label != "",
                "contextual descriptor copied");
        require(adapter.keyboard_action("space", true) &&
                    application.snapshot().playing,
                "keyboard simulation uses transport semantic action");
        require(adapter.invoke_function(7) && application.snapshot().playing,
                "contextual function dispatch");
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
