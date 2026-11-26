#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace neurogenic {
namespace {
std::optional<double> number(const ApplicationValue &value) {
    if (const auto *real = std::get_if<double>(&value)) return *real;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto *boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    return {};
}
std::optional<double> normalized(const ParameterDescriptor &parameter) {
    const auto value = number(parameter.effective_value),
               minimum = number(parameter.minimum), maximum = number(parameter.maximum);
    if (!value || !minimum || !maximum || *maximum <= *minimum) return {};
    return std::clamp((*value - *minimum) / (*maximum - *minimum), 0.0, 1.0);
}
std::string context_key(const ContextControl &value) {
    std::ostringstream output;
    output << value.label << '\x1f' << value.value << '\x1f' << value.unit
           << '\x1f' << value.parameter_id << '\x1f' << value.action_id
           << '\x1f' << value.bank_id << '\x1f' << value.role
           << '\x1f' << value.minimum << '\x1f' << value.maximum << '\x1f'
           << value.active << value.bipolar << value.pickup << value.locked << ':'
           << static_cast<int>(value.pickup_direction) << ':'
           << value.assignment_id;
    for (const auto &[name, argument] : value.arguments)
        output << '\x1f' << name << '=' << argument.index() << ':'
               << std::visit([](const auto &item) {
                      using Value = std::decay_t<decltype(item)>;
                      if constexpr (std::is_same_v<Value, std::monostate>)
                          return std::string{};
                      else if constexpr (std::is_same_v<Value, std::string>)
                          return item;
                      else if constexpr (std::is_same_v<Value, bool>)
                          return item ? std::string("1") : std::string("0");
                      else if constexpr (std::is_same_v<Value, NormalizedXY>)
                          return std::to_string(item.x) + "," +
                                 std::to_string(item.y);
                      else
                          return std::to_string(item);
                  }, argument);
    return output.str();
}
} // namespace

ControllerSession::ControllerSession(ApplicationService &application,
                                     ControllerDeviceDescriptor descriptor,
                                     ControllerProfile profile,
                                     std::string session_id,
                                     std::size_t feedback_capacity,
                                     InputGestureArbiter::Clock gesture_clock)
    : application_(&application), descriptor_(std::move(descriptor)),
      profile_(std::move(profile)), bindings_(application),
      gestures_(std::move(gesture_clock)),
      capacity_(std::max<std::size_t>(1, std::min(
          feedback_capacity, descriptor_.maximum_feedback_queue))) {
    std::string error;
    valid_ = validate_controller_configuration(descriptor_, profile_, application,
                                               error);
    state_.session_id = session_id.empty() ? descriptor_.device_id :
                                             std::move(session_id);
    state_.device_id = descriptor_.device_id;
    state_.profile_id = profile_.profile_id;
    state_.error = std::move(error);
}

const ControlBinding *ControllerSession::binding(std::string_view control) const {
    const auto matches = [&](const ControlBinding &candidate) {
        return candidate.control_id == control &&
               candidate.shift ==
                   (state_.modifiers.shift || state_.modifiers.shift_latched) &&
               candidate.select == (state_.modifiers.select ||
                                     state_.modifiers.select_latched);
    };
    auto found = std::find_if(profile_.bindings.begin(), profile_.bindings.end(),
                              matches);
    if (found == profile_.bindings.end() && profile_.fallback_to_base &&
        (state_.modifiers.shift || state_.modifiers.shift_latched ||
         state_.modifiers.select || state_.modifiers.select_latched))
        found = std::find_if(profile_.bindings.begin(), profile_.bindings.end(),
                             [&](const auto &candidate) {
                                 return candidate.control_id == control &&
                                        !candidate.shift && !candidate.select;
                             });
    return found == profile_.bindings.end() ? nullptr : &*found;
}

bool ControllerSession::submit(ApplicationCommand command, std::string &error) {
    command.source_id = state_.session_id;
    if (!application_->submit(std::move(command))) {
        ++state_.rejected;
        error = "application command queue is full";
        state_.error = error;
        return false;
    }
    error.clear();
    return true;
}

