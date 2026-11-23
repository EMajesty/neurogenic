#include "neurogenic/application/controller_coordinator.hpp"
#include "neurogenic/application/lifecycle.hpp"
#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/midi/output.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/ui/gui.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> running{true};
void stop(int) { running = false; }
void usage() {
    std::cout
        << "usage: neurogenic [OPTIONS] [SAMPLE.wav]\n"
           "  --slot ID=FILE    load FILE into stable numeric sample slot ID "
           "(repeatable)\n"
           "  --map NOTE=ID     map MIDI/sequencer NOTE to a loaded slot "
           "(repeatable)\n"
           "  --audio NAME      select audio output by index or name\n"
           "  --input NAME      open a duplex audio input by index or name\n"
           "  --input-channels N  request 1..6 device input channels\n"
           "  --output-channels N request 1..6 device output channels\n"
           "  --midi NAME       select MIDI input by index or name\n"
           "  --midi-out NAME   connect MIDI output by index or name "
           "(repeatable)\n"
           "  --controller PATH dedicated controller CDC serial device path\n"
           "  --controller-id ID require the negotiated controller identity\n"
           "  --midi-controller-profile FILE load a MIDI control-surface .ngc profile\n"
           "  --script FILE     Lua action for MIDI note 36\n"
           "  --timeline FILE   load a NEUROGENIC_TIMELINE version 1 file\n"
           "  --project PATH    restore a versioned NEUROGENIC project\n"
           "  --preferences PATH  appliance preference file\n"
           "  --reopen-last     reopen the last project when --project is "
           "absent\n"
           "  --no-reopen-last  disable automatic last-project restoration\n"
           "  --shutdown-dirty CHOICE  save, discard, or cancel (default "
           "cancel)\n"
           "  --run-for SECONDS diagnostic bounded appliance run\n"
           "  --interactive     accept live load/unload/status/quit commands "
           "on stdin\n"
           "  --list-devices    enumerate audio and MIDI devices\n"
           "  --headless        explicitly select null audio\n"
           "  --gui             launch the optional graphical frontend\n"
           "  --frontend NAME   select headless, sdl, or slint frontend\n"
           "  --fullscreen      launch the graphical frontend fullscreen\n"
           "  --ui-scale SCALE  override GUI scale (1.0..2.5)\n"
           "  --reset-ui-scale  restore automatic GUI scaling\n"
           "  --stats           print one performance.v1 JSON snapshot and "
           "exit\n"
           "  --synth           route the demo track and MIDI notes to the "
           "native synth\n";
}

void print_status(const neurogenic::SampleSlotStatus &state) {
    std::cout << "slot " << state.id.value << " "
              << neurogenic::sample_slot_state_name(state.state);
    if (state.has_sample)
        std::cout << " source=\"" << state.source_path.string() << "\" "
                  << state.source_sample_rate << "->"
                  << state.engine_sample_rate << " Hz " << state.channels
                  << "ch " << state.duration_seconds << "s";
    if (!state.pending_source_path.empty())
        std::cout << " pending=\"" << state.pending_source_path.string() << '"';
    if (!state.error.empty())
        std::cout << " error=\"" << state.error << '"';
    std::cout << '\n';
}

