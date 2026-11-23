#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    using namespace neurogenic;
    try {
        AudioEngine engine;
        SampleManager samples(engine);
        Timeline initial;
        initial.tracks.push_back({1, "UI Track"});
        initial.tracks.push_back({2, "Second Track"});
        initial.tracks[0].pattern.length = ticks_per_quarter * 4;
        initial.tracks[1].pattern.length = ticks_per_quarter * 4;
        PatternDefinition pattern;
        pattern.id = 1;
        pattern.name = "UI Pattern";
        pattern.tracks.push_back({1, {pattern.length, {}}});
        initial.arrangement.patterns.push_back(std::move(pattern));
        initial.arrangement.launch_slots.push_back(
            {11, 1, LaunchSourceType::pattern, 1, LaunchBehavior::loop,
             LaunchQuantization::bar});
        initial.arrangement.launch_slots.push_back(
            {3, 2, LaunchSourceType::pattern, 1, LaunchBehavior::loop,
             LaunchQuantization::bar});
        initial.arrangement.clips.push_back(
            {31, 1, 0, ticks_per_quarter * 4, true, 0, 1.0F, true});
        initial.arrangement.performance_scenes.push_back(
            {21, "UI Scene", {{1, 11}}, LaunchQuantization::bar});
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, initial);
        ApplicationService application(engine, timeline, samples);
        Sample sample{48'000, 2, {}};
        sample.interleaved.resize(4'800 * 2);
        for (std::size_t i = 0; i < 4'800; ++i) {
            sample.interleaved[i * 2] = static_cast<float>(i % 100) / 100.0F;
            sample.interleaved[i * 2 + 1] = -sample.interleaved[i * 2];
        }
        require(samples.publish_prepared(
                    SampleSlotId{0}, std::make_shared<const Sample>(sample),
                    "ui-stereo.wav"),
                "sample fixture publication");
        std::array<float, 16> ack_left{}, ack_right{};
        engine.process(ack_left, ack_right);
        SlintFrontendAdapter adapter(application);
        require(adapter.active(), "adapter lifecycle starts active");
        require(adapter.state().performance.selected_track == "UI Track",
                "selected track copied from application state");
        require(adapter.state().context.encoders.size() == 8 &&
                    adapter.state().context.functions.size() == 8 &&
                    adapter.state().performance.pads.size() == 16,
                "fixed presentation model dimensions");
        require(adapter.state().shell.pages.size() == 8 &&
                    adapter.state().shell.page_id == "performance" &&
                    adapter.state().shell.pages.front().implemented &&
                    adapter.state().shell.pages[1].implemented,
                "page registry exposes eight default destinations");
        require(adapter.state().performance.pads[0].available &&
                    adapter.state().performance.pads[0].slot_id == 3 &&
                    adapter.state().performance.pads[1].slot_id == 11,
                "performance slots map to pads by ascending stable ID");
        require(adapter.state().performance.scenes.size() == 1 &&
                    adapter.state().performance.scenes[0].label == "UI Scene",
                "performance scenes use authoritative launcher state");
        if (!adapter.cycle_launch_quantization())
            throw std::runtime_error("launch quantization: " + adapter.last_error());
        require(adapter.state().context.encoders[0].label != "",
                "contextual descriptor copied");
        require(format_slint_musical_position(ticks_per_quarter * 3, 6, 8) ==
                    "2.1.0",
                "musical position honors denominator-sized beats");
        require(!adapter.state().shell.record_armed &&
                    !adapter.state().shell.recording &&
                    adapter.state().shell.transport != "Armed" &&
                    adapter.state().shell.transport != "Recording",
                "idle recording state is distinct");
        require(slint_recording_label(RecordingState::armed, false) == "Armed",
                "armed recording state is distinct");
        require(slint_recording_label(RecordingState::recording, true) ==
                    "Recording",
                "active recording state is distinct");
        require(adapter.keyboard_action("space", true) &&
                    application.snapshot().playing,
                "keyboard simulation uses transport semantic action");
        require(adapter.invoke_function(7) && application.snapshot().playing,
                "contextual function dispatch");
        require(adapter.select_track(1) &&
                    application.snapshot().selected_track == 2,
                "track selection uses semantic actions");
        require(adapter.select_page(1) &&
                    adapter.state().shell.page_id == "sequencer" &&
                    adapter.state().shell.notification.empty(),
                "sequencer page selection is implemented");
        require(adapter.state().sequencer.steps[0].available &&
                    !adapter.state().sequencer.steps[0].active,
                "sequencer exposes authoritative empty steps");
        require(adapter.select_step(0) &&
                    adapter.state().sequencer.steps[0].active,
                "step grid edits through semantic action");
        require(adapter.set_step_velocity(0.5) &&
                    application.snapshot().selected_event &&
                    application.snapshot().selected_event->velocity == 0.5F,
                "selected step velocity edits through semantic action");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.set_step_duration(ticks_per_quarter / 2),
                "selected step length action accepted");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(application.snapshot().selected_event &&
                    application.snapshot().selected_event->duration ==
                        ticks_per_quarter / 2,
                "selected step length edits through semantic action");
        require(adapter.select_pattern(0) &&
                    application.snapshot().selected_pattern == 1,
                "pattern selection uses semantic action");
        const auto prior_resolution = application.snapshot().step_resolution;
        require(adapter.cycle_step_resolution() &&
                    application.snapshot().step_resolution != prior_resolution,
                "resolution changes through semantic action");
        require(adapter.toggle_arrangement_view() &&
                    adapter.state().sequencer.arrangement_view &&
                    adapter.state().sequencer.arrangement.size() == 1,
                "arrangement subview updates from page-local revision");
        require(adapter.select_arrangement_clip(0) &&
                    adapter.state().sequencer.arrangement[0].selected,
                "arrangement clip selection is page-local and observable");
        require(adapter.select_page(0) &&
                    adapter.state().shell.page_id == "performance",
                "performance navigation restores working page");
        require(adapter.select_page(2) &&
                    adapter.state().shell.page_id == "sample" &&
                    adapter.state().shell.notification.empty() &&
                    adapter.state().sample.ready &&
                    adapter.state().sample.channels == 2 &&
                    !adapter.state().sample.waveform.empty(),
                "sample page exposes bounded stereo waveform metadata");
        require(adapter.set_waveform_view(4.0F, 0.5F) &&
                    adapter.state().sample.zoom == 4.0F,
                "waveform zoom is page-local and revisioned");
        require(adapter.create_slice(100) &&
                    !adapter.state().sample.slices.empty(),
                "manual slice insertion uses semantic action");
        require(adapter.select_slice(0) && adapter.assign_selected_slice(2),
                "slice selection and pad assignment use semantic actions");
        require(adapter.preview_sample("slice") && adapter.stop_preview(),
                "slice preview uses production preview actions");
        require(application.execute(
                    {"region.create.v1",
                     {{"start_frame", std::int64_t{0}},
                      {"end_frame", std::int64_t{1000}},
                      {"name", std::string("UI Region")}}}).accepted &&
                    adapter.refresh() && !adapter.state().sample.regions.empty(),
                "region fixture created through semantic action");
        require(adapter.select_region(0) &&
                    adapter.set_region_bounds(10, 900) &&
                    adapter.set_region_fades(20, 30) &&
                    adapter.toggle_region_loop(),
                "region bounds fades and loop use semantic actions");
        require(!adapter.load_sample("") && !adapter.last_error().empty(),
                "missing sample path is actionable");
        require(adapter.analyze_transients(),
                "transient analysis starts through semantic action");
        require(adapter.apply_analysis(false),
                "analysis proposals can be rejected");
        require(adapter.start_processing("normalize-peak"),
                "offline processing lifecycle uses semantic actions");
        Sample replacement{48'000, 1, std::vector<float>(2'400, 0.25F)};
        require(samples.publish_prepared(
                    SampleSlotId{0},
                    std::make_shared<const Sample>(std::move(replacement)),
                    "ui-mono.wav"),
                "replacement sample publication");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().sample.channels == 1 &&
                    adapter.state().sample.name == "ui-mono.wav" &&
                    !adapter.state().sample.waveform.empty(),
                "acknowledged replacement refreshes mono waveform state");
        require(adapter.select_page(0), "sample page returns through navigation");
        require(!adapter.transport_record() &&
                    adapter.last_error().find("semantic action") !=
                        std::string::npos,
                "unsupported record action is honest");
        adapter.dismiss_notification();
        require(adapter.state().shell.notification.empty(),
                "notifications can be dismissed without application mutation");
        require(adapter.turn_encoder(0, 1),
                "contextual encoder uses parameter binding");
        require(!adapter.turn_encoder(99, 1) && !adapter.last_error().empty(),
                "unavailable binding is visible");
        require(!adapter.press_pad(16), "invalid pad rejected");
        require(adapter.select_page(3) &&
                    adapter.state().shell.page_id == "sound" &&
                    adapter.state().shell.notification.empty() &&
                    adapter.state().sound.sample,
                "Sound page exposes the selected sample instrument");
        require(adapter.cycle_sample_playback_mode() &&
                    adapter.adjust_sample_instrument("pitch", 1) &&
                    adapter.adjust_sample_instrument("attack", 1),
                "sample instrument playback tuning and envelope use actions");
        require(adapter.set_track_instrument(
                    std::string(subtractive_instrument_id)) &&
                    adapter.state().sound.synth &&
                    !adapter.state().sound.parameters.empty(),
                "track instrument selection exposes synth parameters");
        const auto cutoff = std::find_if(
            adapter.state().sound.parameters.begin(),
            adapter.state().sound.parameters.end(), [](const auto &parameter) {
                return parameter.id == "synth.cutoff";
            });
        require(cutoff != adapter.state().sound.parameters.end() &&
                    adapter.adjust_sound_parameter(static_cast<std::size_t>(
                        cutoff - adapter.state().sound.parameters.begin()), 1),
                "typed synth parameter editing uses instance semantic action");
        require(application.execute(
                    {"modulator.create.v1",
                     {{"type", std::int64_t{static_cast<std::int64_t>(
                                    ModulatorType::lfo)}},
                      {"owner", std::int64_t{static_cast<std::int64_t>(
                                     ModulatorOwnerKind::track)}},
                      {"owner_id", static_cast<std::int64_t>(
                                       application.snapshot().selected_track)},
                      {"name", std::string("Sound LFO")}}}).accepted,
                "track modulator fixture");
        const auto sound_modulator =
            application.snapshot().modulation.modulators[0].id;
        require(application
                    .execute({"modulator.route-create.v1",
                              {{"source_id", static_cast<std::int64_t>(
                                                 sound_modulator)},
                               {"destination", std::string("synth.cutoff")},
                               {"depth", 0.25}}})
                    .accepted,
                "local modulation route fixture");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    !adapter.state().sound.modulation.empty(),
                "Sound page discovers local modulation assignments");
        require(adapter.select_sound_subview("modulation") &&
                    adapter.edit_sound_modulation("depth-up"),
                "Sound modulation inspector edits local assignments");
        require(adapter.select_sound_subview("presets") &&
                    !adapter.preset_action("save", "", "") &&
                    adapter.last_error().find("semantic action") !=
                        std::string::npos,
                "unavailable preset save remains explicit");
        require(application.snapshot().project_dirty,
                "instrument and sample edits preserve project dirty state");
        require(adapter.select_page(4) &&
                    adapter.state().shell.page_id == "mixer" &&
                    adapter.state().shell.notification.empty() &&
                    adapter.state().mixer.channels.size() >= 5,
                "Mixer page discovers tracks, aux returns, and master");
        require(adapter.select_mixer_channel(0),
                "stable mixer track selection uses semantic action");
        const auto original_gain = adapter.state().mixer.channels[0].gain;
        require(adapter.adjust_mixer("gain", -1) &&
                    adapter.state().mixer.channels[0].gain < original_gain &&
                    adapter.adjust_mixer("pan", 1) &&
                    adapter.adjust_mixer("aux-1", 1),
                "track gain pan and aux send use mixer semantic actions");
        require(adapter.toggle_mixer("mute"),
                "track mute uses mixer semantic action");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.toggle_mixer("solo"),
                "track solo uses mixer semantic action");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().mixer.channels[0].mute &&
                    adapter.state().mixer.channels[0].solo,
                "acknowledged track mute and solo update presentation state");
        require(application.execute(
                    {"mixer.group-create.v1", {{"name", std::string("UI Bus")}}})
                    .accepted && adapter.refresh(),
                "group fixture uses semantic action");
        const auto group_index = std::find_if(
            adapter.state().mixer.channels.begin(),
            adapter.state().mixer.channels.end(), [](const auto &channel) {
                return channel.type == "Group";
            });
        require(group_index != adapter.state().mixer.channels.end(),
                "created group appears in Mixer presentation");
        const auto group_position = static_cast<std::size_t>(
            group_index - adapter.state().mixer.channels.begin());
        require(adapter.select_mixer_channel(0) &&
                    adapter.cycle_mixer_group(),
                "track group assignment uses routing semantic action");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    adapter.state().mixer.channels[0].route == "Group 1",
                "acknowledged group route is presented");
        require(!adapter.cycle_mixer_route() &&
                    adapter.last_error().find("Master") != std::string::npos,
                "direct routing cannot silently coexist with a group route");
        require(adapter.cycle_mixer_group(),
                "group assignment cycles back to master");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.cycle_mixer_route(),
                "direct output routing uses validated mixer action");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    adapter.state().mixer.channels[0].route == "Output 3/4",
                "accepted physical output route is presented");
        require(!application.execute(
                    {"mixer.output-pair-set.v1", {{"pair", std::int64_t{1}}}})
                     .accepted,
                "invalid output pair is rejected");
        require(
                    adapter.select_mixer_channel(group_position) &&
                    adapter.adjust_mixer("gain", -1) &&
                    adapter.toggle_mixer("mute"),
                "group selection levels and mute use group actions");
        require(adapter.state().mixer.external_fx.size() == 2 &&
                    !adapter.state().mixer.external_fx[0].send_available &&
                    !adapter.state().mixer.external_fx[0].return_available,
                "missing physical external-FX channels remain unavailable");
        require(adapter.select_external_fx(0) &&
                    adapter.adjust_external_fx("wet", 1),
                "external FX wet control uses existing semantic action");
        require(adapter.bank_mixer(1) && adapter.bank_mixer(-1),
                "mixer channel banking is revisioned page-local state");
        require(adapter.state().mixer.channels.back().type == "Master" &&
                    adapter.state().mixer.channels.back().meter_left ==
                        application.snapshot().mixer.master.left,
                "master meter presentation copies bounded mixer telemetry");
        const auto revision = adapter.state().shell.revision;
        require(!adapter.refresh() || adapter.state().shell.revision >= revision,
                "revisioned refresh");
        adapter.shutdown();
        require(!adapter.active() && !adapter.refresh() &&
                    !adapter.invoke_action("transport.stop.v1"),
                "shutdown prevents dispatch");
        samples.shutdown();
        std::cout << "neurogenic.slint-adapter-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
