#pragma once

#include "neurogenic/application/controller.hpp"
#include "neurogenic/application/input_gesture.hpp"
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
    std::map<std::string, ApplicationValue> arguments;
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
    std::string session_id, device_id, profile_id, context{"sequencer"},
        destination{"sequencer"}, error;
    ModifierState modifiers;
    std::unordered_map<std::string, PickupState> pickup;
    std::uint64_t received{}, rejected{}, dropped_feedback{}, feedback_revision{};
    std::uint64_t local_revision{1}, delivered_local_revision{};
    std::uint64_t root_revision{1};
    bool resync_required{true};
    GestureInteractionState interaction;
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
                      std::size_t feedback_capacity = 128,
                      InputGestureArbiter::Clock gesture_clock = {});
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const ControllerDeviceDescriptor &descriptor() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] const ControllerSessionState &state() const noexcept {
        return state_;
    }
    bool input(const ControllerInput &, std::string &error);
    bool configure_gestures(GestureConfiguration, std::string &error);
    bool update_gestures(std::string &error);
    void set_modal_context(std::string modal_context);
    void observe(const ApplicationState &, bool full_refresh = false);
    [[nodiscard]] std::optional<ControllerFeedback> next_feedback();
    [[nodiscard]] bool has_pending_feedback() const noexcept {
        return !feedback_.empty();
    }
    void feedback_delivery_failed() noexcept;
    void invalidate_feedback() noexcept;
    [[nodiscard]] bool needs_feedback() const noexcept {
        return state_.resync_required || !feedback_.empty() ||
               state_.delivered_local_revision != state_.local_revision;
    }
    void reconnect();
    void set_context(std::string context);
    bool set_binding(std::size_t index, ControlBinding, std::string &error);
    [[nodiscard]] const ControllerProfile &profile() const noexcept {
        return profile_;
    }

  private:
    bool dispatch_legacy(const ControllerInput &, std::string &error);
    bool dispatch_gesture(const GestureEvent &, const ControllerInput &,
                          std::string &error);
    void synchronize_interaction() noexcept;
    const ControlBinding *binding(std::string_view control) const;
    bool submit(ApplicationCommand, std::string &error);
    bool feedback(ControllerFeedback);
    ApplicationService *application_{};
    ControllerDeviceDescriptor descriptor_;
    ControllerProfile profile_;
    ParameterBindingService bindings_;
    InputGestureArbiter gestures_;
    ControllerSessionState state_;
    std::deque<ControllerFeedback> feedback_;
    std::size_t capacity_{};
    bool valid_{};
    std::array<std::uint32_t, 16> pad_colors_{};
    std::unordered_map<std::string, std::uint32_t> feedback_colors_;
    std::unordered_map<std::string, std::string> context_cache_;
    std::unordered_map<std::string, FaderPickup> pickup_;
    std::unordered_map<std::string, std::string> pickup_targets_;
};

} // namespace neurogenic
