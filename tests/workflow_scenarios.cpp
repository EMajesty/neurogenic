#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller.hpp"
#include "neurogenic/application/recording_workflow.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/ui/gui.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace neurogenic;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

void write_u16(std::ofstream &out, std::uint16_t value) {
    out.put(static_cast<char>(value));
    out.put(static_cast<char>(value >> 8));
}
void write_u32(std::ofstream &out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.put(static_cast<char>(value >> shift));
}
void write_fixture(const std::filesystem::path &path, float amplitude,
                   std::size_t frames = 4096) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(out), "cannot create deterministic WAV fixture");
    const auto bytes = static_cast<std::uint32_t>(frames * 2);
    out.write("RIFF", 4);
    write_u32(out, 36 + bytes);
    out.write("WAVEfmt ", 8);
    write_u32(out, 16);
    write_u16(out, 1);
    write_u16(out, 1);
    write_u32(out, 48000);
    write_u32(out, 96000);
    write_u16(out, 2);
    write_u16(out, 16);
    out.write("data", 4);
    write_u32(out, bytes);
    const auto sample = static_cast<std::int16_t>(amplitude * 32767.0F);
    for (std::size_t frame = 0; frame < frames; ++frame)
        write_u16(out, static_cast<std::uint16_t>(sample));
}

struct Context {
    AudioEngine engine{48000, 64};
    SampleManager samples{engine};
    TimelineService timeline;
    ApplicationService application;
    bool background_pump{};

    Context()
        : timeline([this](const Timeline &value, std::string &error) {
              return engine.queue_timeline(value, error);
          }),
          application(engine, timeline, samples) {}
    ~Context() { samples.shutdown(); }

    void action(std::string id,
                std::map<std::string, ApplicationValue> arguments = {}) {
        const auto result = application.execute(
            {std::move(id), std::move(arguments), "workflow", 0});
        require(result.accepted, result.error.empty() ? "action rejected"
                                                      : result.error);
        if (!background_pump)
            (void)process(64);
    }
    double process(std::size_t frames, std::size_t block = 128) {
        double energy{};
        std::vector<float> left(block), right(block);
        while (frames) {
            const auto count = std::min(frames, block);
            engine.process({left.data(), count}, {right.data(), count});
            for (std::size_t i = 0; i < count; ++i)
                energy += std::fabs(left[i]) + std::fabs(right[i]);
            frames -= count;
        }
        return energy;
    }
    void load(SampleSlotId slot, const std::filesystem::path &path) {
        action("sample.load.v1",
               {{"slot_id", static_cast<std::int64_t>(slot.value)},
                {"path", path.string()}});
        samples.wait_idle();
        for (int attempt = 0; attempt < 16; ++attempt) {
            process(64);
            if (samples.status(slot).state == SampleSlotLoadState::loaded)
                return;
        }
        throw std::runtime_error("sample did not become audio-active");
    }
    void open_project(const std::filesystem::path &path) {
        background_pump = true;
        std::jthread pump([this](std::stop_token stop) {
            std::array<float, 64> left{}, right{};
            while (!stop.stop_requested()) {
                engine.process(left, right);
                std::this_thread::yield();
            }
        });
        action("project.open.v1", {{"path", path.string()}});
        pump.request_stop();
        pump.join();
        background_pump = false;
    }
};

