#pragma once
#include "neurogenic/controller_v2_wire.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {
enum class V2WireControlType : std::uint8_t { button, relative_encoder };
struct V2WireControl {
    std::string id;
    std::uint16_t wire_id{};
    V2WireControlType type{V2WireControlType::button};
};
[[nodiscard]] const std::vector<V2WireControl> &reference_v2_wire_controls();
[[nodiscard]] const std::vector<V2WireControl> &
reference_v2_optional_push_controls();
[[nodiscard]] const V2WireControl *reference_v2_wire_control(std::uint16_t);
[[nodiscard]] std::optional<std::uint16_t>
reference_v2_wire_id(std::string_view);
[[nodiscard]] bool validate_reference_v2_wire_manifest(std::string &error);
} // namespace neurogenic
