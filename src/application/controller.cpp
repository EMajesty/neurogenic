#include "neurogenic/application/controller.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>

namespace neurogenic {
namespace {
std::optional<ControlType> control_type(std::string_view value) {
    if (value == "momentary")
        return ControlType::momentary_button;
    if (value == "toggle")
        return ControlType::toggle_button;
    if (value == "encoder")
        return ControlType::relative_encoder;
    if (value == "pot")
        return ControlType::absolute_potentiometer;
    if (value == "fader")
        return ControlType::fader;
    if (value == "crossfader")
        return ControlType::crossfader;
    if (value == "joystick")
        return ControlType::joystick;
    if (value == "touch_xy")
        return ControlType::touch_xy;
    if (value == "keyboard")
        return ControlType::keyboard;
    if (value == "pointer")
        return ControlType::pointer;
    return std::nullopt;
}
std::string_view control_type_name(ControlType value) {
    switch (value) {
    case ControlType::momentary_button: return "momentary";
    case ControlType::toggle_button: return "toggle";
    case ControlType::relative_encoder: return "encoder";
    case ControlType::absolute_potentiometer: return "pot";
    case ControlType::fader: return "fader";
    case ControlType::crossfader: return "crossfader";
    case ControlType::joystick: return "joystick";
    case ControlType::touch_xy: return "touch_xy";
    case ControlType::keyboard: return "keyboard";
    case ControlType::pointer: return "pointer";
    }
    return "momentary";
}
std::vector<std::string> split(std::string_view line) {
    std::vector<std::string> result;
    std::size_t start{};
    while (start <= line.size()) {
        const auto end = line.find('|', start);
        result.emplace_back(line.substr(start, end == std::string_view::npos
                                                   ? line.size() - start
                                                   : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return result;
}
ControlCapability control(std::string id, ControlType type,
                          std::string label = {},
                          std::vector<FeedbackType> feedback = {}) {
    return {std::move(id), std::move(label), type, std::move(feedback)};
}
ControlBinding action_binding(std::string control_id, std::string action_id,
                              std::string feedback = {}) {
    ControlBinding value;
    value.control_id = std::move(control_id);
    value.action_id = std::move(action_id);
    value.feedback_id = std::move(feedback);
    return value;
}
ControlBinding parameter_binding(std::string control_id,
                                 std::string parameter_id) {
    ControlBinding value;
    value.control_id = std::move(control_id);
    value.parameter_id = std::move(parameter_id);
    return value;
}
} // namespace

bool validate_profile(const ControllerProfile &profile,
                      const ActionRegistry &actions, std::string &error) {
    if (profile.capabilities.controller_id.empty() ||
        profile.capabilities.name.empty()) {
        error = "controller ID and name are required";
        return false;
    }
    std::set<std::string> controls;
    for (const auto &value : profile.capabilities.controls)
        if (value.id.empty() || !controls.insert(value.id).second) {
            error = "control IDs must be nonempty and unique";
            return false;
        }
    for (const auto &binding : profile.bindings) {
        if (!controls.contains(binding.control_id)) {
            error = "binding references unknown control: " + binding.control_id;
            return false;
        }
        const auto target_count = (!binding.action_id.empty() ? 1 : 0) +
                                  (!binding.parameter_id.empty() ? 1 : 0) +
                                  (!binding.semantic.empty() ? 1 : 0) +
                                  (binding.explicitly_unassigned ? 1 : 0);
        if (target_count != 1) {
            error = "binding must name exactly one action, parameter, semantic "
                    "role or explicit unassigned state";
            return false;
        }
        if (!binding.action_id.empty()) {
            const auto *descriptor = actions.find(binding.action_id);
            if (!descriptor) {
                error = "binding references unknown action: " + binding.action_id;
                return false;
            }
            const auto capability = std::find_if(
                profile.capabilities.controls.begin(),
                profile.capabilities.controls.end(), [&](const auto &value) {
                    return value.id == binding.control_id;
                });
            const bool relative_value =
                capability != profile.capabilities.controls.end() &&
                capability->type == ControlType::relative_encoder &&
                descriptor->arguments.size() == 1 &&
                descriptor->arguments.front().type == ValueType::real &&
                !binding.arguments.contains(descriptor->arguments.front().name);
            if (!relative_value &&
                !actions.validate(binding.action_id, binding.arguments, error)) {
                error = "invalid bound arguments for " + binding.action_id +
                        ": " + error;
                return false;
            }
        }
    }
    error.clear();
    return true;
}

bool validate_device_descriptor(const ControllerDeviceDescriptor &descriptor,
                                std::string &error) {
    if (descriptor.schema_version != 1 || descriptor.descriptor_id.empty() ||
        descriptor.device_id.empty() || descriptor.display_name.empty()) {
        error = "descriptor schema, identity and display name are required";
        return false;
    }
    if (!descriptor.maximum_feedback_queue) {
        error = "descriptor feedback queue must be bounded";
        return false;
    }
    std::set<std::string> controls;
    for (const auto &control : descriptor.controls) {
        if (control.id.empty() || !controls.insert(control.id).second ||
            !std::isfinite(control.minimum) ||
            !std::isfinite(control.maximum) ||
            control.maximum < control.minimum) {
            error = "descriptor controls require unique IDs and valid ranges";
            return false;
        }
    }
    error.clear();
    return true;
}

ControllerDeviceDescriptor rp2040_controller_descriptor() {
    ControllerDeviceDescriptor descriptor;
    descriptor.descriptor_id = "neurogenic.rp2040.task068.v1";
    descriptor.device_id = "neurogenic.rp2040";
    descriptor.display_name = "NEUROGENIC dedicated controller";
    descriptor.transport = ControllerTransport::usb_cdc;
    descriptor.protocol_major = 1;
    descriptor.protocol_minor = 2;
    auto add = [&](std::string id, std::string label, ControlType type,
                   std::vector<FeedbackType> feedback = {}) {
        descriptor.controls.push_back(
            control(std::move(id), type, std::move(label), std::move(feedback)));
    };
    for (int i = 0; i < 16; ++i)
        add("grid." + std::to_string(i), "Performance " + std::to_string(i + 1),
            ControlType::momentary_button, {FeedbackType::rgb_led});
    for (int i = 0; i < 8; ++i) {
        add("parameter." + std::to_string(i),
            "Context Encoder " + std::to_string(i + 1),
            ControlType::relative_encoder, {FeedbackType::parameter_value});
        add("function." + std::to_string(i),
            "Context Function " + std::to_string(i + 1),
            ControlType::momentary_button, {FeedbackType::single_led});
        add("page." + std::to_string(i), "Page " + std::to_string(i + 1),
            ControlType::momentary_button, {FeedbackType::single_led});
    }
    add("data", "Main Data Encoder", ControlType::relative_encoder,
        {FeedbackType::parameter_value});
    for (int i = 0; i < 10; ++i) {
        add("edit." + std::to_string(i), "Edit " + std::to_string(i + 1),
            ControlType::momentary_button, {FeedbackType::single_led});
        add("performance." + std::to_string(i),
            "Performance Cluster " + std::to_string(i + 1),
            ControlType::momentary_button, {FeedbackType::single_led});
    }
    static constexpr std::string_view navigation[]{"up", "down", "left",
                                                    "right", "confirm", "back"};
    for (const auto name : navigation)
        add("navigation." + std::string(name), "Navigation " + std::string(name),
            ControlType::momentary_button, {FeedbackType::single_led});
    return descriptor;
}

ControllerDeviceDescriptor legacy_controller_descriptor(
    std::uint8_t protocol_minor, std::uint32_t capabilities) {
    auto profile = legacy_performance_profile(protocol_minor, capabilities);
    ControllerDeviceDescriptor descriptor;
    descriptor.descriptor_id = "neurogenic.controller.protocol-1." +
                               std::to_string(protocol_minor);
    descriptor.device_id = "neurogenic.protocol-1x";
    descriptor.display_name = "NEUROGENIC legacy controller";
    descriptor.transport = ControllerTransport::usb_cdc;
    descriptor.protocol_major = 1;
    descriptor.protocol_minor = protocol_minor;
    descriptor.controls = profile.capabilities.controls;
    return descriptor;
}

ControllerMapper::ControllerMapper(ControllerProfile profile,
                                   const ActionRegistry &actions,
                                   std::string &error)
    : profile_(std::move(profile)), actions_(&actions) {
    valid_ = validate_profile(profile_, actions, error);
}
ResolvedInput ControllerMapper::resolve(const ControllerInput &input,
                                        const ModifierState &modifiers,
                                        std::string &error) const {
    ResolvedInput result;
    if (!valid_) {
        error = "controller profile is invalid";
        return result;
    }
    const auto control_it = std::find_if(
        profile_.capabilities.controls.begin(),
        profile_.capabilities.controls.end(),
        [&](const auto &value) { return value.id == input.control_id; });
    if (control_it == profile_.capabilities.controls.end()) {
        error = "unknown controller input";
        return result;
    }
    const auto shifted = modifiers.shift || modifiers.shift_latched,
               selected = modifiers.select || modifiers.select_latched;
    auto binding =
        std::find_if(profile_.bindings.begin(), profile_.bindings.end(),
                     [&](const auto &value) {
                         return value.control_id == input.control_id &&
                                value.shift == shifted &&
                                value.select == selected;
                     });
    if (binding == profile_.bindings.end() && profile_.fallback_to_base &&
        (shifted || selected))
        binding =
            std::find_if(profile_.bindings.begin(), profile_.bindings.end(),
                         [&](const auto &value) {
                             return value.control_id == input.control_id &&
                                    !value.shift && !value.select;
                         });
    if (binding == profile_.bindings.end()) {
        error = "control has no binding in the active modifier layer";
        return result;
    }
    if (!binding->action_id.empty()) {
        const auto *descriptor = actions_->find(binding->action_id);
        if (!descriptor || !descriptor->available) {
            error =
                descriptor ? descriptor->unavailable_reason : "unknown action";
            return result;
        }
        ApplicationCommand command;
        command.action_id = binding->action_id;
        command.arguments = binding->arguments;
        command.source_id = profile_.capabilities.controller_id;
        command.source_sequence = input.sequence;
        if (descriptor->arguments.empty() ||
            command.arguments.size() == descriptor->arguments.size())
            result.action = std::move(command);
        else if (descriptor->arguments.size() == 1 &&
                 descriptor->arguments[0].type == ValueType::real &&
                 control_it->type == ControlType::relative_encoder) {
            command.arguments.insert_or_assign(descriptor->arguments[0].name,
                                               input.value);
            result.action = std::move(command);
        } else {
            error =
                "mapped action requires arguments not supplied by this binding";
            return result;
        }
    } else {
        result.parameter_id = binding->parameter_id;
        if (control_it->type == ControlType::relative_encoder)
            result.relative_steps = input.value;
        else {
            result.parameter_value = std::clamp(input.value, 0.0, 1.0);
        }
    }
    error.clear();
    return result;
}
std::vector<FeedbackMessage>
ControllerMapper::feedback(const ApplicationState &state,
                           std::string_view origin) const {
    std::vector<FeedbackMessage> result;
    for (const auto &binding : profile_.bindings) {
        if (binding.feedback_id.empty() || binding.feedback_id == origin)
            continue;
        ApplicationValue value;
        if (binding.action_id == "transport.play.v1")
            value = state.playing;
        else if (binding.action_id == "pad.mode-select.v1")
            value = std::string(pad_mode_name(state.pad_mode));
        else if (binding.action_id.rfind("track.", 0) == 0)
            value = static_cast<std::int64_t>(state.selected_track);
        else if (binding.action_id.rfind("sample.", 0) == 0)
            value = static_cast<std::int64_t>(state.selected_sample_slot.value);
        else if (!binding.parameter_id.empty()) {
            const auto parameter =
                std::find_if(state.parameters.begin(), state.parameters.end(),
                             [&](const auto &item) {
                                 return item.id == binding.parameter_id;
                             });
            if (parameter == state.parameters.end())
                continue;
            value = parameter->current_value;
        } else
            value = state.focused_element;
        result.push_back({binding.feedback_id, std::move(value),
                          std::string(origin), state.revision});
    }
    return result;
}

bool parse_controller_profile(std::string_view source,
                              ControllerProfile &profile, std::string &error) {
    ControllerProfile parsed;
    std::istringstream input{std::string(source)};
    std::string line;
    std::size_t number{};
    while (std::getline(input, line)) {
        ++number;
        if (line.empty() || line[0] == '#')
            continue;
        auto fields = split(line);
        if (fields[0] == "controller" && fields.size() == 3) {
            parsed.capabilities.controller_id = fields[1];
            parsed.capabilities.name = fields[2];
        } else if (fields[0] == "profile" && fields.size() == 4) {
            std::uint32_t schema{};
            const auto text = std::string_view(fields[1]);
            const auto result = std::from_chars(text.data(),
                                                text.data() + text.size(), schema);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                schema == 0 || schema > 2) {
                error = "line " + std::to_string(number) +
                        ": unsupported profile schema";
                return false;
            }
            parsed.schema_version = schema;
            parsed.profile_id = fields[2];
            parsed.device_id = fields[3];
        } else if (fields[0] == "control" && fields.size() >= 4) {
            const auto type = control_type(fields[2]);
            if (!type) {
                error =
                    "line " + std::to_string(number) + ": unknown control type";
                return false;
            }
            ControlCapability descriptor{fields[1], fields[3], *type, {}};
            for (std::size_t i = 4; i < fields.size(); ++i) {
                if (fields[i] == "feedback=rgb")
                    descriptor.feedback.push_back(FeedbackType::rgb_led);
                else if (fields[i] == "feedback=indicator")
                    descriptor.feedback.push_back(FeedbackType::single_led);
                else if (fields[i] == "feedback=text")
                    descriptor.feedback.push_back(FeedbackType::text_display);
                else if (fields[i] == "feedback=value")
                    descriptor.feedback.push_back(FeedbackType::parameter_value);
                else if (fields[i] == "feedback=motorized")
                    descriptor.feedback.push_back(FeedbackType::motorized_fader);
                else {
                    error = "line " + std::to_string(number) +
                            ": unknown control capability option";
                    return false;
                }
            }
            parsed.capabilities.controls.push_back(std::move(descriptor));
        } else if (fields[0] == "bind" && fields.size() >= 4) {
            ControlBinding binding;
            binding.control_id = fields[1];
            if (fields[2] == "action")
                binding.action_id = fields[3];
            else if (fields[2] == "parameter")
                binding.parameter_id = fields[3];
            else if (fields[2] == "semantic")
                binding.semantic = fields[3];
            else if (fields[2] == "unassigned")
                binding.explicitly_unassigned = true;
            else {
                error = "line " + std::to_string(number) +
                        ": binding target must be action or parameter";
                return false;
            }
            for (std::size_t i = 4; i < fields.size(); ++i) {
                if (fields[i] == "shift")
                    binding.shift = true;
                else if (fields[i] == "select")
                    binding.select = true;
                else if (fields[i].rfind("feedback=", 0) == 0)
                    binding.feedback_id = fields[i].substr(9);
                else if (fields[i].rfind("label=", 0) == 0)
                    binding.label = fields[i].substr(6);
                else if (fields[i].rfind("context=", 0) == 0)
                    binding.context = fields[i].substr(8);
                else if (fields[i].rfind("index=", 0) == 0) {
                    const auto value = std::string_view(fields[i]).substr(6);
                    const auto result = std::from_chars(
                        value.data(), value.data() + value.size(), binding.index);
                    if (result.ec != std::errc{} ||
                        result.ptr != value.data() + value.size()) {
                        error = "line " + std::to_string(number) +
                                ": invalid binding index";
                        return false;
                    }
                }
                else if (fields[i] == "pickup")
                    binding.pickup = true;
                else if (fields[i] == "invert")
                    binding.invert = true;
                else if (fields[i].rfind("fine=", 0) == 0 ||
                         fields[i].rfind("acceleration=", 0) == 0) {
                    const bool fine = fields[i].rfind("fine=", 0) == 0;
                    const auto value = std::string_view(fields[i]).substr(
                        fine ? 5 : 13);
                    double parsed_value{};
                    const auto converted = std::from_chars(
                        value.data(), value.data() + value.size(), parsed_value);
                    if (converted.ec != std::errc{} ||
                        converted.ptr != value.data() + value.size() ||
                        !std::isfinite(parsed_value) || parsed_value <= 0) {
                        error = "line " + std::to_string(number) +
                                ": invalid binding scale";
                        return false;
                    }
                    (fine ? binding.fine_scale : binding.acceleration) =
                        parsed_value;
                }
                else if (fields[i].rfind("arg:", 0) == 0) {
                    const auto separator = fields[i].find('=', 4);
                    if (separator == std::string::npos || separator + 3 > fields[i].size() ||
                        fields[i][separator + 2] != ':') {
                        error = "line " + std::to_string(number) +
                                ": invalid static action argument";
                        return false;
                    }
                    const auto name = fields[i].substr(4, separator - 4);
                    const auto kind = fields[i][separator + 1];
                    const auto value = std::string_view(fields[i]).substr(separator + 3);
                    if (name.empty()) {
                        error = "line " + std::to_string(number) +
                                ": empty static argument name";
                        return false;
                    }
                    if (kind == 's') binding.arguments[name] = std::string(value);
                    else if (kind == 'b' && (value == "0" || value == "1"))
                        binding.arguments[name] = value == "1";
                    else if (kind == 'i') {
                        std::int64_t parsed_value{};
                        const auto converted = std::from_chars(
                            value.data(), value.data() + value.size(), parsed_value);
                        if (converted.ec != std::errc{} ||
                            converted.ptr != value.data() + value.size()) {
                            error = "line " + std::to_string(number) +
                                    ": invalid integer action argument";
                            return false;
                        }
                        binding.arguments[name] = parsed_value;
                    } else if (kind == 'd') {
                        double parsed_value{};
                        const auto converted = std::from_chars(
                            value.data(), value.data() + value.size(), parsed_value);
                        if (converted.ec != std::errc{} ||
                            converted.ptr != value.data() + value.size() ||
                            !std::isfinite(parsed_value)) {
                            error = "line " + std::to_string(number) +
                                    ": invalid real action argument";
                            return false;
                        }
                        binding.arguments[name] = parsed_value;
                    } else {
                        error = "line " + std::to_string(number) +
                                ": unsupported action argument type";
                        return false;
                    }
                }
                else {
                    error = "line " + std::to_string(number) +
                            ": unknown binding option";
                    return false;
                }
            }
            parsed.bindings.push_back(std::move(binding));
        } else {
            error =
                "line " + std::to_string(number) + ": invalid profile record";
            return false;
        }
    }
    if (parsed.capabilities.controller_id.empty()) {
        error = "profile has no controller record";
        return false;
    }
    profile = std::move(parsed);
    error.clear();
    return true;
}

std::string serialize_controller_profile(const ControllerProfile &profile) {
    std::ostringstream output;
    output << "# neurogenic controller profile schema " << profile.schema_version
           << '\n';
    output << "profile|" << profile.schema_version << '|' << profile.profile_id
           << '|' << profile.device_id << '\n';
    output << "controller|" << profile.capabilities.controller_id << '|'
           << profile.capabilities.name << '\n';
    for (const auto &value : profile.capabilities.controls) {
        output << "control|" << value.id << '|'
               << control_type_name(value.type) << '|' << value.label;
        for (const auto feedback : value.feedback) {
            output << "|feedback=";
            switch (feedback) {
            case FeedbackType::rgb_led: output << "rgb"; break;
            case FeedbackType::single_led: output << "indicator"; break;
            case FeedbackType::text_display: output << "text"; break;
            case FeedbackType::parameter_value: output << "value"; break;
            case FeedbackType::motorized_fader: output << "motorized"; break;
            }
        }
        output << '\n';
    }
    for (const auto &binding : profile.bindings) {
        output << "bind|" << binding.control_id << '|';
        if (!binding.action_id.empty())
            output << "action|" << binding.action_id;
        else if (!binding.parameter_id.empty())
            output << "parameter|" << binding.parameter_id;
        else if (!binding.semantic.empty())
            output << "semantic|" << binding.semantic;
        else
            output << "unassigned|-";
        if (binding.shift) output << "|shift";
        if (binding.select) output << "|select";
        if (!binding.feedback_id.empty())
            output << "|feedback=" << binding.feedback_id;
        if (!binding.label.empty()) output << "|label=" << binding.label;
        if (!binding.context.empty()) output << "|context=" << binding.context;
        if (binding.index >= 0) output << "|index=" << binding.index;
        if (binding.pickup) output << "|pickup";
        if (binding.invert) output << "|invert";
        if (binding.fine_scale != 0.1)
            output << "|fine=" << binding.fine_scale;
        if (binding.acceleration != 2.0)
            output << "|acceleration=" << binding.acceleration;
        for (const auto &[name, value] : binding.arguments) {
            output << "|arg:" << name << '=';
            if (const auto *text = std::get_if<std::string>(&value))
                output << "s:" << *text;
            else if (const auto *boolean = std::get_if<bool>(&value))
                output << "b:" << (*boolean ? 1 : 0);
            else if (const auto *integer = std::get_if<std::int64_t>(&value))
                output << "i:" << *integer;
            else if (const auto *real = std::get_if<double>(&value))
                output << "d:" << *real;
            else
                output << "s:";
        }
        output << '\n';
    }
    return output.str();
}

bool validate_controller_configuration(
    const ControllerDeviceDescriptor &descriptor,
    const ControllerProfile &profile, const ApplicationService &application,
    std::string &error) {
    if (!validate_device_descriptor(descriptor, error) ||
        !validate_profile(profile, application.actions(), error))
        return false;
    if (!profile.device_id.empty() && profile.device_id != descriptor.device_id) {
        error = "profile targets a different device identity";
        return false;
    }
    for (const auto &declared : profile.capabilities.controls) {
        const auto physical = std::find_if(
            descriptor.controls.begin(), descriptor.controls.end(),
            [&](const auto &value) { return value.id == declared.id; });
        if (physical == descriptor.controls.end()) {
            error = "profile control is absent from device descriptor: " +
                    declared.id;
            return false;
        }
        if (physical->type != declared.type ||
            declared.minimum < physical->minimum ||
            declared.maximum > physical->maximum ||
            std::any_of(declared.feedback.begin(), declared.feedback.end(),
                        [&](const auto value) {
                            return std::find(physical->feedback.begin(),
                                             physical->feedback.end(), value) ==
                                   physical->feedback.end();
                        })) {
            error = "profile control exceeds device capabilities: " +
                    declared.id;
            return false;
        }
    }
    const auto snapshot = application.snapshot();
    for (const auto &binding : profile.bindings) {
        const auto device_control = std::find_if(
            descriptor.controls.begin(), descriptor.controls.end(),
            [&](const auto &value) { return value.id == binding.control_id; });
        if (device_control == descriptor.controls.end()) {
            error = "profile control is absent from device descriptor: " +
                    binding.control_id;
            return false;
        }
        if (!device_control->available && !binding.explicitly_unassigned) {
            error = "profile binds an unavailable device control: " +
                    binding.control_id;
            return false;
        }
        if (!binding.feedback_id.empty() &&
            !binding.feedback_id.starts_with("parameter.display.")) {
            const auto feedback = std::find_if(
                descriptor.controls.begin(), descriptor.controls.end(),
                [&](const auto &value) { return value.id == binding.feedback_id; });
            if (feedback == descriptor.controls.end() ||
                feedback->feedback.empty()) {
                error = "profile feedback destination is absent or has no "
                        "supported feedback capability: " + binding.feedback_id;
                return false;
            }
        }
        if (!binding.parameter_id.empty() &&
            std::none_of(snapshot.parameters.begin(), snapshot.parameters.end(),
                         [&](const auto &value) {
                             return value.id == binding.parameter_id;
                         })) {
            error = "profile references unavailable parameter: " +
                    binding.parameter_id;
            return false;
        }
    }
    error.clear();
    return true;
}

ControllerProfile full_performance_profile() {
    ControllerProfile profile;
    profile.profile_id = "neurogenic.rp2040.task068.default.v2";
    profile.device_id = "neurogenic.rp2040";
    profile.capabilities = {
        "neurogenic.full.v1", "Full performance controller", {}, true};
    for (int i = 0; i < 16; ++i)
        profile.capabilities.controls.push_back(
            control("grid." + std::to_string(i), ControlType::momentary_button,
                    "Grid " + std::to_string(i + 1), {FeedbackType::rgb_led}));
    for (int i = 0; i < 8; ++i)
        profile.capabilities.controls.push_back(control(
            "parameter." + std::to_string(i), ControlType::relative_encoder,
            "Parameter " + std::to_string(i + 1),
            {FeedbackType::parameter_value}));
    for (int i = 0; i < 8; ++i) {
        profile.capabilities.controls.push_back(control(
            "function." + std::to_string(i), ControlType::momentary_button,
            "Context Function " + std::to_string(i + 1),
            {FeedbackType::single_led}));
        profile.capabilities.controls.push_back(control(
            "page." + std::to_string(i), ControlType::momentary_button,
            "Page " + std::to_string(i + 1), {FeedbackType::single_led}));
    }
    for (int i = 0; i < 10; ++i) {
        profile.capabilities.controls.push_back(control(
            "edit." + std::to_string(i), ControlType::momentary_button,
            "Edit " + std::to_string(i + 1), {FeedbackType::single_led}));
        profile.capabilities.controls.push_back(control(
            "performance." + std::to_string(i),
            ControlType::momentary_button,
            "Performance " + std::to_string(i + 1),
            {FeedbackType::single_led}));
    }
    static constexpr std::string_view navigation[]{"up", "down", "left",
                                                    "right", "confirm", "back"};
    for (const auto name : navigation)
        profile.capabilities.controls.push_back(control(
            "navigation." + std::string(name), ControlType::momentary_button,
            "Navigation " + std::string(name), {FeedbackType::single_led}));
    profile.capabilities.controls.push_back(
        control("data", ControlType::relative_encoder, "Data"));
    auto data = parameter_binding("data", "transport.tempo");
    data.feedback_id = "parameter.display.0";
    profile.bindings = {std::move(data)};
    ControlBinding recording;
    recording.control_id = "performance.2";
    recording.semantic = "recording-toggle";
    profile.bindings.push_back(std::move(recording));
    ControlBinding shift;
    shift.control_id = "performance.9";
    shift.semantic = "modifier-shift";
    profile.bindings.push_back(std::move(shift));
    ControlBinding select;
    select.control_id = "edit.9";
    select.semantic = "modifier-select";
    profile.bindings.push_back(std::move(select));
    profile.bindings.push_back(
        action_binding("performance.0", "transport.play.v1", "performance.0"));
    profile.bindings.push_back(
        action_binding("performance.1", "transport.stop.v1", "performance.1"));
    for (int i = 0; i < 16; ++i) {
        ControlBinding binding;
        binding.control_id = "grid." + std::to_string(i);
        binding.semantic = "performance-pad";
        binding.index = i;
        profile.bindings.push_back(std::move(binding));
    }
    for (int i = 0; i < 8; ++i) {
        ControlBinding encoder;
        encoder.control_id = "parameter." + std::to_string(i);
        encoder.semantic = "context-encoder";
        encoder.index = i;
        profile.bindings.push_back(std::move(encoder));
        ControlBinding function;
        function.control_id = "function." + std::to_string(i);
        function.semantic = "context-button";
        function.index = i;
        profile.bindings.push_back(std::move(function));
    }
    static constexpr std::string_view contexts[]{
        "performance", "sequencer", "samples", "sound",
        "mixer", "fx", "browser", "setup"};
    for (int i = 0; i < 8; ++i) {
        ControlBinding page;
        page.control_id = "page." + std::to_string(i);
        page.semantic = "local-context";
        page.context = contexts[i];
        profile.bindings.push_back(std::move(page));
    }
    for (int i = 0; i < 10; ++i) {
        if (i == 9) continue;
        ControlBinding edit, performance;
        edit.control_id = "edit." + std::to_string(i);
        edit.explicitly_unassigned = true;
        performance.control_id = "performance." + std::to_string(i);
        if (i != 0 && i != 1 && i != 2)
            performance.explicitly_unassigned = true;
        profile.bindings.push_back(std::move(edit));
        if (performance.explicitly_unassigned)
            profile.bindings.push_back(std::move(performance));
    }
    const std::pair<std::string_view, std::string_view> navigation_bindings[]{
        {"up", "navigation.focus-previous.v1"},
        {"down", "navigation.focus-next.v1"},
        {"left", "step.page-previous.v1"}, {"right", "step.page-next.v1"},
        {"confirm", "navigation.confirm.v1"},
        {"back", "navigation.cancel.v1"}};
    for (const auto &[control_name, action] : navigation_bindings)
        profile.bindings.push_back(action_binding(
            "navigation." + std::string(control_name), std::string(action)));
    return profile;
}

ControllerProfile legacy_performance_profile(std::uint8_t protocol_minor,
                                             std::uint32_t capabilities) {
    auto profile = full_performance_profile();
    profile.profile_id = "neurogenic.protocol-1." +
                         std::to_string(protocol_minor) + ".default";
    profile.device_id = "neurogenic.protocol-1x";
    const auto retained = [](std::string_view id) {
        return id == "data" || id.starts_with("grid.") ||
               id.starts_with("parameter.") || id.starts_with("function.");
    };
    std::erase_if(profile.capabilities.controls,
                  [&](const auto &value) { return !retained(value.id); });
    std::erase_if(profile.bindings,
                  [&](const auto &value) { return !retained(value.control_id); });
    const auto add_control = [&](std::string id,
                                 ControlType type = ControlType::momentary_button) {
        profile.capabilities.controls.push_back(
            control(std::move(id), type, "Legacy protocol control",
                    {FeedbackType::single_led}));
    };
    for (const auto id : {"play", "stop", "record", "shift", "select",
                          "data.push", "mode.step", "mode.pad", "mode.levels",
                          "mode.notes", "page.previous", "page.next"})
        add_control(id);
    profile.bindings.push_back(action_binding("play", "transport.play.v1"));
    profile.bindings.push_back(action_binding("stop", "transport.stop.v1"));
    profile.bindings.push_back(
        action_binding("data.push", "navigation.confirm.v1"));
    profile.bindings.push_back(
        action_binding("page.previous", "step.page-previous.v1"));
    profile.bindings.push_back(action_binding("page.next", "step.page-next.v1"));
    for (const auto &[id, semantic] :
         std::array<std::pair<std::string_view, std::string_view>, 3>{
             {{"record", "recording-toggle"}, {"shift", "modifier-shift"},
              {"select", "modifier-select"}}}) {
        ControlBinding binding;
        binding.control_id = id;
        binding.semantic = semantic;
        profile.bindings.push_back(std::move(binding));
    }
    for (const auto &[id, mode] :
         std::array<std::pair<std::string_view, std::string_view>, 4>{
             {{"mode.step", "step"}, {"mode.pad", "pad"},
              {"mode.levels", "levels"}, {"mode.notes", "notes"}}}) {
        ControlBinding binding;
        binding.control_id = id;
        binding.action_id = "pad.mode-select.v1";
        binding.arguments["mode"] = std::string(mode);
        profile.bindings.push_back(std::move(binding));
    }
    if (capabilities & (1u << 2))
        for (std::size_t i = 0; i < 5; ++i) {
            add_control("analog." + std::to_string(i),
                        i == 4 ? ControlType::crossfader : ControlType::fader);
            ControlBinding binding;
            binding.control_id = "analog." + std::to_string(i);
            binding.parameter_id = std::array<std::string, 5>{
                "mixer.selected.gain", "mixer.track.aux-1", "mixer.track.aux-2",
                "mixer.master.gain", "mixer.external.selected.wet"}[i];
            binding.pickup = true;
            profile.bindings.push_back(std::move(binding));
        }
    return profile;
}
ControllerProfile minimal_controller_profile() {
    ControllerProfile profile;
    profile.capabilities = {
        "neurogenic.minimal.v1",
        "Minimal controller",
        {control("data", ControlType::relative_encoder, "Data",
                 {FeedbackType::parameter_value}),
         control("enter", ControlType::momentary_button, "Enter"),
         control("back", ControlType::momentary_button, "Back"),
         control("display", ControlType::pointer, "Display",
                 {FeedbackType::text_display}),
         control("joystick", ControlType::joystick, "X/Y")},
        false};
    profile.bindings = {
        action_binding("data", "navigation.adjust.v1", "display"),
        action_binding("enter", "navigation.confirm.v1", "display"),
        action_binding("back", "navigation.cancel.v1", "display")};
    auto preview = action_binding("enter", "slice.preview.v1", "display");
    preview.shift = true;
    auto stop_preview =
        action_binding("back", "slice.preview-stop.v1", "display");
    stop_preview.shift = true;
    profile.bindings.push_back(std::move(preview));
    profile.bindings.push_back(std::move(stop_preview));
    return profile;
}
ControllerProfile generic_midi_profile() {
    ControllerProfile profile;
    profile.capabilities = {
        "neurogenic.midi.v1",
        "Generic MIDI controller",
        {control("button.1", ControlType::momentary_button),
         control("button.2", ControlType::momentary_button),
         control("knob.1", ControlType::absolute_potentiometer),
         control("encoder.1", ControlType::relative_encoder),
         control("fader.1", ControlType::fader)},
        false};
    profile.bindings = {action_binding("button.1", "transport.play.v1"),
                        action_binding("button.2", "transport.stop.v1"),
                        parameter_binding("knob.1", "performance.xy.x"),
                        parameter_binding("encoder.1", "transport.tempo"),
                        parameter_binding("fader.1", "performance.xy.y")};
    return profile;
}

GridEvent PerformanceGrid::press(std::size_t pad, bool select_only) const {
    if (pad >= 16)
        return {GridEventType::unsupported, {}, 0, 0, {}, 0,
                "grid index must be 0..15"};
    switch (mode_) {
    case PadMode::step:
        return {select_only ? GridEventType::select_step
                            : GridEventType::toggle_step,
                page_ * 16 + pad,
                0,
                0,
                {},
                0,
                {}};
    case PadMode::pad:
        return {GridEventType::trigger_pad,
                pad,
                static_cast<std::uint8_t>(36 + pad),
                127,
                SampleSlotId{static_cast<std::uint32_t>(pad)},
                0,
                "assigned slices take precedence over the default sample "
                "slot mapping"};
    case PadMode::levels:
        return {GridEventType::trigger_level,
                selected_pad_,
                static_cast<std::uint8_t>(36 + selected_pad_),
                static_cast<std::uint8_t>(
                    std::min<std::size_t>(127, (pad + 1) * 8)),
                slot_,
                0,
                "sixteen velocity levels target the selected pad"};
    case PadMode::notes:
        if (root_ > 112)
            return {GridEventType::unsupported,
                    pad,
                    0,
                    0,
                    {},
                    0,
                    "16-note layout exceeds MIDI note range"};
        return {GridEventType::trigger_note,
                selected_pad_,
                static_cast<std::uint8_t>(36 + selected_pad_),
                127,
                slot_,
                static_cast<float>(static_cast<int>(root_) - 60 +
                                   static_cast<int>(pad)),
                "chromatic playback-rate layout targets the selected pad"};
    }
    return {};
}

std::optional<ApplicationCommand>
grid_application_command(const GridEvent &event, std::string source_id,
                         std::uint64_t sequence) {
    ApplicationCommand command;
    command.source_id = std::move(source_id);
    command.source_sequence = sequence;
    if (event.type == GridEventType::toggle_step)
        command.action_id = "step.toggle.v1";
    else if (event.type == GridEventType::select_step)
        command.action_id = "step.event-select.v1";
    else if (event.type == GridEventType::trigger_pad ||
             event.type == GridEventType::trigger_level ||
             event.type == GridEventType::trigger_note) {
        command.action_id = "pad.trigger.v1";
        command.arguments.emplace("pad",
                                  static_cast<std::int64_t>(event.index));
        command.arguments.emplace("velocity",
                                  static_cast<double>(event.velocity) / 127.0);
        if (event.pitch_offset != 0)
            command.arguments.emplace("pitch_offset",
                                      static_cast<double>(event.pitch_offset));
        return command;
    } else
        return std::nullopt;
    command.arguments.emplace("step", static_cast<std::int64_t>(event.index));
    return command;
}
} // namespace neurogenic
