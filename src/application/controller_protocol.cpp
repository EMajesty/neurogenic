#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <type_traits>
#include <unistd.h>

namespace neurogenic {
namespace {
constexpr std::uint16_t magic = 0x474e;
std::uint16_t read16(const std::uint8_t *p) {
    return std::uint16_t(p[0]) | std::uint16_t(p[1]) << 8;
}
std::uint32_t read32(const std::uint8_t *p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
std::uint64_t read64(const std::uint8_t *p) {
    std::uint64_t result{};
    for (int i = 0; i < 8; ++i) result |= std::uint64_t(p[i]) << (i * 8);
    return result;
}
void append16(std::vector<std::uint8_t> &v, std::uint16_t n) {
    v.push_back(n & 255);
    v.push_back(n >> 8);
}
void append32(std::vector<std::uint8_t> &v, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        v.push_back((n >> (i * 8)) & 255);
}
void append64(std::vector<std::uint8_t> &v, std::uint64_t n) {
    for (int i = 0; i < 8; ++i)
        v.push_back((n >> (i * 8)) & 255);
}
std::uint16_t crc(std::span<const std::uint8_t> data) {
    std::uint16_t value = 0xffff;
    for (auto byte : data) {
        value ^= std::uint16_t(byte) << 8;
        for (int i = 0; i < 8; ++i)
            value = value & 0x8000 ? std::uint16_t((value << 1) ^ 0x1021)
                                   : std::uint16_t(value << 1);
    }
    return value;
}
std::string escape(std::string_view s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\')
            r += '\\';
        if (static_cast<unsigned char>(c) >= 32)
            r += c;
    }
    return r;
}
std::string mode_name(std::uint16_t index) {
    switch (index) {
    case 0:
        return "step";
    case 1:
        return "pad";
    case 2:
        return "levels";
    case 3:
        return "notes";
    default:
        return {};
    }
}
std::string protocol_button_id(std::uint16_t id, std::uint8_t minor) {
    if (minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR)
        if (const auto *control = reference_v2_wire_control(id);
            control && control->type == V2WireControlType::button)
            return control->id;
    if (id < 16) return "grid." + std::to_string(id);
    if (id < 24) return "function." + std::to_string(id - 16);
    if (minor == 0 && id >= 24 && id <= 27)
        return "mode." + mode_name(id - 24);
    static constexpr std::string_view legacy[]{
        "play", "stop", "record", "shift", "select", "data.push",
        "mode.step", "mode.pad", "mode.levels", "mode.notes",
        "page.previous", "page.next"};
    if (id >= 24 && id <= 35) return std::string(legacy[id - 24]);
    if (id >= 36 && id <= 43) return "page." + std::to_string(id - 36);
    if (id >= 44 && id <= 53) return "edit." + std::to_string(id - 44);
    if (id >= 54 && id <= 63)
        return "performance." + std::to_string(id - 54);
    static constexpr std::string_view navigation[]{"up", "down", "left",
                                                    "right", "confirm", "back"};
    if (id >= 64 && id <= 69)
        return "navigation." + std::string(navigation[id - 64]);
    return {};
}
std::string protocol_encoder_id(std::uint16_t id, std::uint8_t minor) {
    if (minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR)
        if (const auto *control = reference_v2_wire_control(id);
            control && control->type == V2WireControlType::relative_encoder)
            return control->id;
    if (id < 8) return "parameter." + std::to_string(id);
    if (id == 8) return "data";
    return {};
}
std::optional<std::uint16_t> protocol_feedback_id(std::string_view id,
                                                  std::uint8_t minor) {
    if (minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR)
    {
        if (id.starts_with("grid.")) {
            std::uint16_t index{};
            const auto text = id.substr(5);
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), index);
            if (ec == std::errc{} && end == text.data() + text.size() && index < 16)
                return static_cast<std::uint16_t>(NG_V2_PAD_1 + index);
        }
        for (const auto [prefix, base] :
             {std::pair<std::string_view, std::uint16_t>{"parameter.", NG_V2_PARAMETER_1},
              {"function.", NG_V2_FUNCTION_1}}) {
            if (!id.starts_with(prefix)) continue;
            std::uint16_t index{};
            const auto text = id.substr(prefix.size());
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), index);
            if (ec == std::errc{} && end == text.data() + text.size() && index < 8)
                return static_cast<std::uint16_t>(base + index);
        }
        return reference_v2_wire_id(id);
    }
    const auto suffix = [&](std::string_view prefix)
        -> std::optional<std::uint16_t> {
        if (!id.starts_with(prefix)) return {};
        std::uint16_t value{};
        const auto text = id.substr(prefix.size());
        const auto [end, error] = std::from_chars(text.data(),
                                                  text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size()
                   ? std::optional<std::uint16_t>(value)
                   : std::nullopt;
    };
    if (auto value = suffix("grid.")) return *value;
    if (auto value = suffix("function.")) return 16 + *value;
    if (auto value = suffix("page.")) return 36 + *value;
    if (auto value = suffix("edit.")) return 44 + *value;
    if (auto value = suffix("performance.")) return 54 + *value;
    if (auto value = suffix("parameter.")) return *value;
    return {};
}
std::optional<std::uint16_t> protocol_context_slot(std::uint16_t wire_id,
                                                   std::uint8_t minor) {
    if (minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR) {
        if (wire_id >= NG_V2_PARAMETER_1 && wire_id <= NG_V2_PARAMETER_8)
            return wire_id - NG_V2_PARAMETER_1;
        if (wire_id >= NG_V2_FUNCTION_1 && wire_id <= NG_V2_FUNCTION_8)
            return 8 + wire_id - NG_V2_FUNCTION_1;
        return {};
    }
    return wire_id < 8 ? wire_id : 8 + (wire_id - 16);
}
double numeric(const ApplicationValue &value, double fallback = 0) {
    if (const auto *real = std::get_if<double>(&value))
        return *real;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return *integer;
    if (const auto *boolean = std::get_if<bool>(&value))
        return *boolean ? 1 : 0;
    return fallback;
}
std::string formatted(const ApplicationValue &value) {
    if (const auto *real = std::get_if<double>(&value)) {
        char text[24]{};
        std::snprintf(text, sizeof(text), "%.3g", *real);
        return text;
    }
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return std::to_string(*integer);
    if (const auto *boolean = std::get_if<bool>(&value))
        return *boolean ? "On" : "Off";
    if (const auto *text = std::get_if<std::string>(&value))
        return *text;
    return "--";
}
ContextControl parameter_control(const ApplicationState &state,
                                 std::uint16_t id,
                                 std::string_view parameter_id) {
    ContextControl result;
    result.control_id = id;
    result.parameter_id = parameter_id;
    const auto found =
        std::find_if(state.parameters.begin(), state.parameters.end(),
                     [&](const auto &p) { return p.id == parameter_id; });
    if (found == state.parameters.end()) {
        result.label = "--";
        return result;
    }
    result.active = true;
    result.role = "parameter";
    result.label = found->name;
    result.value = formatted(found->current_value);
    result.unit = found->unit;
    result.minimum = numeric(found->minimum);
    result.maximum = numeric(found->maximum);
    result.bipolar = result.minimum < 0 && result.maximum > 0;
    result.feedback_revision = state.revision;
    return result;
}
ContextControl action_control(std::uint16_t id, std::string label,
                              std::string action, bool active = true) {
    ContextControl c;
    c.control_id = id;
    c.label = std::move(label);
    c.action_id = std::move(action);
    c.active = active;
    c.role = "action";
    return c;
}
std::map<std::string, ApplicationValue> application_arguments(
    const std::map<std::string, PerformanceArgumentValue> &arguments) {
    std::map<std::string, ApplicationValue> result;
    for (const auto &[name, value] : arguments)
        std::visit([&](const auto &item) { result[name] = item; }, value);
    return result;
}
std::string_view truncate_view(std::string_view value, std::size_t maximum) {
    return value.substr(0, std::min(value.size(), maximum));
}
} // namespace