struct TempDirectory {
    std::filesystem::path path;
    explicit TempDirectory(std::string name) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = std::filesystem::temp_directory_path() /
               ("neurogenic-workflow-" + name + "-" +
                std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void create_track(Context &context, TrackId id, std::string name,
                  std::string instrument = std::string(sample_instrument_id),
                  InstrumentInstanceId instance = default_synth_instance_id) {
    context.action(
        "track.create.v1",
        {{"track_id", static_cast<std::int64_t>(id)},
         {"name", std::move(name)},
         {"length", static_cast<std::int64_t>(ticks_per_quarter * 4)},
         {"instrument_id", std::move(instrument)},
         {"instance_id", static_cast<std::int64_t>(instance)}});
}

void scenario_beat_project() {
    TempDirectory temporary("beat");
    const auto kick = temporary.path / "kick.wav";
    const auto hat = temporary.path / "hat.wav";
    const auto project = temporary.path / "beat.ngp";
    write_fixture(kick, 0.5F);
    write_fixture(hat, 0.2F);

    Context context;
    context.action("project.new.v1", {{"name", std::string("Beat")}});
    create_track(context, 1, "Kick");
    create_track(context, 2, "Hat");
    context.load(SampleSlotId{0}, kick);
    context.load(SampleSlotId{1}, hat);
    context.action("sample.slot-select.v1", {{"slot_id", std::int64_t{0}}});
    context.action("slice.equal-divisions.v1",
                   {{"divisions", std::int64_t{2}}});
    auto state = context.application.snapshot();
    require(state.slices.size() == 2, "equal slicing did not publish state");
    context.action("slice.select.v1",
                   {{"slice_id", static_cast<std::int64_t>(state.slices[0].id)}});
    context.action("slice.pad-assign.v1", {{"pad", std::int64_t{0}}});
    context.action("track.select.v1", {{"track_id", std::int64_t{1}}});
    context.action("step.toggle.v1", {{"step", std::int64_t{0}},
                                       {"velocity", 0.9},
                                       {"duration_ticks", std::int64_t{240}}});
    context.action("step.event-select.v1", {{"step", std::int64_t{0}}});
    context.action("step.probability-set.v1", {{"probability", 1.0}});
    context.action("step.ratchet-set.v1", {{"count", std::int64_t{2}}});
    context.action("step.lock-set.v1",
                   {{"parameter_id", std::string("sample.gain")},
                    {"value", 0.75}});
    context.action("track.select.v1", {{"track_id", std::int64_t{2}}});
    context.action("step.toggle.v1", {{"step", std::int64_t{2}}});
    context.action("transport.tempo-set.v1", {{"bpm", 126.0}});
    state = context.application.snapshot();
    require(std::fabs(state.tempo_bpm - 126.0) < 0.01,
            "tempo action did not update authoritative transport");
    context.action("transport.restart.v1");
    require(context.process(26000) > 1.0,
            "beat workflow produced no meaningful audio");
    context.action("transport.stop.v1");
    context.process(128);
    context.action("project.save-as.v1",
                   {{"path", project.string()},
                    {"name", std::string("Beat")}});
    require(!context.application.snapshot().project_dirty,
            "saved beat remained dirty");

    Context restored;
    restored.open_project(project);
    const auto restored_state = restored.application.snapshot();
    require(restored_state.tracks.size() == 2 &&
                restored_state.slice_pad_assignments[0].has_value() &&
                std::fabs(restored_state.tempo_bpm - 126.0) < 0.01,
            "beat project did not restore its musical state");
    restored.action("transport.restart.v1");
    require(restored.process(26000) > 1.0,
            "restored beat produced no audio");
}

void scenario_synth_modulation() {
    TempDirectory temporary("synth");
    const auto project = temporary.path / "synth.ngp";
    Context context;
    context.action("project.new.v1", {{"name", std::string("Synth")}});
    create_track(context, 10, "Bass", std::string(subtractive_instrument_id), 1);
    create_track(context, 20, "Lead", std::string(subtractive_instrument_id), 2);
    for (const auto [track, pitch] :
         std::array<std::pair<TrackId, int>, 2>{{{10, 48}, {20, 67}}}) {
        context.action("track.select.v1",
                       {{"track_id", static_cast<std::int64_t>(track)}});
        context.action("track.note-target-set.v1",
                       {{"note", static_cast<std::int64_t>(pitch)}});
        context.action("step.toggle.v1", {{"step", std::int64_t{0}},
                                           {"duration_ticks",
                                            std::int64_t{720}}});
    }
    context.action("instrument.instance-parameter-set.v1",
                   {{"instance_id", std::int64_t{1}},
                    {"parameter_id", std::string("synth.cutoff")},
                    {"value", 900.0}});
    context.action("instrument.instance-parameter-set.v1",
                   {{"instance_id", std::int64_t{2}},
                    {"parameter_id", std::string("synth.cutoff")},
                    {"value", 6000.0}});
    context.action("modulation.source-configure.v1",
                   {{"source", std::int64_t{0}},
                    {"waveform", std::int64_t{0}},
                    {"rate", 1.5},
                    {"tempo_sync", false},
                    {"beats", 1.0},
                    {"scope", std::int64_t{1}},
                    {"retrigger", true}});
    context.action("modulation.route-create.v1",
                   {{"source", std::int64_t{0}},
                    {"destination", std::string("synth.cutoff")},
                    {"depth", 0.35},
                    {"scope", std::int64_t{1}},
                    {"track_id", std::int64_t{10}},
                    {"instance_id", std::int64_t{1}}});
    context.action("transport.restart.v1");
    require(context.process(12000) > 0.1,
            "independent synth tracks produced no audio");
    context.action("project.save-as.v1",
                   {{"path", project.string()},
                    {"name", std::string("Synth")}});
    const auto saved = load_project(project);
    require(saved.timeline.tracks.size() == 2 &&
                saved.timeline.tracks[0].instrument_instance_id == 1 &&
                saved.timeline.tracks[1].instrument_instance_id == 2 &&
                saved.modulation.route_count == 1 &&
                saved.parameters.contains("instance.1.synth.cutoff") &&
                saved.parameters.contains("instance.2.synth.cutoff"),
            "synth instances or scoped modulation were not persisted");
}

void scenario_mixer_recording_external() {
    TempDirectory temporary("recording");
    const auto loop = temporary.path / "loop.wav";
    write_fixture(loop, 0.35F, 8192);
    Context context;
    context.action("project.new.v1", {{"name", std::string("Resample")}});
    create_track(context, 1, "Loop");
    context.load(SampleSlotId{0}, loop);
    context.action("track.select.v1", {{"track_id", std::int64_t{1}}});
    context.action("step.toggle.v1", {{"step", std::int64_t{0}},
                                       {"duration_ticks", std::int64_t{960}}});
    (void)context.application.snapshot();
    context.action("mixer.channel-select.v1", {{"index", std::int64_t{0}}});
    context.action("mixer.gain-set.v1", {{"value", 0.8}});
    context.action("mixer.pan-set.v1", {{"value", -0.2}});
    context.action("mixer.aux-send-set.v1",
                   {{"aux", std::int64_t{0}}, {"value", 0.25}});
    context.action("mixer.fx-loop-set.v1",
                   {{"index", std::int64_t{0}},
                    {"source", std::string("track")},
                    {"source_id", std::int64_t{1}},
                    {"send_left", std::int64_t{2}},
                    {"send_right", std::int64_t{3}},
                    {"return_left", std::int64_t{0}},
                    {"return_right", std::int64_t{1}},
                    {"enabled", true}});
    context.action("mixer.fx-loop-levels.v1",
                   {{"index", std::int64_t{0}},
                    {"send_gain", 0.5},
                    {"return_gain", 0.8},
                    {"wet", 0.6}});
    context.action("mixer.scene-save.v1",
                   {{"name", std::string("External loop")}});
    RecordingWorkflowCoordinator recording(context.application);
    RecordingWorkflowConfiguration recording_configuration;
    recording_configuration.mode = RecordingWorkflowMode::resample;
    recording_configuration.source = "master-post";
    recording_configuration.destination = SampleSlotId{8};
    std::string recording_error;
    require(recording.configure(recording_configuration, recording_error) &&
                recording.arm(recording_error) && recording.start(recording_error),
            "resampling workflow coordinator failed: " + recording_error);
    context.action("transport.restart.v1");
    require(context.process(4096) > 0.1, "resampling source was silent");
    require(recording.stop(recording_error),
            "resampling workflow stop failed: " + recording_error);
    context.process(128);
    (void)context.application.snapshot();
    context.process(128);
    auto state = context.application.snapshot();
    require(state.recording.captured_frames > 0 &&
                state.recording.overflow_count == 0,
            "internal recording did not complete cleanly");
    context.action("recording.assign-pad.v1", {{"pad", std::int64_t{3}}});
    context.process(128);
    state = context.application.snapshot();
    require(state.slice_pad_assignments[3].has_value() &&
                state.routing_scenes.size() == 1,
            "recording assignment or routing scene was not published");

    std::array<float, 128 * 6> input{}, output{};
    for (std::size_t frame = 0; frame < 128; ++frame) {
        input[frame * 6] = 0.2F;
        input[frame * 6 + 1] = 0.1F;
    }
    context.engine.process_io(output, 6, input, 6);
    const auto mixer = context.engine.mixer_state();
    require(mixer.external_send_available[0] &&
                mixer.external_return_available[0],
            "simulated six-channel external loop was unavailable");
}

void scenario_midi_performance_gui() {
    TempDirectory temporary("midi");
    const auto project = temporary.path / "midi.ngp";
    Context context;
    context.action("project.new.v1", {{"name", std::string("MIDI")}});
    create_track(context, 7, "External", std::string(external_midi_instrument_id));
    context.action("track.midi-destination-set.v1",
                   {{"port", std::int64_t{2}},
                    {"channel", std::int64_t{3}}});
    context.action("track.note-target-set.v1", {{"note", std::int64_t{60}}});
    context.action("step.toggle.v1", {{"step", std::int64_t{0}},
                                       {"duration_ticks", std::int64_t{240}}});
    context.action("midi.clock-output-set.v1",
                   {{"port", std::int64_t{1}}, {"enabled", true}});
    context.action("transport.restart.v1");
    context.process(8000);
    std::vector<MidiPacket> outbound;
    context.application.midi_output().dispatch([&](const MidiPacket &packet) {
        outbound.push_back(packet);
        return true;
    });
    require(std::any_of(outbound.begin(), outbound.end(), [](const auto &packet) {
                return packet.port == 1 && packet.bytes[0] == 0xF8;
            }) &&
                std::any_of(outbound.begin(), outbound.end(),
                            [](const auto &packet) {
                                return packet.port == 2 &&
                                       (packet.bytes[0] & 0xF0U) == 0x90 &&
                                       (packet.bytes[0] & 0x0FU) == 2;
                            }),
            "clock and external notes did not share the MIDI scheduler");
    context.action("midi.clock-source-set.v1",
                   {{"source", std::string("external")}});
    context.action("midi.clock-forward-set.v1", {{"enabled", true}});
    require(context.application.receive_midi(
                {1, {0xFA, 0, 0}, 1, 1, 0}),
            "external MIDI Start queue overflow");
    for (std::uint64_t pulse = 0; pulse < 12; ++pulse) {
        require(context.application.receive_midi(
                    {1, {0xF8, 0, 0}, 1,
                     20'833'333ULL * (pulse + 1), 0}),
                "external clock queue overflow");
        context.process(1000);
    }
    require(context.application.snapshot().external_midi_clock.available,
            "external clock did not acquire");

    PerformanceGrid grid;
    for (const auto mode :
         {PadMode::step, PadMode::pad, PadMode::levels, PadMode::notes}) {
        grid.set_mode(mode);
        require(grid_application_command(grid.press(0)).has_value(),
                "performance-grid mode had no semantic action");
    }
    GuiModel gui(context.application);
    gui.begin_project_name_entry();
    require(std::any_of(gui.frame(1024, 600).commands.begin(),
                        gui.frame(1024, 600).commands.end(),
                        [](const auto &draw) {
                            return draw.type == DrawCommand::Type::text &&
                                   draw.text.find("PROJECT NAME") !=
                                       std::string::npos;
                        }),
            "touch keyboard was not rendered at compact resolution");
    require(gui.frame(1024, 600).layout.text_commit.y +
                    gui.frame(1024, 600).layout.text_commit.h <=
                600.0F,
            "compact on-screen keyboard clipped its commit action");
    GuiInput direct;
    direct.type = GuiInput::Type::text_input;
    direct.text = " Session";
    gui.input(direct, 1024, 600);
    constexpr auto keyboard = TextEntryModel::characters();
    // Focus starts on A (index 1); rotate through the character and action
    // entries to DONE, then activate it using only wheel-equivalent input.
    for (std::size_t i = 0; i < keyboard.size() + 1; ++i)
        gui.input({GuiInput::Type::key_down, GuiKey::right}, 1024, 600);
    gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1024, 600);
    require(context.application.snapshot().project_name == "MIDI Session",
            "client-independent text entry did not rename the project");
    gui.begin_project_name_entry();
    const auto cancel = gui.frame(1024, 600).layout.text_cancel;
    gui.input({GuiInput::Type::touch_down, {}, 0, cancel.x + 2, cancel.y + 2},
              1024, 600);
    require(!gui.text_entry_state().active,
            "touch-equivalent text cancellation failed");
    context.action("project.save-as.v1",
                   {{"path", project.string()},
                    {"name", context.application.snapshot().project_name}});
    const auto restored = load_project(project);
    require(restored.timeline.tracks[0].instrument_id ==
                external_midi_instrument_id &&
                restored.timeline.tracks[0].midi_output_port == 2 &&
                restored.timeline.tracks[0].midi_channel == 2 &&
                restored.midi.clock_source == MidiClockSource::external &&
                restored.midi.forward_external_clock,
            "external MIDI routing did not survive project round-trip");
}

} // namespace

int main(int argc, char **argv) {
    const std::string selected = argc > 1 ? argv[1] : "all";
    const std::array scenarios{
        std::pair{"beat-project", scenario_beat_project},
        std::pair{"synth-modulation", scenario_synth_modulation},
        std::pair{"mixer-recording-external", scenario_mixer_recording_external},
        std::pair{"midi-performance-gui", scenario_midi_performance_gui}};
    try {
        std::size_t completed{};
        for (const auto &[name, scenario] : scenarios) {
            if (selected != "all" && selected != name)
                continue;
            const auto start = std::chrono::steady_clock::now();
            scenario();
            const auto elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            std::cout << "{\"schema\":\"neurogenic.workflow.v1\",\"scenario\":\""
                      << name << "\",\"status\":\"passed\",\"milliseconds\":"
                      << elapsed << "}\n";
            ++completed;
        }
        require(completed != 0, "unknown workflow scenario: " + selected);
        std::cout << "neurogenic.workflow.summary.v1 passed=" << completed
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "WORKFLOW FAILURE: " << error.what() << '\n';
        return 1;
    }
}
