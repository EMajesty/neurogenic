#include "neurogenic/ui/slint_adapter.hpp"
#include "performance.h"
#include <slint.h>
#include <chrono>
#include <cstdlib>
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
    window->set_project_name(text(state.project_name));
    window->set_project_dirty(state.project_dirty);
    window->set_time_signature(text(state.time_signature));
    window->set_position(text(state.position));
    window->set_page_id(text(state.page_id));
    window->set_page_title(text(state.page_title));
    window->set_selected_track(text(state.selected_track));
    window->set_track_owner(text(state.track_owner));
    window->set_active_content(text(state.active_content));
    window->set_launch_quantization(text(state.launch_quantization));
    window->set_notification(text(state.notification));
    std::vector<ContextItem> encoders, functions;
    std::vector<PageItem> pages;
    std::vector<SceneItem> scenes;
    encoders.reserve(state.encoders.size());
    functions.reserve(state.functions.size());
    for (const auto &item : state.encoders)
        encoders.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    for (const auto &item : state.functions)
        functions.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    pages.reserve(state.pages.size());
    for (const auto &page : state.pages)
        pages.push_back({text(page.id), text(page.title), page.implemented});
    scenes.reserve(state.scenes.size());
    for (const auto &scene : state.scenes)
        scenes.push_back({text(scene.label), scene.queued});
    std::vector<PadItem> pads;
    pads.reserve(state.pads.size());
    for (const auto &pad : state.pads)
        pads.push_back({text(pad.label), text(pad.status), pad.active,
                        pad.queued, pad.selected, pad.available});
    window->set_pages(
        std::make_shared<slint::VectorModel<PageItem>>(std::move(pages)));
    window->set_scenes(
        std::make_shared<slint::VectorModel<SceneItem>>(std::move(scenes)));
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
#if defined(__linux__)
    const auto wayland = std::getenv("WAYLAND_DISPLAY");
    const auto display = std::getenv("DISPLAY");
    if ((wayland == nullptr || *wayland == '\0') &&
        (display == nullptr || *display == '\0')) {
        error = "Slint requires an available Wayland or X11 display";
        return 1;
    }
#endif
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
        window->on_page_selected([&](int index) {
            (void)adapter.select_page(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_transport_invoked([&](slint::SharedString operation) {
            if (operation == "play")
                (void)adapter.invoke_action("transport.play.v1");
            else if (operation == "stop")
                (void)adapter.invoke_action("transport.stop.v1");
            else
                (void)adapter.transport_record();
            publish(adapter.state(), window);
        });
        window->on_track_selected([&](int direction) {
            (void)adapter.select_track(direction);
            publish(adapter.state(), window);
        });
        window->on_return_arrangement([&] {
            (void)adapter.return_to_arrangement();
            publish(adapter.state(), window);
        });
        window->on_scene_invoked([&](int index) {
            (void)adapter.launch_scene(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_quantization_invoked([&] {
            (void)adapter.cycle_launch_quantization();
            publish(adapter.state(), window);
        });
        window->on_notification_dismissed([&] {
            adapter.dismiss_notification();
            publish(adapter.state(), window);
        });
        slint::Timer refresh;
        refresh.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16),
                      [&] {
                          if (adapter.refresh())
                              publish(adapter.state(), window);
                          if (options.external_stop_requested &&
                              options.external_stop_requested())
                              window->hide();
                      });
        do {
            window->run();
        } while (options.request_close && !options.request_close());
        refresh.stop();
        adapter.shutdown();
        return 0;
    } catch (const std::exception &exception) {
        error = exception.what();
        return 1;
    }
}
} // namespace neurogenic
