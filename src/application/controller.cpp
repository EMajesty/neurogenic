#include "neurogenic/application/controller.hpp"
#include <algorithm>
#include <charconv>
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
        if (binding.action_id.empty() == binding.parameter_id.empty()) {
            error = "binding must name exactly one action or parameter";
            return false;
        }
        if (!binding.action_id.empty() && !actions.find(binding.action_id)) {
            error = "binding references unknown action: " + binding.action_id;
            return false;
        }
    }
    error.clear();
    return true;
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
        command.source_id = profile_.capabilities.controller_id;
        command.source_sequence = input.sequence;
        if (descriptor->arguments.empty())
            result.action = std::move(command);
        else if (descriptor->arguments.size() == 1 &&
                 descriptor->arguments[0].type == ValueType::real &&
                 control_it->type == ControlType::relative_encoder) {
            command.arguments.emplace(descriptor->arguments[0].name,
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
        } else if (fields[0] == "control" && fields.size() >= 4) {
            const auto type = control_type(fields[2]);
            if (!type) {
                error =
                    "line " + std::to_string(number) + ": unknown control type";
                return false;
            }
            parsed.capabilities.controls.push_back(
                {fields[1], fields[3], *type, {}});
        } else if (fields[0] == "bind" && fields.size() >= 4) {
            ControlBinding binding;
            binding.control_id = fields[1];
            if (fields[2] == "action")
                binding.action_id = fields[3];
            else if (fields[2] == "parameter")
                binding.parameter_id = fields[3];
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

ControllerProfile full_performance_profile() {
    ControllerProfile profile;
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
    profile.capabilities.controls.insert(
        profile.capabilities.controls.end(),
        {control("data", ControlType::relative_encoder, "Data"),
         control("play", ControlType::momentary_button, "Play",
                 {FeedbackType::single_led}),
         control("stop", ControlType::momentary_button, "Stop",
                 {FeedbackType::single_led}),
         control("shift", ControlType::momentary_button),
         control("select", ControlType::momentary_button),
         control("mode.step", ControlType::momentary_button),
         control("mode.pad", ControlType::momentary_button)});
    auto data = parameter_binding("data", "transport.tempo");
    data.feedback_id = "parameter.display.0";
    profile.bindings = {action_binding("play", "transport.play.v1", "led.play"),
                        action_binding("stop", "transport.stop.v1", "led.stop"),
                        std::move(data)};
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
