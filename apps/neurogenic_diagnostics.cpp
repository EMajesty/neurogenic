#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/lifecycle.hpp"
#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/diagnostics/hardware.hpp"
#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/midi/output.hpp"
#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {
using namespace neurogenic;
struct Options {
    bool json{}, headless{}, stress{}, realtime{}, controller_inspect{},
        controller_protocol_test{}, controller_v2_sim_test{}, display_test{}, lifecycle_test{};
    double seconds{2.0};
    std::uint32_t buffer{256}, sample_voices{8}, synth_voices{4};
    std::uint16_t input_channels{}, output_channels{2};
    std::string input, output;
};
std::string json_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        if (character == '"' || character == '\\')
            result += '\\';
        if (static_cast<unsigned char>(character) >= 0x20)
            result += character;
    }
    return result;
}
std::uint32_t number(const char *text, const char *name) {
    std::uint32_t value{};
    const std::string_view source{text};
    const auto result =
        std::from_chars(source.data(), source.data() + source.size(), value);
    if (result.ec != std::errc{} || result.ptr != source.data() + source.size())
        throw std::invalid_argument(std::string(name) + " expects an integer");
    return value;
}
void usage() {
    std::cout
        << "usage: neurogenic-diagnostics [OPTIONS]\n"
           "  --json                 emit neurogenic.hardware-diagnostics.v2 "
           "JSON\n"
           "  --headless             use the explicit null audio backend\n"
           "  --stress SECONDS       run deterministic engine stress "
           "diagnostics\n"
           "  --realtime             use a physical/null device during stress\n"
           "  --buffer FRAMES        offline stress callback size (default "
           "256); realtime mode requests this device period\n"
           "  --sample-voices N      simultaneous sample voices (default 8)\n"
           "  --synth-voices N       simultaneous synth voices (default 4)\n"
           "  --audio NAME           output device index/name\n"
           "  --input NAME           duplex input device index/name\n"
           "  --input-channels N     requested capture channels (0..6)\n"
           "  --output-channels N    requested playback channels (1..6)\n"
           "  --display-test         report native display and scaling test "
           "profiles\n"
           "  --lifecycle-test       emit simulated bounded startup/shutdown "
           "stages\n"
           "  --controller-inspect   inspect lines: CONTROL TYPE RAW [ACTION]\n"
           "  --controller-protocol-test emit simulated controller protocol "
           "JSON\n"
           "  --controller-v2-sim-test run byte-stream V2 negotiation and inventory diagnostics\n";
}
PerformanceSnapshot performance(AudioEngine &engine) {
    PerformanceSnapshot result;
    result.audio = engine.callback_statistics();
    result.system = sample_system_now();
    result.engine.sample_rate = engine.sample_rate();
    result.engine.buffer_frames = engine.buffer_frames();
    result.engine.active_voices = engine.active_voice_count();
    result.engine.maximum_voices = engine.voice_capacity();
    const auto synth = engine.synth_state();
    result.engine.active_synth_voices = synth.active_voices;
    result.engine.maximum_synth_voices = synth.maximum_voices;
    result.engine.synth_stolen_voices = synth.stolen_voices;
    result.engine.dropped_triggers = engine.dropped_triggers();
    result.engine.dropped_timelines = engine.dropped_timelines();
    result.engine.timeline_event_overruns = engine.timeline_event_overruns();
    return result;
}
HardwareDiagnosticSnapshot inspect(AudioDevice &audio, MidiInput &input,
                                   MidiOutput &output, AudioEngine &engine) {
    auto result = system_diagnostics();
    result.audio_backend = audio.backend();
    result.selected_buffer_frames = audio.selected_buffer_frames();
    result.selected_sample_rate = audio.selected_sample_rate();
    result.selected_input_channels = audio.selected_input_channels();
    result.selected_output_channels = audio.selected_output_channels();
    const auto audio_status = audio.status();
    result.audio_running = audio_status.running;
    result.backend_xruns_available = audio_status.backend_xruns_available;
    result.backend_xruns = audio_status.backend_xruns;
    result.device_starts = audio_status.starts;
    result.device_stops = audio_status.stops;
    result.device_reroutes = audio_status.reroutes;
    result.device_interruptions = audio_status.interruptions;
    for (const auto &port : audio.output_capabilities()) {
        DiagnosticAudioPort item{port.name, {}};
        for (const auto &format : port.native_formats)
            item.formats.push_back({format.sample_rate, format.channels});
        result.audio_outputs.push_back(std::move(item));
    }
    for (const auto &port : audio.input_capabilities()) {
        DiagnosticAudioPort item{port.name, {}};
        for (const auto &format : port.native_formats)
            item.formats.push_back({format.sample_rate, format.channels});
        result.audio_inputs.push_back(std::move(item));
    }
    result.midi_input_backend = input.backend();
    result.midi_output_backend = output.backend();
    std::uint32_t id{1};
    for (const auto &name : input.inputs())
        result.midi_inputs.push_back({id++, name, false});
    for (const auto &port : output.outputs())
        result.midi_outputs.push_back({port.id, port.name, port.connected});
    const auto display = query_display_diagnostic();
    result.display = {display.available,       display.width,
                      display.height,          display.drawable_width,
                      display.drawable_height, display.content_scale,
                      display.backend};
    return result;
}
void inspect_controller() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string id, type, action;
        double raw{};
        if (!(in >> id >> type >> raw)) {
            std::cout << "{\"schema\":\"neurogenic.controller-event.v1\","
                         "\"error\":\"expected CONTROL TYPE RAW [ACTION]\"}\n";
            continue;
        }
        in >> action;
        const double normalized = type == "encoder" ? std::clamp(raw, -1.0, 1.0)
                                                    : std::clamp(raw, 0.0, 1.0);
        std::cout << "{\"schema\":\"neurogenic.controller-event.v1\",\"control_"
                     "id\":\""
                  << json_escape(id) << "\",\"type\":\"" << json_escape(type)
                  << "\",\"raw\":" << raw << ",\"normalized\":" << normalized
                  << ",\"transition\":\""
                  << ((type == "button") ? (raw > 0.5 ? "pressed" : "released")
                                         : "value")
                  << "\",\"action\":\"" << json_escape(action) << "\"}\n";
    }
}
void display_test() {
    const auto native = query_display_diagnostic();
    std::cout << "{\"schema\":\"neurogenic.display-test.v1\",\"native\":{"
                 "\"available\":"
              << (native.available ? "true" : "false")
              << ",\"width\":" << native.width
              << ",\"height\":" << native.height
              << ",\"content_scale\":" << native.content_scale
              << ",\"backend\":\"" << json_escape(native.backend)
              << "\"},\"profiles\":["
                 "{\"width\":1920,\"height\":1200,\"recommended_scale\":1.5},"
                 "{\"width\":1024,\"height\":800,\"recommended_scale\":1.0},"
                 "{\"width\":1024,\"height\":600,\"recommended_scale\":1.0}],"
                 "\"checks\":{\"pointer_touch_share_conversion\":true,"
                 "\"onscreen_keyboard\":true,\"fullscreen_supported\":"
              << (native.available ? "true" : "false") << "}}\n";
}
void controller_v2_sim_test() {
    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{1, "diagnostic", false, {3840, {}}}};
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 64);
    ControllerHost host(application, 128, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator firmware(host);
    std::string manifest_error;
    const auto manifest_valid = validate_reference_v2_wire_manifest(manifest_error);
    const auto negotiated = manifest_valid && firmware.negotiate();
    const auto state = host.snapshot();
    std::cout << "{\"schema\":\"neurogenic.controller-v2-diagnostic.v1\","
                 "\"physical_hardware\":false,\"manifest_valid\":"
              << (manifest_valid ? "true" : "false")
              << ",\"negotiated\":" << (negotiated ? "true" : "false")
              << ",\"device_id\":\"" << json_escape(state.device_id)
              << "\",\"protocol\":\"" << unsigned(state.protocol_major) << "."
              << unsigned(state.protocol_minor) << "\",\"control_count\":"
              << reference_v2_wire_controls().size()
              << ",\"generation\":" << state.input_generation
              << ",\"input_state_valid\":"
              << (state.input_state_valid ? "true" : "false")
              << ",\"error\":\"" << json_escape(manifest_error.empty()
                    ? state.error : manifest_error) << "\"}\n";
    samples.shutdown();
}
PerformanceSnapshot stress(AudioEngine &engine, const Options &options,
                           AudioDevice &device, std::string &error) {
    Sample sample{48000, 1, std::vector<float>(48000)};
    for (std::size_t i = 0; i < sample.interleaved.size(); ++i)
        sample.interleaved[i] =
            0.15F *
            std::sin(6.283185307F * 220.0F * static_cast<float>(i) / 48000.0F);
    engine.set_sample(std::move(sample));
    for (std::uint32_t i = 0; i < options.sample_voices; ++i)
        engine.trigger(36, 0.7F);
    for (std::uint32_t i = 0; i < options.synth_voices; ++i)
        engine.trigger_synth(static_cast<std::uint8_t>(48 + i % 24), 0.7F,
                             true);
    const auto begin = std::chrono::steady_clock::now();
    const auto finish = begin + std::chrono::duration<double>(options.seconds);
    if (options.realtime) {
        const bool started =
            options.input_channels || options.output_channels != 2
                ? device.start_duplex(options.output, options.input,
                                      options.input_channels, error,
                                      options.output_channels, options.buffer)
                : device.start(options.output, error, options.buffer);
        if (!started)
            throw std::runtime_error(error);
        std::this_thread::sleep_until(finish);
        device.stop();
    } else {
        std::vector<float> output(static_cast<std::size_t>(options.buffer) *
                                  options.output_channels);
        while (std::chrono::steady_clock::now() < finish)
            engine.process_io(output, options.output_channels);
    }
    return performance(engine);
}
} // namespace

