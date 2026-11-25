#include "neurogenic/application/input_gesture.hpp"
#include <algorithm>
#include <chrono>

namespace neurogenic {
namespace {
std::uint64_t steady_milliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

InputGestureArbiter::InputGestureArbiter(Clock clock)
    : clock_(clock ? std::move(clock) : Clock{steady_milliseconds}) {}

bool InputGestureArbiter::configure(
    GestureConfiguration configuration,
    const std::vector<ControlCapability> &capabilities, std::string &error) {
    if (configuration.maximum_active_controls == 0 ||
        configuration.maximum_active_controls > capabilities.size()) {
        error = "gesture active-control capacity is invalid";
        return false;
    }
    std::set<std::string> controls;
    for (const auto &capability : capabilities) controls.insert(capability.id);
    std::set<std::string> configured;
    for (const auto &candidate : configuration.controls) {
        if (!controls.contains(candidate.control_id)) {
            error = "gesture policy references an unknown control";
            return false;
        }
        if (!configured.insert(candidate.control_id).second) {
            error = "gesture policy control is duplicated";
            return false;
        }
        if ((candidate.policy == GestureDispatchPolicy::modifier_only ||
             candidate.latchable) && candidate.modifier.empty()) {
            error = "modifier gesture requires a logical modifier identity";
            return false;
        }
        if (candidate.timing.maximum_repeats_per_update == 0 ||
            candidate.timing.repeat_interval_ms == 0 ||
            candidate.timing.minimum_repeat_interval_ms == 0) {
            error = "gesture timing must be bounded and nonzero";
            return false;
        }
    }
    for (const auto &candidate : configuration.chords) {
        if (!configured.contains(candidate.owner_control) ||
            !controls.contains(candidate.input_control)) {
            error = "gesture chord references an unavailable control";
            return false;
        }
        if (candidate.target.action_id.empty() &&
            candidate.target.parameter_id.empty()) {
            error = "gesture chord has no semantic target";
            return false;
        }
    }
    configuration_ = std::move(configuration);
    configured_ = true;
    error.clear();
    (void)cancel_all("gesture configuration changed");
    return true;
}

const GestureControlPolicy *InputGestureArbiter::policy(
    std::string_view control) const {
    const auto found = std::find_if(configuration_.controls.begin(),
        configuration_.controls.end(), [&](const auto &candidate) {
            return candidate.control_id == control;
        });
    return found == configuration_.controls.end() ? nullptr : &*found;
}

const GestureChord *InputGestureArbiter::chord(std::string_view owner,
                                                std::string_view input) const {
    const auto found = std::find_if(configuration_.chords.begin(),
        configuration_.chords.end(), [&](const auto &candidate) {
            return candidate.owner_control == owner &&
                   candidate.input_control == input;
        });
    return found == configuration_.chords.end() ? nullptr : &*found;
}

GestureEvent InputGestureArbiter::event(GestureEventKind kind,
                                        std::string control,
                                        std::string owner, double value,
                                        GestureTarget target) const {
    return {kind, std::move(control), std::move(owner), observable_.context,
            value, std::move(target), observable_.connection_generation,
            observable_.context_generation};
}

void InputGestureArbiter::rebuild_modifiers() {
    observable_.held_controls.clear();
    observable_.effective_modifiers = latched_;
    observable_.latched_modifiers = latched_;
    observable_.active_owner.clear();
    for (const auto &[control, ownership] : active_) {
        observable_.held_controls.insert(control);
        if (!ownership.policy.modifier.empty())
            observable_.effective_modifiers.insert(ownership.policy.modifier);
        if (observable_.active_owner.empty() &&
            (ownership.pending_tap || ownership.consumed))
            observable_.active_owner = control;
    }
}

std::vector<GestureEvent> InputGestureArbiter::process(
    const GestureInput &input, std::string &error) {
    std::vector<GestureEvent> result;
    if (!configured_) {
        error = "gesture arbitration is not configured";
        return result;
    }
    if (input.connection_generation != 0 &&
        input.connection_generation != observable_.connection_generation) {
        error = "stale controller input generation";
        observable_.last_cancellation = error;
        ++observable_.revision;
        return result;
    }
    const auto candidate = policy(input.control_id);
    if (!candidate) {
        error = "control has no gesture policy";
        return result;
    }
    if (const auto sequence = last_sequence_.find(input.control_id);
        sequence != last_sequence_.end() && input.sequence != 0 &&
        input.sequence <= sequence->second) {
        error = "duplicate or stale controller input sequence";
        return result;
    }
    if (input.sequence != 0) last_sequence_[input.control_id] = input.sequence;
    const auto now = clock_();

    if (input.encoder) {
        for (auto &[owner, ownership] : active_) {
            const auto combination = chord(owner, input.control_id);
            if (!combination) continue;
            if (!observable_.modal_context.empty() &&
                !combination->allowed_during_modal) {
                error = "gesture chord is unavailable while a modal owns input";
                return result;
            }
            ownership.consumed = ownership.consumed ||
                                 combination->consume_owner_tap;
            if (ownership.consumed) ownership.pending_tap = false;
            result.push_back(event(GestureEventKind::chord, input.control_id,
                                   owner, input.value, combination->target));
            rebuild_modifiers();
            ++observable_.revision;
            error.clear();
            return result;
        }
        if (!observable_.modal_context.empty() &&
            !candidate->global_during_modal) {
            error = "control is owned by the active modal";
            return result;
        }
        result.push_back(event(GestureEventKind::press, input.control_id, {},
                               input.value));
        error.clear();
        return result;
    }

    if (input.pressed) {
        if (active_.contains(input.control_id)) {
            error = "duplicate controller press";
            return result;
        }
        if (active_.size() >= configuration_.maximum_active_controls) {
            error = "gesture active-control capacity exceeded";
            return result;
        }
        for (auto &[owner, ownership] : active_) {
            const auto combination = chord(owner, input.control_id);
            if (!combination) continue;
            if (!observable_.modal_context.empty() &&
                !combination->allowed_during_modal) {
                error = "gesture chord is unavailable while a modal owns input";
                return result;
            }
            ownership.consumed = ownership.consumed ||
                                 combination->consume_owner_tap;
            if (ownership.consumed) ownership.pending_tap = false;
            active_.emplace(input.control_id,
                Ownership{*candidate, now, 0, input.sequence,
                          observable_.context_generation, false, true, false});
            result.push_back(event(GestureEventKind::chord, input.control_id,
                                   owner, input.value, combination->target));
            rebuild_modifiers();
            ++observable_.revision;
            error.clear();
            return result;
        }
        if (!observable_.modal_context.empty() &&
            !candidate->global_during_modal) {
            error = "control is owned by the active modal";
            return result;
        }
        Ownership ownership{*candidate, now, 0, input.sequence,
                            observable_.context_generation};
        ownership.pending_tap =
            candidate->policy == GestureDispatchPolicy::deferred_tap;
        ownership.dispatched_press =
            candidate->policy == GestureDispatchPolicy::immediate ||
            candidate->policy == GestureDispatchPolicy::momentary;
        if (candidate->policy == GestureDispatchPolicy::repeatable)
            ownership.next_repeat = now + candidate->timing.repeat_delay_ms;
        active_.emplace(input.control_id, ownership);
        if (ownership.dispatched_press)
            result.push_back(event(GestureEventKind::press, input.control_id,
                                   {}, {}, candidate->press_target));
        if (candidate->policy == GestureDispatchPolicy::modifier_only)
            result.push_back(event(GestureEventKind::modifier_changed,
                                   input.control_id));
        rebuild_modifiers();
        ++observable_.revision;
        error.clear();
        return result;
    }

    const auto found = active_.find(input.control_id);
    if (found == active_.end()) {
        error = "release has no matching press ownership";
        return result;
    }
    auto ownership = found->second;
    active_.erase(found);
    if (ownership.context_generation != observable_.context_generation) {
        result.push_back(event(GestureEventKind::cancelled, input.control_id));
    } else if (ownership.pending_tap && !ownership.consumed) {
        result.push_back(event(GestureEventKind::tap, input.control_id));
    } else if (ownership.dispatched_press) {
        result.push_back(event(GestureEventKind::release, input.control_id,
                               {}, {}, ownership.policy.release_target));
    }
    if (!ownership.policy.modifier.empty()) {
        if (ownership.policy.latchable && !ownership.consumed) {
            const auto previous = last_tap_[ownership.policy.modifier];
            if (previous != 0 && now >= previous &&
                now - previous <= ownership.policy.timing.double_tap_ms) {
                if (latched_.contains(ownership.policy.modifier))
                    latched_.erase(ownership.policy.modifier);
                else
                    latched_.insert(ownership.policy.modifier);
                last_tap_[ownership.policy.modifier] = 0;
            } else {
                last_tap_[ownership.policy.modifier] = now;
            }
        } else if (ownership.consumed) {
            last_tap_[ownership.policy.modifier] = 0;
        }
        result.push_back(event(GestureEventKind::modifier_changed,
                               input.control_id));
    }
    rebuild_modifiers();
    ++observable_.revision;
    error.clear();
    return result;
}

std::vector<GestureEvent> InputGestureArbiter::update(std::string &error) {
    std::vector<GestureEvent> result;
    const auto now = clock_();
    for (auto &[control, ownership] : active_) {
        if (ownership.policy.policy != GestureDispatchPolicy::repeatable ||
            ownership.next_repeat == 0 || now < ownership.next_repeat)
            continue;
        std::size_t count{};
        while (ownership.next_repeat <= now &&
               count < ownership.policy.timing.maximum_repeats_per_update) {
            result.push_back(event(GestureEventKind::repeat, control, {}, {},
                                   ownership.policy.press_target));
            ++count;
            const auto held = now - ownership.pressed_at;
            const auto interval = held >= ownership.policy.timing.long_press_ms * 3
                ? std::max(ownership.policy.timing.minimum_repeat_interval_ms,
                           ownership.policy.timing.repeat_interval_ms / 2)
                : ownership.policy.timing.repeat_interval_ms;
            ownership.next_repeat += interval;
        }
        if (ownership.next_repeat <= now)
            ownership.next_repeat = now +
                ownership.policy.timing.minimum_repeat_interval_ms;
    }
    if (!result.empty()) ++observable_.revision;
    error.clear();
    return result;
}

std::vector<GestureEvent> InputGestureArbiter::cancel_all(std::string reason) {
    std::vector<GestureEvent> result;
    for (const auto &[control, ownership] : active_)
        if (ownership.dispatched_press || ownership.consumed)
            result.push_back(event(GestureEventKind::cancelled, control, {}, {},
                                   ownership.policy.release_target));
    active_.clear();
    last_tap_.clear();
    observable_.last_cancellation = std::move(reason);
    rebuild_modifiers();
    ++observable_.revision;
    return result;
}

std::vector<GestureEvent> InputGestureArbiter::transition_context(
    std::string reason) {
    std::vector<GestureEvent> result;
    for (auto current = active_.begin(); current != active_.end();) {
        if (current->second.policy.global_across_context) {
            ++current;
            continue;
        }
        const auto &ownership = current->second;
        if (ownership.dispatched_press || ownership.consumed)
            result.push_back(event(GestureEventKind::cancelled, current->first,
                                   {}, {}, ownership.policy.release_target));
        current = active_.erase(current);
    }
    last_tap_.clear();
    observable_.last_cancellation = std::move(reason);
    rebuild_modifiers();
    ++observable_.revision;
    return result;
}

std::vector<GestureEvent> InputGestureArbiter::set_context(
    std::string context, std::string modal_context) {
    if (observable_.context == context &&
        observable_.modal_context == modal_context)
        return {};
    auto result = transition_context("input context changed");
    observable_.context = std::move(context);
    observable_.modal_context = std::move(modal_context);
    ++observable_.context_generation;
    for (auto &[control, ownership] : active_) {
        (void)control;
        ownership.context_generation = observable_.context_generation;
    }
    ++observable_.revision;
    return result;
}

std::vector<GestureEvent> InputGestureArbiter::reconnect() {
    auto result = cancel_all("controller connection generation changed");
    latched_.clear();
    last_sequence_.clear();
    ++observable_.connection_generation;
    ++observable_.context_generation;
    rebuild_modifiers();
    ++observable_.revision;
    return result;
}

} // namespace neurogenic
