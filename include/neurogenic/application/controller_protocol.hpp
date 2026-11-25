#pragma once
#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/controller_v2_protocol.hpp"
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
inline constexpr std::uint8_t controller_protocol_minor = 3;
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
    reset,
    inventory,
    input_state,
    led_render,
    diagnostic
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
    controller_inventory_068 = 1u << 6,
    controller_inventory_v2 = 1u << 7,
    controller_input_state = 1u << 8,
    controller_led_render = 1u << 9,
    controller_diagnostics = 1u << 10
};
enum class ControllerLedMode : std::uint8_t {
    off = NG_LED_OFF, steady = NG_LED_STEADY, dim = NG_LED_DIM,
    blink = NG_LED_BLINK, pulse = NG_LED_PULSE
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
        reconnects{}, event_rate{}, feedback_revision{}, sequence_gaps{},
        duplicate_input{}, stale_input{}, state_synchronizations{}, heartbeats{};
    std::uint32_t input_generation{}, last_input_sequence{};
    bool inventory_valid{}, input_state_valid{};
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
    void set_brightness_limit(std::uint8_t value) noexcept;
    void tick(std::uint64_t monotonic_milliseconds) noexcept;
    [[nodiscard]] std::optional<ControllerPacket> next_output() noexcept;
    [[nodiscard]] ControllerProtocolState snapshot() const { return state_; }
    [[nodiscard]] ControllerSessionState session_snapshot() const {
        return session_ ? session_->state() : ControllerSessionState{};
    }

  private:
    bool enqueue(ControllerPacket) noexcept;
    bool configure_session(std::uint32_t capabilities, std::string &error);
    bool accept_input_sequence(const ControllerPacket &, std::string &error);
    void request_input_state() noexcept;
    ApplicationService *application_{};
    std::deque<ControllerPacket> output_;
    std::size_t capacity_{};
    std::uint32_t sequence_{1};
    std::string expected_device_id_;
    ControllerProtocolState state_;
    std::optional<ControllerSession> session_;
    std::uint8_t brightness_limit_{224};
    bool force_full_refresh_{true};
    std::uint64_t last_receive_ms_{}, last_heartbeat_ms_{};
    std::uint32_t next_input_generation_{1};
    std::vector<bool> quarantined_buttons_;
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
                   std::uint8_t minor = 2);
    bool button(std::uint16_t id, bool pressed);
    bool encoder(std::uint16_t id, std::int16_t delta);
    bool analog(std::uint16_t id, std::uint16_t value);
    [[nodiscard]] std::vector<ControllerPacket> drain_feedback();

  private:
    bool send(ControllerMessageType, std::vector<std::uint8_t>);
    ControllerHost *host_{};
    std::uint32_t sequence_{1};
};

// Byte-stream fixture implementing the firmware side of protocol 1.3. It uses
// production framing/decoding and never calls ControllerSession directly.
class ReferenceV2FirmwareSimulator {
  public:
    explicit ReferenceV2FirmwareSimulator(ControllerHost &host);
    bool negotiate();
    bool button(std::string_view logical_id, bool pressed,
                bool transmit = true);
    bool encoder(std::string_view logical_id, std::int16_t delta);
    bool synchronize_input_state();
    bool heartbeat(std::uint64_t monotonic_milliseconds);
    void reboot();
    [[nodiscard]] std::vector<ControllerPacket> drain_host_output();
    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] const std::vector<ControllerPacket> &received_feedback() const {
        return feedback_;
    }
  private:
    bool send(ControllerMessageType, std::vector<std::uint8_t>,
              std::optional<std::uint32_t> sequence = {});
    void consume_host_output();
    ControllerHost *host_{};
    ControllerFrameDecoder host_decoder_, firmware_decoder_;
    std::uint32_t sequence_{1}, generation_{};
    std::vector<bool> buttons_;
    std::vector<ControllerPacket> feedback_;
};
[[nodiscard]] std::string
controller_protocol_json(const ControllerProtocolState &);
} // namespace neurogenic