bool ControllerSession::input(const ControllerInput &input, std::string &error) {
    ++state_.received;
    if (!valid_) {
        error = state_.error.empty() ? "invalid controller session" : state_.error;
        return false;
    }
    const auto control = std::find_if(
        descriptor_.controls.begin(), descriptor_.controls.end(),
        [&](const auto &candidate) { return candidate.id == input.control_id; });
    if (control == descriptor_.controls.end() || !control->available) {
        error = "control is unknown or unavailable for this device";
        ++state_.rejected;
        return false;
    }
    if (gestures_.configured()) {
        const bool encoder = control->type == ControlType::relative_encoder ||
            control->type == ControlType::absolute_potentiometer ||
            control->type == ControlType::fader ||
            control->type == ControlType::crossfader;
        if (!input.pressed &&
            active_performance_actions_.contains(input.control_id) &&
            !dispatch_legacy(input, error))
            return false;
        if (input.pressed && input.control_id.starts_with("pad.") &&
            state_.context.starts_with("sequencer") &&
            state_.interaction.modal_context.empty())
            if (auto target = capture_step_target(input.control_id))
                held_steps_[input.control_id] = *target;
        const GestureInput gesture{input.control_id, input.value, input.pressed,
                                   encoder, input.sequence,
                                   input.connection_generation == 0
                                       ? state_.interaction.connection_generation
                                       : input.connection_generation};
        auto events = gestures_.process(gesture, error);
        synchronize_interaction();
        if (!error.empty()) { ++state_.rejected; return false; }
        for (const auto &event : events)
            if (!dispatch_gesture(event, input, error)) return false;
        if (!input.pressed && input.control_id.starts_with("pad.")) {
            if (held_step_edit_ &&
                held_step_edit_->owner_control == input.control_id &&
                !finish_held_step_edit(true, error))
                return false;
            held_steps_.erase(input.control_id);
        }
        ++state_.local_revision;
        state_.resync_required = true;
        return true;
    }
    return dispatch_legacy(input, error);
}

std::optional<ControllerSession::HeldStepTarget>
ControllerSession::capture_step_target(std::string_view control) const {
    constexpr std::string_view prefix{"pad."};
    if (!control.starts_with(prefix)) return {};
    std::size_t pad{};
    try {
        const auto value = std::stoul(std::string(control.substr(prefix.size())));
        if (value == 0 || value > 16) return {};
        pad = value - 1;
    } catch (...) {
        return {};
    }
    const auto snapshot = application_->snapshot();
    HeldStepTarget target;
    target.pattern_id = snapshot.selected_pattern;
    target.track_id = snapshot.selected_track;
    target.step = static_cast<std::int64_t>(snapshot.step_page * 16 + pad);
    target.connection_generation = state_.interaction.connection_generation;
    target.context_generation = state_.interaction.context_generation;
    const auto track = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                    [&](const auto &item) {
                                        return item.id == target.track_id;
                                    });
    if (track == snapshot.tracks.end()) return target;
    const auto begin = target.step * snapshot.step_ticks;
    const auto end = begin + snapshot.step_ticks;
    for (const auto &event : track->events)
        if (event.type == MusicalEventType::note && event.tick >= begin &&
            event.tick < end) {
            if (target.event_id) target.ambiguous = true;
            else target.event_id = event.id;
        }
    return target;
}

bool ControllerSession::finish_held_step_edit(bool commit, std::string &error) {
    if (!held_step_edit_) {
        error.clear();
        return true;
    }
    const auto grouped = held_step_edit_->grouped;
    held_step_edit_.reset();
    if (!grouped) {
        error.clear();
        return true;
    }
    return submit({commit ? "edit.group-commit.v1" : "edit.group-cancel.v1",
                   {}, state_.session_id, 0}, error);
}

void ControllerSession::cancel_performance_actions() noexcept {
    for (auto &[control, owned] : active_performance_actions_) {
        (void)control;
        std::string ignored;
        (void)submit({owned.action_id, std::move(owned.release_arguments),
                      state_.session_id, 0}, ignored);
    }
    active_performance_actions_.clear();
    for (auto &[control, owned] : active_pad_actions_) {
        (void)control;
        if (owned.release_action_id.empty()) continue;
        std::string ignored;
        (void)submit({owned.release_action_id, std::move(owned.release_arguments),
                      state_.session_id, 0}, ignored);
    }
    active_pad_actions_.clear();
}

