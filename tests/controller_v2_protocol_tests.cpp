#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using namespace neurogenic;
    std::string error;
    require(validate_reference_v2_wire_manifest(error), error.c_str());
    require(reference_v2_wire_controls().size() == 67,
            "V2 manifest control count");
    const auto descriptor = reference_controller_v2_descriptor();
    require(descriptor.transport == ControllerTransport::usb_cdc &&
                descriptor.protocol_major == 1 && descriptor.protocol_minor == 3,
            "V2 physical descriptor protocol");
    for (const auto &control : descriptor.controls)
        require(reference_v2_wire_id(control.id).has_value(),
                "descriptor identity missing from wire manifest");

    ControllerPacket golden{ControllerMessageType::button, 0x01020304,
                            {1, 0, 0, 0, 0x10, 1, 1}};
    const auto bytes = encode_controller_packet(golden);
    const std::array<std::uint8_t, 19> expected{
        0x4e, 0x47, 0x01, 0x04, 0x07, 0x00, 0x04, 0x03, 0x02, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x10, 0x01, 0x01, 0x7c, 0x9e};
    require(std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()),
            "independent golden button frame changed");
    ControllerFrameDecoder decoder;
    decoder.push(bytes);
    require(decoder.pop().has_value(), "golden button frame decode");
    auto invalid = bytes;
    invalid.back() ^= 1;
    decoder.push(invalid);
    require(!decoder.pop() && decoder.invalid_frames() > 0,
            "invalid CRC rejection");

    AudioEngine engine;
    Timeline timeline_value;
    timeline_value.tracks = {{1, "V2", false, {3840, {}}}};
    TimelineService timeline([&](const Timeline &value, std::string &message) {
        return engine.queue_timeline(value, message);
    }, timeline_value);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 128);
    ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator firmware(host);
    require(firmware.negotiate(), "V2 byte-stream negotiation");
    const auto negotiated = host.snapshot();
    require(negotiated.connection == ControllerConnection::connected &&
                negotiated.inventory_valid && negotiated.input_state_valid &&
                negotiated.protocol_minor == 3,
            "V2 negotiation did not reach synchronized connected state");

    require(firmware.button("page.2", true) &&
                firmware.button("page.2", false),
            "V2 primary page input");
    (void)application.dispatch_pending();
    require(host.session_snapshot().destination == "sequencer",
            "V2 primary destination routing");
    require(firmware.button("edit.shift", true) &&
                firmware.button("page.2", true) &&
                firmware.button("page.2", false) &&
                firmware.button("edit.shift", false),
            "V2 shifted page input");
    (void)application.dispatch_pending();
    require(host.session_snapshot().destination == "song",
            "V2 shifted destination routing");

    require(firmware.button("page.4", true) && firmware.button("page.4", false),
            "navigate to Sound");
    (void)application.dispatch_pending();
    require(host.session_snapshot().destination == "sound",
            "V2 Sound destination routing");
    const auto before = std::get<double>(
        application.parameters().get("synth.cutoff")->current_value);
    require(firmware.encoder("parameter.1", 2), "V2 encoder input");
    const auto results = application.dispatch_pending();
    require(!results.empty() && results.back().accepted,
            "V2 encoder semantic dispatch");
    require(std::get<double>(application.parameters().get("synth.cutoff")->current_value) != before,
            "V2 encoder did not change authoritative parameter");

    require(firmware.button("transport.fill", true), "V2 Fill press");
    (void)application.dispatch_pending();
    require(application.snapshot().fill_active, "V2 Fill press not authoritative");
    require(firmware.button("transport.fill", false, false),
            "simulate lost Fill release");
    require(firmware.synchronize_input_state(), "V2 input-state recovery");
    (void)application.dispatch_pending();
    require(!application.snapshot().fill_active,
            "input-state recovery left Fill active");

    host.publish(application.snapshot());
    auto feedback = firmware.drain_host_output();
    require(std::any_of(feedback.begin(), feedback.end(), [](const auto &packet) {
        return packet.type == ControllerMessageType::led_render;
    }), "V2 full feedback refresh lacks LED rendering instructions");

    const auto old_generation = firmware.generation();
    host.disconnected("test disconnect");
    require(firmware.negotiate() && firmware.generation() != old_generation,
            "V2 reconnect did not establish a new generation");
    std::vector<std::uint8_t> stale;
    for (int i = 0; i < 4; ++i) stale.push_back((old_generation >> (i * 8)) & 255);
    stale.push_back(NG_V2_PAGE_1 & 255); stale.push_back(NG_V2_PAGE_1 >> 8);
    stale.push_back(1);
    require(!host.receive({ControllerMessageType::button, 1000, stale}, error) &&
                host.snapshot().stale_input > 0,
            "stale V2 generation was accepted");

    host.tick(1000);
    require(firmware.heartbeat(1000), "V2 heartbeat round trip");
    require(host.snapshot().heartbeats > 0, "V2 heartbeat diagnostics");

    ControllerHost expected_host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator expected_firmware(expected_host);
    require(expected_firmware.negotiate(), "reference feedback projection negotiation");
    expected_host.publish(application.snapshot());
    (void)expected_firmware.drain_host_output();
    for (std::size_t pass = 0; pass < 8 &&
         expected_host.snapshot().feedback_revision != application.snapshot().revision;
         ++pass) {
        expected_host.publish(application.snapshot());
        (void)expected_firmware.drain_host_output();
    }

    ControllerHost bounded_host(application, 16, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator bounded_firmware(bounded_host);
    require(bounded_firmware.negotiate(), "bounded feedback negotiation");
    bounded_host.publish(application.snapshot());
    require(bounded_host.snapshot().feedback_revision !=
                application.snapshot().revision,
            "bounded V2 feedback did not expose backpressure");
    for (std::size_t pass = 0; pass < 256 &&
         bounded_host.snapshot().feedback_revision != application.snapshot().revision;
         ++pass) {
        (void)bounded_firmware.drain_host_output();
        bounded_host.publish(application.snapshot());
    }
    (void)bounded_firmware.drain_host_output();
    bounded_host.publish(application.snapshot());
    require(bounded_host.snapshot().feedback_revision ==
                application.snapshot().revision &&
                bounded_host.snapshot().leds == expected_host.snapshot().leds,
            "bounded V2 feedback did not converge to authoritative final LEDs");
    bounded_host.disconnected("pending feedback reconnect");
    require(bounded_firmware.negotiate(),
            "feedback reconnect did not renegotiate");
    for (std::size_t pass = 0; pass < 256 &&
         bounded_host.snapshot().feedback_revision != application.snapshot().revision;
         ++pass) {
        (void)bounded_firmware.drain_host_output();
        bounded_host.publish(application.snapshot());
    }
    require(bounded_host.snapshot().feedback_revision ==
                application.snapshot().revision &&
                bounded_host.snapshot().leds == expected_host.snapshot().leds,
            "reconnected V2 feedback did not fully resynchronize");
    samples.shutdown();
    std::cout << "neurogenic.controller-v2-protocol-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
