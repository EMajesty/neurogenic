#pragma once
#include "neurogenic/application/control.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <map>

namespace neurogenic {
enum class ControlType : std::uint8_t {
    momentary_button,
    toggle_button,
    relative_encoder,
    absolute_potentiometer,
    fader,
    crossfader,
    joystick,
    touch_xy,
    keyboard,
    pointer
};
enum class FeedbackType : std::uint8_t {
    rgb_led,
    single_led,
    text_display,
    parameter_value,
    motorized_fader
};
struct ControlCapability {
    std::string id, label;
    ControlType type{};
    std::vector<FeedbackType> feedback;
    double minimum{}, maximum{1.0};
    bool available{true};
};
struct ControllerCapabilities {
    std::string controller_id, name;
    std::vector<ControlCapability> controls;
    bool touchscreen{};
};
struct ControlBinding {
    std::string control_id, action_id, parameter_id, feedback_id, label;
    bool shift{}, select{}, absolute{};
    std::string semantic, context;
    std::int32_t index{-1};
    double fine_scale{0.1}, acceleration{2.0};
    bool pickup{}, invert{}, explicitly_unassigned{};
    std::map<std::string, ApplicationValue> arguments;
};
struct ControllerProfile {
    std::uint32_t schema_version{2};
    std::string profile_id, device_id;
    ControllerCapabilities capabilities;
    std::vector<ControlBinding> bindings;
    bool fallback_to_base{true};
};
enum class ControllerTransport : std::uint8_t { usb_cdc, midi, simulated, future };
struct ControllerDeviceDescriptor {
    std::uint32_t schema_version{1};
    std::string descriptor_id, device_id, display_name;
    ControllerTransport transport{ControllerTransport::simulated};
    std::uint8_t protocol_major{}, protocol_minor{};
    std::vector<ControlCapability> controls;
    std::size_t maximum_feedback_queue{128};
    std::uint8_t brightness_limit{224};
};
[[nodiscard]] bool validate_device_descriptor(const ControllerDeviceDescriptor &,
                                              std::string &error);
[[nodiscard]] ControllerDeviceDescriptor rp2040_controller_descriptor();
[[nodiscard]] ControllerDeviceDescriptor
legacy_controller_descriptor(std::uint8_t protocol_minor,
                             std::uint32_t capabilities);
struct ControllerInput {
    std::string control_id;
    double value{}, secondary{};
    bool pressed{};
    std::uint64_t sequence{};
};
struct ResolvedInput {
    std::optional<ApplicationCommand> action;
    std::string parameter_id;
    std::optional<ApplicationValue> parameter_value;
    double relative_steps{};
};
struct FeedbackMessage {
    std::string destination;
    ApplicationValue value;
    std::string origin;
    std::uint64_t state_revision{};
};

class ControllerMapper {
  public:
    ControllerMapper(ControllerProfile, const ActionRegistry &,
                     std::string &error);
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] ResolvedInput resolve(const ControllerInput &,
                                        const ModifierState &,
                                        std::string &error) const;
    [[nodiscard]] std::vector<FeedbackMessage>
    feedback(const ApplicationState &, std::string_view origin = {}) const;

  private:
    ControllerProfile profile_;
    const ActionRegistry *actions_{};
    bool valid_{};
};
bool validate_profile(const ControllerProfile &, const ActionRegistry &,
                      std::string &error);
bool parse_controller_profile(std::string_view, ControllerProfile &,
                              std::string &error);
[[nodiscard]] std::string serialize_controller_profile(const ControllerProfile &);
bool validate_controller_configuration(const ControllerDeviceDescriptor &,
                                       const ControllerProfile &,
                                       const ApplicationService &,
                                       std::string &error);
[[nodiscard]] ControllerProfile full_performance_profile();
[[nodiscard]] ControllerProfile
legacy_performance_profile(std::uint8_t protocol_minor,
                           std::uint32_t capabilities);
[[nodiscard]] ControllerProfile minimal_controller_profile();
[[nodiscard]] ControllerProfile generic_midi_profile();

enum class GridEventType : std::uint8_t {
    select_step,
    toggle_step,
    trigger_pad,
    trigger_level,
    trigger_note,
    unsupported
};
struct GridEvent {
    GridEventType type{GridEventType::unsupported};
    std::uint64_t index{};
    std::uint8_t note{}, velocity{};
    SampleSlotId slot{};
    float pitch_offset{};
    std::string reason;
};
class PerformanceGrid {
  public:
    void set_mode(PadMode mode) noexcept { mode_ = mode; }
    void set_step_page(std::uint64_t page) noexcept { page_ = page; }
    void set_selected_slot(SampleSlotId slot) noexcept { slot_ = slot; }
    void set_root_note(std::uint8_t note) noexcept { root_ = note; }
    void set_selected_pad(std::size_t pad) noexcept {
        selected_pad_ = std::min<std::size_t>(pad, 15);
    }
    [[nodiscard]] GridEvent press(std::size_t pad,
                                  bool select_only = false) const;

  private:
    PadMode mode_{PadMode::step};
    std::uint64_t page_{};
    SampleSlotId slot_{};
    std::uint8_t root_{60};
    std::size_t selected_pad_{};
};
[[nodiscard]] std::optional<ApplicationCommand>
grid_application_command(const GridEvent &, std::string source_id = {},
                         std::uint64_t sequence = 0);
} // namespace neurogenic