bool ControllerSession::dispatch_legacy(const ControllerInput &input,
                                        std::string &error) {
    const auto control = std::find_if(
        descriptor_.controls.begin(), descriptor_.controls.end(),
        [&](const auto &candidate) { return candidate.id == input.control_id; });
    if (control == descriptor_.controls.end()) {
        error = "gesture event references an unknown control";
        return false;
    }
    const auto *mapping = binding(input.control_id);
    if (!mapping || mapping->explicitly_unassigned) {
        error.clear();
        return true;
    }
    if (mapping->semantic == "modifier-shift") {
        state_.modifiers.shift = input.pressed;
        ++state_.local_revision;
        state_.resync_required = true;
        error.clear();
        return true;
    }
    if (mapping->semantic == "modifier-select") {
        state_.modifiers.select = input.pressed;
        ++state_.local_revision;
        state_.resync_required = true;
        error.clear();
        return true;
    }
    if (mapping->semantic == "context-button" && !input.pressed) {
        const auto active = active_performance_actions_.find(input.control_id);
        if (active == active_performance_actions_.end()) {
            error.clear();
            return true;
        }
        auto owned = std::move(active->second);
        active_performance_actions_.erase(active);
        return submit({owned.action_id, std::move(owned.release_arguments),
                       state_.session_id, input.sequence}, error);
    }
    if (mapping->semantic == "performance-pad" && !input.pressed) {
        const auto active = active_pad_actions_.find(input.control_id);
        if (active == active_pad_actions_.end()) {
            error.clear();
            return true;
        }
        auto owned = std::move(active->second);
        active_pad_actions_.erase(active);
        if (owned.release_action_id.empty()) {
            error.clear();
            return true;
        }
        return submit({owned.release_action_id, std::move(owned.release_arguments),
                       state_.session_id, input.sequence}, error);
    }
    if (!input.pressed && control->type != ControlType::relative_encoder &&
        control->type != ControlType::absolute_potentiometer &&
        control->type != ControlType::fader &&
        control->type != ControlType::crossfader) {
        error.clear();
        return true;
    }
    if (mapping->semantic == "local-context") {
        set_context(mapping->context);
        error.clear();
        return true;
    }
    if (mapping->semantic == "reference-page") {
        if (mapping->index < 0 || mapping->index >= 8) {
            error = "reference page position is invalid";
            return false;
        }
        const auto &table = state_.modifiers.shift || state_.modifiers.shift_latched
                                ? reference_secondary_destinations()
                                : reference_primary_destinations();
        const auto &destination = table[static_cast<std::size_t>(mapping->index)];
        if (!destination.available) {
            error = "reference destination is unavailable";
            return false;
        }
        if (state_.destination == destination.id) {
            if (application_->snapshot().interaction.destination != destination.id &&
                !application_->set_interaction_destination(
                    destination.id,
                    destination.subview.empty() ? "root" : destination.subview,
                    error))
                return false;
            ++state_.root_revision;
            return submit({"navigation.root.v1", {}, state_.session_id,
                           input.sequence}, error);
        }
        if (!application_->set_interaction_destination(
                destination.id,
                destination.subview.empty() ? "root" : destination.subview,
                error))
            return false;
        state_.destination = destination.id;
        set_context(profile_.profile_id ==
                            "neurogenic.reference-controller-v2.default.v1"
                        ? destination.id + ":v2"
                        : destination.id);
        error.clear();
        return true;
    }
    const auto snapshot = application_->snapshot();
    if (mapping->semantic == "recording-toggle") {
        if (snapshot.recording.state == RecordingState::recording)
            return submit({"recording.stop.v1", {}, state_.session_id,
                           input.sequence}, error);
        if (snapshot.recording.state == RecordingState::armed)
            return submit({"recording.start.v1", {}, state_.session_id,
                           input.sequence}, error);
        if (!submit({"recording.arm.v1", {}, state_.session_id, input.sequence},
                    error))
            return false;
        return submit({"recording.start.v1", {}, state_.session_id,
                       input.sequence}, error);
    }
    if (mapping->semantic == "focus-data") {
        auto steps = input.value;
        if (mapping->invert) steps = -steps;
        if (state_.modifiers.shift || state_.modifiers.shift_latched)
            steps *= mapping->fine_scale;
        else if (std::abs(input.value) >= 4)
            steps *= mapping->acceleration;
        return submit({"navigation.adjust.v1", {{"steps", steps}},
                       state_.session_id, input.sequence}, error);
    }
    if (mapping->semantic == "performance-pad") {
        if (profile_.profile_id !=
                "neurogenic.reference-controller-v2.default.v1" &&
            !state_.context.starts_with("performance")) {
            PerformanceGrid grid;
            grid.set_mode(snapshot.pad_mode);
            grid.set_step_page(snapshot.step_page);
            grid.set_selected_slot(snapshot.selected_sample_slot);
            grid.set_selected_pad(snapshot.selected_pad);
            auto command = grid_application_command(
                grid.press(static_cast<std::size_t>(mapping->index),
                           state_.modifiers.select),
                state_.session_id, input.sequence);
            return !command || submit(std::move(*command), error);
        }
        if (mapping->index < 0 || mapping->index >= 16) {
            error = "context pad index is invalid";
            return false;
        }
        const auto pads = resolve_controller_pads(
            snapshot, state_.context, state_.modifiers);
        const auto &target = pads[static_cast<std::size_t>(mapping->index)];
        if (!target.available || target.action_id.empty()) {
            error.clear();
            return true;
        }
        if (!submit({target.action_id, target.arguments, state_.session_id,
                     input.sequence}, error))
            return false;
        if (!target.release_action_id.empty())
            active_pad_actions_[input.control_id] = target;
        return true;
    }
    const auto context = resolve_controller_context(
        snapshot, state_.context, state_.modifiers);
    if (mapping->semantic == "context-button") {
        if (mapping->index < 0 || mapping->index >= 8) {
            error = "context button index is invalid";
            return false;
        }
        const auto &target = context[8 + mapping->index];
        if (!target.active || target.action_id.empty()) {
            error.clear();
            return true;
        }
        if (!submit({target.action_id, target.arguments, state_.session_id,
                     input.sequence}, error))
            return false;
        if (target.role == "performance-momentary") {
            const auto bank = std::find_if(
                snapshot.performance_banks.banks.begin(),
                snapshot.performance_banks.banks.end(), [&](const auto &value) {
                    return value.id == snapshot.performance_banks.active_bank;
                });
            if (bank == snapshot.performance_banks.banks.end()) {
                error = "Performance momentary bank is unavailable";
                return false;
            }
            const auto assignment = std::find_if(
                bank->actions.begin(), bank->actions.end(), [&](const auto &value) {
                    return value.id == target.assignment_id;
                });
            if (assignment == bank->actions.end()) {
                error = "Performance momentary assignment is unavailable";
                return false;
            }
            std::map<std::string, ApplicationValue> release;
            for (const auto &[name, value] : assignment->release_arguments)
                std::visit([&](const auto &item) { release[name] = item; }, value);
            active_performance_actions_[input.control_id] =
                {bank->id, assignment->id, assignment->action_id,
                 std::move(release)};
        }
        return true;
    }
    if (mapping->semantic == "context-encoder") {
        if (mapping->index < 0 || mapping->index >= 8) {
            error = "context encoder index is invalid";
            return false;
        }
        const auto &target = context[mapping->index];
        if (!target.active) {
            error.clear();
            return true;
        }
        auto steps = input.value;
        if (mapping->invert) steps = -steps;
        if ((state_.modifiers.shift || state_.modifiers.shift_latched) &&
            target.role != "performance-parameter")
            steps *= mapping->fine_scale;
        else if (std::abs(input.value) >= 4)
            steps *= mapping->acceleration;
        if (target.role == "step-property") {
            const auto property = target.arguments.find("property");
            if (property == target.arguments.end() ||
                !std::holds_alternative<std::string>(property->second)) {
                error = "step-property context binding is invalid";
                return false;
            }
            if (!submit({"step.property-select.v1",
                         {{"property", std::get<std::string>(property->second)}},
                         state_.session_id, input.sequence}, error))
                return false;
            return submit({"navigation.adjust.v1", {{"steps", steps}},
                           state_.session_id, input.sequence}, error);
        }
        if (target.role == "step-lock") {
            if (!submit({"step.lock-select.v1",
                         {{"parameter_id", target.parameter_id}},
                         state_.session_id, input.sequence}, error))
                return false;
            return submit({"navigation.adjust.v1", {{"steps", steps}},
                           state_.session_id, input.sequence}, error);
        }
        if (target.parameter_id.empty()) {
            error.clear();
            return true;
        }
        if (target.role == "performance-parameter" ||
            target.role == "performance-macro") {
            const auto scale = target.arguments.find("scale");
            if (scale != target.arguments.end())
                if (const auto amount = number(scale->second)) steps *= *amount;
        }
        if (target.role == "performance-macro") {
            const auto macro = target.arguments.find("macro_id");
            if (macro == target.arguments.end() ||
                !std::holds_alternative<std::int64_t>(macro->second)) {
                error = "Performance macro target is invalid";
                return false;
            }
            return submit({"macro.value-adjust.v1",
                           {{"id", std::get<std::int64_t>(macro->second)},
                            {"steps", steps}},
                           state_.session_id, input.sequence}, error);
        }
        return submit({"parameter.adjust.v1",
                       {{"parameter_id", target.parameter_id}, {"steps", steps}},
                       state_.session_id, input.sequence},
                      error);
    }
    if (!mapping->action_id.empty()) {
        ApplicationCommand command{mapping->action_id, mapping->arguments,
                                   state_.session_id, input.sequence};
        return submit(std::move(command), error);
    }
    if (!mapping->parameter_id.empty()) {
        const auto relative = control->type == ControlType::relative_encoder;
        auto value = input.value;
        if (mapping->invert)
            value = relative ? -value : 1.0 - value;
        if (relative && (state_.modifiers.shift || state_.modifiers.shift_latched))
            value *= mapping->fine_scale;
        else if (relative && std::abs(value) >= 4)
            value *= mapping->acceleration;
        if (!relative && mapping->pickup) {
            const auto parameter = std::find_if(
                snapshot.parameters.begin(), snapshot.parameters.end(),
                [&](const auto &candidate) {
                    return candidate.id == mapping->parameter_id;
                });
            if (parameter == snapshot.parameters.end()) {
                error = "pickup target is unavailable";
                ++state_.rejected;
                return false;
            }
            const auto target = normalized(*parameter);
            if (!target) {
                error = "pickup target is not a numeric parameter";
                ++state_.rejected;
                return false;
            }
            auto &pickup = pickup_.try_emplace(mapping->control_id, 0.01).first->second;
            auto &target_id = pickup_targets_[mapping->control_id];
            if (target_id != mapping->parameter_id) {
                pickup.set_target(*target);
                target_id = mapping->parameter_id;
            } else if (std::fabs(pickup.state().target - *target) > 0.01)
                pickup.set_target(*target);
            if (!pickup.update(value)) {
                state_.pickup[mapping->control_id] = pickup.state();
                ++state_.local_revision;
                state_.resync_required = true;
                error.clear();
                return true;
            }
            state_.pickup[mapping->control_id] = pickup.state();
            ++state_.local_revision;
            state_.resync_required = true;
        }
        return submit({relative ? "parameter.adjust.v1"
                                : "parameter.set-normalized.v1",
                       {{"parameter_id", mapping->parameter_id},
                        {relative ? "steps" : "value", value}},
                       state_.session_id, input.sequence},
                      error);
    }
    error.clear();
    return true;
}

