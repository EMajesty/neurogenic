#include "neurogenic/application/reference_controller.hpp"
#include <algorithm>

namespace neurogenic {
namespace {
const std::array<ReferenceDestination, 8> primary{{
    {"performance", "PERF", "Performance", "performance", "root"},
    {"sequencer", "SEQ", "Sequencer", "sequencer", "steps"},
    {"sample", "SAMPLE", "Sample", "sample", "sample"},
    {"sound", "SOUND", "Sound", "sound", "instrument"},
    {"mixer", "MIX", "Mixer", "mixer", "tracks"},
    {"modulation", "MOD", "Modulation", "fx", "modulation"},
    {"macros", "MACRO", "Macros", "fx", "macros"},
    {"browser", "BROWSE", "Browser", "browser", "root"},
}};
const std::array<ReferenceDestination, 8> secondary{{
    {"xy", "XY", "XY Performance", "performance", "xy"},
    {"song", "SONG", "Song Arrangement", "sequencer", "arrangement"},
    {"slice", "SLICE", "Slice Editor", "sample", "slice"},
    {"presets", "PRESET", "Presets", "sound", "presets"},
    {"fx", "FX", "Effects", "fx", "chain"},
    {"modulation-routing", "ROUTE", "Modulation Routing", "fx", "routes"},
    {"scenes", "SCENE", "Scenes", "fx", "scenes"},
    {"setup", "SETUP", "Setup", "setup", "audio"},
}};
ControlCapability button(std::string id, std::string label) {
    return {std::move(id),
            std::move(label),
            ControlType::momentary_button,
            {FeedbackType::rgb_led}};
}
ControlBinding binding(std::string id, std::string semantic, int index = -1) {
    ControlBinding result;
    result.control_id = std::move(id);
    result.semantic = std::move(semantic);
    result.index = index;
    return result;
}
} // namespace

const std::array<ReferenceDestination, 8> &reference_primary_destinations() {
    return primary;
}
const std::array<ReferenceDestination, 8> &reference_secondary_destinations() {
    return secondary;
}
const ReferenceDestination *reference_destination(std::string_view id) {
    for (const auto *table : {&primary, &secondary})
        for (const auto &item : *table)
            if (item.id == id)
                return &item;
    return nullptr;
}

ControllerDeviceDescriptor reference_controller_v2_descriptor() {
    ControllerDeviceDescriptor result;
    result.descriptor_id = "neurogenic.reference-controller-v2.v1";
    result.device_id = "neurogenic.reference-controller-v2";
    result.display_name = "NEUROGENIC Reference Controller V2";
    result.transport = ControllerTransport::simulated;
    result.protocol_major = 2;
    result.protocol_minor = 0;
    for (int i = 1; i <= 8; ++i)
        result.controls.push_back(
            button("page." + std::to_string(i), "Page " + std::to_string(i)));
    for (int i = 1; i <= 8; ++i)
        result.controls.push_back(button("function." + std::to_string(i),
                                         "Function " + std::to_string(i)));
    for (int i = 1; i <= 8; ++i)
        result.controls.push_back({"parameter." + std::to_string(i),
                                   "Parameter " + std::to_string(i),
                                   ControlType::relative_encoder,
                                   {FeedbackType::parameter_value}});
    result.controls.push_back({"data",
                               "DATA",
                               ControlType::relative_encoder,
                               {FeedbackType::parameter_value}});
    for (const auto id : {"back", "up", "enter", "left", "down", "right"})
        result.controls.push_back(button("navigation." + std::string(id), id));
    for (const auto id : {"mute", "solo", "arm", "track", "page", "erase",
                          "undo", "copy", "shift", "select"})
        result.controls.push_back(button("edit." + std::string(id), id));
    for (const auto id : {"click", "tap", "quant", "record-mode", "fill",
                          "repeat", "loop", "record", "stop", "play"})
        result.controls.push_back(button("transport." + std::string(id), id));
    for (int i = 1; i <= 16; ++i)
        result.controls.push_back(
            button("pad." + std::to_string(i), "Pad " + std::to_string(i)));
    return result;
}

ControllerProfile reference_controller_v2_profile() {
    ControllerProfile result;
    result.profile_id = "neurogenic.reference-controller-v2.default.v1";
    result.device_id = "neurogenic.reference-controller-v2";
    result.capabilities = {result.device_id, "Reference Controller V2",
                           reference_controller_v2_descriptor().controls, true};
    for (int i = 1; i <= 8; ++i)
        result.bindings.push_back(
            binding("page." + std::to_string(i), "reference-page", i - 1));
    for (int i = 1; i <= 8; ++i)
        result.bindings.push_back(
            binding("function." + std::to_string(i), "context-button", i - 1));
    for (int i = 1; i <= 8; ++i)
        result.bindings.push_back(binding("parameter." + std::to_string(i),
                                          "context-encoder", i - 1));
    result.bindings.push_back(binding("edit.shift", "modifier-shift"));
    for (int i = 1; i <= 16; ++i)
        result.bindings.push_back(
            binding("pad." + std::to_string(i), "performance-pad", i - 1));
    for (const auto &control : result.capabilities.controls)
        if (std::none_of(result.bindings.begin(), result.bindings.end(),
                         [&](const auto &item) {
                             return item.control_id == control.id;
                         })) {
            ControlBinding empty;
            empty.control_id = control.id;
            empty.explicitly_unassigned = true;
            result.bindings.push_back(std::move(empty));
        }
    return result;
}

GestureConfiguration reference_controller_v2_gestures() {
    GestureConfiguration result;
    result.maximum_active_controls = 67;
    for (const auto &control : reference_controller_v2_descriptor().controls) {
        GestureControlPolicy policy;
        policy.control_id = control.id;
        policy.policy = GestureDispatchPolicy::immediate;
        policy.global_across_context = control.id.starts_with("page.");
        if (control.id == "edit.shift") {
            policy.modifier = "shift";
            policy.policy = GestureDispatchPolicy::modifier_only;
            policy.latchable = true;
            policy.global_across_context = true;
        }
        result.controls.push_back(std::move(policy));
    }
    return result;
}
} // namespace neurogenic
