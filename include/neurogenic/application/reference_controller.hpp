#pragma once

#include "neurogenic/application/controller.hpp"
#include "neurogenic/application/input_gesture.hpp"
#include <array>

namespace neurogenic {

struct ReferenceDestination {
    std::string id, short_label, title, page_id, subview;
    bool available{true};
};

[[nodiscard]] const std::array<ReferenceDestination, 8> &
reference_primary_destinations();
[[nodiscard]] const std::array<ReferenceDestination, 8> &
reference_secondary_destinations();
[[nodiscard]] const ReferenceDestination *
reference_destination(std::string_view id);
[[nodiscard]] ControllerDeviceDescriptor reference_controller_v2_descriptor();
[[nodiscard]] ControllerProfile reference_controller_v2_profile();
[[nodiscard]] GestureConfiguration reference_controller_v2_gestures();

} // namespace neurogenic
