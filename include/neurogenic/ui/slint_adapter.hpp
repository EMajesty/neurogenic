#pragma once

#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace neurogenic {

struct SlintContextItem {
    std::string label{"Unavailable"}, value{"--"}, unit, parameter_id,
        action_id;
    bool available{};
};

struct SlintPadItem {
    std::string label, status{"Unavailable"};
    std::uint64_t slot_id{};
    bool active{}, queued{}, selected{}, available{};
};

struct SlintPageItem {
    std::string id, title;
    bool implemented{};
};

struct SlintSceneItem {
    std::string label;
    bool queued{};
};

struct SlintPerformancePageState {
    std::uint64_t revision{};
    bool playing{}, recording{}, project_dirty{};
    double tempo_bpm{120.0};
    std::string project_name{"Untitled"}, transport{"Stopped"},
        time_signature{"4/4"}, position{"1.1.1"},
        selected_track{"Unavailable"}, active_content{"Unavailable"},
        track_owner{"Arrangement"}, launch_quantization{"Bar"},
        page_id{"performance"}, page_title{"Performance"}, notification;
    std::vector<SlintPageItem> pages;
    std::vector<SlintSceneItem> scenes;
    std::array<SlintContextItem, 8> encoders;
    std::array<SlintContextItem, 8> functions;
    std::array<SlintPadItem, 16> pads;
};

// Presentation-neutral bridge used by the Slint renderer and renderer-free
// tests. It owns no engine state: every value is copied from ApplicationState
// and every mutation goes through a semantic action or ParameterBindingService.
class SlintFrontendAdapter {
  public:
    explicit SlintFrontendAdapter(ApplicationService &application);
    [[nodiscard]] bool refresh();
    [[nodiscard]] const SlintPerformancePageState &state() const noexcept {
        return state_;
    }
    [[nodiscard]] bool invoke_action(std::string action_id);
    [[nodiscard]] bool turn_encoder(std::size_t index, double steps,
                                    bool fine = false);
    [[nodiscard]] bool invoke_function(std::size_t index);
    [[nodiscard]] bool press_pad(std::size_t index, bool pressed = true);
    [[nodiscard]] bool select_page(std::size_t index);
    [[nodiscard]] bool select_track(int direction);
    [[nodiscard]] bool launch_scene(std::size_t index);
    [[nodiscard]] bool return_to_arrangement();
    [[nodiscard]] bool set_launch_quantization(std::string value);
    [[nodiscard]] bool cycle_launch_quantization();
    [[nodiscard]] bool transport_record();
    void dismiss_notification() noexcept;
    [[nodiscard]] bool keyboard_action(std::string_view key, bool pressed);
    void shutdown() noexcept { active_ = false; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string &last_error() const noexcept {
        return last_error_;
    }

  private:
    bool execute(ApplicationCommand command);
    ApplicationService &application_;
    ParameterBindingService bindings_;
    SlintPerformancePageState state_;
    std::string last_error_;
    bool active_{true};
};

struct SlintOptions {
    int width{1280}, height{800};
    bool fullscreen{};
    std::function<bool()> request_close;
    std::function<bool()> external_stop_requested;
};

[[nodiscard]] bool slint_available() noexcept;
int run_slint_frontend(ApplicationService &, const SlintOptions &,
                       std::string &error);

} // namespace neurogenic