bool ControllerSession::dispatch_gesture(const GestureEvent &event,
                                         const ControllerInput &original,
                                         std::string &error) {
    if (event.kind == GestureEventKind::cancelled && held_step_edit_ &&
        held_step_edit_->owner_control == event.control_id)
        return finish_held_step_edit(false, error);
    if (event.target.action_id.starts_with("reference."))
        return dispatch_reference_gesture(event, original, error);
    if (!event.target.action_id.empty())
        return submit({event.target.action_id, event.target.arguments,
                       state_.session_id, original.sequence}, error);
    if (!event.target.parameter_id.empty())
        return submit({"parameter.adjust.v1",
                       {{"parameter_id", event.target.parameter_id},
                        {"steps", event.value}}, state_.session_id,
                       original.sequence}, error);
    if (event.kind == GestureEventKind::press ||
        event.kind == GestureEventKind::tap ||
        event.kind == GestureEventKind::repeat) {
        if (event.kind == GestureEventKind::tap &&
            event.control_id.starts_with("pad.") &&
            state_.context.starts_with("sequencer")) {
            const auto held = held_steps_.find(event.control_id);
            if (held == held_steps_.end()) {
                error = "sequencer pad has no stable gesture target";
                return false;
            }
            return submit({"step.toggle.v1",
                           {{"pattern_id", static_cast<std::int64_t>(
                                               held->second.pattern_id)},
                            {"track_id", static_cast<std::int64_t>(
                                             held->second.track_id)},
                            {"step", held->second.step}},
                           state_.session_id, original.sequence}, error);
        }
        auto dispatched = original;
        dispatched.pressed = true;
        return dispatch_legacy(dispatched, error);
    }
    error.clear();
    return true;
}

