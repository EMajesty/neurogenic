#pragma once

#include "neurogenic/application/controller.hpp"
#include <functional>
#include <map>
#include <set>

namespace neurogenic {

enum class GestureDispatchPolicy : std::uint8_t {
    immediate,
    deferred_tap,
    momentary,
    modifier_only,
    repeatable
};

enum class GestureEventKind : std::uint8_t {
    press,
    release,
    tap,
    chord,
    repeat,
    modifier_changed,
    cancelled
};

struct GestureTiming {
    std::uint64_t double_tap_ms{260}, long_press_ms{420},
        repeat_delay_ms{360}, repeat_interval_ms{90},
        minimum_repeat_interval_ms{35};
    std::size_t maximum_repeats_per_update{4};
};

struct GestureTarget {
    std::string action_id, parameter_id;
    std::map<std::string, ApplicationValue> arguments;
};

struct GestureControlPolicy {
    std::string control_id, modifier;
    GestureDispatchPolicy policy{GestureDispatchPolicy::immediate};
    bool latchable{}, global_during_modal{};
    GestureTiming timing;
    GestureTarget press_target, release_target;
};

struct GestureChord {
    std::string owner_control, input_control;
    GestureTarget target;
    bool consume_owner_tap{true}, allowed_during_modal{};
};

struct GestureConfiguration {
    std::vector<GestureControlPolicy> controls;
    std::vector<GestureChord> chords;
    std::size_t maximum_active_controls{32};
};

struct GestureInput {
    std::string control_id;
    double value{};
    bool pressed{};
    bool encoder{};
    std::uint64_t sequence{}, connection_generation{};
};

struct GestureEvent {
    GestureEventKind kind{GestureEventKind::press};
    std::string control_id, owner_control, context;
    double value{};
    GestureTarget target;
    std::uint64_t connection_generation{}, context_generation{};
};

struct GestureInteractionState {
    std::set<std::string> held_controls, effective_modifiers,
        latched_modifiers;
    std::string context, modal_context, active_owner, last_cancellation;
    std::uint64_t connection_generation{1}, context_generation{1}, revision{1};
};

class InputGestureArbiter {
  public:
    using Clock = std::function<std::uint64_t()>;
    explicit InputGestureArbiter(Clock clock = {});
    bool configure(GestureConfiguration,
                   const std::vector<ControlCapability> &, std::string &error);
    [[nodiscard]] std::vector<GestureEvent> process(const GestureInput &,
                                                    std::string &error);
    [[nodiscard]] std::vector<GestureEvent> update(std::string &error);
    [[nodiscard]] std::vector<GestureEvent> set_context(
        std::string context, std::string modal_context = {});
    [[nodiscard]] std::vector<GestureEvent> reconnect();
    [[nodiscard]] const GestureInteractionState &state() const noexcept {
        return observable_;
    }
    [[nodiscard]] bool configured() const noexcept { return configured_; }

  private:
    struct Ownership {
        GestureControlPolicy policy;
        std::uint64_t pressed_at{}, next_repeat{}, sequence{}, context_generation{};
        bool pending_tap{}, consumed{}, dispatched_press{};
    };
    [[nodiscard]] const GestureControlPolicy *policy(std::string_view) const;
    [[nodiscard]] const GestureChord *chord(std::string_view,
                                            std::string_view) const;
    std::vector<GestureEvent> cancel_all(std::string reason);
    void rebuild_modifiers();
    [[nodiscard]] GestureEvent event(GestureEventKind, std::string,
                                     std::string = {}, double = {},
                                     GestureTarget = {}) const;

    Clock clock_;
    GestureConfiguration configuration_;
    std::map<std::string, Ownership> active_;
    std::map<std::string, std::uint64_t> last_sequence_, last_tap_;
    std::set<std::string> latched_;
    GestureInteractionState observable_;
    bool configured_{};
};

} // namespace neurogenic
