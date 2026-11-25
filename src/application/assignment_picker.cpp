#include "neurogenic/application/assignment_picker.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace neurogenic {
namespace {
std::string argument_object_type(std::string_view name) {
    if (name == "track" || name == "track_id") return "track";
    if (name == "pattern" || name == "pattern_id") return "pattern";
    if (name == "slot" || name == "slot_id") return "launch-slot";
    if (name == "scene" || name == "scene_id") return "scene";
    if (name == "macro" || name == "macro_id") return "performance-macro";
    if (name == "clip" || name == "clip_id") return "arrangement-clip";
    return {};
}

} // namespace

bool performance_safe_assignment_action(const ActionDescriptor &action) noexcept {
    if (!action.available) return false;
    static constexpr std::string_view safe[]{
        "performance.slot-launch.v1", "performance.slot-stop.v1",
        "performance.slot-retrigger.v1", "performance.scene-launch.v1",
        "performance.fill-set.v1", "performance.fill-latch.v1",
        "macro.value-set.v1", "macro.scene-recall.v1",
        "track.mute-toggle.v1", "mixer.mute-toggle.v1", "effect.bypass.v1",
        "transport.play.v1", "transport.stop.v1"};
    return std::find(std::begin(safe), std::end(safe), action.id) !=
           std::end(safe);
}

AssignmentTargetPicker::AssignmentTargetPicker(const ApplicationService &application)
    : application_(&application) {}

std::vector<AssignmentTarget>
AssignmentTargetPicker::targets(ActionFilter filter) const {
    const auto discovery = discover_application(*application_);
    std::vector<AssignmentTarget> result;
    result.reserve(discovery.parameters.size() + discovery.actions.size() + 24);
    for (const auto &parameter : discovery.parameters) {
        result.push_back({AssignmentTargetKind::parameter, "Parameters",
                          parameter.parent, parameter.id,
                          parameter.display_name, {}, parameter.writable, true, {}});
    }
    for (const auto &action : discovery.actions) {
        if (filter && !filter(action.descriptor)) continue;
        AssignmentTarget target{AssignmentTargetKind::action,
                                action.descriptor.category,
                                action.target_type,
                                action.descriptor.id,
                                action.descriptor.name,
                                action.descriptor.unavailable_reason,
                                action.descriptor.available,
                                performance_safe_assignment_action(action.descriptor),
                                {}};
        for (const auto &argument : action.descriptor.arguments) {
            AssignmentArgumentField field{argument.name, argument.name,
                                          argument.type, argument.required, {}};
            const auto object_type = argument_object_type(argument.name);
            if (!object_type.empty())
                for (const auto &object : discovery.objects)
                    if (object.type == object_type)
                        field.choices.push_back(
                            {object.id.substr(object.id.find(':') + 1),
                             object.display_name});
            target.arguments.push_back(std::move(field));
        }
        result.push_back(std::move(target));
    }
    for (std::uint32_t index = 0; index < 8; ++index) {
        result.push_back({AssignmentTargetKind::contextual_role, "Contextual roles", {},
            "context.encoder." + std::to_string(index),
            "Context encoder " + std::to_string(index + 1), {}, true, false, {}});
        result.push_back({AssignmentTargetKind::contextual_role, "Contextual roles", {},
            "context.function." + std::to_string(index),
            "Context function " + std::to_string(index + 1), {}, true, false, {}});
        result.push_back({AssignmentTargetKind::performance_role, "Performance roles", {},
            "performance.parameter." + std::to_string(index),
            "Performance parameter " + std::to_string(index + 1), {}, true, false, {}});
        result.push_back({AssignmentTargetKind::performance_role, "Performance roles", {},
            "performance.action." + std::to_string(index),
            "Performance action " + std::to_string(index + 1), {}, true, false, {}});
    }
    for (std::uint32_t index = 0; index < 16; ++index) {
        result.push_back({AssignmentTargetKind::contextual_role, "Contextual roles", {},
            "context.pad." + std::to_string(index),
            "Context pad " + std::to_string(index + 1), {}, true, false, {}});
        result.push_back({AssignmentTargetKind::performance_role, "Performance roles", {},
            "performance.pad." + std::to_string(index),
            "Performance pad " + std::to_string(index + 1), {}, true, false, {}});
    }
    result.push_back({AssignmentTargetKind::contextual_role, "Contextual roles", {},
        "focus.data", "Focused DATA", {}, true, false, {}});
    return result;
}

bool AssignmentTargetPicker::validate(const AssignmentProposal &proposal,
                                      std::string &error,
                                      ActionFilter filter) const {
    const auto available = targets(std::move(filter));
    const auto target = std::find_if(available.begin(), available.end(),
        [&](const auto &value) {
            return value.kind == proposal.kind && value.id == proposal.target_id;
        });
    if (target == available.end()) {
        error = "assignment target is unavailable or incompatible";
        return false;
    }
    if (!target->available) {
        error = target->diagnostic.empty() ? "assignment target is unavailable"
                                           : target->diagnostic;
        return false;
    }
    if (proposal.kind == AssignmentTargetKind::action)
        return application_->actions().validate(proposal.target_id,
                                                proposal.arguments, error);
    if (!proposal.arguments.empty()) {
        error = "non-action assignment does not accept typed arguments";
        return false;
    }
    error.clear();
    return true;
}

bool AssignmentTargetPicker::parse_argument(const AssignmentArgumentField &field,
                                            std::string_view text,
                                            ApplicationValue &value,
                                            std::string &error) {
    if (text.empty() && !field.required) {
        value = std::monostate{};
        error.clear();
        return true;
    }
    if (!field.choices.empty() &&
        std::none_of(field.choices.begin(), field.choices.end(),
                     [&](const auto &choice) { return choice.value == text; })) {
        error = "argument value is not one of the discovered choices";
        return false;
    }
    if (field.type == ValueType::boolean) {
        if (text == "true" || text == "1" || text == "on") value = true;
        else if (text == "false" || text == "0" || text == "off") value = false;
        else { error = "boolean argument must be true or false"; return false; }
    } else if (field.type == ValueType::integer) {
        std::int64_t parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
            error = "integer argument is invalid"; return false;
        }
        value = parsed;
    } else if (field.type == ValueType::real) {
        std::string owned(text);
        char *end{};
        const auto parsed = std::strtod(owned.c_str(), &end);
        if (!end || end != owned.c_str() + owned.size() || !std::isfinite(parsed)) {
            error = "real argument is invalid"; return false;
        }
        value = parsed;
    } else if (field.type == ValueType::text) value = std::string(text);
    else { error = "argument type is not authorable"; return false; }
    error.clear();
    return true;
}

} // namespace neurogenic