bool ControllerSession::dispatch_reference_gesture(
    const GestureEvent &event, const ControllerInput &original,
    std::string &error) {
    const auto pad_index = [&]() -> std::optional<std::size_t> {
        constexpr std::string_view prefix{"pad."};
        if (!event.control_id.starts_with(prefix)) return {};
        try {
            const auto value = std::stoul(event.control_id.substr(prefix.size()));
            if (value == 0 || value > 16) return {};
            return value - 1;
        } catch (...) {
            return {};
        }
    }();
    const auto snapshot = application_->snapshot();
    if (event.target.action_id == "reference.tempo-data.v1") {
        auto steps = event.value;
        if (state_.modifiers.shift || state_.modifiers.shift_latched)
            steps *= 0.1;
        else if (std::abs(steps) >= 4)
            steps *= 2.0;
        return submit({"parameter.adjust.v1",
                       {{"parameter_id", std::string("transport.tempo")},
                        {"steps", steps}},
                       state_.session_id, original.sequence}, error);
    }
    if (!pad_index &&
        event.target.action_id != "reference.step-encoder.v1") {
        error = "reference gesture has an invalid logical pad identity";
        return false;
    }
    if (event.target.action_id == "reference.step-encoder.v1") {
        const auto held = held_steps_.find(event.owner_control);
        if (held == held_steps_.end() || held->second.ambiguous) {
            error = held == held_steps_.end()
                ? "held step target is unavailable"
                : "held step contains multiple note events";
            return false;
        }
        constexpr std::string_view prefix{"parameter."};
        if (!event.control_id.starts_with(prefix)) {
            error = "held step encoder identity is invalid";
            return false;
        }
        std::size_t index{};
        try {
            const auto value = std::stoul(event.control_id.substr(prefix.size()));
            if (value == 0 || value > 8) throw std::out_of_range("encoder");
            index = value - 1;
        } catch (...) {
            error = "held step encoder identity is invalid";
            return false;
        }
        const auto context = resolve_controller_context(
            snapshot, state_.context, state_.modifiers);
        const auto &control = context[index];
        if (!control.active ||
            (control.role != "step-property" &&
             control.role != "step-lock")) {
            error = "contextual encoder is unavailable for held-step editing";
            return false;
        }
        if (!held_step_edit_) {
            held_step_edit_ = HeldStepEdit{event.owner_control,
                                           event.control_id, held->second, true};
            if (!submit({"edit.group-begin.v1", {}, state_.session_id,
                         original.sequence}, error)) {
                held_step_edit_.reset();
                return false;
            }
        }
        std::map<std::string, ApplicationValue> arguments{
            {"pattern_id", static_cast<std::int64_t>(held->second.pattern_id)},
            {"track_id", static_cast<std::int64_t>(held->second.track_id)},
            {"step", held->second.step},
            {"steps", event.value},
            {"apply_selection", true}};
        if (control.role == "step-lock") {
            arguments.emplace("parameter_id", control.parameter_id);
            return submit({"step.lock-adjust-stable.v1", std::move(arguments),
                           state_.session_id, original.sequence}, error);
        }
        if (held->second.event_id)
            arguments.emplace("event_id", static_cast<std::int64_t>(
                                              *held->second.event_id));
        const auto property = control.arguments.find("property");
        if (property == control.arguments.end() ||
            !std::holds_alternative<std::string>(property->second)) {
            error = "held-step property binding is invalid";
            return false;
        }
        arguments.emplace("property", std::get<std::string>(property->second));
        return submit({"step.adjust-stable.v1", std::move(arguments),
                       state_.session_id, original.sequence}, error);
    }
    if (event.target.action_id == "reference.page-pad.v1")
        return submit({"step.page-select.v1",
                       {{"page", static_cast<std::int64_t>(*pad_index)},
                        {"pattern_id", static_cast<std::int64_t>(
                                           snapshot.selected_pattern)},
                        {"track_id", static_cast<std::int64_t>(
                                         snapshot.selected_track)}},
                       state_.session_id, original.sequence}, error);
    if (event.target.action_id == "reference.track-pad.v1" ||
        event.target.action_id == "reference.mute-pad.v1") {
        if (*pad_index >= snapshot.tracks.size()) {
            error.clear();
            return true;
        }
        const auto track_id = static_cast<std::int64_t>(
            snapshot.tracks[*pad_index].id);
        return submit({event.target.action_id == "reference.track-pad.v1"
                           ? "track.select.v1"
                           : "track.mute-toggle.v1",
                       {{"track_id", track_id}}, state_.session_id,
                       original.sequence}, error);
    }
    if (event.target.action_id == "reference.erase-pad.v1") {
        const auto step = static_cast<std::int64_t>(snapshot.step_page * 16 +
                                                    *pad_index);
        return submit({"step.erase.v1",
                       {{"pattern_id", static_cast<std::int64_t>(
                                            snapshot.selected_pattern)},
                        {"track_id", static_cast<std::int64_t>(
                                          snapshot.selected_track)},
                        {"step", step}},
                       state_.session_id, original.sequence}, error);
    }
    if (event.target.action_id == "reference.select-step.v1" ||
        event.target.action_id == "reference.extend-step-selection.v1") {
        const auto held = held_steps_.find(event.control_id);
        if (held == held_steps_.end()) {
            error = "step selection target is unavailable";
            return false;
        }
        if (event.target.action_id == "reference.select-step.v1" &&
            !submit({"step.selection-clear.v1", {}, state_.session_id,
                     original.sequence}, error))
            return false;
        return submit({"step.selection-toggle.v1",
                       {{"pattern_id", static_cast<std::int64_t>(
                                           held->second.pattern_id)},
                        {"track_id", static_cast<std::int64_t>(
                                         held->second.track_id)},
                        {"step", held->second.step}},
                       state_.session_id, original.sequence}, error);
    }
    error = "unsupported reference gesture target";
    return false;
}