std::array<ContextControl, 16>
resolve_controller_context(const ApplicationState &state, std::string_view context,
                           const ModifierState &modifiers) {
    std::array<ContextControl, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].control_id = i;
        result[i].label = "--";
        result[i].feedback_revision = state.revision;
    }
    auto set_parameters = [&](std::initializer_list<std::string_view> ids) {
        std::size_t i{};
        for (auto id : ids) {
            result[i] =
                parameter_control(state, static_cast<std::uint16_t>(i), id);
            ++i;
        }
    };
    // A context identity may include a configurable bank suffix (for example
    // "sequencer:advanced"). Base-page fallback keeps profiles compatible
    // while allowing future panel layouts to select richer banks.
    const auto separator = context.find(':');
    const auto explicit_bank = separator != std::string_view::npos;
    const auto page = context.substr(0, separator);
    const auto bank = separator == std::string_view::npos
        ? std::string_view("primary") : context.substr(separator + 1);
    for (auto &control : result) control.bank_id = std::string(bank);
    if (page == "performance") {
        const auto selected = std::find_if(
            state.performance_banks.banks.begin(),
            state.performance_banks.banks.end(), [&](const auto &value) {
                return value.id == state.performance_banks.active_bank;
            });
        if (selected != state.performance_banks.banks.end()) {
            const auto secondary = modifiers.shift || modifiers.shift_latched;
            for (const auto &assignment : selected->parameters) {
                if (assignment.secondary != secondary || assignment.position >= 8)
                    continue;
                auto control = parameter_control(
                    state, static_cast<std::uint16_t>(assignment.position),
                    assignment.parameter_id);
                PerformanceMacroId macro_id{};
                if (!control.active && assignment.parameter_id.starts_with("macro.") &&
                    assignment.parameter_id.ends_with(".value")) {
                    const auto text = std::string_view(assignment.parameter_id).substr(
                        6, assignment.parameter_id.size() - 12);
                    const auto [end, parse_error] = std::from_chars(
                        text.data(), text.data() + text.size(), macro_id);
                    const auto macro = std::find_if(
                        state.performance_macros.macros.begin(),
                        state.performance_macros.macros.end(),
                        [=](const auto &value) { return value.id == macro_id; });
                    if (parse_error == std::errc{} &&
                        end == text.data() + text.size() &&
                        macro != state.performance_macros.macros.end()) {
                        control.active = true;
                        control.label = macro->name;
                        control.value = formatted(static_cast<double>(macro->value));
                        control.minimum = 0;
                        control.maximum = 1;
                        control.role = "performance-macro";
                        control.arguments["macro_id"] =
                            static_cast<std::int64_t>(macro_id);
                    }
                }
                control.label = assignment.label.empty()
                    ? control.label : assignment.label;
                control.bank_id = "performance:" +
                    std::to_string(selected->id) +
                    (secondary ? ":secondary" : ":primary");
                if (control.role != "performance-macro")
                    control.role = "performance-parameter";
                control.arguments["assignment"] =
                    static_cast<std::int64_t>(assignment.id);
                control.arguments["scale"] = assignment.scale;
                control.assignment_id = assignment.id;
                result[assignment.position] = std::move(control);
            }
            for (const auto &assignment : selected->actions) {
                if (assignment.secondary != secondary || assignment.position >= 8)
                    continue;
                const auto arguments = application_arguments(assignment.arguments);
                auto target_available = true;
                if (assignment.action_id == "performance.slot-launch.v1" ||
                    assignment.action_id == "performance.slot-stop.v1" ||
                    assignment.action_id == "performance.slot-retrigger.v1") {
                    const auto slot = arguments.find("slot");
                    target_available = slot != arguments.end() &&
                        std::holds_alternative<std::int64_t>(slot->second) &&
                        std::any_of(state.performance_launching.slots.begin(),
                                    state.performance_launching.slots.end(),
                                    [&](const auto &value) {
                                        return value.id == static_cast<LaunchSlotId>(
                                            std::get<std::int64_t>(slot->second));
                                    });
                }
                auto control = action_control(
                    static_cast<std::uint16_t>(8 + assignment.position),
                    assignment.label.empty() ? assignment.action_id
                                             : assignment.label,
                    assignment.action_id,
                    target_available && std::find(state.available_actions.begin(),
                              state.available_actions.end(),
                              assignment.action_id) !=
                        state.available_actions.end());
                control.arguments = arguments;
                control.assignment_id = assignment.id;
                control.bank_id = "performance:" +
                    std::to_string(selected->id) +
                    (secondary ? ":secondary" : ":primary");
                control.role = assignment.behavior ==
                                       PerformanceActionBehavior::momentary
                                   ? "performance-momentary"
                                   : assignment.behavior ==
                                             PerformanceActionBehavior::toggle
                                         ? "performance-toggle"
                                         : "performance-action";
                result[8 + assignment.position] = std::move(control);
            }
        } else
            set_parameters({"transport.tempo", "synth.cutoff", "synth.resonance",
                            "synth.attack", "synth.release", "sample.gain",
                            "mixer.selected.pan", "effect.delay.mix"});
    } else if (page == "mixer")
        set_parameters({"mixer.selected.gain", "mixer.selected.pan",
                        "mixer.track.aux-1", "mixer.track.aux-2",
                        "mixer.aux.return", "mixer.master.gain",
                        "external-fx.send-gain", "external-fx.return-gain"});
    else if (page == "samples" || page == "sample" || page == "slice")
        set_parameters({"sample.pitch", "sample.gain", "sample.pan",
                        "effect.delay.mix", "effect.delay.time",
                        "effect.delay.feedback"});
    else if (page == "modulation" || page == "modulation-routing" ||
             page == "fx" || page == "macros" || page == "scenes")
        set_parameters({"synth.cutoff", "synth.resonance", "synth.pitch",
                        "synth.gain", "sample.pitch", "sample.gain",
                        "mixer.track.pan", "external-fx.wet"});
    else if (page == "routing" || page == "song" || page == "setup")
        set_parameters({"external-fx.wet", "external-fx.send-gain",
                        "external-fx.return-gain", "mixer.master.gain"});
    else if (page == "parameters" || page == "sound")
        set_parameters({"synth.cutoff", "synth.resonance", "synth.attack",
                        "synth.decay", "synth.sustain", "synth.release",
                        "synth.gain", "effect.delay.mix"});
    else if (page == "sequencer" && explicit_bank) {
        static constexpr std::array<std::string_view, 7> properties{
            "pitch", "velocity", "duration", "probability", "condition",
            "ratchets", "microtiming"};
        for (std::size_t index = 0; index < properties.size(); ++index) {
            auto &control = result[index];
            control.label = std::string(properties[index]);
            std::transform(control.label.begin(), control.label.end(),
                           control.label.begin(), [](unsigned char value) {
                               return static_cast<char>(std::toupper(value));
                           });
            control.role = "step-property";
            control.active = state.selected_track != 0;
            control.arguments = {{"property", std::string(properties[index])}};
            if (state.selected_event) {
                const auto &event = *state.selected_event;
                if (properties[index] == "pitch")
                    control.value = std::to_string(event.pitch);
                else if (properties[index] == "velocity")
                    control.value = formatted(static_cast<double>(event.velocity));
                else if (properties[index] == "duration") {
                    control.value = std::to_string(event.duration);
                    control.unit = "ticks";
                } else if (properties[index] == "probability") {
                    control.value = formatted(
                        static_cast<double>(event.trigger_probability));
                    control.unit = "%";
                } else if (properties[index] == "condition")
                    control.value = std::to_string(
                        static_cast<int>(event.trigger_condition));
                else if (properties[index] == "ratchets")
                    control.value = std::to_string(event.ratchet_count);
                else {
                    control.value = std::to_string(event.timing_offset);
                    control.unit = "ticks";
                }
            }
        }
        result[7] = parameter_control(state, 7,
                                      state.selected_lock_parameter);
        result[7].label = result[7].active ? "LOCK " + result[7].label
                                          : "LOCK --";
        result[7].role = "step-lock";
        result[7].arguments = {
            {"parameter_id", state.selected_lock_parameter}};
        const auto selected = std::find_if(
            state.selected_step_locks.begin(), state.selected_step_locks.end(),
            [&](const auto &lock) {
                return lock.parameter_id == state.selected_lock_parameter;
            });
        if (selected != state.selected_step_locks.end()) {
            result[7].locked = true;
            result[7].value = formatted(static_cast<double>(selected->value));
        }
    } else
        set_parameters({"transport.tempo", "synth.cutoff", "synth.resonance",
                        "synth.attack", "synth.release", "sample.gain",
                        "mixer.selected.pan", "effect.delay.mix"});
    std::array<std::pair<std::string_view, std::string_view>, 8> lower{
        {{"FOCUS-", "navigation.focus-previous.v1"},
         {"FOCUS+", "navigation.focus-next.v1"},
         {"PREV", "navigation.focus-previous.v1"},
         {"NEXT", "navigation.focus-next.v1"},
         {"BACK", "navigation.cancel.v1"},
         {"ENTER", "navigation.confirm.v1"},
         {"PAGE+", "step.page-next.v1"},
         {"PLAY", "transport.play.v1"}}};
    if (page == "samples" || page == "sample" || page == "slice") {
        lower[0] = {"PREVIEW", "slice.preview.v1"};
        lower[1] = {"STOP PREV", "slice.preview-stop.v1"};
    } else if (page == "mixer") {
        lower[0] = {"MUTE", "mixer.mute-toggle.v1"};
        lower[1] = {"SOLO", "mixer.solo-toggle.v1"};
    } else if (page == "sequencer") {
        lower[0] = {"PAGE-", "step.page-previous.v1"};
        lower[1] = {"PAGE+", "step.page-next.v1"};
        lower[2] = {"RESET STEP", "step.trigger-reset.v1"};
        lower[3] = {"MUTE", "track.mute-toggle.v1"};
        lower[4] = {"UNDO", "edit.undo.v1"};
        lower[5] = {"REDO", "edit.redo.v1"};
        lower[6] = {"PATTERN-", "pattern.previous.v1"};
        lower[7] = {"PATTERN+", "pattern.next.v1"};
    } else if ((page == "modulation" || page == "modulation-routing" ||
                page == "fx") && state.selected_modulation_route) {
        const auto route = std::find_if(
            state.modulation.routes.begin(),
            state.modulation.routes.begin() + state.modulation.route_count,
            [&](const auto &candidate) {
                return candidate.id == state.selected_modulation_route;
            });
        if (route != state.modulation.routes.begin() +
                         state.modulation.route_count) {
            lower[0] = {"DEPTH-", "modulation.route-depth.v1"};
            lower[1] = {"DEPTH+", "modulation.route-depth.v1"};
            lower[2] = {route->enabled ? "DISABLE" : "ENABLE",
                        "modulation.route-enable.v1"};
        }
    } else if (page == "browser") {
        lower = {{{"PREVIOUS", "navigation.focus-previous.v1"},
                  {"NEXT", "navigation.focus-next.v1"},
                  {"PARENT", "navigation.cancel.v1"},
                  {"OPEN", "navigation.confirm.v1"},
                  {"BACK", "navigation.cancel.v1"},
                  {"CONFIRM", "navigation.confirm.v1"},
                  {"PAGE-", "step.page-previous.v1"},
                  {"PAGE+", "step.page-next.v1"}}};
    } else if (page == "performance" && !state.live_loops.empty()) {
        const auto session = std::find_if(
            state.live_loops.begin(), state.live_loops.end(),
            [](const auto &candidate) {
                return candidate.state != LiveLoopState::playing &&
                       candidate.state != LiveLoopState::idle &&
                       candidate.state != LiveLoopState::canceled &&
                       candidate.state != LiveLoopState::error;
            });
        const auto &loop = session != state.live_loops.end()
                               ? *session : state.live_loops.front();
        lower[0] = {loop.state == LiveLoopState::armed ? "START LOOP"
                                                       : "ARM LOOP",
                    loop.state == LiveLoopState::armed
                        ? "live-loop.start.v1" : "live-loop.arm.v1"};
        lower[1] = {"STOP LOOP", "live-loop.stop.v1"};
        lower[2] = {"OVERDUB", "live-loop.overdub.v1"};
        lower[3] = {"REPLACE", "live-loop.replace.v1"};
        lower[4] = {"CANCEL", "live-loop.cancel.v1"};
    }
    const auto custom_performance = page == "performance" &&
        std::any_of(state.performance_banks.banks.begin(),
                    state.performance_banks.banks.end(), [&](const auto &value) {
                        return value.id == state.performance_banks.active_bank;
                    });
    if ((modifiers.shift || modifiers.shift_latched) && !custom_performance) {
        if (page == "sequencer" && explicit_bank) {
            lower[0] = {"SELECT", "step.selection-toggle.v1"};
            lower[1] = {"CLEAR SEL", "step.selection-clear.v1"};
            lower[2] = {"REMOVE LOCK", "step.lock-remove.v1"};
            lower[3] = {"CLEAR LOCKS", "step.locks-clear.v1"};
            lower[4] = {"UNDO", "edit.undo.v1"};
            lower[5] = {"REDO", "edit.redo.v1"};
            lower[6] = {"PAGE-", "step.page-previous.v1"};
            lower[7] = {"PAGE+", "step.page-next.v1"};
        } else {
            lower[0] = {"UNDO", "edit.undo.v1"};
            lower[1] = {"REDO", "edit.redo.v1"};
        }
    }
    for (std::size_t i = 0; i < 8 && !custom_performance; ++i) {
        const bool available =
            std::find(state.available_actions.begin(),
                      state.available_actions.end(),
                      lower[i].second) != state.available_actions.end();
        result[8 + i] = action_control(8 + i, std::string(lower[i].first),
                                       std::string(lower[i].second), available);
        result[8 + i].bank_id = std::string(bank);
        result[8 + i].feedback_revision = state.revision;
    }
    if (page == "sequencer" && explicit_bank &&
        (modifiers.shift || modifiers.shift_latched)) {
        result[8].arguments = {
            {"pattern_id", static_cast<std::int64_t>(state.selected_pattern)},
            {"track_id", static_cast<std::int64_t>(state.selected_track)},
            {"step", state.selected_step}};
        result[10].arguments = {
            {"parameter_id", state.selected_lock_parameter}};
    }
    if ((page == "modulation" || page == "modulation-routing" ||
         page == "fx") && state.selected_modulation_route) {
        const auto route = std::find_if(
            state.modulation.routes.begin(),
            state.modulation.routes.begin() + state.modulation.route_count,
            [&](const auto &candidate) {
                return candidate.id == state.selected_modulation_route;
            });
        if (route != state.modulation.routes.begin() +
                         state.modulation.route_count) {
            const auto route_id = static_cast<std::int64_t>(route->id);
            result[8].arguments = {
                {"route_id", route_id},
                {"depth", static_cast<double>(std::clamp(route->depth - .05F,
                                                         -1.0F, 1.0F))}};
            result[9].arguments = {
                {"route_id", route_id},
                {"depth", static_cast<double>(std::clamp(route->depth + .05F,
                                                         -1.0F, 1.0F))}};
            result[10].arguments = {{"route_id", route_id},
                                    {"enabled", !route->enabled}};
        }
    } else if (page == "performance" && !state.live_loops.empty()) {
        const auto session = std::find_if(
            state.live_loops.begin(), state.live_loops.end(),
            [](const auto &candidate) {
                return candidate.state != LiveLoopState::playing &&
                       candidate.state != LiveLoopState::idle &&
                       candidate.state != LiveLoopState::canceled &&
                       candidate.state != LiveLoopState::error;
            });
        const auto &loop = session != state.live_loops.end()
                               ? *session : state.live_loops.front();
        const auto id = static_cast<std::int64_t>(loop.id);
        result[8].arguments = {{"id", id}};
        if (loop.state != LiveLoopState::armed)
            result[8].arguments.emplace("mode", std::string("capture"));
        for (std::size_t index = 9; index <= 12; ++index)
            result[index].arguments = {{"id", id}};
        result[9].active = loop.state == LiveLoopState::recording ||
                           loop.state == LiveLoopState::overdubbing ||
                           loop.state == LiveLoopState::replacing;
        result[10].active = result[11].active =
            loop.state == LiveLoopState::playing;
        result[12].active = loop.state == LiveLoopState::armed ||
                            loop.state == LiveLoopState::waiting ||
                            result[9].active;
    }
    for (auto &control : result) control.bank_id = std::string(bank);
    return result;
}
std::array<ContextControl, 16>
build_controller_context(const ApplicationState &state) {
    return resolve_controller_context(state, state.controller_context,
                                      state.modifiers);
}
std::vector<std::uint8_t>
encode_controller_packet(const ControllerPacket &packet) {
    if (packet.payload.size() > controller_maximum_payload)
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(packet.payload.size() + 12);
    append16(out, magic);
    out.push_back(controller_protocol_major);
    out.push_back(static_cast<std::uint8_t>(packet.type));
    append16(out, static_cast<std::uint16_t>(packet.payload.size()));
    append32(out, packet.sequence);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    append16(out, crc(out));
    return out;
}
void ControllerFrameDecoder::push(std::span<const std::uint8_t> data) noexcept {
    for (auto byte : data) {
        if (bytes_.size() == capacity_) {
            bytes_.pop_front();
            ++dropped_;
        }
        bytes_.push_back(byte);
    }
}
std::optional<ControllerPacket> ControllerFrameDecoder::pop() noexcept {
    while (bytes_.size() >= 12) {
        if (bytes_[0] != (magic & 255) || bytes_[1] != (magic >> 8)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        std::array<std::uint8_t, 10> header{};
        for (std::size_t i = 0; i < header.size(); ++i)
            header[i] = bytes_[i];
        if (header[2] != controller_protocol_major) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto length = read16(header.data() + 4);
        if (length > controller_maximum_payload) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto total = std::size_t(length) + 12;
        if (bytes_.size() < total)
            return std::nullopt;
        std::vector<std::uint8_t> raw(total);
        for (std::size_t i = 0; i < total; ++i)
            raw[i] = bytes_[i];
        if (crc(std::span<const std::uint8_t>(raw.data(), total - 2)) !=
            read16(raw.data() + total - 2)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        for (std::size_t i = 0; i < total; ++i)
            bytes_.pop_front();
        ControllerPacket packet{static_cast<ControllerMessageType>(raw[3]),
                                read32(raw.data() + 6),
                                {}};
        packet.payload.assign(raw.begin() + 10, raw.end() - 2);
        return packet;
    }
    return std::nullopt;
}
ControllerHost::ControllerHost(ApplicationService &app, std::size_t capacity,
                               std::string expected_device_id)
    : application_(&app), capacity_(std::max<std::size_t>(1, capacity)),
      expected_device_id_(std::move(expected_device_id)) {}
bool ControllerHost::enqueue(ControllerPacket p) noexcept {
    if (output_.size() >= capacity_) {
        ++state_.dropped_output;
        return false;
    }
    output_.push_back(std::move(p));
    return true;
}
void ControllerHost::connected() {
    output_.clear();
    session_.reset();
    state_.connection = ControllerConnection::negotiating;
    state_.feedback_revision = ~std::uint64_t{};
    force_full_refresh_ = true;
    state_.inventory_valid = false;
    state_.input_state_valid = false;
    state_.last_input_sequence = 0;
    state_.input_generation = next_input_generation_++;
    quarantined_buttons_.assign(reference_v2_wire_controls().size(), false);
    enqueue({ControllerMessageType::hello,
             sequence_++,
             {controller_protocol_major, controller_protocol_minor}});
}
void ControllerHost::disconnected(std::string reason) {
    state_.connection = ControllerConnection::disconnected;
    state_.error = std::move(reason);
    output_.clear();
    if (session_) session_->reconnect();
    session_.reset();
}
bool ControllerHost::set_analog_calibration(std::size_t id,
                                            AnalogCalibration calibration,
                                            std::string &error) noexcept {
    if (id >= state_.calibration.size() ||
        calibration.maximum <= calibration.minimum ||
        calibration.dead_zone >= calibration.maximum - calibration.minimum) {
        error = "invalid analog calibration";
        return false;
    }
    state_.calibration[id] = calibration;
    state_.pickup_acquired[id] = false;
    state_.pickup_direction[id] = 0;
    error.clear();
    return true;
}
void ControllerHost::set_brightness_limit(std::uint8_t value) noexcept {
    brightness_limit_ = value;
    force_full_refresh_ = true;
    state_.feedback_revision = ~std::uint64_t{};
    if (session_) session_->invalidate_feedback();
}
bool ControllerHost::configure_session(std::uint32_t capabilities,
                                       std::string &error) {
    ControllerProfile profile;
    ControllerDeviceDescriptor descriptor;
    if (state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR) {
        const auto required = controller_buttons | controller_relative_encoders |
            controller_inventory_v2 | controller_input_state;
        if ((capabilities & required) != required || !state_.inventory_valid) {
            error = "protocol 1.3 V2 device lacks validated inventory/input-state capabilities";
            return false;
        }
        profile = reference_controller_v2_profile();
        descriptor = reference_controller_v2_descriptor();
    } else if (state_.protocol_minor >= 2) {
        if (!(capabilities & controller_inventory_068)) {
            error = "protocol 1.2 device lacks the dedicated inventory capability";
            return false;
        }
        profile = full_performance_profile();
        descriptor = rp2040_controller_descriptor();
        for (auto &control : descriptor.controls) {
            const auto button = control.type == ControlType::momentary_button ||
                                control.type == ControlType::toggle_button;
            if (button && !(capabilities & controller_buttons))
                control.available = false;
            if (control.type == ControlType::relative_encoder &&
                !(capabilities & controller_relative_encoders))
                control.available = false;
            if (!(capabilities & controller_rgb))
                std::erase(control.feedback, FeedbackType::rgb_led);
            if (!(capabilities & controller_context_feedback))
                std::erase(control.feedback, FeedbackType::parameter_value);
        }
    } else {
        profile = legacy_performance_profile(state_.protocol_minor, capabilities);
        descriptor = legacy_controller_descriptor(state_.protocol_minor,
                                                  capabilities);
    }
    session_.emplace(*application_, std::move(descriptor), std::move(profile),
                     state_.device_id, capacity_);
    if (!session_->valid()) {
        error = session_->state().error;
        session_.reset();
        return false;
    }
    if (state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR &&
        !session_->configure_gestures(reference_controller_v2_gestures(), error)) {
        session_.reset();
        return false;
    }
    error.clear();
    return true;
}
void ControllerHost::request_input_state() noexcept {
    if (session_) session_->reconnect();
    state_.input_state_valid = false;
    std::vector<std::uint8_t> payload;
    append32(payload, state_.input_generation);
    enqueue({ControllerMessageType::state_sync, sequence_++, std::move(payload)});
}
bool ControllerHost::accept_input_sequence(const ControllerPacket &packet,
                                           std::string &error) {
    if (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR) return true;
    if (!state_.input_state_valid) {
        error = "V2 input received before full input-state synchronization";
        ++state_.stale_input;
        return false;
    }
    if (packet.payload.size() < 4 || read32(packet.payload.data()) !=
                                       state_.input_generation) {
        error = "stale V2 input generation";
        ++state_.stale_input;
        return false;
    }
    if (packet.sequence == state_.last_input_sequence) {
        error = "duplicate V2 input sequence";
        ++state_.duplicate_input;
        return false;
    }
    if (state_.last_input_sequence &&
        packet.sequence != state_.last_input_sequence + 1) {
        ++state_.sequence_gaps;
        request_input_state();
        error = "V2 input sequence gap requires state synchronization";
        return false;
    }
    state_.last_input_sequence = packet.sequence;
    return true;
}
bool ControllerHost::receive(const ControllerPacket &p,
                             std::string &error) noexcept {
    ++state_.received;
    if (p.payload.size() > controller_maximum_payload) {
        error = "payload exceeds protocol limit";
        return false;
    }
    if (p.type == ControllerMessageType::hello_reply) {
        if (p.payload.size() < 4) {
            error = "short hello reply";
            return false;
        }
        state_.protocol_major = p.payload[0];
        state_.protocol_minor = p.payload[1];
        const auto idlen = p.payload[2], fwlen = p.payload[3];
        if (4u + idlen + fwlen > p.payload.size()) {
            error = "invalid identity lengths";
            return false;
        }
        state_.device_id.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4), idlen);
        state_.firmware.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4 + idlen),
            fwlen);
        if (state_.protocol_major != controller_protocol_major) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = "protocol major version mismatch";
            error = state_.error;
            return false;
        }
        if (state_.protocol_minor > controller_protocol_minor) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = "protocol minor version is newer than this host";
            error = state_.error;
            return false;
        }
        if (!expected_device_id_.empty() &&
            state_.device_id != expected_device_id_) {
            state_.connection = ControllerConnection::incompatible;
            state_.error =
                "controller device identity does not match configuration";
            error = state_.error;
            return false;
        }
        ++state_.reconnects;
        if (state_.protocol_minor <= 1 &&
            !configure_session(controller_buttons | controller_relative_encoders |
                                   controller_rgb | controller_context_feedback |
                                   controller_heartbeat |
                                   (state_.protocol_minor == 1
                                        ? controller_analog
                                        : std::uint32_t{}),
                               error)) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = error;
            return false;
        }
        if (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR)
            state_.connection = ControllerConnection::connected;
        enqueue({ControllerMessageType::capabilities, sequence_++, {}});
        if (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR)
            enqueue({ControllerMessageType::state_sync, sequence_++, {}});
    } else if (p.type == ControllerMessageType::capabilities) {
        if (p.payload.size() != 4) {
            error = "invalid capability payload";
            return false;
        }
        state_.capabilities = read32(p.payload.data());
        if (state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR) {
            const auto required = controller_inventory_v2 | controller_input_state;
            if ((state_.capabilities & required) != required) {
                error = "V2 capability response lacks inventory or input-state support";
                state_.connection = ControllerConnection::incompatible;
                return false;
            }
            enqueue({ControllerMessageType::inventory, sequence_++, {}});
        } else if (state_.protocol_minor >= 2 &&
            !configure_session(state_.capabilities, error)) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = error;
            return false;
        }
    } else if (p.type == ControllerMessageType::inventory) {
        if (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR ||
            p.payload.size() != 6 || read16(p.payload.data()) !=
                NG_CONTROLLER_V2_CONTROL_COUNT ||
            read32(p.payload.data() + 2) !=
                NG_CONTROLLER_V2_INVENTORY_FINGERPRINT) {
            error = "Reference Controller V2 inventory mismatch";
            state_.connection = ControllerConnection::incompatible;
            return false;
        }
        state_.inventory_valid = true;
        if (!configure_session(state_.capabilities, error)) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = error;
            return false;
        }
        request_input_state();
    } else if (p.type == ControllerMessageType::input_state) {
        constexpr std::size_t bitmap_size =
            (NG_CONTROLLER_V2_CONTROL_COUNT + 7) / 8;
        if (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR ||
            p.payload.size() != 4 + bitmap_size ||
            read32(p.payload.data()) != state_.input_generation) {
            error = "invalid or stale V2 input-state snapshot";
            ++state_.stale_input;
            return false;
        }
        if (session_) session_->reconnect();
        for (std::size_t i = 0; i < reference_v2_wire_controls().size(); ++i)
            quarantined_buttons_[i] =
                reference_v2_wire_controls()[i].type == V2WireControlType::button &&
                (p.payload[4 + i / 8] & (1u << (i % 8)));
        state_.input_state_valid = true;
        state_.last_input_sequence = p.sequence;
        ++state_.state_synchronizations;
        state_.connection = ControllerConnection::connected;
        force_full_refresh_ = true;
    } else if (p.type == ControllerMessageType::button) {
        if ((state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR &&
             p.payload.size() != 7) ||
            (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR &&
             p.payload.size() != 3)) {
            error = "invalid button payload";
            return false;
        }
        if (!accept_input_sequence(p, error)) {
            ++state_.dropped_input;
            return false;
        }
        const auto offset = state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR ? 4u : 0u;
        const auto id = read16(p.payload.data() + offset);
        const bool pressed = p.payload[offset + 2] != 0;
        bool queued = true;
        if (session_) {
            const auto logical = protocol_button_id(id, state_.protocol_minor);
            if (logical.empty()) {
                queued = false;
                state_.error = "unknown controller button identity";
            } else {
                if (state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR) {
                    const auto &controls = reference_v2_wire_controls();
                    const auto index = std::find_if(controls.begin(), controls.end(),
                        [=](const auto &value) { return value.wire_id == id; });
                    if (index != controls.end()) {
                        const auto position = static_cast<std::size_t>(index - controls.begin());
                        if (quarantined_buttons_[position]) {
                            if (!pressed) quarantined_buttons_[position] = false;
                            error.clear();
                            return true;
                        }
                    }
                }
                std::string session_error;
                queued = session_->input({logical, pressed ? 1.0 : 0.0, 0,
                                          pressed, p.sequence,
                                          0},
                                         session_error);
                if (!queued) state_.error = std::move(session_error);
            }
        } else {
            queued = false;
            state_.error = "controller input received before negotiation";
        }
        if (!queued)
            ++state_.dropped_input;
    } else if (p.type == ControllerMessageType::encoder) {
        if ((state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR &&
             p.payload.size() != 8) ||
            (state_.protocol_minor < NG_CONTROLLER_V2_PROTOCOL_MINOR &&
             p.payload.size() != 4)) {
            error = "invalid encoder payload";
            return false;
        }
        if (!accept_input_sequence(p, error)) {
            ++state_.dropped_input;
            return false;
        }
        const auto offset = state_.protocol_minor >= NG_CONTROLLER_V2_PROTOCOL_MINOR ? 4u : 0u;
        const auto id = read16(p.payload.data() + offset);
        const auto delta =
            static_cast<std::int16_t>(read16(p.payload.data() + offset + 2));
        if (id < state_.encoders.size())
            state_.encoders[id] += delta;
        if (session_) {
            const auto logical = protocol_encoder_id(id, state_.protocol_minor);
            if (!logical.empty()) {
                std::string session_error;
                if (!session_->input({logical, static_cast<double>(delta), 0,
                                      true, p.sequence,
                                      0},
                                     session_error)) {
                    ++state_.dropped_input;
                    state_.error = std::move(session_error);
                }
            }
        } else
            ++state_.dropped_input;
    } else if (p.type == ControllerMessageType::analog) {
        if (p.payload.size() != 4) {
            error = "invalid analog payload";
            return false;
        }
        if (state_.protocol_minor >= 2 &&
            !(state_.capabilities & controller_analog)) {
            error = "analog controls are unavailable in negotiated profile";
            return false;
        }
        const auto id = read16(p.payload.data()),
                   value = read16(p.payload.data() + 2);
        if (id >= state_.analog.size()) {
            error = "unknown analog control";
            return false;
        }
        state_.analog[id] = value;
        const auto &calibration = state_.calibration[id];
        const auto range = std::max<std::uint16_t>(1, calibration.maximum -
                                                          calibration.minimum);
        const auto normalized =
            std::clamp((double(value) - calibration.minimum) / range, 0.0, 1.0);
        if (session_) {
            std::string session_error;
            if (!session_->input({"analog." + std::to_string(id), normalized, 0,
                                  true, p.sequence},
                                 session_error)) {
                ++state_.dropped_input;
                state_.error = std::move(session_error);
            }
            error.clear();
            return true;
        }
        ++state_.dropped_input;
    } else if (p.type == ControllerMessageType::state_sync) {
        state_.feedback_revision = ~std::uint64_t{};
        force_full_refresh_ = true;
        if (session_) session_->reconnect();
    }
    else if (p.type == ControllerMessageType::heartbeat) {
        if (p.payload.size() == 8) last_receive_ms_ = read64(p.payload.data());
        ++state_.heartbeats;
        enqueue({ControllerMessageType::heartbeat, sequence_++, p.payload});
    }
    else if (p.type == ControllerMessageType::reset) {
        state_.connection = ControllerConnection::negotiating;
        connected();
    }
    error.clear();
    return true;
}
void ControllerHost::publish(const ApplicationState &s) {
    if (state_.connection != ControllerConnection::connected)
        return;
    if (session_) {
        if (output_.size() >= capacity_) return;
        const bool full_refresh = force_full_refresh_;
        force_full_refresh_ = false;
        session_->observe(s, full_refresh);
        while (output_.size() < capacity_ && session_->has_pending_feedback()) {
            auto item = session_->next_feedback();
            if (!item) break;
            const auto id = protocol_feedback_id(item->control_id,
                                                 state_.protocol_minor);
            if (!id) continue;
            std::vector<std::uint8_t> payload;
            append16(payload, *id);
            if (item->kind == ControllerFeedbackKind::rgb ||
                item->kind == ControllerFeedbackKind::indicator) {
                append32(payload, item->color);
                payload.push_back(std::min(item->brightness, brightness_limit_));
                const auto message = state_.protocol_minor >=
                        NG_CONTROLLER_V2_PROTOCOL_MINOR
                    ? ControllerMessageType::led_render
                    : ControllerMessageType::led;
                if (message == ControllerMessageType::led_render) {
                    payload.push_back(static_cast<std::uint8_t>(
                        item->brightness ? ControllerLedMode::steady
                                         : ControllerLedMode::off));
                    append16(payload, 0); // firmware-default temporal period
                }
                if (!enqueue({message, sequence_++,
                              std::move(payload)})) {
                    state_.feedback_revision = ~std::uint64_t{};
                    break;
                }
                if (*id < state_.leds.size()) state_.leds[*id] = item->color;
            } else if (item->kind == ControllerFeedbackKind::context) {
                auto context = std::move(item->context);
                const auto slot = protocol_context_slot(*id, state_.protocol_minor);
                if (!slot || *slot >= state_.context.size()) continue;
                context.control_id = *slot;
                payload.clear();
                append16(payload, context.control_id);
                if (state_.protocol_minor == 0) {
                    const auto label = truncate_view(context.label, 31);
                    payload.push_back(context.active);
                    payload.push_back(label.size());
                    payload.insert(payload.end(), label.begin(), label.end());
                } else {
                    const auto flags = std::uint8_t(
                        (context.active ? 1 : 0) |
                        (context.bipolar ? 2 : 0) |
                        (context.pickup ? 4 : 0));
                    payload.push_back(flags);
                    payload.push_back(
                        static_cast<std::uint8_t>(context.pickup_direction));
                    append64(payload, context.feedback_revision);
                    const std::string fields[]{
                        std::string(truncate_view(context.label, 24)),
                        std::string(truncate_view(context.value, 16)),
                        std::string(truncate_view(context.unit, 8)),
                        formatted(context.minimum), formatted(context.maximum)};
                    for (const auto &field : fields) {
                        payload.push_back(field.size());
                        payload.insert(payload.end(), field.begin(), field.end());
                    }
                }
                if (!enqueue({ControllerMessageType::context, sequence_++,
                              std::move(payload)})) {
                    state_.feedback_revision = ~std::uint64_t{};
                    break;
                }
                state_.context[context.control_id] = context;
            }
        }
        if (session_->needs_feedback())
            state_.feedback_revision = ~std::uint64_t{};
        else
            state_.feedback_revision = s.revision;
        return;
    }
}
void ControllerHost::tick(std::uint64_t now) noexcept {
    if (state_.connection == ControllerConnection::disconnected ||
        state_.connection == ControllerConnection::incompatible) return;
    if (!last_heartbeat_ms_ || now - last_heartbeat_ms_ >= 500) {
        std::vector<std::uint8_t> payload;
        append64(payload, now);
        enqueue({ControllerMessageType::heartbeat, sequence_++, std::move(payload)});
        last_heartbeat_ms_ = now;
    }
    if (last_receive_ms_ && now > last_receive_ms_ + 2000)
        disconnected("controller heartbeat timeout");
}
std::optional<ControllerPacket> ControllerHost::next_output() noexcept {
    if (output_.empty())
        return {};
    auto p = std::move(output_.front());
    output_.pop_front();
    ++state_.sent;
    return p;
}
ControllerSerialTransport::ControllerSerialTransport(std::size_t c)
    : capacity_(std::max<std::size_t>(1, c)) {}