std::pair<std::uint32_t, std::string> assignment(std::string_view value,
                                                 const char *option) {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == value.size())
        throw std::invalid_argument(std::string(option) +
                                    " expects NUMBER=VALUE");
    std::uint32_t number{};
    const auto first = value.data();
    const auto last = first + separator;
    const auto parsed = std::from_chars(first, last, number);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        throw std::invalid_argument(std::string(option) +
                                    " has an invalid number");
    return {number, std::string(value.substr(separator + 1))};
}
std::uint32_t unsigned_number(std::string_view value, const char *option) {
    std::uint32_t number{};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument(std::string(option) +
                                    " has an invalid slot ID");
    return number;
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::string wav, audio_name, input_name,
            script = "examples/generate.lua", timeline_file, project_file,
            preferences_file,
            shutdown_dirty{"cancel"};
        std::vector<std::string> midi_inputs, midi_outputs, controller_paths,
            controller_ids, midi_controller_profiles;
        std::vector<std::pair<std::uint32_t, std::filesystem::path>> slot_files;
        std::vector<std::pair<std::uint8_t, std::uint32_t>> note_maps;
        bool list = false, headless = false, interactive = false, gui = false,
             slint = false,
             fullscreen = false, reset_ui_scale = false, stats = false,
             synth = false;
        bool audio_explicit{}, input_explicit{}, midi_input_explicit{},
            midi_output_explicit{}, fullscreen_explicit{}, scale_explicit{},
            input_channels_explicit{}, output_channels_explicit{};
        std::optional<bool> reopen_last_override;
        std::optional<float> ui_scale;
        std::optional<double> run_for_seconds;
        std::uint32_t input_channels{2}, output_channels{2};
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--audio" && i + 1 < argc) {
                audio_name = argv[++i];
                audio_explicit = true;
            } else if (arg == "--input" && i + 1 < argc) {
                input_name = argv[++i];
                input_explicit = true;
            } else if (arg == "--input-channels" && i + 1 < argc)
                input_channels = unsigned_number(argv[++i], "--input-channels"),
                input_channels_explicit = true;
            else if (arg == "--output-channels" && i + 1 < argc)
                output_channels =
                    unsigned_number(argv[++i], "--output-channels"),
                output_channels_explicit = true;
            else if (arg == "--midi" && i + 1 < argc) {
                midi_inputs.emplace_back(argv[++i]);
                midi_input_explicit = true;
            } else if (arg == "--midi-out" && i + 1 < argc) {
                midi_outputs.emplace_back(argv[++i]);
                midi_output_explicit = true;
            } else if (arg == "--controller" && i + 1 < argc)
                controller_paths.emplace_back(argv[++i]);
            else if (arg == "--controller-id" && i + 1 < argc)
                controller_ids.emplace_back(argv[++i]);
            else if (arg == "--midi-controller-profile" && i + 1 < argc)
                midi_controller_profiles.emplace_back(argv[++i]);
            else if (arg == "--script" && i + 1 < argc)
                script = argv[++i];
            else if (arg == "--timeline" && i + 1 < argc)
                timeline_file = argv[++i];
            else if (arg == "--project" && i + 1 < argc)
                project_file = argv[++i];
            else if (arg == "--preferences" && i + 1 < argc)
                preferences_file = argv[++i];
            else if (arg == "--reopen-last")
                reopen_last_override = true;
            else if (arg == "--no-reopen-last")
                reopen_last_override = false;
            else if (arg == "--shutdown-dirty" && i + 1 < argc)
                shutdown_dirty = argv[++i];
            else if (arg == "--run-for" && i + 1 < argc) {
                run_for_seconds = std::stod(argv[++i]);
                if (!std::isfinite(*run_for_seconds) ||
                    *run_for_seconds <= 0.0 || *run_for_seconds > 3600.0)
                    throw std::invalid_argument(
                        "--run-for must be greater than zero and at most 3600 "
                        "seconds");
            } else if (arg == "--slot" && i + 1 < argc) {
                auto [id, path] = assignment(argv[++i], "--slot");
                slot_files.emplace_back(id, path);
            } else if (arg == "--map" && i + 1 < argc) {
                auto [note, id_text] = assignment(argv[++i], "--map");
                if (note > 127)
                    throw std::invalid_argument("--map note must be 0..127");
                note_maps.emplace_back(static_cast<std::uint8_t>(note),
                                       unsigned_number(id_text, "--map"));
            } else if (arg == "--list-devices")
                list = true;
            else if (arg == "--headless")
                headless = true;
            else if (arg == "--stats") {
                stats = true;
                headless = true;
            } else if (arg == "--synth")
                synth = true;
            else if (arg == "--interactive")
                interactive = true;
            else if (arg == "--gui")
                gui = true;
            else if (arg == "--frontend" && i + 1 < argc) {
                const std::string frontend = argv[++i];
                if (frontend == "headless") {
                    headless = true;
                    gui = slint = false;
                } else if (frontend == "sdl") {
                    headless = false;
                    gui = true;
                    slint = false;
                } else if (frontend == "slint") {
                    headless = false;
                    gui = true;
                    slint = true;
                } else
                    throw std::invalid_argument(
                        "--frontend must be headless, sdl, or slint");
            }
            else if (arg == "--fullscreen") {
                gui = true;
                fullscreen = true;
                fullscreen_explicit = true;
            } else if (arg == "--ui-scale" && i + 1 < argc) {
                gui = true;
                ui_scale = std::stof(argv[++i]);
                scale_explicit = true;
                if (!std::isfinite(*ui_scale) || *ui_scale < 1.0F ||
                    *ui_scale > 2.5F)
                    throw std::invalid_argument("--ui-scale must be 1.0..2.5");
            } else if (arg == "--reset-ui-scale") {
                gui = true;
                reset_ui_scale = true;
                scale_explicit = true;
            } else if (arg == "--help") {
                usage();
                return 0;
            } else if (!arg.empty() && arg[0] != '-' && wav.empty())
                wav = arg;
            else {
                usage();
                return 2;
            }
        }

        neurogenic::ApplianceLifecycle lifecycle;
        auto announce = [&](neurogenic::StartupStage stage, float progress) {
            lifecycle.transition(stage, progress);
            std::cerr << "startup[" << neurogenic::startup_stage_name(stage)
                      << "] " << static_cast<int>(progress * 100) << "%\n";
        };
        announce(neurogenic::StartupStage::configuration, 0.05F);
        neurogenic::AppliancePreferences preferences;
        const auto preference_path =
            preferences_file.empty() ? neurogenic::default_preferences_path()
                                     : std::filesystem::path(preferences_file);
        std::string preference_error;
        if (!neurogenic::load_preferences(preference_path, preferences,
                                          preference_error)) {
            lifecycle.issue(neurogenic::StartupStage::configuration,
                            preference_error, true);
            std::cerr << "preferences ignored: " << preference_error << '\n';
            preferences = {};
        }
        if (reopen_last_override)
            preferences.reopen_last_project = *reopen_last_override;
        if (!audio_explicit)
            audio_name = preferences.audio_output;
        if (!input_explicit)
            input_name = preferences.audio_input;
        if (!input_channels_explicit)
            input_channels = preferences.audio_input_channels;
        if (!output_channels_explicit)
            output_channels = preferences.audio_output_channels;
        if (!midi_input_explicit)
            midi_inputs = preferences.midi_inputs;
        if (!midi_output_explicit)
            midi_outputs = preferences.midi_outputs;
        if (!fullscreen_explicit)
            fullscreen = preferences.fullscreen;
        if (!scale_explicit && preferences.ui_scale)
            ui_scale = preferences.ui_scale;
        announce(neurogenic::StartupStage::project_selection, 0.12F);
        std::string project_warning;
        project_file = neurogenic::select_startup_project(
                           preferences, project_file, project_warning)
                           .string();
        if (!project_warning.empty()) {
            lifecycle.issue(neurogenic::StartupStage::project_selection,
                            project_warning, true);
            std::cerr << project_warning << '\n';
        }
        neurogenic::UnsavedShutdownChoice shutdown_choice;
        if (shutdown_dirty == "save")
            shutdown_choice = neurogenic::UnsavedShutdownChoice::save;
        else if (shutdown_dirty == "discard")
            shutdown_choice = neurogenic::UnsavedShutdownChoice::discard;
        else if (shutdown_dirty == "cancel")
            shutdown_choice = neurogenic::UnsavedShutdownChoice::cancel;
        else
            throw std::invalid_argument(
                "--shutdown-dirty must be save, discard, or cancel");

        neurogenic::AudioEngine engine;
        neurogenic::SampleManager samples(engine);
        neurogenic::MidiMapper mapper;
        neurogenic::ControlDispatcher dispatcher(mapper);
        neurogenic::AudioDevice audio(engine, headless);
        neurogenic::MidiInput midi(mapper);
        neurogenic::MidiOutput midi_output;
        if (list) {
            std::cout << "Audio backend: " << audio.backend()
                      << "\nAudio outputs:\n";
            std::size_t index = 0;
            for (const auto &port : audio.output_capabilities())
                std::cout << "  [" << index++ << "] " << port.name
                          << (port.maximum_channels
                                  ? " (up to " +
                                        std::to_string(port.maximum_channels) +
                                        " ch)"
                                  : " (channel capability unknown)")
                          << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            std::cout << "Audio inputs:\n";
            index = 0;
            for (const auto &port : audio.input_capabilities())
                std::cout << "  [" << index++ << "] " << port.name
                          << (port.maximum_channels
                                  ? " (up to " +
                                        std::to_string(port.maximum_channels) +
                                        " ch)"
                                  : " (channel capability unknown)")
                          << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            std::cout << "MIDI backend: " << midi.backend()
                      << "\nMIDI inputs:\n";
            index = 0;
            for (const auto &name : midi.inputs())
                std::cout << "  [" << index++ << "] " << name << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            std::cout << "MIDI output backend: " << midi_output.backend()
                      << "\nMIDI outputs:\n";
            index = 0;
            for (const auto &port : midi_output.outputs())
                std::cout << "  [" << index++ << "] " << port.name << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            if (wav.empty() && slot_files.empty() && !interactive && !gui)
                return 0;
        }
        if (wav.empty() && slot_files.empty() && project_file.empty() &&
            !interactive && !gui && !stats && !synth) {
            usage();
            return 2;
        }

        std::optional<neurogenic::Project> startup_project;
        announce(neurogenic::StartupStage::project_restoration, 0.2F);
        if (!project_file.empty()) {
            if (!timeline_file.empty() || !wav.empty() || !slot_files.empty())
                throw std::invalid_argument("--project cannot be combined with "
                                            "sample or timeline inputs");
            try {
                startup_project = neurogenic::load_project(project_file);
            } catch (const std::exception &exception) {
                lifecycle.issue(neurogenic::StartupStage::project_restoration,
                                exception.what(), false);
                std::cerr << neurogenic::lifecycle_json(lifecycle.snapshot())
                          << '\n';
                throw;
            }
            for (const auto &[slot, path] :
                 startup_project->samples.sample_paths)
                if (std::filesystem::exists(path))
                    slot_files.emplace_back(slot, path);
        }

        if (slot_files.empty() && !wav.empty())
            engine.set_sample(neurogenic::load_wav(wav, engine.sample_rate()));
        else {
            if (!wav.empty())
                slot_files.emplace_back(0, wav);
            for (const auto &[id, path] : slot_files) {
                if (!samples.load(neurogenic::SampleSlotId{id}, path))
                    throw std::runtime_error("sample command queue is full");
            }
            samples.wait_idle();
            for (const auto &[id, path] : slot_files) {
                (void)path;
                const auto state = samples.status(neurogenic::SampleSlotId{id});
                if (state.state == neurogenic::SampleSlotLoadState::error)
                    throw std::runtime_error("slot " + std::to_string(id) +
                                             " failed: " + state.error);
            }
            for (const auto &[note, id] : note_maps)
                if (!engine.map_note_to_slot(note,
                                             neurogenic::SampleSlotId{id}))
                    throw std::invalid_argument(
                        "--map references an unloaded sample slot");
            std::array<float, 0> no_audio{};
            engine.process(no_audio, no_audio);
            for (const auto &[id, path] : slot_files) {
                (void)path;
                print_status(samples.status(neurogenic::SampleSlotId{id}));
            }
        }

        neurogenic::Timeline editable_timeline;
        if (startup_project)
            editable_timeline = startup_project->timeline;
        else if (timeline_file.empty()) {
            neurogenic::Track track;
            track.id = 1;
            track.name = synth ? "Synth" : "Drums";
            track.instrument_id =
                synth ? std::string(neurogenic::subtractive_instrument_id)
                      : std::string(neurogenic::sample_instrument_id);
            track.pattern.length = neurogenic::ticks_per_quarter * 4;
            neurogenic::EventId id = 1;
            for (const auto tick : {0, 960, 1920, 2880})
                track.pattern.events.push_back(
                    {id++, tick, neurogenic::MusicalEventType::note,
                     static_cast<std::uint8_t>(synth ? 60 : 36), 1.0F, 240, 0,
                     0, 0});
            editable_timeline.tracks.push_back(std::move(track));
        } else
            editable_timeline = neurogenic::load_timeline(timeline_file);
        {
            std::string timeline_error;
            if (!engine.queue_timeline(editable_timeline, timeline_error))
                throw std::runtime_error("timeline queue failed: " +
                                         timeline_error);
        }
        neurogenic::TimelineService timeline_service(
            [&](const auto &timeline, std::string &error) {
                return engine.queue_timeline(timeline, error);
            },
            std::move(editable_timeline));
        neurogenic::ApplicationService application(engine, timeline_service,
                                                   samples);
        neurogenic::ControllerCoordinator controllers(application);
        std::vector<std::pair<neurogenic::MidiPortId, std::string>>
            controller_midi_inputs;
        std::vector<std::pair<neurogenic::MidiPortId, std::string>>
            controller_midi_outputs;
        const auto add_controller_port = [](auto &ports,
                                            neurogenic::MidiPortId port,
                                            const std::string &selector,
                                            std::string &port_error) {
            const auto existing = std::find_if(
                ports.begin(), ports.end(),
                [&](const auto &entry) { return entry.first == port; });
            if (existing != ports.end() && existing->second != selector) {
                port_error = "logical MIDI port " + std::to_string(port) +
                             " is assigned to conflicting exact devices";
                return false;
            }
            if (existing == ports.end()) ports.emplace_back(port, selector);
            return true;
        };
        for (const auto &profile_path : midi_controller_profiles) {
            neurogenic::MidiControllerProfile profile;
            std::string profile_error;
            if (!neurogenic::load_midi_controller_profile(
                    profile_path, profile, profile_error))
                throw std::runtime_error("MIDI controller profile: " +
                                         profile_error);
            const auto endpoint_id =
                profile.controller.profile_id.empty()
                    ? std::filesystem::path(profile_path).stem().string()
                    : profile.controller.profile_id;
            if (profile.port_selector.empty())
                throw std::runtime_error(
                    "MIDI controller profile requires an exact midi-device "
                    "selector");
            for (const auto &binding : profile.midi) {
                if (!add_controller_port(controller_midi_inputs, binding.port,
                                         profile.port_selector, profile_error) ||
                    (binding.feedback &&
                     !add_controller_port(controller_midi_outputs, binding.port,
                                          profile.port_selector,
                                          profile_error)))
                    throw std::runtime_error("MIDI controller profile: " +
                                             profile_error);
            }
            if (!controllers.add_midi(endpoint_id, std::move(profile), mapper,
                                      profile_error))
                throw std::runtime_error("MIDI controller profile: " +
                                         profile_error);
        }
        for (const auto &[port, selector] : controller_midi_inputs)
            if (port <= midi_inputs.size() && !midi_inputs[port - 1].empty() &&
                midi_inputs[port - 1] != selector)
                throw std::runtime_error(
                    "MIDI controller logical input port conflicts with the "
                    "configured musical MIDI device");
        for (const auto &[port, selector] : controller_midi_outputs)
            if (port <= midi_outputs.size() && !midi_outputs[port - 1].empty() &&
                midi_outputs[port - 1] != selector)
                throw std::runtime_error(
                    "MIDI controller logical output port conflicts with the "
                    "configured musical MIDI device");
        mapper.set_raw_handler([&](const neurogenic::MidiMessage &message) {
            (void)application.receive_midi(
                {message.port,
                 {message.status, message.data1, message.data2},
                 message.size,
                 message.timestamp_ns,
                 0});
        });
        if (startup_project) {
            const auto restored = application.execute(
                {"project.open.v1", {{"path", project_file}}});
            if (!restored.accepted)
                throw std::runtime_error("project restore failed: " +
                                         restored.error);
            if (!startup_project->missing_assets.empty())
                std::cerr << "project opened with "
                          << startup_project->missing_assets.size()
                          << " missing sample asset(s)\n";
        }
        if (stats) {
            std::array<float, 256> left{}, right{};
            engine.process(left, right);
            std::cout << neurogenic::performance_json(
                             application.snapshot().performance)
                      << '\n';
            samples.shutdown();
            return 0;
        }
        neurogenic::ScriptRuntime lua(
            [&](auto pattern) { return engine.queue_pattern(pattern); },
            [&] { return engine.frame_position(); }, &timeline_service,
            &application.slices());
        if (synth)
            mapper.set_note_event_handler(
                [&](auto note, auto velocity, bool on) {
                    engine.trigger_synth(note, velocity, on);
                });
        else
            mapper.set_note_handler([&](auto note, auto velocity) {
                engine.trigger(note, velocity);
            });
        mapper.map_button(0, 36, "pattern.generate", [&] {
            std::string error;
            if (lua.execute_file(script, error))
                std::cout << "Lua pattern queued for next bar\n";
            else
                std::cerr << "Lua error: " << error << '\n';
        });
        dispatcher.start();
        std::string error;
        announce(neurogenic::StartupStage::audio, 0.55F);
        if (input_channels == 0 || input_channels > 6 || output_channels == 0 ||
            output_channels > 6)
            throw std::invalid_argument("audio channel counts must be 1..6");
        const auto audio_started =
            input_name.empty() && output_channels == 2
                ? audio.start(audio_name, error)
                : audio.start_duplex(audio_name, input_name,
                                     input_name.empty() ? 0 : input_channels,
                                     error, output_channels);
        if (!audio_started) {
            lifecycle.issue(neurogenic::StartupStage::audio, error,
                            gui || interactive);
            std::cerr << "audio unavailable: " << error << '\n';
            if (!gui && !interactive)
                return 1;
            std::cerr << "continuing in degraded control-only mode\n";
        }
        announce(neurogenic::StartupStage::midi, 0.68F);
        for (std::size_t i = 0;
             i < midi_inputs.size() && i < neurogenic::maximum_midi_ports;
             ++i) {
            if (!midi.connect(static_cast<neurogenic::MidiPortId>(i + 1),
                              midi_inputs[i], error)) {
                lifecycle.issue(neurogenic::StartupStage::midi, error, true);
                std::cerr << "MIDI input unavailable (audio continues): "
                          << error << '\n';
            } else
                std::cout << "MIDI input " << i + 1 << " active\n";
        }
        for (const auto &[port, selector] : controller_midi_inputs) {
            if (!midi.connect_exact(port, selector, error)) {
                lifecycle.issue(neurogenic::StartupStage::midi, error, true);
                std::cerr << "MIDI controller input unavailable: " << error
                          << '\n';
            } else
                std::cout << "MIDI controller input "
                          << static_cast<unsigned>(port) << " active\n";
        }
        for (const auto &[port, selector] : controller_midi_outputs) {
            if (!midi_output.connect_exact(port, selector, error)) {
                lifecycle.issue(neurogenic::StartupStage::midi, error, true);
                std::cerr << "MIDI controller feedback output unavailable: "
                          << error << '\n';
            } else
                std::cout << "MIDI controller output "
                          << static_cast<unsigned>(port) << " active\n";
        }
        for (std::size_t i = 0;
             i < midi_outputs.size() && i < neurogenic::maximum_midi_ports;
             ++i) {
            if (!midi_output.connect(static_cast<neurogenic::MidiPortId>(i + 1),
                                     midi_outputs[i], error)) {
                lifecycle.issue(neurogenic::StartupStage::midi, error, true);
                std::cerr << "MIDI output unavailable: " << error << '\n';
            } else
                std::cout << "MIDI output " << i + 1 << " active\n";
        }
        std::jthread midi_output_dispatcher([&](std::stop_token stop_token) {
            auto next_refresh = std::chrono::steady_clock::now();
            while (!stop_token.stop_requested()) {
                const auto sent = application.midi_output().dispatch(
                    [&](const neurogenic::MidiPacket &packet) {
                        std::string send_error;
                        return midi_output.send(packet, send_error);
                    });
                const auto controller_sent = controllers.dispatch_midi_feedback(
                    [&](const neurogenic::MidiMessage &message) {
                        neurogenic::MidiPacket packet;
                        packet.port = message.port;
                        packet.size = message.size;
                        packet.bytes = {message.status, message.data1,
                                        message.data2};
                        std::string send_error;
                        return midi_output.send(packet, send_error);
                    });
                if (sent == 0 && controller_sent == 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_refresh) {
                    std::string reconnect_error;
                    if (midi_output.reconnect_missing(reconnect_error)) {
                        engine.midi_panic();
                        controllers.reconnect_midi();
                    }
                    if (midi.reconnect_missing(reconnect_error))
                        controllers.reconnect_midi();
                    application.set_midi_output_connections(
                        midi_output.connection_state());
                    application.set_midi_input_connections(
                        midi.connection_state());
                    next_refresh = now + std::chrono::seconds(1);
                }
            }
        });
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        announce(neurogenic::StartupStage::controllers, 0.78F);
        for (std::size_t i = 0; i < controller_paths.size(); ++i) {
            std::string controller_error;
            const auto expected =
                i < controller_ids.size() ? controller_ids[i] : std::string{};
            if (!controllers.add_serial("usb-cdc-" + std::to_string(i + 1),
                                        controller_paths[i], expected,
                                        controller_error))
                throw std::runtime_error(controller_error);
            if (!controller_error.empty()) {
                lifecycle.issue(neurogenic::StartupStage::controllers,
                                controller_error, true);
                std::cerr << "controller unavailable (will retry exact path): "
                          << controller_error << '\n';
            }
        }
        std::jthread controller_dispatcher([&](std::stop_token token) {
            while (!token.stop_requested()) {
                controllers.poll(application.snapshot());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            controllers.shutdown();
        });
        // Controller absence is a supported state; an explicit path is matched
        // exactly and never replaced with another serial device.
        if (gui)
            announce(neurogenic::StartupStage::gui, 0.9F);
        lifecycle.ready();
        std::cerr << neurogenic::lifecycle_json(lifecycle.snapshot()) << '\n';
        std::jthread automatic_stop;
        if (run_for_seconds)
            automatic_stop = std::jthread([duration = *run_for_seconds](
                                              std::stop_token token) {
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::duration<double>(duration);
                while (!token.stop_requested() &&
                       std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (!token.stop_requested())
                    running = false;
            });
        std::cout << "Playing " << engine.sample_slot_count()
                  << " sample slot(s). MIDI note 36 runs " << script
                  << ". Press Ctrl-C to stop.\n";
        int exit_code{};
        auto approve_shutdown = [&]() {
            const auto state = application.snapshot();
            if (!state.project_dirty)
                return true;
            if (shutdown_choice == neurogenic::UnsavedShutdownChoice::save) {
                const auto saved = application.execute({"project.save.v1", {}});
                if (!saved.accepted) {
                    std::cerr << "shutdown save failed: " << saved.error
                              << '\n';
                    return false;
                }
                return true;
            }
            std::string reason;
            const auto allowed =
                lifecycle.request_shutdown(true, shutdown_choice, reason);
            if (!allowed)
                std::cerr << "shutdown cancelled: " << reason << '\n';
            if (!allowed)
                running = true;
            return allowed;
        };
        if (gui) {
            neurogenic::GuiOptions gui_options;
            gui_options.fullscreen = fullscreen;
            gui_options.ui_scale = ui_scale;
            gui_options.reset_ui_scale = reset_ui_scale;
            gui_options.request_close = approve_shutdown;
            gui_options.external_stop_requested = [] {
                return !running.load();
            };
            if (slint) {
                neurogenic::SlintOptions slint_options;
                slint_options.fullscreen = fullscreen;
                slint_options.request_close = approve_shutdown;
                slint_options.external_stop_requested = [] {
                    return !running.load();
                };
                exit_code = neurogenic::run_slint_frontend(
                    application, slint_options, error);
            } else
                exit_code =
                    neurogenic::run_gui(application, gui_options, error);
            if (exit_code != 0) {
                lifecycle.issue(neurogenic::StartupStage::gui, error, true);
                std::cerr << "GUI unavailable: " << error << '\n';
            }
        } else if (interactive) {
            std::cout << "Commands: load ID FILE | replace ID FILE | unload ID "
                         "| status [ID] | record-start ID SOURCE | record-stop "
                         "| record-status | record-save FILE [pcm16|float32] | "
                         "quit\n";
            std::string line;
            while (running.load() && std::getline(std::cin, line)) {
                std::istringstream input(line);
                std::string command;
                input >> command;
                if (command == "quit" || command == "exit")
                    break;
                if (command == "load" || command == "replace") {
                    std::uint32_t id{};
                    std::string path;
                    input >> id >> std::ws;
                    std::getline(input, path);
                    if (path.empty() ||
                        !samples.load(neurogenic::SampleSlotId{id}, path))
                        std::cerr << "sample command rejected\n";
                } else if (command == "unload") {
                    std::uint32_t id{};
                    if (!(input >> id) ||
                        !samples.unload(neurogenic::SampleSlotId{id}))
                        std::cerr << "unload command rejected\n";
                } else if (command == "status") {
                    std::uint32_t id{};
                    if (input >> id)
                        print_status(
                            samples.status(neurogenic::SampleSlotId{id}));
                    else
                        for (const auto &state : samples.states())
                            print_status(state);
                } else if (command == "record-start") {
                    std::uint32_t slot{};
                    std::string source;
                    input >> slot >> source;
                    const auto select_source = application.execute(
                        {"recording.source-select.v1", {{"source", source}}});
                    const auto select_slot = application.execute(
                        {"recording.destination-select.v1",
                         {{"slot_id", static_cast<std::int64_t>(slot)}}});
                    const auto arm =
                        application.execute({"recording.arm.v1", {}});
                    const auto start =
                        application.execute({"recording.start.v1", {}});
                    if (!select_source.accepted || !select_slot.accepted ||
                        !arm.accepted || !start.accepted)
                        std::cerr
                            << "record command rejected: "
                            << (!select_source.error.empty()
                                    ? select_source.error
                                : !select_slot.error.empty() ? select_slot.error
                                : !arm.error.empty()         ? arm.error
                                                             : start.error)
                            << '\n';
                } else if (command == "record-stop") {
                    const auto result =
                        application.execute({"recording.stop.v1", {}});
                    if (!result.accepted)
                        std::cerr << result.error << '\n';
                } else if (command == "record-status") {
                    const auto state = application.snapshot().recording;
                    std::cout << neurogenic::recording_state_name(state.state)
                              << " " << state.captured_frames << "/"
                              << state.capacity_frames
                              << " frames, overflows=" << state.overflow_count
                              << '\n';
                } else if (command == "record-save") {
                    std::string path, format{"pcm16"};
                    input >> path >> format;
                    const auto result = application.execute(
                        {"recording.save.v1",
                         {{"path", path}, {"format", format}}});
                    if (!result.accepted)
                        std::cerr << result.error << '\n';
                } else
                    std::cerr << "unknown command\n";
            }
        } else
            while (running.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!gui && !approve_shutdown()) {
            // A non-GUI appliance remains alive after a rejected signal rather
            // than silently discarding a dirty project.
            running = true;
            while (running.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!approve_shutdown())
                return 3;
        }
        std::string shutdown_reason;
        if (application.snapshot().recording.state !=
            neurogenic::RecordingState::idle)
            (void)application.execute({"recording.cancel.v1", {}});
        engine.stop();
        engine.midi_panic();
        if (audio_started)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (void)lifecycle.request_shutdown(false, shutdown_choice,
                                         shutdown_reason);
        audio.stop();
        automatic_stop.request_stop();
        midi_output_dispatcher.request_stop();
        midi_output_dispatcher.join();
        controller_dispatcher.request_stop();
        controller_dispatcher.join();
        midi.close();
        controllers.disconnect_midi();
        dispatcher.stop();
        samples.shutdown();
        if (!application.snapshot().project_path.empty())
            preferences.last_project = application.snapshot().project_path;
        preferences.audio_output = audio_name;
        preferences.audio_input = input_name;
        preferences.audio_input_channels =
            static_cast<std::uint16_t>(input_channels);
        preferences.audio_output_channels =
            static_cast<std::uint16_t>(output_channels);
        preferences.midi_inputs = midi_inputs;
        preferences.midi_outputs = midi_outputs;
        preferences.fullscreen = fullscreen;
        preferences.ui_scale = ui_scale;
        if (!neurogenic::save_preferences(preference_path, preferences,
                                          preference_error))
            std::cerr << "preferences not saved: " << preference_error << '\n';
        lifecycle.stopped();
        std::cerr << neurogenic::lifecycle_json(lifecycle.snapshot()) << '\n';
        std::cout << "Stopped. dropped MIDI=" << mapper.dropped_messages()
                  << ", triggers=" << engine.dropped_triggers()
                  << ", patterns=" << engine.dropped_patterns()
                  << ", timelines=" << engine.dropped_timelines()
                  << ", timeline overruns=" << engine.timeline_event_overruns()
                  << ", legacy events=" << engine.dropped_legacy_events()
                  << ", transport commands="
                  << engine.dropped_transport_commands()
                  << ", missing slots=" << engine.missing_slot_triggers()
                  << ", stolen voices=" << engine.stolen_voices() << '\n';
        return exit_code;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
#include "neurogenic/application/control.hpp"
