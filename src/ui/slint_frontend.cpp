#include "neurogenic/ui/slint_adapter.hpp"
#include "performance.h"
#include <slint.h>
#include <chrono>
#include <memory>

namespace neurogenic {
namespace {
slint::SharedString text(const std::string &value) {
    return slint::SharedString(std::string_view(value));
}

void publish(const SlintPerformancePageState &state,
             const slint::ComponentHandle<PerformanceWindow> &window) {
    window->set_transport_state(text(state.transport));
    window->set_tempo(text(std::to_string(state.tempo_bpm) + " BPM"));
    window->set_selected_track(text(state.selected_track));
    window->set_active_content(text(state.active_content));
    window->set_notification(text(state.notification));
    std::vector<ContextItem> encoders, functions;
    encoders.reserve(state.encoders.size());
    functions.reserve(state.functions.size());
    for (const auto &item : state.encoders)
        encoders.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    for (const auto &item : state.functions)
        functions.push_back({text(item.label), text(item.value), text(item.unit),
                             item.available});
    std::vector<PadItem> pads;
    pads.reserve(state.pads.size());
    for (const auto &pad : state.pads)
        pads.push_back({text(pad.label), pad.active, pad.selected});
    window->set_encoders(std::make_shared<slint::VectorModel<ContextItem>>(
        std::move(encoders)));
    window->set_functions(std::make_shared<slint::VectorModel<ContextItem>>(
        std::move(functions)));
    window->set_pads(
        std::make_shared<slint::VectorModel<PadItem>>(std::move(pads)));
}
} // namespace

bool slint_available() noexcept { return true; }

int run_slint_frontend(ApplicationService &application,
                       const SlintOptions &options, std::string &error) {
    try {
        SlintFrontendAdapter adapter(application);
        auto window = PerformanceWindow::create();
        window->window().set_size(slint::PhysicalSize({
            static_cast<std::uint32_t>(options.width),
            static_cast<std::uint32_t>(options.height)}));
        window->window().set_fullscreen(options.fullscreen);
        publish(adapter.state(), window);
        window->on_encoder_turned([&](int index, int delta) {
            (void)adapter.turn_encoder(static_cast<std::size_t>(index), delta);
            publish(adapter.state(), window);
        });
        window->on_function_invoked([&](int index) {
            (void)adapter.invoke_function(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_pad_invoked([&](int index, bool pressed) {
            (void)adapter.press_pad(static_cast<std::size_t>(index), pressed);
            publish(adapter.state(), window);
        });
        window->on_keyboard_input([&](slint::SharedString key, bool pressed) {
            (void)adapter.keyboard_action(key.data(), pressed);
            publish(adapter.state(), window);
        });
        slint::Timer refresh;
        refresh.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16),
                      [&] {
                          if (adapter.refresh())
                              publish(adapter.state(), window);
                      });
        window->run();
        refresh.stop();
        adapter.shutdown();
        return 0;
    } catch (const std::exception &exception) {
        error = exception.what();
        return 1;
    }
}
} // namespace neurogenic