void ControllerSession::synchronize_interaction() noexcept {
    state_.interaction = gestures_.state();
    state_.modifiers.shift =
        state_.interaction.effective_modifiers.contains("shift") &&
        !state_.interaction.latched_modifiers.contains("shift");
    state_.modifiers.shift_latched =
        state_.interaction.latched_modifiers.contains("shift");
    state_.modifiers.select =
        state_.interaction.effective_modifiers.contains("select") &&
        !state_.interaction.latched_modifiers.contains("select");
    state_.modifiers.select_latched =
        state_.interaction.latched_modifiers.contains("select");
}

bool ControllerSession::configure_gestures(GestureConfiguration configuration,
                                           std::string &error) {
    if (!gestures_.configure(std::move(configuration), descriptor_.controls,
                             error)) return false;
    synchronize_interaction();
    ++state_.local_revision;
    invalidate_feedback();
    return true;
}

bool ControllerSession::update_gestures(std::string &error) {
    ControllerInput synthetic;
    for (const auto &event : gestures_.update(error))
        if (!dispatch_gesture(event, synthetic, error)) return false;
    synchronize_interaction();
    return error.empty();
}

void ControllerSession::set_modal_context(std::string modal_context) {
    if (!modal_context.empty()) cancel_performance_actions();
    const auto events = gestures_.set_context(state_.context,
                                              std::move(modal_context));
    ControllerInput synthetic;
    std::string ignored;
    for (const auto &event : events)
        (void)dispatch_gesture(event, synthetic, ignored);
    held_steps_.clear();
    synchronize_interaction();
    ++state_.local_revision;
    invalidate_feedback();
}

