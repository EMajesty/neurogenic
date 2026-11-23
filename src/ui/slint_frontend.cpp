#include "neurogenic/ui/slint_adapter.hpp"
#include "performance.h"
#include <slint.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>

namespace neurogenic {
namespace {
slint::SharedString text(const std::string &value) {
    return slint::SharedString(std::string_view(value));
}

void publish(const SlintUiState &state,
             const slint::ComponentHandle<PerformanceWindow> &window) {
    window->set_transport_state(text(state.shell.transport));
    window->set_tempo(text(std::to_string(state.shell.tempo_bpm) + " BPM"));
    window->set_project_name(text(state.shell.project_name));
    window->set_project_dirty(state.shell.project_dirty);
    window->set_time_signature(text(state.shell.time_signature));
    window->set_position(text(state.shell.position));
    window->set_page_id(text(state.shell.page_id));
    window->set_page_title(text(state.shell.page_title));
    window->set_selected_track(text(state.performance.selected_track));
    window->set_track_owner(text(state.performance.track_owner));
    window->set_active_content(text(state.performance.active_content));
    window->set_launch_quantization(text(state.performance.launch_quantization));
    window->set_notification(text(state.shell.notification));
    window->set_arrangement_view(state.sequencer.arrangement_view);
    window->set_selected_pattern(text(state.sequencer.selected_pattern));
    window->set_pattern_length(text(state.sequencer.pattern_length));
    window->set_step_resolution(text(state.sequencer.resolution));
    window->set_selected_velocity(text(state.sequencer.selected_velocity));
    window->set_selected_duration(text(state.sequencer.selected_duration));
    window->set_arrangement_loop(text(state.sequencer.loop_range));
    std::vector<ContextItem> encoders, functions;
    std::vector<PageItem> pages;
    std::vector<SceneItem> scenes;
    encoders.reserve(state.context.encoders.size());
    functions.reserve(state.context.functions.size());
    for (const auto &item : state.context.encoders)
        encoders.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    for (const auto &item : state.context.functions)
        functions.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    pages.reserve(state.shell.pages.size());
    for (const auto &page : state.shell.pages)
        pages.push_back({text(page.id), text(page.title), page.implemented});
    scenes.reserve(state.performance.scenes.size());
    for (const auto &scene : state.performance.scenes)
        scenes.push_back({text(scene.label), scene.queued});
    std::vector<PadItem> pads;
    pads.reserve(state.performance.pads.size());
    for (const auto &pad : state.performance.pads)
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
    std::vector<StepItem> steps;
    for (const auto &step : state.sequencer.steps)
        steps.push_back({text(step.label), text(step.status), step.active,
                         step.selected, step.playhead, step.available});
    window->set_steps(std::make_shared<slint::VectorModel<StepItem>>(std::move(steps)));
    std::vector<PatternItem> patterns;
    for (const auto &pattern : state.sequencer.patterns)
        patterns.push_back({static_cast<int>(pattern.id), text(pattern.name),
                            pattern.selected, pattern.active, pattern.queued});
    window->set_patterns(
        std::make_shared<slint::VectorModel<PatternItem>>(std::move(patterns)));
    std::vector<ArrangementItem> arrangement;
    for (const auto &item : state.sequencer.arrangement)
        arrangement.push_back({static_cast<int>(item.id), text(item.label),
                               text(item.range), text(item.kind), item.selected,
                               item.enabled});
    window->set_arrangement_items(
        std::make_shared<slint::VectorModel<ArrangementItem>>(std::move(arrangement)));
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
        window->on_sequencer_step_invoked([&](int index) {
            (void)adapter.select_step(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_pattern_invoked([&](int index, bool launch) {
            (void)adapter.select_pattern(static_cast<std::size_t>(index), launch);
            publish(adapter.state(), window);
        });
        window->on_pattern_create([&](bool duplicate) {
            (void)adapter.create_pattern(duplicate);
            publish(adapter.state(), window);
        });
        window->on_resolution_invoked([&] {
            (void)adapter.cycle_step_resolution();
            publish(adapter.state(), window);
        });
        window->on_arrangement_toggle([&] {
            (void)adapter.toggle_arrangement_view();
            publish(adapter.state(), window);
        });
        window->on_arrangement_item_invoked([&](int index) {
            (void)adapter.select_arrangement_clip(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_step_edit([&](slint::SharedString operation) {
            const auto snapshot = application.snapshot();
            if (operation == "velocity-up")
                (void)adapter.set_step_velocity(std::min(1.0, snapshot.selected_event ? snapshot.selected_event->velocity + 0.05 : 1.0));
            else if (operation == "velocity-down")
                (void)adapter.set_step_velocity(std::max(0.0, snapshot.selected_event ? snapshot.selected_event->velocity - 0.05 : 0.0));
            else if (operation == "length-up")
                (void)adapter.set_step_duration(snapshot.selected_event ? snapshot.selected_event->duration + snapshot.step_ticks : snapshot.step_ticks);
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
