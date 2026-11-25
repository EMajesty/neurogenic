#pragma once

#include "neurogenic/application/discovery.hpp"
#include <functional>
#include <map>

namespace neurogenic {

enum class AssignmentTargetKind : std::uint8_t {
    parameter,
    action,
    contextual_role,
    performance_role
};

struct AssignmentArgumentChoice {
    std::string value, label;
};

struct AssignmentArgumentField {
    std::string name, label;
    ValueType type{ValueType::none};
    bool required{true};
    std::vector<AssignmentArgumentChoice> choices;
};

struct AssignmentTarget {
    AssignmentTargetKind kind{AssignmentTargetKind::parameter};
    std::string category, object_id, id, label, diagnostic;
    bool available{true}, performance_safe{};
    std::vector<AssignmentArgumentField> arguments;
};

struct AssignmentProposal {
    AssignmentTargetKind kind{AssignmentTargetKind::parameter};
    std::string target_id;
    std::map<std::string, ApplicationValue> arguments;
};

// Copy-only discovery view shared by project Performance authoring and
// appliance controller-profile authoring. It owns neither persistence nor the
// selected musical state; callers apply their own compatibility policy.
class AssignmentTargetPicker {
  public:
    using ActionFilter = std::function<bool(const ActionDescriptor &)>;
    explicit AssignmentTargetPicker(const ApplicationService &);
    [[nodiscard]] std::vector<AssignmentTarget>
    targets(ActionFilter action_filter = {}) const;
    [[nodiscard]] bool validate(const AssignmentProposal &, std::string &error,
                                ActionFilter action_filter = {}) const;
    [[nodiscard]] static bool parse_argument(const AssignmentArgumentField &,
                                             std::string_view,
                                             ApplicationValue &,
                                             std::string &error);

  private:
    const ApplicationService *application_{};
};

[[nodiscard]] bool
performance_safe_assignment_action(const ActionDescriptor &) noexcept;

} // namespace neurogenic
