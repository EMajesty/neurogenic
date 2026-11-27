#include "neurogenic/application/controller_v2_protocol.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace neurogenic {
const std::vector<V2WireControl> &reference_v2_wire_controls() {
    static const auto controls = [] {
        std::vector<V2WireControl> result;
        result.reserve(NG_CONTROLLER_V2_CONTROL_COUNT);
        const auto numbered = [&](std::string_view prefix, std::uint16_t base,
                                  int count, V2WireControlType type) {
            for (int i = 1; i <= count; ++i)
                result.push_back({std::string(prefix) + std::to_string(i),
                                  static_cast<std::uint16_t>(base + i - 1), type});
        };
        numbered("pad.", NG_V2_PAD_1, 16, V2WireControlType::button);
        numbered("page.", NG_V2_PAGE_1, 8, V2WireControlType::button);
        numbered("function.", NG_V2_FUNCTION_1, 8, V2WireControlType::button);
        numbered("parameter.", NG_V2_PARAMETER_1, 8,
                 V2WireControlType::relative_encoder);
        result.push_back({"data", NG_V2_DATA, V2WireControlType::relative_encoder});
        constexpr std::array navigation{"back", "up", "enter", "left", "down", "right"};
        for (std::size_t i = 0; i < navigation.size(); ++i)
            result.push_back({"navigation." + std::string(navigation[i]),
                              static_cast<std::uint16_t>(NG_V2_NAV_BACK + i),
                              V2WireControlType::button});
        constexpr std::array editing{"mute", "solo", "arm", "track", "page",
                                     "erase", "undo", "copy", "shift", "select"};
        for (std::size_t i = 0; i < editing.size(); ++i)
            result.push_back({"edit." + std::string(editing[i]),
                              static_cast<std::uint16_t>(NG_V2_EDIT_MUTE + i),
                              V2WireControlType::button});
        constexpr std::array transport{"click", "tap", "quant", "record-mode",
                                       "fill", "repeat", "loop", "record",
                                       "stop", "play"};
        for (std::size_t i = 0; i < transport.size(); ++i)
            result.push_back({"transport." + std::string(transport[i]),
                              static_cast<std::uint16_t>(NG_V2_TRANSPORT_CLICK + i),
                              V2WireControlType::button});
        return result;
    }();
    return controls;
}
const std::vector<V2WireControl> &reference_v2_optional_push_controls() {
    static const auto controls = [] {
        std::vector<V2WireControl> result;
        result.reserve(NG_CONTROLLER_V2_OPTIONAL_PUSH_COUNT);
        for (std::uint16_t i = 0; i < NG_CONTROLLER_V2_OPTIONAL_PUSH_COUNT; ++i)
            result.push_back({"parameter." + std::to_string(i + 1) + ".push",
                              static_cast<std::uint16_t>(
                                  NG_V2_PARAMETER_PUSH_1 + i),
                              V2WireControlType::button});
        return result;
    }();
    return controls;
}
const V2WireControl *reference_v2_wire_control(std::uint16_t wire_id) {
    for (const auto *controls : {&reference_v2_wire_controls(),
                                 &reference_v2_optional_push_controls()}) {
        const auto found = std::find_if(controls->begin(), controls->end(),
            [=](const auto &value) { return value.wire_id == wire_id; });
        if (found != controls->end()) return &*found;
    }
    return nullptr;
}
std::optional<std::uint16_t> reference_v2_wire_id(std::string_view id) {
    for (const auto *controls : {&reference_v2_wire_controls(),
                                 &reference_v2_optional_push_controls()}) {
        const auto found = std::find_if(controls->begin(), controls->end(),
            [&](const auto &value) { return value.id == id; });
        if (found != controls->end()) return found->wire_id;
    }
    return {};
}
bool validate_reference_v2_wire_manifest(std::string &error) {
    const auto &controls = reference_v2_wire_controls();
    std::set<std::string> ids;
    std::set<std::uint16_t> wire_ids;
    if (controls.size() != NG_CONTROLLER_V2_CONTROL_COUNT) {
        error = "V2 wire manifest does not contain 67 controls";
        return false;
    }
    for (const auto &control : controls)
        if (control.id.empty() || !ids.insert(control.id).second ||
            !wire_ids.insert(control.wire_id).second) {
            error = "V2 wire manifest contains an empty or duplicate identity";
            return false;
    }
    for (const auto &control : reference_v2_optional_push_controls())
        if (control.id.empty() || !ids.insert(control.id).second ||
            !wire_ids.insert(control.wire_id).second) {
            error = "V2 optional wire manifest contains an empty or duplicate identity";
            return false;
        }
    error.clear();
    return true;
}
} // namespace neurogenic