ControllerSerialTransport::~ControllerSerialTransport() { close(); }
bool ControllerSerialTransport::open(const std::filesystem::path &p,
                                     std::string &e) {
    close();
    fd_ = ::open(p.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        e = std::strerror(errno);
        return false;
    }
    termios t{};
    if (tcgetattr(fd_, &t) == 0) {
        cfmakeraw(&t);
        cfsetspeed(&t, B115200);
        tcsetattr(fd_, TCSANOW, &t);
    }
    identity_ = p;
    e.clear();
    return true;
}
void ControllerSerialTransport::close() noexcept {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    output_.clear();
}
bool ControllerSerialTransport::send(const ControllerPacket &p) noexcept {
    auto bytes = encode_controller_packet(p);
    if (bytes.empty())
        return false;
    if (output_.size() >= capacity_) {
        ++dropped_;
        return false;
    }
    output_.push_back(std::move(bytes));
    return true;
}
bool ControllerSerialTransport::poll(ControllerHost &host,
                                     std::string &e) noexcept {
    if (fd_ < 0) {
        e = "controller serial device is not open";
        return false;
    }
    std::array<std::uint8_t, 512> b{};
    const auto n = ::read(fd_, b.data(), b.size());
    if (n > 0)
        decoder_.push(std::span<const std::uint8_t>(b.data(), n));
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        e = std::strerror(errno);
        host.disconnected(e);
        close();
        return false;
    }
    while (auto p = decoder_.pop())
        host.receive(*p, e);
    while (auto p = host.next_output())
        send(*p);
    while (!output_.empty()) {
        auto &w = output_.front();
        const auto wrote = ::write(fd_, w.data(), w.size());
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (wrote < 0) {
            e = std::strerror(errno);
            host.disconnected(e);
            close();
            return false;
        }
        if (static_cast<std::size_t>(wrote) < w.size()) {
            w.erase(w.begin(), w.begin() + wrote);
            break;
        }
        output_.pop_front();
    }
    e.clear();
    return true;
}
bool SimulatedController::send(ControllerMessageType t,
                               std::vector<std::uint8_t> p) {
    std::string e;
    return host_->receive({t, sequence_++, std::move(p)}, e);
}
bool SimulatedController::negotiate(std::string id, std::uint8_t minor) {
    host_->connected();
    std::vector<std::uint8_t> p{controller_protocol_major, minor,
                                static_cast<std::uint8_t>(id.size()), 3};
    p.insert(p.end(), id.begin(), id.end());
    p.insert(p.end(), {'s', 'i', 'm'});
    if (!send(ControllerMessageType::hello_reply, std::move(p)))
        return false;
    if (minor >= 2) {
        const auto capabilities =
            controller_buttons | controller_relative_encoders | controller_rgb |
            controller_context_feedback | controller_heartbeat |
            controller_inventory_068;
        return send(ControllerMessageType::capabilities,
                    {static_cast<std::uint8_t>(capabilities),
                     static_cast<std::uint8_t>(capabilities >> 8),
                     static_cast<std::uint8_t>(capabilities >> 16),
                     static_cast<std::uint8_t>(capabilities >> 24)});
    }
    return true;
}
bool SimulatedController::button(std::uint16_t id, bool pressed) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    p.push_back(pressed);
    return send(ControllerMessageType::button, std::move(p));
}
bool SimulatedController::encoder(std::uint16_t id, std::int16_t d) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, d);
    return send(ControllerMessageType::encoder, std::move(p));
}
bool SimulatedController::analog(std::uint16_t id, std::uint16_t v) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, v);
    return send(ControllerMessageType::analog, std::move(p));
}
std::vector<ControllerPacket> SimulatedController::drain_feedback() {
    std::vector<ControllerPacket> r;
    while (auto p = host_->next_output())
        r.push_back(std::move(*p));
    return r;
}
ReferenceV2FirmwareSimulator::ReferenceV2FirmwareSimulator(ControllerHost &host)
    : host_(&host), buttons_(reference_v2_wire_controls().size(), false) {}
