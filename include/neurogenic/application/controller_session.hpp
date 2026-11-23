#pragma once

#include "neurogenic/application/controller.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include <deque>
#include <unordered_map>

namespace neurogenic {

struct ContextControl {
    std::uint16_t control_id{};
    std::string label, value, unit, parameter_id, action_id;
    double minimum{}, maximum{};
    bool active{}, bipolar{}, pickup{};
    std::int8_t pickup_direction{};
    std::uint64_t feedback_revision{};
};
enum class ControllerFeedbackKind : std::uint8_t { rgb, indicator, context, text };
struct ControllerFeedback {
    ControllerFeedbackKind kind{ControllerFeedbackKind::indicator};
    std::string control_id;
    std::uint32_t color{};
    std::uint8_t brightness{};
    ContextControl context;
};
struct ControllerSessionState {
    std::string session_id, device_id, profile_id, context{"sequencer"}, error;
    ModifierState modifiers;
    std::unordered_map<std::string, PickupState> pickup;
    std::uint64_t received{}, rejected{}, dropped_feedback{}, feedback_revision{};
    bool resync_required{true};
};

[[nodiscard]] std::array<ContextControl, 16>
resolve_controller_context(const ApplicationState &, std::string_view context,
                           const ModifierState &modifiers);
[[nodiscard]] std::array<ContextControl, 16>
build_controller_context(const ApplicationState &);

class ControllerSession {
  public:
    ControllerSession(ApplicationService &, ControllerDeviceDescriptor,
                      ControllerProfile, std::string session_id = {},
                      std::size_t feedback_capacity = 128);
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const ControllerDeviceDescriptor &descriptor() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] const ControllerSessionState &state() const noexcept {
        return state_;
    }
    bool input(const ControllerInput &, std::string &error);
    void observe(const ApplicationState &, bool full_refresh = false);
    [[nodiscard]] std::optional<ControllerFeedback> next_feedback();
    void reconnect() noexcept;
    void set_context(std::string context);

  private:
    const ControlBinding *binding(std::string_view control) const;
    bool submit(ApplicationCommand, std::string &error);
    void feedback(ControllerFeedback);
    ApplicationService *application_{};
    ControllerDeviceDescriptor descriptor_;
    ControllerProfile profile_;
    ParameterBindingService bindings_;
    ControllerSessionState state_;
    std::deque<ControllerFeedback> feedback_;
    std::size_t capacity_{};
    bool valid_{};
    std::array<std::uint32_t, 16> pad_colors_{};
    std::unordered_map<std::string, std::uint32_t> feedback_colors_;
    std::unordered_map<std::string, FaderPickup> pickup_;
    std::unordered_map<std::string, std::string> pickup_targets_;
};

} // namespace neurogenic
