#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include "neurogenic/project/preset.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

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
        SlintSetupRuntimeSnapshot setup_runtime;
        setup_runtime.revision = 1;
        setup_runtime.audio_backend = "simulated";
        setup_runtime.audio_status = "Running";
        setup_runtime.active_audio_output = "Output A";
        setup_runtime.active_audio_input = "Input A";
        setup_runtime.active_buffer_size = 128;
        setup_runtime.active_input_channels = 2;
        setup_runtime.active_output_channels = 2;
        setup_runtime.audio_inputs = {{"Input A", "Input A", "2 channels", true, true}};
        setup_runtime.audio_outputs = {
            {"Output A", "Output A", "2 channels", true, true},
            {"Output B", "Output B", "2 channels", false, true}};
        setup_runtime.midi_inputs = {{"MIDI A", "MIDI A", "simulated", false, true}};
        setup_runtime.controllers = {{"surface.1", "midi", "Surface",
            "profile.1", "MIDI", "Connected", {}, true, true, false}};
        setup_runtime.requested.audio_output = "Output A";
        setup_runtime.requested.audio_input = "Input A";
        setup_runtime.requested.audio_buffer_frames = 128;
        bool preferences_saved{}, learn_started{}, learn_cancelled{}, exit_requested{};
        SlintSetupServices setup_services;
        setup_services.snapshot = [&] { return setup_runtime; };
        setup_services.save = [&](const AppliancePreferences &value,
                                  std::string &) {
            setup_runtime.requested = value;
            ++setup_runtime.revision;
            preferences_saved = true;
            return true;
        };
        setup_services.begin_midi_learn =
            [&](std::string_view id, std::string_view control,
                std::string_view mode, std::string &) {
                learn_started = id == "surface.1" && control == "encoder.1" &&
                                mode == "cc-absolute";
                return learn_started;
            };
        setup_services.cancel_midi_learn =
            [&](std::string_view id, std::string &) {
                learn_cancelled = id == "surface.1";
                return learn_cancelled;
            };
        setup_services.request_exit = [&] {
            exit_requested = true;
            return true;
        };
        SlintFrontendAdapter adapter(application, std::move(setup_services));
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
        const auto long_length = application.snapshot().step_ticks * 17;
        require(application.execute({"track.pattern-length-set.v1",
                    {{"length", static_cast<std::int64_t>(long_length)}}}).accepted &&
                    adapter.refresh() && adapter.state().sequencer.step_page_count == 2 &&
                    adapter.change_step_page(1) &&
                    adapter.state().sequencer.step_page == 1 &&
                    adapter.state().sequencer.steps[0].label == "17" &&
                    adapter.state().sequencer.steps[0].available &&
                    !adapter.state().sequencer.steps[1].available,
                "long patterns expose absolute positions and partial final pages");
        require(adapter.select_step(0), "page-two step insertion");
        require(application.snapshot().selected_event &&
                    application.snapshot().selected_event->step == 16,
                "page-two absolute event selection");
        const auto page_two_pitch = application.snapshot().selected_event->pitch;
        const auto page_two_timing =
            application.snapshot().selected_event->timing_offset;
        require(adapter.edit_step_property("probability", -1),
                "page-two probability edit");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        if (!adapter.edit_step_property("ratchet", 1))
            throw std::runtime_error("page-two ratchet edit: " +
                                     adapter.state().shell.notification);
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.cycle_step_condition(), "page-two condition edit");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.edit_step_property("pitch", 1),
                "page-two pitch edit");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.edit_step_property("timing", 1),
                "page-two microtiming edit");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(application.snapshot().selected_event->trigger_probability == 0.95F &&
                    application.snapshot().selected_event->ratchet_count == 2 &&
                    application.snapshot().selected_event->trigger_condition ==
                        TriggerCondition::first_loop &&
                    application.snapshot().selected_event->pitch ==
                        page_two_pitch + 1 &&
                    application.snapshot().selected_event->timing_offset ==
                        page_two_timing + 1,
                "advanced step properties round trip through authoritative state");
        require(application.execute({"step.lock-set.v1",
                    {{"parameter_id", std::string("sample.gain")},
                     {"value", 0.75}}}).accepted && adapter.refresh() &&
                    application.snapshot().selected_step_locks.size() == 1,
                "parameter lock creation targets a discoverable parameter");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(
                    application.execute({"step.lock-remove.v1",
                    {{"parameter_id", std::string("sample.gain")}}}).accepted &&
                    adapter.refresh() &&
                    application.snapshot().selected_step_locks.empty(),
                "parameter locks create and remove against discoverable targets");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(application.execute({"edit.undo.v1", {}}).accepted,
                "parameter lock removal participates in Undo");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(application.snapshot().selected_step_locks.size() == 1 &&
                    application.snapshot().selected_step_locks.front().parameter_id ==
                        "sample.gain",
                "Undo restores the selected parameter lock");
        require(application.execute({"step.lock-remove.v1",
                    {{"parameter_id", std::string("sample.gain")}}}).accepted,
                "restored parameter lock remains editable");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(!application.execute({"track.pattern-length-set.v1",
                    {{"length", static_cast<std::int64_t>(
                        application.snapshot().step_ticks * 16)}}}).accepted,
                "shortening refuses to discard events beyond the new length");
        const auto tracks_before = application.snapshot().tracks.size();
        require(adapter.manage_track("duplicate") &&
                    application.snapshot().tracks.size() == tracks_before + 1,
                "track duplication preserves stable lifecycle state");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.manage_track("delete") &&
                    adapter.state().sequencer.confirm_delete_track &&
                    adapter.manage_track("confirm-delete") &&
                    application.snapshot().tracks.size() == tracks_before,
                "track duplication and confirmed deletion preserve stable lifecycle state");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.manage_track("create-synth") &&
                    application.snapshot().tracks.back().instrument_id ==
                        subtractive_instrument_id,
                "standalone track creation selects a supported instrument type");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.manage_track("delete") &&
                    adapter.manage_track("confirm-delete"),
                "created track can be removed with confirmation");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.select_pattern(0) &&
                    application.snapshot().selected_pattern == 1,
                "pattern selection uses semantic action");
        const auto pattern_length_before = application.snapshot().patterns[0].length;
        require(adapter.manage_pattern("length-up") &&
                    application.snapshot().patterns[0].length > pattern_length_before,
                "pattern length uses transactional semantic action");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.create_pattern(true),
                "pattern duplication uses transactional semantic action");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        const auto duplicated_pattern = application.snapshot().patterns.back().id;
        require(application.execute({"pattern.select.v1",
                    {{"pattern_id", static_cast<std::int64_t>(duplicated_pattern)}}}).accepted &&
                    adapter.refresh() && adapter.manage_pattern("delete") &&
                    adapter.state().sequencer.confirm_delete_pattern,
                "pattern deletion requires confirmation");
        if (!adapter.manage_pattern("confirm-delete"))
            throw std::runtime_error("pattern deletion: " +
                                     adapter.state().shell.notification);
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        const auto post_delete_timeline = timeline.snapshot();
        if (!std::none_of(post_delete_timeline.timeline.arrangement.patterns.begin(),
                        post_delete_timeline.timeline.arrangement.patterns.end(),
                        [&](const auto &pattern) { return pattern.id == duplicated_pattern; }))
            throw std::runtime_error("timeline pattern remained after confirmed deletion");
        const auto post_delete_application = application.snapshot();
        if (!std::none_of(post_delete_application.patterns.begin(),
                        post_delete_application.patterns.end(), [&](const auto &pattern) {
                            return pattern.id == duplicated_pattern;
                        }))
            throw std::runtime_error("pattern remained after confirmed deletion: selected=" +
                std::to_string(adapter.state().sequencer.selected_pattern_id) +
                " duplicate=" + std::to_string(duplicated_pattern));
        require(application.snapshot().selected_pattern == 1 &&
                    !application.execute({"pattern.select.v1",
                        {{"pattern_id", static_cast<std::int64_t>(999'999)}}}).accepted,
                "deleted and unknown pattern selections recover without stale identities");
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
        for (int attempt = 0; attempt < 200; ++attempt) {
            (void)adapter.refresh();
            if (adapter.state().sample.processing_can_publish)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(adapter.state().sample.processing_can_publish &&
                    adapter.processing_action("publish", "1"),
                "completed processing result requires explicit destination publication");
        for (int attempt = 0; attempt < 16; ++attempt) {
            engine.process(ack_left, ack_right);
            (void)application.snapshot();
            (void)adapter.refresh();
            if (adapter.state().sample.processing_audio_active)
                break;
        }
        const auto processed = samples.active_sample(SampleSlotId{1});
        require(adapter.state().sample.processing_audio_active && processed &&
                    processed->frames() == 890 &&
                    *std::max_element(processed->interleaved.begin(),
                                      processed->interleaved.end()) > 0.99F,
                "processed result became audio-active with normalized PCM");
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
        require(adapter.transport_record() && adapter.state().recording.visible &&
                    adapter.set_recording_mode("audio") &&
                    adapter.set_recording_destination(2),
                "record control opens an explicitly configured workflow");
        setup_runtime.active_input_channels = 0;
        ++setup_runtime.revision;
        require(!adapter.recording_action("arm") &&
                    samples.active_sample(SampleSlotId{2}) == nullptr,
                "unavailable hardware input rejects arm without replacing destination");
        setup_runtime.active_input_channels = 2;
        ++setup_runtime.revision;
        require(adapter.recording_action("arm") &&
                    adapter.recording_action("start"),
                "configured external workflow arms and starts");
        std::array<float, 256> capture_input{}, capture_output{};
        for (std::size_t frame = 0; frame < 128; ++frame) {
            capture_input[frame * 2] = 0.25F;
            capture_input[frame * 2 + 1] = -0.5F;
        }
        engine.process_io(capture_output, 2, capture_input, 2);
        require(adapter.recording_action("stop"),
                "recording workflow stops through the existing recorder");
        std::array<float, 2> stop_input{}, stop_output{};
        engine.process_io(stop_output, 2, stop_input, 2);
        for (int attempt = 0; attempt < 16; ++attempt) {
            (void)application.snapshot();
            engine.process(ack_left, ack_right);
            (void)adapter.refresh();
            if (adapter.state().recording.result_audio_active)
                break;
        }
        const auto captured = samples.active_sample(SampleSlotId{2});
        require(adapter.state().recording.result_audio_active && captured &&
                    captured->channels == 2 && captured->frames() == 128 &&
                    std::fabs(captured->interleaved[0] - 0.25F) < 0.0001F &&
                    std::fabs(captured->interleaved[1] + 0.5F) < 0.0001F,
                "external capture publishes acknowledged frame-accurate PCM");
        adapter.close_recording();
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
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.select_page(5) &&
                    adapter.state().shell.page_id == "fx" &&
                    adapter.state().shell.pages[5].implemented &&
                    !adapter.state().fx.owners.empty(),
                "FX page is registered and discovers stable mixer owners");
        require(adapter.select_fx_owner(0) && adapter.add_effect("gain"),
                "FX insertion uses create and insert semantic actions");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().fx.effects.size() == 1 &&
                    !adapter.state().fx.parameters.empty(),
                "published effect chain and descriptor parameters are presented");
        require(adapter.select_effect(0) && adapter.edit_effect("bypass"),
                "effect bypass uses semantic action");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().fx.effects[0].bypass,
                "effect bypass acknowledgement updates UI state");
        const auto fx_value = adapter.state().fx.parameters[0].normalized;
        require(adapter.select_effect_parameter(0) &&
                    adapter.adjust_effect_parameter(1),
                "descriptor-driven effect parameter adjustment is accepted");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    adapter.state().fx.parameters[0].normalized > fx_value,
                "effect parameter presentation follows published configuration");
        require(application.execute({"modulator.create.v1",
                    {{"type", static_cast<std::int64_t>(ModulatorType::lfo)},
                     {"owner", static_cast<std::int64_t>(ModulatorOwnerKind::master)},
                     {"owner_id", std::int64_t{0}},
                     {"name", std::string("FX LFO")}}}).accepted,
                "global FX modulator fixture");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && !adapter.state().fx.modulators.empty() &&
                    adapter.select_fx_modulator(adapter.state().fx.modulators.size() - 1),
                "global FX modulator is selectable");
        if (!adapter.create_fx_route())
            throw std::runtime_error("FX route creation: " + adapter.last_error());
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && !adapter.state().fx.routes.empty() &&
                    adapter.select_fx_route(adapter.state().fx.routes.size() - 1) &&
                    adapter.edit_fx_route("depth-down") &&
                    adapter.edit_fx_route("toggle"),
                "FX modulation routes support selection depth and enable actions");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.edit_fx_route("remove"),
                "FX modulation routes can be removed through the graph service");
        require(application.execute({"macro.create.v1",
                    {{"name", std::string("UI Macro")},
                     {"owner", std::string("project")}, {"value", 0.25}}}).accepted,
                "macro fixture uses semantic action");
        require(adapter.refresh() && !adapter.state().fx.macros.empty(),
                "FX page discovers performance macros");
        const auto macro_value = adapter.state().fx.macros[0].value;
        require(adapter.select_fx_macro(0) && adapter.adjust_fx_macro(1) &&
                    adapter.state().fx.macros[0].value > macro_value,
                "macro adjustment uses existing performance macro service");
        require(adapter.select_fx_subview("external") &&
                    adapter.state().fx.subview == "external" &&
                    adapter.state().mixer.external_fx.size() == 2,
                "external-FX subview reuses validated mixer loop state");
        require(adapter.select_external_fx(0) &&
                    adapter.adjust_external_fx("latency", 1),
                "external-FX manual latency uses semantic routing action");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    adapter.state().mixer.external_fx[0].latency_frames == 16 &&
                    adapter.toggle_external_fx("return-mute"),
                "external-FX latency and return monitoring are observable");
        engine.process(ack_left, ack_right);
        (void)adapter.refresh();
        require(adapter.select_fx_subview("chain") &&
                    adapter.add_effect("delay"),
                "a second native processor can be inserted");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().fx.effects.size() == 2 &&
                    adapter.select_effect(1) && adapter.edit_effect("up"),
                "effect reorder uses stable instance identities");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() &&
                    adapter.state().fx.effects[0].type.find("delay") != std::string::npos &&
                    adapter.select_effect(0) && adapter.edit_effect("remove"),
                "reordered chain is acknowledged before removal");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.select_effect(0) &&
                    adapter.edit_effect("remove"),
                "effect removal uses remove then delete semantic actions");
        engine.process(ack_left, ack_right);
        require(adapter.refresh() && adapter.state().fx.effects.empty(),
                "removed effect cannot remain as a stale selection");
        const auto browser_root = std::filesystem::temp_directory_path() /
            ("neurogenic-browser-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(browser_root / "folder");
        std::filesystem::create_directories(browser_root / "project");
        { std::ofstream(browser_root / "project" / "manifest.ngp")
              << "NEUROGENIC_PROJECT 1\n"; }
        { std::ofstream(browser_root / "kick.wav", std::ios::binary) << "RIFF"; }
        { std::ofstream(browser_root / "ignore.txt") << "not content"; }
        PresetLibrary preset_library(browser_root / "presets");
        SoundPreset synth_preset;
        synth_preset.metadata.id = "browser-synth";
        synth_preset.metadata.name = "Browser Synth";
        synth_preset.metadata.type = PresetType::synth_patch;
        preset_library.save(synth_preset);
        require(adapter.select_page(6) &&
                    adapter.state().shell.page_id == "browser" &&
                    adapter.state().shell.pages[6].implemented,
                "Browser page is an implemented independent destination");
        require(adapter.select_browser_category("projects") &&
                    adapter.navigate_browser(browser_root.string()),
                "project browser accepts a temporary location");
        const auto project_entry = std::find_if(
            adapter.state().browser.entries.begin(),
            adapter.state().browser.entries.end(), [](const auto &entry) {
                return entry.kind == SlintBrowserEntryKind::project;
            });
        require(project_entry != adapter.state().browser.entries.end(),
                "project manifests are distinguished from ordinary folders");
        const auto project_index = static_cast<std::size_t>(std::distance(
            adapter.state().browser.entries.begin(), project_entry));
        require(adapter.select_browser_entry(project_index) &&
                    adapter.activate_browser_entry() &&
                    adapter.state().browser.confirmation_pending &&
                    adapter.confirm_browser_action(false),
                "activating a project enters dirty-session confirmation instead of folder navigation");
        require(adapter.select_browser_category("samples") &&
                    adapter.navigate_browser(browser_root.string()) &&
                    adapter.state().browser.entries.size() == 4 &&
                    std::none_of(adapter.state().browser.entries.begin(),
                        adapter.state().browser.entries.end(), [](const auto &entry) {
                            return entry.name == "ignore.txt";
                        }),
                "sample browser includes directories and filters unsupported files");
        const auto sample_entry = std::find_if(
            adapter.state().browser.entries.begin(),
            adapter.state().browser.entries.end(), [](const auto &entry) {
                return entry.kind == SlintBrowserEntryKind::sample;
            });
        require(sample_entry != adapter.state().browser.entries.end() &&
                    sample_entry->name == "kick.wav",
                "sample content exposes compatible typed metadata");
        require(adapter.set_browser_filter("kick") &&
                    adapter.state().browser.entries.size() == 1 &&
                    adapter.cycle_browser_sort() &&
                    adapter.state().browser.sort == "type",
                "Browser filtering and sorting are page-local and revisioned");
        require(!adapter.navigate_browser((browser_root / "missing").string()) &&
                    !adapter.state().browser.error.empty(),
                "missing browser locations return an actionable error");
        require(adapter.select_browser_category("instrument-presets") &&
                    adapter.navigate_browser((browser_root / "presets").string()) &&
                    adapter.state().browser.entries.size() == 1 &&
                    adapter.state().browser.entries[0].id == "browser-synth" &&
                    adapter.browser_preset_action("validate"),
                "preset library enumeration and validation use production services");
        require(adapter.browser_project_action("new") &&
                    adapter.state().browser.confirmation_pending &&
                    adapter.confirm_browser_action(false) &&
                    !adapter.state().browser.confirmation_pending,
                "dirty-project replacement requires explicit confirmation and can cancel");
        require(adapter.select_page(7) &&
                    adapter.state().shell.page_id == "setup" &&
                    adapter.state().shell.pages[7].implemented &&
                    adapter.state().setup.active_sample_rate == 48000 &&
                    adapter.state().setup.audio_backend == "simulated",
                "Setup is an implemented page backed by copied runtime state");
        require(adapter.select_setup_subview("audio") &&
                    adapter.select_setup_device("audio-output", 1) &&
                    adapter.state().setup.pending &&
                    adapter.state().setup.restart_required &&
                    adapter.state().setup.requested_audio_output == "Output B",
                "audio changes remain staged and explicitly require restart");
        require(adapter.adjust_setup_value("buffer", 1) &&
                    adapter.state().setup.requested_buffer_size == 256 &&
                    adapter.setup_preferences_action("save") &&
                    preferences_saved && !adapter.state().setup.pending,
                "validated preferences save through the runtime service");
        require(adapter.toggle_setup_preference("fullscreen") &&
                    adapter.state().setup.fullscreen &&
                    adapter.setup_preferences_action("revert") &&
                    !adapter.state().setup.fullscreen,
                "pending Setup preferences can be reverted without affecting active state");
        require(adapter.select_setup_subview("controllers") &&
                    adapter.select_setup_controller(0) &&
                    adapter.setup_midi_learn("start", "encoder.1", "cc-absolute") &&
                    learn_started && adapter.setup_midi_learn("cancel") &&
                    learn_cancelled,
                "controller learn is delegated to the coordinator-facing service");
        require(adapter.request_application_exit() && exit_requested,
                "Setup exit uses the appliance dirty-session lifecycle callback");
        std::filesystem::remove_all(browser_root);
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