bool ReferenceV2FirmwareSimulator::send(
    ControllerMessageType type, std::vector<std::uint8_t> payload,
    std::optional<std::uint32_t> sequence) {
    const ControllerPacket source{type, sequence.value_or(sequence_++),
                                  std::move(payload)};
    const auto bytes = encode_controller_packet(source);
    host_decoder_.push(bytes);
    const auto decoded = host_decoder_.pop();
    if (!decoded) return false;
    std::string error;
    const auto accepted = host_->receive(*decoded, error);
    consume_host_output();
    return accepted;
}
void ReferenceV2FirmwareSimulator::consume_host_output() {
    while (auto output = host_->next_output()) {
        const auto bytes = encode_controller_packet(*output);
        firmware_decoder_.push(bytes);
        while (auto decoded = firmware_decoder_.pop()) {
            if (decoded->type == ControllerMessageType::state_sync &&
                decoded->payload.size() == 4)
                generation_ = read32(decoded->payload.data());
            else
                feedback_.push_back(std::move(*decoded));
        }
    }
}
bool ReferenceV2FirmwareSimulator::negotiate() {
    host_->connected();
    consume_host_output();
    const std::string id = NG_CONTROLLER_V2_DEVICE_ID;
    const std::string firmware = "v2-simulator";
    std::vector<std::uint8_t> hello{controller_protocol_major,
        NG_CONTROLLER_V2_PROTOCOL_MINOR, static_cast<std::uint8_t>(id.size()),
        static_cast<std::uint8_t>(firmware.size())};
    hello.insert(hello.end(), id.begin(), id.end());
    hello.insert(hello.end(), firmware.begin(), firmware.end());
    if (!send(ControllerMessageType::hello_reply, std::move(hello))) return false;
    const std::uint32_t capabilities = controller_buttons |
        controller_relative_encoders | controller_rgb |
        controller_context_feedback | controller_heartbeat |
        controller_inventory_v2 | controller_input_state |
        controller_led_render | controller_diagnostics;
    std::vector<std::uint8_t> caps;
    append32(caps, capabilities);
    if (!send(ControllerMessageType::capabilities, std::move(caps))) return false;
    std::vector<std::uint8_t> inventory;
    append16(inventory, NG_CONTROLLER_V2_CONTROL_COUNT);
    append32(inventory, NG_CONTROLLER_V2_INVENTORY_FINGERPRINT);
    if (!send(ControllerMessageType::inventory, std::move(inventory))) return false;
    return synchronize_input_state();
}
bool ReferenceV2FirmwareSimulator::button(std::string_view id, bool pressed,
                                          bool transmit) {
    const auto wire = reference_v2_wire_id(id);
    if (!wire) return false;
    const auto &controls = reference_v2_wire_controls();
    const auto found = std::find_if(controls.begin(), controls.end(),
        [&](const auto &value) { return value.id == id; });
    if (found == controls.end() || found->type != V2WireControlType::button)
        return false;
    buttons_[static_cast<std::size_t>(found - controls.begin())] = pressed;
    if (!transmit) return true;
    std::vector<std::uint8_t> payload;
    append32(payload, generation_);
    append16(payload, *wire);
    payload.push_back(pressed);
    return send(ControllerMessageType::button, std::move(payload));
}
bool ReferenceV2FirmwareSimulator::encoder(std::string_view id,
                                           std::int16_t delta) {
    const auto wire = reference_v2_wire_id(id);
    const auto *control = wire ? reference_v2_wire_control(*wire) : nullptr;
    if (!control || control->type != V2WireControlType::relative_encoder)
        return false;
    std::vector<std::uint8_t> payload;
    append32(payload, generation_);
    append16(payload, *wire);
    append16(payload, static_cast<std::uint16_t>(delta));
    return send(ControllerMessageType::encoder, std::move(payload));
}
bool ReferenceV2FirmwareSimulator::synchronize_input_state() {
    constexpr std::size_t bitmap_size =
        (NG_CONTROLLER_V2_CONTROL_COUNT + 7) / 8;
    std::vector<std::uint8_t> payload;
    append32(payload, generation_);
    payload.resize(4 + bitmap_size);
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i]) payload[4 + i / 8] |= 1u << (i % 8);
    return send(ControllerMessageType::input_state, std::move(payload));
}
bool ReferenceV2FirmwareSimulator::heartbeat(std::uint64_t now) {
    std::vector<std::uint8_t> payload;
    append64(payload, now);
    return send(ControllerMessageType::heartbeat, std::move(payload));
}
void ReferenceV2FirmwareSimulator::reboot() {
    (void)send(ControllerMessageType::reset, {});
    generation_ = 0;
    std::fill(buttons_.begin(), buttons_.end(), false);
}
std::vector<ControllerPacket> ReferenceV2FirmwareSimulator::drain_host_output() {
    consume_host_output();
    auto result = std::move(feedback_);
    feedback_.clear();
    return result;
}
std::string controller_protocol_json(const ControllerProtocolState &s) {
    std::string pickup = "[";
    for (std::size_t i = 0; i < s.pickup_direction.size(); ++i) {
        if (i)
            pickup += ",";
        pickup +=
            "{\"raw\":" + std::to_string(s.analog[i]) +
            ",\"acquired\":" + (s.pickup_acquired[i] ? "true" : "false") +
            ",\"direction\":" + std::to_string(s.pickup_direction[i]) +
            ",\"minimum\":" + std::to_string(s.calibration[i].minimum) +
            ",\"maximum\":" + std::to_string(s.calibration[i].maximum) +
            ",\"dead_zone\":" + std::to_string(s.calibration[i].dead_zone) +
            "}";
    }
    pickup += "]";
    std::string context = "[";
    for (std::size_t i = 0; i < s.context.size(); ++i) {
        if (i)
            context += ",";
        context += "{\"id\":" + std::to_string(s.context[i].control_id) +
                   ",\"label\":\"" + escape(s.context[i].label) +
                   "\",\"value\":\"" + escape(s.context[i].value) +
                   "\",\"active\":" + (s.context[i].active ? "true" : "false") +
                   "}";
    }
    context += "]";
    return "{\"schema\":\"neurogenic.controller-protocol.v1\",\"connection\":" +
           std::to_string(static_cast<int>(s.connection)) +
           ",\"device_id\":\"" + escape(s.device_id) + "\",\"protocol\":\"" +
           std::to_string(s.protocol_major) + "." +
           std::to_string(s.protocol_minor) +
           "\",\"received\":" + std::to_string(s.received) +
           ",\"sent\":" + std::to_string(s.sent) +
           ",\"dropped_input\":" + std::to_string(s.dropped_input) +
           ",\"dropped_output\":" + std::to_string(s.dropped_output) +
           ",\"reconnects\":" + std::to_string(s.reconnects) +
           ",\"input_generation\":" + std::to_string(s.input_generation) +
           ",\"last_input_sequence\":" + std::to_string(s.last_input_sequence) +
           ",\"sequence_gaps\":" + std::to_string(s.sequence_gaps) +
           ",\"stale_input\":" + std::to_string(s.stale_input) +
           ",\"state_synchronizations\":" +
               std::to_string(s.state_synchronizations) +
           ",\"heartbeats\":" + std::to_string(s.heartbeats) +
           ",\"inventory_valid\":" + (s.inventory_valid ? "true" : "false") +
           ",\"input_state_valid\":" + (s.input_state_valid ? "true" : "false") +
           ",\"feedback_revision\":" + std::to_string(s.feedback_revision) +
           ",\"pickup\":" + pickup + ",\"context\":" + context + "}";
}
} // namespace neurogenic