int main(int argc, char **argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--json")
                options.json = true;
            else if (arg == "--headless")
                options.headless = true;
            else if (arg == "--realtime")
                options.realtime = true;
            else if (arg == "--controller-inspect")
                options.controller_inspect = true;
            else if (arg == "--controller-protocol-test")
                options.controller_protocol_test = true;
            else if (arg == "--controller-v2-sim-test")
                options.controller_v2_sim_test = true;
            else if (arg == "--display-test")
                options.display_test = true;
            else if (arg == "--lifecycle-test")
                options.lifecycle_test = true;
            else if (arg == "--stress" && i + 1 < argc) {
                options.stress = true;
                options.seconds = std::stod(argv[++i]);
            } else if (arg == "--buffer" && i + 1 < argc)
                options.buffer = number(argv[++i], "--buffer");
            else if (arg == "--sample-voices" && i + 1 < argc)
                options.sample_voices = number(argv[++i], "--sample-voices");
            else if (arg == "--synth-voices" && i + 1 < argc)
                options.synth_voices = number(argv[++i], "--synth-voices");
            else if (arg == "--audio" && i + 1 < argc)
                options.output = argv[++i];
            else if (arg == "--input" && i + 1 < argc)
                options.input = argv[++i];
            else if (arg == "--input-channels" && i + 1 < argc)
                options.input_channels = static_cast<std::uint16_t>(
                    number(argv[++i], "--input-channels"));
            else if (arg == "--output-channels" && i + 1 < argc)
                options.output_channels = static_cast<std::uint16_t>(
                    number(argv[++i], "--output-channels"));
            else if (arg == "--help") {
                usage();
                return 0;
            } else {
                usage();
                return 2;
            }
        }
        if (!(options.seconds > 0.0 && options.seconds <= 3600.0) ||
            !options.buffer || options.buffer > 8192 ||
            options.sample_voices > 256 || options.synth_voices > 512 ||
            options.input_channels > 6 || !options.output_channels ||
            options.output_channels > 6)
            throw std::invalid_argument("duration/buffer/channel configuration "
                                        "is outside diagnostic limits");
        if (options.controller_inspect) {
            inspect_controller();
            return 0;
        }
        if (options.controller_protocol_test) {
            ControllerProtocolState state;
            state.connection = ControllerConnection::connected;
            state.device_id = "neurogenic.simulator";
            state.firmware = "diagnostic";
            state.protocol_major = controller_protocol_major;
            state.protocol_minor = controller_protocol_minor;
            state.capabilities = 0x3f;
            state.received = 4;
            state.sent = 3;
            state.feedback_revision = 1;
            state.analog[0] = 24000;
            state.pickup_direction[0] = 1;
            state.context[0] = {0, "Tempo", "120", "BPM",
                                "transport.tempo", {}, 20, 300, true, false,
                                false, 0, 1};
            std::cout << controller_protocol_json(state) << '\n';
            return 0;
        }
        if (options.controller_v2_sim_test) {
            controller_v2_sim_test();
            return 0;
        }
        if (options.display_test) {
            display_test();
            return 0;
        }
        if (options.lifecycle_test) {
            ApplianceLifecycle lifecycle;
            for (const auto stage :
                 {StartupStage::configuration, StartupStage::project_selection,
                  StartupStage::project_restoration, StartupStage::audio,
                  StartupStage::midi, StartupStage::controllers,
                  StartupStage::gui}) {
                lifecycle.transition(stage, 0.5F);
                std::cout << lifecycle_json(lifecycle.snapshot()) << '\n';
            }
            lifecycle.ready();
            std::cout << lifecycle_json(lifecycle.snapshot()) << '\n';
            std::string reason;
            (void)lifecycle.request_shutdown(
                false, UnsavedShutdownChoice::cancel, reason);
            lifecycle.stopped();
            std::cout << lifecycle_json(lifecycle.snapshot()) << '\n';
            return 0;
        }
        AudioEngine engine(48000,
                           std::max<std::size_t>(32, options.sample_voices));
        MidiMapper mapper;
        AudioDevice audio(engine, options.headless);
        MidiInput midi_input(mapper);
        MidiOutput midi_output;
        auto snapshot = inspect(audio, midi_input, midi_output, engine);
        if (options.stress) {
            std::string error;
            const auto measurements = stress(engine, options, audio, error);
            snapshot = inspect(audio, midi_input, midi_output, engine);
            HardwareTestReport report;
            report.application_commit = snapshot.application_commit;
            report.hardware_identity =
                snapshot.cpu_model + " / " + snapshot.architecture;
            report.os_kernel = snapshot.distribution + " / " + snapshot.kernel;
            report.audio_configuration =
                std::to_string(options.realtime ? audio.selected_sample_rate()
                                                : engine.sample_rate()) +
                " Hz, requested " + std::to_string(options.buffer) +
                " frames, negotiated " +
                std::to_string(options.realtime
                                   ? audio.selected_buffer_frames()
                                   : options.buffer) +
                " frames, " + std::to_string(options.input_channels) + "/" +
                std::to_string(options.output_channels) + " input/output channels";
            report.scenario = options.realtime ? "realtime-device-stress"
                                               : "offline-engine-stress";
            report.status =
                measurements.audio.deadline_overruns == 0 ? "pass" : "fail";
            report.limitations =
                options.realtime
                    ? "deadline misses are callback-duration overruns, not "
                      "hardware XRUNs; this miniaudio/ALSA adapter does not "
                      "expose a backend XRUN counter"
                    : "offline processing is not evidence of hardware "
                      "real-time performance";
            report.sample_rate = options.realtime ? audio.selected_sample_rate()
                                                  : engine.sample_rate();
            report.requested_buffer_frames = options.buffer;
            report.negotiated_buffer_frames =
                options.realtime ? audio.selected_buffer_frames() : options.buffer;
            report.input_channels = options.input_channels;
            report.output_channels = options.output_channels;
            report.physical_device = options.realtime && !options.headless;
            const auto device_status = audio.status();
            report.backend_xruns_available =
                device_status.backend_xruns_available;
            report.backend_xruns = device_status.backend_xruns;
            report.device_starts = device_status.starts;
            report.device_stops = device_status.stops;
            report.device_reroutes = device_status.reroutes;
            report.device_interruptions = device_status.interruptions;
            report.realtime = snapshot.realtime;
            report.measurements = measurements;
            std::cout << hardware_test_report_json(report) << '\n';
            return 0;
        }
        std::cout << (options.json ? hardware_diagnostics_json(snapshot)
                                   : hardware_diagnostics_summary(snapshot))
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "diagnostics failed: " << error.what() << '\n';
        return 1;
    }
}