bool ControllerSession::feedback(ControllerFeedback value) {
    if (feedback_.size() >= capacity_) {
        ++state_.dropped_feedback;
        state_.resync_required = true;
        return false;
    }
    feedback_.push_back(std::move(value));
    return true;
}

void ControllerSession::observe(const ApplicationState &snapshot,
                                bool full_refresh) {
    if (state_.interaction.modal_context !=
        snapshot.interaction.modal.identity)
        set_modal_context(snapshot.interaction.modal.identity);
    if (std::any_of(active_performance_actions_.begin(),
                    active_performance_actions_.end(), [&](const auto &entry) {
                        return entry.second.bank !=
                               snapshot.performance_banks.active_bank;
                    }))
        cancel_performance_actions();
    if (!full_refresh && !state_.resync_required &&
        state_.feedback_revision == snapshot.revision &&
        state_.delivered_local_revision == state_.local_revision)
        return;
    if (full_refresh) invalidate_feedback();
    state_.resync_required = false;
    const auto track = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                    [&](const auto &item) {
                                        return item.id == snapshot.selected_track;
                                    });
    constexpr std::uint32_t off = 0x080808, active = 0x0060b0,
                            selected = 0xff7a00, play = 0x00c840;
    for (std::size_t i = 0; i < 16; ++i) {
        auto color = off;
        if (snapshot.pad_mode == PadMode::step && track != snapshot.tracks.end()) {
            const auto tick = static_cast<MusicalTick>(snapshot.step_page * 16 + i) *
                              snapshot.step_ticks;
            if (std::any_of(track->events.begin(), track->events.end(),
                            [&](const auto &event) {
                                return event.tick == tick &&
                                       event.type == MusicalEventType::note;
                            }))
                color = active;
        } else if (snapshot.pad_mode == PadMode::pad &&
                   snapshot.slice_pad_assignments[i])
            color = active;
        if ((snapshot.pad_mode == PadMode::step &&
             snapshot.selected_step == static_cast<std::int64_t>(
                                           snapshot.step_page * 16 + i)) ||
            (snapshot.pad_mode != PadMode::step && snapshot.selected_pad == i))
            color = selected;
        if (snapshot.playing &&
            i == static_cast<std::size_t>((snapshot.transport_tick /
                                           std::max<MusicalTick>(
                                               1, snapshot.step_ticks)) %
                                          16))
            color = play;
        if (pad_colors_[i] != color) {
            if (feedback({ControllerFeedbackKind::rgb,
                          "grid." + std::to_string(i), color,
                          descriptor_.brightness_limit, {}}))
                pad_colors_[i] = color;
        }
    }
    for (const auto &mapping : profile_.bindings) {
        if (mapping.feedback_id.empty()) continue;
        const auto target = std::find_if(
            descriptor_.controls.begin(), descriptor_.controls.end(),
            [&](const auto &value) { return value.id == mapping.feedback_id; });
        if (target == descriptor_.controls.end()) continue;
        const auto supported = std::find(target->feedback.begin(),
                                         target->feedback.end(),
                                         FeedbackType::rgb_led) != target->feedback.end();
        const auto indicator = std::find(target->feedback.begin(),
                                         target->feedback.end(),
                                         FeedbackType::single_led) != target->feedback.end();
        if (!supported && !indicator) continue;
        bool enabled{};
        if (mapping.action_id == "transport.play.v1") enabled = snapshot.playing;
        else if (mapping.action_id == "transport.stop.v1") enabled = !snapshot.playing;
        else if (mapping.action_id == "transport.metronome-toggle.v1")
            enabled = snapshot.metronome_enabled || snapshot.count_in_active;
        else if (mapping.action_id == "transport.tap-tempo.v1")
            enabled = snapshot.tempo_authority == "internal";
        else if (mapping.semantic == "recording-toggle")
            enabled = snapshot.recording.state == RecordingState::recording;
        else if (mapping.semantic == "modifier-shift")
            enabled = state_.modifiers.shift || state_.modifiers.shift_latched;
        const auto color = enabled ? active : off;
        auto [cached, inserted] = feedback_colors_.try_emplace(mapping.feedback_id,
                                                               color);
        if (inserted || cached->second != color) {
            if (feedback({supported ? ControllerFeedbackKind::rgb
                                    : ControllerFeedbackKind::indicator,
                          mapping.feedback_id, color,
                          descriptor_.brightness_limit, {}}))
                cached->second = color;
            else if (inserted)
                feedback_colors_.erase(cached);
        }
    }
    auto context = resolve_controller_context(snapshot, state_.context,
                                              state_.modifiers);
    for (std::size_t i = 0; i < context.size(); ++i) {
        const auto id = (i < 8 ? "parameter." : "function.") +
                        std::to_string(i < 8 ? i : i - 8);
        const auto key = context_key(context[i]);
        const auto cached = context_cache_.find(id);
        if (cached == context_cache_.end() || cached->second != key) {
            if (feedback({ControllerFeedbackKind::context, id, 0,
                          descriptor_.brightness_limit, std::move(context[i])}))
                context_cache_[id] = key;
        }
    }
    if (!state_.resync_required) {
        state_.feedback_revision = snapshot.revision;
        state_.delivered_local_revision = state_.local_revision;
    }
}

