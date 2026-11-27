#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint16_t read16(const std::vector<std::uint8_t> &value) {
    return std::uint16_t(value[0]) | std::uint16_t(value[1]) << 8;
}
std::uint32_t read32(const std::vector<std::uint8_t> &value,
                     std::size_t offset) {
    return std::uint32_t(value[offset]) |
           std::uint32_t(value[offset + 1]) << 8 |
           std::uint32_t(value[offset + 2]) << 16 |
           std::uint32_t(value[offset + 3]) << 24;
}
std::vector<std::uint8_t> hex(std::string_view text) {
    std::vector<std::uint8_t> result;
    const auto digit = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        throw std::runtime_error("invalid fixture hex");
    };
    if (text.size() % 2) throw std::runtime_error("odd fixture hex");
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>(digit(text[i]) * 16 +
                                                   digit(text[i + 1])));
    return result;
}
void golden(neurogenic::ControllerMessageType type, std::uint32_t sequence,
            std::string_view payload, std::string_view frame) {
    require(neurogenic::encode_controller_packet(
                {type, sequence, hex(payload)}) == hex(frame),
            "normative golden frame changed");
}
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0xffffffffu;
    for (const auto byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            value = value & 1 ? (value >> 1) ^ 0xedb88320u : value >> 1;
    }
    return value ^ 0xffffffffu;
}
std::uint32_t optional_fingerprint(bool pushes) {
    std::vector<std::uint8_t> canonical;
    for (std::uint16_t index = 0; index < 8; ++index) {
        const auto identity = "parameter." + std::to_string(index + 1) +
                              (pushes ? ".push" : "");
        canonical.insert(canonical.end(), identity.begin(), identity.end());
        canonical.push_back(0);
        const auto id = static_cast<std::uint16_t>(
            (pushes ? NG_V2_PARAMETER_PUSH_1 : NG_V2_PARAMETER_1) + index);
        canonical.push_back(id & 255);
        canonical.push_back(id >> 8);
        canonical.push_back(pushes ? 1 : 2);
    }
    return crc32(canonical);
}
}

