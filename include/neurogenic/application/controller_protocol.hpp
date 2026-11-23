#pragma once
#include "neurogenic/application/controller_session.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
inline constexpr std::uint8_t controller_protocol_major = 1;
inline constexpr std::uint8_t controller_protocol_minor = 2;
inline constexpr std::size_t controller_maximum_payload = 256;
inline constexpr std::size_t controller_maximum_frame = 268;

enum class ControllerMessageType : std::uint8_t {
    hello = 1,
    hello_reply,
    capabilities,
    button,
    encoder,
    analog,
    led,
    context,
    state_sync,
    heartbeat,
    error,
    reset
};
struct ControllerPacket {
    ControllerMessageType type{};
    std::uint32_t sequence{};
    std::vector<std::uint8_t> payload;
};
[[nodiscard]] std::vector<std::uint8_t>
encode_controller_packet(const ControllerPacket &);
class ControllerFrameDecoder {
  public:
    explicit ControllerFrameDecoder(std::size_t capacity = 1024)
        : capacity_(capacity) {}
    void push(std::span<const std::uint8_t>) noexcept;
    [[nodiscard]] std::optional<ControllerPacket> pop() noexcept;
    [[nodiscard]] std::uint64_t invalid_frames() const noexcept {
        return invalid_;
    }
    [[nodiscard]] std::uint64_t dropped_bytes() const noexcept {
        return dropped_;
    }

  private:
    std::deque<std::uint8_t> bytes_;
    std::size_t capacity_{};
    std::uint64_t invalid_{}, dropped_{};
};

enum class ControllerConnection : std::uint8_t {
    disconnected,
    negotiating,
    connected,
    incompatible,
    error
};
enum class DedicatedControlId : std::uint16_t {
    // 24..35 retain their protocol-1.1 meanings for legacy devices.
    play = 24,
    stop,
    record,
    shift,
    select,
    data_push,
    mode_step,
    mode_pad,
    mode_levels,
    mode_notes,
    page_previous,
    page_next,
    page_select_1 = 36,
    page_select_2,
    page_select_3,
    page_select_4,
    page_select_5,
    page_select_6,
    page_select_7,
    page_select_8 = 43,
    edit_1 = 44,
    edit_2,
    edit_3,
    edit_4,
    edit_5,
    edit_6,
    edit_7,
    edit_8,
    edit_9,
    edit_10 = 53,
    performance_1 = 54,
    performance_2,
    performance_3,
    performance_4,
    performance_5,
    performance_6,
    performance_7,
    performance_8,
    performance_9,
    performance_10 = 63,
    navigation_up = 64,
    navigation_down,
    navigation_left,
    navigation_right,
    navigation_confirm,
    navigation_back
};
enum ControllerCapability : std::uint32_t {
    controller_buttons = 1u << 0,
    controller_relative_encoders = 1u << 1,
    controller_analog = 1u << 2,
    controller_rgb = 1u << 3,
    controller_context_feedback = 1u << 4,
    controller_heartbeat = 1u << 5,
    controller_inventory_068 = 1u << 6
};
struct AnalogCalibration {
    std::uint16_t minimum{}, maximum{65535}, dead_zone{128};
};
struct ControllerProtocolState {
    ControllerConnection connection{ControllerConnection::disconnected};
    std::string device_id, firmware, error;
    std::uint8_t protocol_major{}, protocol_minor{};
    std::uint32_t capabilities{};
    std::uint64_t received{}, sent{}, dropped_input{}, dropped_output{},
        reconnects{}, event_rate{}, feedback_revision{};
    std::array<std::uint16_t, 5> analog{};
    std::array<AnalogCalibration, 5> calibration{};
    std::array<bool, 5> pickup_acquired{};
    std::array<std::int8_t, 5> pickup_direction{};
    std::array<std::int32_t, 9> encoders{};
    std::array<std::uint32_t, 70> leds{};
    std::array<ContextControl, 16> context{};
};

class ControllerHost {
  public:
    explicit ControllerHost(ApplicationService &,
                            std::size_t queue_capacity = 128,
                            std::string expected_device_id = {});
    bool receive(const ControllerPacket &, std::string &error) noexcept;
    void publish(const ApplicationState &);
    void connected();
    void disconnected(std::string reason = {});
    bool set_analog_calibration(std::size_t, AnalogCalibration,
                                std::string &error) noexcept;
    void set_brightness_limit(std::uint8_t value) noexcept {
        brightness_limit_ = value;
        state_.feedback_revision = ~std::uint64_t{};
    }
    [[nodiscard]] std::optional<ControllerPacket> next_output() noexcept;
    [[nodiscard]] ControllerProtocolState snapshot() const { return state_; }

  private:
    bool enqueue(ControllerPacket) noexcept;
    void dispatch_input(std::string id, double value, bool pressed,
                        std::uint64_t sequence);
    ApplicationService *application_{};
    std::deque<ControllerPacket> output_;
    std::size_t capacity_{};
    std::uint32_t sequence_{1};
    std::string expected_device_id_;
    ControllerProtocolState state_;
    std::optional<ControllerSession> session_;
    std::array<bool, 5> pickup_{};
    std::array<double, 5> pickup_target_{};
    std::array<double, 5> last_effective_target_{};
    std::uint8_t brightness_limit_{224};
};

class ControllerSerialTransport {
  public:
    explicit ControllerSerialTransport(std::size_t queue_capacity = 128);
    ~ControllerSerialTransport();
    bool open(const std::filesystem::path &, std::string &error);
    void close() noexcept;
    bool poll(ControllerHost &, std::string &error) noexcept;
    bool send(const ControllerPacket &) noexcept;
    [[nodiscard]] bool connected() const noexcept { return fd_ >= 0; }
    [[nodiscard]] std::uint64_t dropped_output() const noexcept {
        return dropped_;
    }

  private:
    int fd_{-1};
    std::filesystem::path identity_;
    ControllerFrameDecoder decoder_;
    std::deque<std::vector<std::uint8_t>> output_;
    std::size_t capacity_{};
    std::uint64_t dropped_{};
};

class SimulatedController {
  public:
    explicit SimulatedController(ControllerHost &host) : host_(&host) {}
    bool negotiate(std::string device_id = "neurogenic.simulator",
                   std::uint8_t minor = controller_protocol_minor);
    bool button(std::uint16_t id, bool pressed);
    bool encoder(std::uint16_t id, std::int16_t delta);
    bool analog(std::uint16_t id, std::uint16_t value);
    [[nodiscard]] std::vector<ControllerPacket> drain_feedback();

  private:
    bool send(ControllerMessageType, std::vector<std::uint8_t>);
    ControllerHost *host_{};
    std::uint32_t sequence_{1};
};
[[nodiscard]] std::string
controller_protocol_json(const ControllerProtocolState &);
} // namespace neurogenic