std::optional<ControllerFeedback> ControllerSession::next_feedback() {
    if (feedback_.empty())
        return {};
    auto value = std::move(feedback_.front());
    feedback_.pop_front();
    return value;
}

void ControllerSession::reconnect() {
    cancel_performance_actions();
    feedback_.clear();
    if (gestures_.configured()) {
        const auto events = gestures_.reconnect();
        ControllerInput synthetic;
        std::string ignored;
        for (const auto &event : events)
            (void)dispatch_gesture(event, synthetic, ignored);
        held_steps_.clear();
        synchronize_interaction();
    }
    ++state_.local_revision;
    invalidate_feedback();
}

void ControllerSession::set_context(std::string context) {
    if (state_.context == context) return;
    cancel_performance_actions();
    state_.context = std::move(context);
    if (gestures_.configured()) {
        const auto events = gestures_.set_context(
            state_.context, state_.interaction.modal_context);
        ControllerInput synthetic;
        std::string ignored;
        for (const auto &event : events)
            (void)dispatch_gesture(event, synthetic, ignored);
        held_steps_.clear();
        synchronize_interaction();
    }
    ++state_.local_revision;
    state_.resync_required = true;
}

bool ControllerSession::set_binding(std::size_t index, ControlBinding binding,
                                    std::string &error) {
    if (index >= profile_.bindings.size()) {
        error = "controller binding index is out of range";
        return false;
    }
    auto candidate = profile_;
    candidate.bindings[index] = std::move(binding);
    if (!validate_controller_configuration(descriptor_, candidate,
                                           *application_, error))
        return false;
    profile_ = std::move(candidate);
    // A changed binding cannot inherit ownership from the old mapping. Reuse
    // the normal connection-generation transition so held modifiers,
    // momentary actions and queued stale input are cancelled consistently.
    reconnect();
    pickup_.clear();
    pickup_targets_.clear();
    state_.pickup.clear();
    ++state_.local_revision;
    invalidate_feedback();
    error.clear();
    return true;
}

void ControllerSession::feedback_delivery_failed() noexcept {
    feedback_.clear();
    invalidate_feedback();
}

void ControllerSession::invalidate_feedback() noexcept {
    pad_colors_.fill(~std::uint32_t{});
    feedback_colors_.clear();
    context_cache_.clear();
    state_.resync_required = true;
}

} // namespace neurogenic