int main() try {
    using namespace neurogenic;
    AudioEngine engine;
    Timeline timeline_value;
    timeline_value.tracks = {{1, "Protocol", false, {3840, {}}}};
    TimelineService timeline([&](const Timeline &value, std::string &message) {
        return engine.queue_timeline(value, message);
    }, timeline_value);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 128);
    require(optional_fingerprint(true) ==
                NG_CONTROLLER_V2_PUSH_INVENTORY_FINGERPRINT &&
                optional_fingerprint(false) ==
                NG_CONTROLLER_V2_RGB_INVENTORY_FINGERPRINT,
            "canonical optional inventory fingerprints changed");

    golden(ControllerMessageType::hello, 1, "0103",
           "4e47010102000100000001037569");
    golden(ControllerMessageType::inventory, 2, "43004e435632",
           "4e47010d06000200000043004e435632001e");
    golden(ControllerMessageType::button, 3, "07000000100101",
           "4e470104070003000000070000001001019f26");
    golden(ControllerMessageType::button, 4, "07000000100100",
           "4e47010407000400000007000000100100031f");
    golden(ControllerMessageType::encoder, 5, "0700000020010300",
           "4e470105080005000000070000002001030080af");
    golden(ControllerMessageType::encoder, 6, "070000002001feff",
           "4e470105080006000000070000002001feff72d1");
    golden(ControllerMessageType::input_state, 7,
           "07000000000000000000000000",
           "4e47010e0d000700000007000000000000000000000000a0fe");
    golden(ControllerMessageType::led_render, 8, "000100ff000080010000",
           "4e47010f0a0008000000000100ff000080010000ffab");
    golden(ControllerMessageType::heartbeat, 9, "e803000000000000",
           "4e47010a080009000000e8030000000000007ac2");
    golden(ControllerMessageType::reset, 10, "",
           "4e47010c00000a000000b639");
    golden(ControllerMessageType::hello, 1, "0104",
           "4e47010102000100000001049219");
    golden(ControllerMessageType::inventory, 2,
           "43004e4356320108000035cd361902080000c7767c6b",
           "4e47010d16000200000043004e4356320108000035cd361902080000c7767c6b2cee");
    golden(ControllerMessageType::button, 3, "07000000600101",
           "4e4701040700030000000700000060010197fe");
    golden(ControllerMessageType::button, 4, "07000000600100",
           "4e470104070004000000070000006001000bc7");
    golden(ControllerMessageType::input_state, 5,
           "0700000000000000000000000001",
           "4e47010e0e000500000007000000000000000000000000016610");
    golden(ControllerMessageType::led_render, 6, "2001b3bc510070010000",
           "4e47010f0a00060000002001b3bc510070010000c1ae");

    {
        ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
        ReferenceV2FirmwareSimulator firmware(
            host, ReferenceV2Personality::baseline_1_3);
        require(firmware.negotiate(), "1.4 host rejected 1.3 controller");
        const auto state = host.snapshot();
        require(state.protocol_minor == 3 && state.inventory_valid &&
                    !state.optional_push_inventory_valid &&
                    !state.optional_rgb_inventory_valid,
                "baseline fallback enabled optional facilities");
        require(!firmware.button("parameter.1.push", true),
                "baseline simulator emitted optional push");
    }
    {
        ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID,
                            NG_CONTROLLER_V2_BASELINE_MINOR);
        ReferenceV2FirmwareSimulator firmware(
            host, ReferenceV2Personality::push_and_rgb);
        require(firmware.negotiate(), "1.3 host / 1.4 controller fallback");
        const auto state = host.snapshot();
        require(state.protocol_minor == 3 && !state.optional_push_inventory_valid &&
                    !state.optional_rgb_inventory_valid,
                "1.3 host enabled 1.4 facilities");
    }
    for (const auto personality : {ReferenceV2Personality::push_only,
                                   ReferenceV2Personality::rgb_only,
                                   ReferenceV2Personality::push_and_rgb,
                                   ReferenceV2Personality::unknown_capability}) {
        ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
        ReferenceV2FirmwareSimulator firmware(host, personality);
        require(firmware.negotiate(), "valid 1.4 personality rejected");
        const auto state = host.snapshot();
        require(state.protocol_minor == 4 && state.connection ==
                    ControllerConnection::connected,
                "1.4 personality did not connect");
        const bool push = personality == ReferenceV2Personality::push_only ||
                          personality == ReferenceV2Personality::push_and_rgb;
        const bool rgb = personality == ReferenceV2Personality::rgb_only ||
                         personality == ReferenceV2Personality::push_and_rgb;
        require(state.optional_push_inventory_valid == push &&
                    state.optional_rgb_inventory_valid == rgb,
                "independent optional capability validation changed");
    }
    for (const auto personality : {
             ReferenceV2Personality::incorrect_optional_fingerprint,
             ReferenceV2Personality::missing_optional_inventory,
             ReferenceV2Personality::malformed_optional_inventory}) {
        ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
        ReferenceV2FirmwareSimulator firmware(host, personality);
        require(!firmware.negotiate() && host.snapshot().connection ==
                    ControllerConnection::incompatible,
                "invalid optional inventory was accepted");
    }

    ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator firmware(
        host, ReferenceV2Personality::push_and_rgb);
    require(firmware.negotiate(), "both-capability negotiation");
    const auto revision = application.snapshot().revision;
    require(firmware.button("parameter.1.push", true) &&
                firmware.button("parameter.1.push", false),
            "optional push transport");
    (void)application.dispatch_pending();
    require(application.snapshot().revision == revision,
            "unassigned optional push mutated musical state");
    {
        auto descriptor = reference_controller_v2_descriptor();
        descriptor.controls.push_back({"parameter.1.push", "Parameter 1 push",
                                       ControlType::momentary_button, {}});
        auto profile = reference_controller_v2_profile();
        profile.capabilities.controls.push_back(descriptor.controls.back());
        ControlBinding binding;
        binding.control_id = "parameter.1.push";
        binding.action_id = "transport.play.v1";
        profile.bindings.push_back(binding);
        ControllerSession assigned(application, std::move(descriptor),
                                   std::move(profile), "assigned-push");
        require(assigned.valid(), "compatible optional push assignment invalid");
        std::string error;
        require(assigned.input({"parameter.1.push", 1.0, 0.0, true, 1, 1},
                               error),
                "compatible optional push assignment dispatch");
        (void)application.dispatch_pending();
        require(application.snapshot().playing,
                "explicit optional push assignment did not operate");
        (void)application.execute({"transport.stop.v1", {}, "test", 2});
    }

    require(firmware.button("page.4", true) &&
                firmware.button("page.4", false), "Sound destination input");
    (void)application.dispatch_pending();
    host.publish(application.snapshot());
    const auto packets = firmware.drain_host_output();
    const auto encoder_led = std::find_if(packets.begin(), packets.end(),
        [](const auto &packet) {
            return packet.type == ControllerMessageType::led_render &&
                   packet.payload.size() == 10 &&
                   read16(packet.payload) >= NG_V2_PARAMETER_1 &&
                   read16(packet.payload) <= NG_V2_PARAMETER_8;
        });
    require(encoder_led != packets.end(),
            "negotiated encoder RGB projection was not emitted");
    require(encoder_led->payload[6] >=
                NG_CONTROLLER_V2_ASSIGNED_MINIMUM_BRIGHTNESS,
            "assigned encoder minimum brightness was not applied");
    require(read32(encoder_led->payload, 2) == 0x51bcb3,
            "Sound parameter functional hue changed");

    host.disconnected("reconnect test");
    require(firmware.negotiate(), "extended reconnect");
    host.publish(application.snapshot());
    const auto refresh = firmware.drain_host_output();
    require(std::any_of(refresh.begin(), refresh.end(), [](const auto &packet) {
        return packet.type == ControllerMessageType::led_render &&
               packet.payload.size() == 10 &&
               read16(packet.payload) >= NG_V2_PARAMETER_1 &&
               read16(packet.payload) <= NG_V2_PARAMETER_8;
    }), "reconnect omitted complete encoder feedback refresh");

    samples.shutdown();
    std::cout << "neurogenic.controller-v2-protocol-1.4-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
