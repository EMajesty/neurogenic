#pragma once

#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace neurogenic {

struct SlintContextItem {
    std::string label{"Unavailable"}, value{"--"}, unit, parameter_id,
        action_id;
    bool available{};
};

struct SlintPadItem {
    std::string label;
    bool active{}, selected{};
};

struct SlintPerformancePageState {
    std::uint64_t revision{};
    bool playing{};
    double tempo_bpm{120.0};
    std::string transport{"Stopped"}, selected_track{"Unavailable"},
        active_content{"Unavailable"}, notification;
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
};

[[nodiscard]] bool slint_available() noexcept;
int run_slint_frontend(ApplicationService &, const SlintOptions &,
                       std::string &error);

} // namespace neurogenic
