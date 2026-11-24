#include "neurogenic/ui/slint_adapter.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "performance.h"
#include <slint.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <memory>

namespace neurogenic {
namespace {
slint::SharedString text(const std::string &value) {
    return slint::SharedString(std::string_view(value));
}

void publish(const SlintUiState &state,
             const slint::ComponentHandle<PerformanceWindow> &window) {
    window->set_transport_state(text(state.shell.transport));
    window->set_tempo(text(std::to_string(state.shell.tempo_bpm) + " BPM"));
    window->set_project_name(text(state.shell.project_name));
    window->set_project_dirty(state.shell.project_dirty);
    window->set_time_signature(text(state.shell.time_signature));
    window->set_position(text(state.shell.position));
    window->set_page_id(text(state.shell.page_id));
    window->set_page_title(text(state.shell.page_title));
    window->set_selected_track(text(state.performance.selected_track));
    window->set_track_owner(text(state.performance.track_owner));
    window->set_active_content(text(state.performance.active_content));
    window->set_launch_quantization(text(state.performance.launch_quantization));
    window->set_notification(text(state.shell.notification));
    window->set_arrangement_view(state.sequencer.arrangement_view);
    window->set_selected_pattern(text(state.sequencer.selected_pattern));
    window->set_pattern_length(text(state.sequencer.pattern_length));
    window->set_step_resolution(text(state.sequencer.resolution));
    window->set_selected_velocity(text(state.sequencer.selected_velocity));
    window->set_selected_duration(text(state.sequencer.selected_duration));
    window->set_arrangement_loop(text(state.sequencer.loop_range));
    window->set_sample_slot(static_cast<int>(state.sample.slot));
    window->set_sample_name(text(state.sample.name));
    window->set_sample_status(text(state.sample.status));
    window->set_sample_metadata(text(state.sample.metadata));
    window->set_sample_error(text(state.sample.error));
    window->set_analysis_status(text(state.sample.analysis_status));
    window->set_processing_status(text(state.sample.processing_status));
    window->set_processing_progress(state.sample.processing_progress);
    window->set_processing_can_cancel(state.sample.processing_can_cancel);
    window->set_processing_can_publish(state.sample.processing_can_publish);
    window->set_processing_audio_active(state.sample.processing_audio_active);
    window->set_recording_visible(state.recording.visible);
    window->set_recording_mode(text(state.recording.mode));
    window->set_recording_phase(text(state.recording.phase));
    window->set_recording_source(text(state.recording.source));
    window->set_recording_destination(text(state.recording.destination));
    window->set_recording_quantization(text(state.recording.quantization));
    window->set_recording_error(text(state.recording.error));
    window->set_recording_can_arm(state.recording.can_arm);
    window->set_recording_can_start(state.recording.can_start);
    window->set_recording_can_stop(state.recording.can_stop);
    window->set_recording_can_cancel(state.recording.can_cancel);
    window->set_recording_can_commit(state.recording.can_commit);
    window->set_recording_progress(state.recording.progress);
    window->set_recording_overflows(
        static_cast<int>(state.recording.overflow_count));
    window->set_sample_ready(state.sample.ready);
    window->set_sample_stereo(state.sample.channels > 1);
    window->set_waveform_zoom(state.sample.zoom);
    std::vector<ContextItem> encoders, functions;
    std::vector<PageItem> pages;
    std::vector<SceneItem> scenes;
    encoders.reserve(state.context.encoders.size());
    functions.reserve(state.context.functions.size());
    for (const auto &item : state.context.encoders)
        encoders.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    for (const auto &item : state.context.functions)
        functions.push_back({text(item.label), text(item.value), text(item.unit),
                            item.available});
    pages.reserve(state.shell.pages.size());
    for (const auto &page : state.shell.pages)
        pages.push_back({text(page.id), text(page.title), page.implemented});
    scenes.reserve(state.performance.scenes.size());
    for (const auto &scene : state.performance.scenes)
        scenes.push_back({text(scene.label), scene.queued});
    std::vector<PadItem> pads;
    pads.reserve(state.performance.pads.size());
    for (const auto &pad : state.performance.pads)
        pads.push_back({text(pad.label), text(pad.status), pad.active,
                        pad.queued, pad.selected, pad.available});
    window->set_pages(
        std::make_shared<slint::VectorModel<PageItem>>(std::move(pages)));
    window->set_scenes(
        std::make_shared<slint::VectorModel<SceneItem>>(std::move(scenes)));
    window->set_encoders(std::make_shared<slint::VectorModel<ContextItem>>(
        std::move(encoders)));
    window->set_functions(std::make_shared<slint::VectorModel<ContextItem>>(
        std::move(functions)));
    window->set_pads(
        std::make_shared<slint::VectorModel<PadItem>>(std::move(pads)));
    std::vector<StepItem> steps;
    for (const auto &step : state.sequencer.steps)
        steps.push_back({text(step.label), text(step.status), step.active,
                         step.selected, step.playhead, step.available});
    window->set_steps(std::make_shared<slint::VectorModel<StepItem>>(std::move(steps)));
    std::vector<PatternItem> patterns;
    for (const auto &pattern : state.sequencer.patterns)
        patterns.push_back({static_cast<int>(pattern.id), text(pattern.name),
                            pattern.selected, pattern.active, pattern.queued});
    window->set_patterns(
        std::make_shared<slint::VectorModel<PatternItem>>(std::move(patterns)));
    std::vector<ArrangementItem> arrangement;
    for (const auto &item : state.sequencer.arrangement)
        arrangement.push_back({static_cast<int>(item.id), text(item.label),
                               text(item.range), text(item.kind), item.selected,
                               item.enabled});
    window->set_arrangement_items(
        std::make_shared<slint::VectorModel<ArrangementItem>>(std::move(arrangement)));
    const auto visible_fraction = 1.0F / std::max(1.0F, state.sample.zoom);
    const auto visible_start = state.sample.offset * (1.0F - visible_fraction);
    const auto visible_end = visible_start + visible_fraction;
    const auto first_peak = static_cast<std::size_t>(
        visible_start * state.sample.waveform.size());
    const auto last_peak = std::min(state.sample.waveform.size(),
        static_cast<std::size_t>(std::ceil(visible_end * state.sample.waveform.size())));
    std::vector<float> waveform_left, waveform_right, markers;
    waveform_left.reserve(last_peak - std::min(first_peak, last_peak));
    waveform_right.reserve(last_peak - std::min(first_peak, last_peak));
    for (auto i = first_peak; i < last_peak; ++i) {
        const auto &peak = state.sample.waveform[i];
        waveform_left.push_back(std::max(std::abs(peak.left_min), std::abs(peak.left_max)));
        waveform_right.push_back(std::max(std::abs(peak.right_min), std::abs(peak.right_max)));
    }
    if (state.sample.frames)
        for (const auto &slice : state.sample.slices) {
            const auto normalized = static_cast<float>(slice.start_frame) /
                                    static_cast<float>(state.sample.frames);
            if (normalized >= visible_start && normalized <= visible_end)
                markers.push_back((normalized - visible_start) / visible_fraction);
        }
    window->set_waveform_left(
        std::make_shared<slint::VectorModel<float>>(std::move(waveform_left)));
    window->set_waveform_right(
        std::make_shared<slint::VectorModel<float>>(std::move(waveform_right)));
    window->set_slice_markers(
        std::make_shared<slint::VectorModel<float>>(std::move(markers)));
    const auto normalized_region_start = state.sample.frames
        ? static_cast<float>(state.sample.region_start) / state.sample.frames : 0.0F;
    const auto normalized_region_end = state.sample.frames
        ? static_cast<float>(state.sample.region_end) / state.sample.frames : 1.0F;
    window->set_region_start(std::clamp((normalized_region_start - visible_start) /
                                        visible_fraction, 0.0F, 1.0F));
    window->set_region_end(std::clamp((normalized_region_end - visible_start) /
                                      visible_fraction, 0.0F, 1.0F));
    std::vector<SliceItem> slices;
    for (const auto &slice : state.sample.slices)
        slices.push_back({static_cast<int>(slice.id), text(slice.label),
                          text(std::to_string(slice.start_frame) + ".." +
                               std::to_string(slice.end_frame)),
                          slice.selected, slice.assigned});
    window->set_sample_slices(
        std::make_shared<slint::VectorModel<SliceItem>>(std::move(slices)));
    std::vector<RegionItem> regions;
    for (const auto &region : state.sample.regions)
        regions.push_back({static_cast<int>(region.id), text(region.label),
                           text(std::to_string(region.start_frame) + ".." +
                                std::to_string(region.end_frame)),
                           text(region.fade), text(region.loop), region.selected});
    window->set_sample_regions(
        std::make_shared<slint::VectorModel<RegionItem>>(std::move(regions)));
    window->set_sound_track(text(state.sound.track));
    window->set_sound_instrument(text(state.sound.instrument));
    window->set_sound_instrument_id(text(state.sound.instrument_id));
    window->set_sound_instrument_available(state.sound.instrument_available);
    window->set_sound_is_synth(state.sound.synth);
    window->set_sound_is_sample(state.sound.sample);
    window->set_sound_subview(text(state.sound.subview));
    window->set_sound_selected_parameter(
        static_cast<int>(state.sound.selected_parameter));
    window->set_sound_selected_modulation(
        static_cast<int>(state.sound.selected_modulation));
    window->set_sound_sample_mode(text(sample_playback_mode_name(
        state.sound.sample_settings.mode)));
    window->set_sound_sample_tuning(text(
        std::to_string(state.sound.sample_settings.semitones) + " st / " +
        std::to_string(state.sound.sample_settings.fine_cents) + " ct"));
    window->set_sound_sample_envelope(text(
        std::to_string(state.sound.sample_settings.attack_seconds) + " / " +
        std::to_string(state.sound.sample_settings.decay_seconds) + " / " +
        std::to_string(state.sound.sample_settings.sustain) + " / " +
        std::to_string(state.sound.sample_settings.release_seconds)));
    window->set_sound_preset_status(text(state.sound.preset_status));
    window->set_sound_error(text(state.sound.error));
    std::vector<SoundParameterItem> sound_parameters;
    for (const auto &parameter : state.sound.parameters)
        sound_parameters.push_back(
            {text(parameter.id), text(parameter.name), text(parameter.value),
             text(parameter.unit), text(parameter.group),
             static_cast<float>(parameter.normalized), parameter.available,
             parameter.automatable, parameter.modulatable});
    window->set_sound_parameters(
        std::make_shared<slint::VectorModel<SoundParameterItem>>(
            std::move(sound_parameters)));
    std::vector<SoundModulationItem> sound_modulation;
    for (const auto &route : state.sound.modulation)
        sound_modulation.push_back(
            {static_cast<int>(route.id), text(route.source),
             text(route.destination), text(route.amount), route.enabled});
    window->set_sound_modulation(
        std::make_shared<slint::VectorModel<SoundModulationItem>>(
            std::move(sound_modulation)));
    window->set_mixer_bank(static_cast<int>(state.mixer.bank));
    window->set_mixer_bank_label(text(state.mixer.bank_label));
    window->set_mixer_selected_channel(
        static_cast<int>(state.mixer.selected_channel));
    window->set_mixer_error(text(state.mixer.error));
    std::vector<MixerChannelItem> mixer_channels;
    for (const auto &channel : state.mixer.channels)
        mixer_channels.push_back(
            {static_cast<int>(channel.id), text(channel.name), text(channel.type),
             text(channel.route), channel.gain, channel.pan,
             channel.meter_left, channel.meter_right, channel.aux_sends[0],
             channel.aux_sends[1], channel.mute, channel.solo, channel.clip,
             channel.selected, channel.available});
    window->set_mixer_channels(
        std::make_shared<slint::VectorModel<MixerChannelItem>>(
            std::move(mixer_channels)));
    std::vector<MixerFxItem> mixer_fx;
    for (const auto &fx : state.mixer.external_fx)
        mixer_fx.push_back(
            {static_cast<int>(fx.index), static_cast<int>(fx.latency_frames),
             text(fx.name), text(fx.status),
             text(fx.routing), fx.send_gain, fx.return_gain, fx.wet,
             fx.meter_left, fx.meter_right, fx.enabled, fx.bypass,
             fx.return_mute, fx.send_available, fx.return_available});
    window->set_mixer_external_fx(
        std::make_shared<slint::VectorModel<MixerFxItem>>(
            std::move(mixer_fx)));
    window->set_fx_subview(text(state.fx.subview));
    window->set_fx_owner_name(text(state.fx.owner_name));
    window->set_fx_chain_bypass(state.fx.chain_bypass);
    window->set_fx_error(text(state.fx.error));
    std::vector<FxOwnerItem> fx_owners;
    for (const auto &owner : state.fx.owners)
        fx_owners.push_back({text(owner.name), text(owner.kind), owner.selected});
    window->set_fx_owners(std::make_shared<slint::VectorModel<FxOwnerItem>>(std::move(fx_owners)));
    std::vector<FxInstanceItem> fx_effects;
    for (const auto &effect : state.fx.effects)
        fx_effects.push_back({static_cast<int>(effect.id), text(effect.name),
                              text(effect.type), effect.bypass, effect.selected});
    window->set_fx_effects(std::make_shared<slint::VectorModel<FxInstanceItem>>(std::move(fx_effects)));
    std::vector<FxParameterItem> fx_parameters;
    for (const auto &parameter : state.fx.parameters)
        fx_parameters.push_back({static_cast<int>(parameter.id), text(parameter.name),
            text(parameter.value), text(parameter.unit), static_cast<float>(parameter.normalized),
            parameter.automatable, parameter.modulatable, parameter.selected});
    window->set_fx_parameters(std::make_shared<slint::VectorModel<FxParameterItem>>(std::move(fx_parameters)));
    std::vector<FxModulatorItem> fx_modulators;
    for (const auto &modulator : state.fx.modulators)
        fx_modulators.push_back({static_cast<int>(modulator.id), text(modulator.name),
            text(modulator.type), text(modulator.owner), modulator.enabled, modulator.selected});
    window->set_fx_modulators(std::make_shared<slint::VectorModel<FxModulatorItem>>(std::move(fx_modulators)));
    std::vector<FxRouteItem> fx_routes;
    for (const auto &route : state.fx.routes)
        fx_routes.push_back({static_cast<int>(route.id), text(route.source), text(route.destination),
            text(route.depth), route.enabled, route.selected, route.valid});
    window->set_fx_routes(std::make_shared<slint::VectorModel<FxRouteItem>>(std::move(fx_routes)));
    std::vector<FxMacroItem> fx_macros;
    for (const auto &macro : state.fx.macros)
        fx_macros.push_back({static_cast<int>(macro.id), text(macro.name), text(macro.assignments),
            macro.value, macro.enabled, macro.selected});
    window->set_fx_macros(std::make_shared<slint::VectorModel<FxMacroItem>>(std::move(fx_macros)));
    window->set_browser_category(text(state.browser.category));
    window->set_browser_location(text(state.browser.location));
    window->set_browser_destination(text(state.browser.destination));
    window->set_browser_preview(text(state.browser.preview));
    window->set_browser_sort(text(state.browser.sort));
    window->set_browser_filter(text(state.browser.filter));
    window->set_browser_error(text(state.browser.error));
    window->set_browser_truncated(state.browser.truncated);
    window->set_browser_confirmation_pending(state.browser.confirmation_pending);
    window->set_browser_confirmation_message(text(state.browser.confirmation_message));
    std::vector<BrowserEntryItem> browser_entries;
    browser_entries.reserve(state.browser.entries.size());
    for (const auto &entry : state.browser.entries)
        browser_entries.push_back({text(entry.name), text(entry.type),
            text(entry.metadata), entry.directory, entry.compatible, entry.selected});
    window->set_browser_entries(
        std::make_shared<slint::VectorModel<BrowserEntryItem>>(std::move(browser_entries)));
    window->set_setup_subview(text(state.setup.subview));
    window->set_setup_audio_backend(text(state.setup.audio_backend));
    window->set_setup_audio_status(text(state.setup.audio_status));
    window->set_setup_active_input(text(state.setup.active_audio_input));
    window->set_setup_active_output(text(state.setup.active_audio_output));
    window->set_setup_requested_input(text(state.setup.requested_audio_input));
    window->set_setup_requested_output(text(state.setup.requested_audio_output));
    window->set_setup_sample_rate(static_cast<int>(state.setup.active_sample_rate));
    window->set_setup_active_buffer(static_cast<int>(state.setup.active_buffer_size));
    window->set_setup_requested_buffer(static_cast<int>(state.setup.requested_buffer_size));
    window->set_setup_active_input_channels(state.setup.active_input_channels);
    window->set_setup_active_output_channels(state.setup.active_output_channels);
    window->set_setup_requested_input_channels(state.setup.requested_input_channels);
    window->set_setup_requested_output_channels(state.setup.requested_output_channels);
    window->set_setup_restart_required(state.setup.restart_required);
    window->set_setup_pending(state.setup.pending);
    window->set_setup_fullscreen(state.setup.fullscreen);
    window->set_setup_reopen_last_project(state.setup.reopen_last_project);
    window->set_setup_ui_scale(state.setup.ui_scale);
    window->set_setup_startup_project(text(state.setup.startup_project));
    window->set_setup_midi_clock(text(state.setup.midi_clock));
    window->set_setup_system(text(state.setup.system));
    window->set_setup_version(text(state.setup.version));
    window->set_setup_callback_load(static_cast<float>(state.setup.callback_load));
    window->set_setup_callback_peak(static_cast<float>(state.setup.callback_peak_load));
    window->set_setup_xruns(static_cast<int>(state.setup.xruns));
    window->set_setup_xruns_available(state.setup.xruns_available);
    window->set_setup_error(text(state.setup.error));
    window->set_setup_learn_status(text(state.setup.controller_learn_status));
    std::vector<SetupDeviceItem> audio_inputs, audio_outputs, midi_inputs, midi_outputs;
    const auto append_devices = [](const auto &source, auto &target) {
        for (const auto &item : source)
            target.push_back({text(item.name), text(item.detail), item.selected,
                              item.available});
    };
    append_devices(state.setup.audio_inputs, audio_inputs);
    append_devices(state.setup.audio_outputs, audio_outputs);
    append_devices(state.setup.midi_inputs, midi_inputs);
    append_devices(state.setup.midi_outputs, midi_outputs);
    window->set_setup_audio_inputs(std::make_shared<slint::VectorModel<SetupDeviceItem>>(std::move(audio_inputs)));
    window->set_setup_audio_outputs(std::make_shared<slint::VectorModel<SetupDeviceItem>>(std::move(audio_outputs)));
    window->set_setup_midi_inputs(std::make_shared<slint::VectorModel<SetupDeviceItem>>(std::move(midi_inputs)));
    window->set_setup_midi_outputs(std::make_shared<slint::VectorModel<SetupDeviceItem>>(std::move(midi_outputs)));
    std::vector<SetupControllerItem> controllers;
    for (const auto &item : state.setup.controllers)
        controllers.push_back({text(item.id), text(item.transport), text(item.device),
            text(item.profile), text(item.protocol), text(item.status), text(item.error),
            item.connected, item.feedback, item.selected});
    window->set_setup_controllers(
        std::make_shared<slint::VectorModel<SetupControllerItem>>(std::move(controllers)));
}
} // namespace

bool slint_available() noexcept { return true; }

int run_slint_frontend(ApplicationService &application,
                       const SlintOptions &options, std::string &error) {
#if defined(__linux__)
    const auto wayland = std::getenv("WAYLAND_DISPLAY");
    const auto display = std::getenv("DISPLAY");
    if ((wayland == nullptr || *wayland == '\0') &&
        (display == nullptr || *display == '\0')) {
        error = "Slint requires an available Wayland or X11 display";
        return 1;
    }
#endif
    try {
        SlintFrontendAdapter adapter(application, options.setup);
        auto window = PerformanceWindow::create();
        window->window().set_size(slint::PhysicalSize({
            static_cast<std::uint32_t>(options.width),
            static_cast<std::uint32_t>(options.height)}));
        window->window().set_fullscreen(options.fullscreen);
        publish(adapter.state(), window);
        window->on_encoder_turned([&](int index, int delta) {
            (void)adapter.turn_encoder(static_cast<std::size_t>(index), delta);
            publish(adapter.state(), window);
        });
        window->on_function_invoked([&](int index) {
            (void)adapter.invoke_function(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_pad_invoked([&](int index, bool pressed) {
            (void)adapter.press_pad(static_cast<std::size_t>(index), pressed);
            publish(adapter.state(), window);
        });
        window->on_keyboard_input([&](slint::SharedString key, bool pressed) {
            (void)adapter.keyboard_action(key.data(), pressed);
            publish(adapter.state(), window);
        });
        window->on_page_selected([&](int index) {
            (void)adapter.select_page(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_transport_invoked([&](slint::SharedString operation) {
            if (operation == "play")
                (void)adapter.invoke_action("transport.play.v1");
            else if (operation == "stop")
                (void)adapter.invoke_action("transport.stop.v1");
            else
                (void)adapter.transport_record();
            publish(adapter.state(), window);
        });
        window->on_track_selected([&](int direction) {
            (void)adapter.select_track(direction);
            publish(adapter.state(), window);
        });
        window->on_return_arrangement([&] {
            (void)adapter.return_to_arrangement();
            publish(adapter.state(), window);
        });
        window->on_scene_invoked([&](int index) {
            (void)adapter.launch_scene(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_quantization_invoked([&] {
            (void)adapter.cycle_launch_quantization();
            publish(adapter.state(), window);
        });
        window->on_sequencer_step_invoked([&](int index) {
            (void)adapter.select_step(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_pattern_invoked([&](int index, bool launch) {
            (void)adapter.select_pattern(static_cast<std::size_t>(index), launch);
            publish(adapter.state(), window);
        });
        window->on_pattern_create([&](bool duplicate) {
            (void)adapter.create_pattern(duplicate);
            publish(adapter.state(), window);
        });
        window->on_resolution_invoked([&] {
            (void)adapter.cycle_step_resolution();
            publish(adapter.state(), window);
        });
        window->on_arrangement_toggle([&] {
            (void)adapter.toggle_arrangement_view();
            publish(adapter.state(), window);
        });
        window->on_arrangement_item_invoked([&](int index) {
            (void)adapter.select_arrangement_clip(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_step_edit([&](slint::SharedString operation) {
            const auto snapshot = application.snapshot();
            if (operation == "velocity-up")
                (void)adapter.set_step_velocity(std::min(1.0, snapshot.selected_event ? snapshot.selected_event->velocity + 0.05 : 1.0));
            else if (operation == "velocity-down")
                (void)adapter.set_step_velocity(std::max(0.0, snapshot.selected_event ? snapshot.selected_event->velocity - 0.05 : 0.0));
            else if (operation == "length-up")
                (void)adapter.set_step_duration(snapshot.selected_event ? snapshot.selected_event->duration + snapshot.step_ticks : snapshot.step_ticks);
            publish(adapter.state(), window);
        });
        window->on_sample_slot_selected([&](int direction) {
            (void)adapter.select_sample_slot(direction);
            publish(adapter.state(), window);
        });
        window->on_sample_load([&](slint::SharedString path) {
            (void)adapter.load_sample(path.data());
            publish(adapter.state(), window);
        });
        window->on_waveform_selected([&](float normalized) {
            const auto &sample = adapter.state().sample;
            const auto visible_fraction = 1.0F / std::max(1.0F, sample.zoom);
            const auto absolute = sample.offset * (1.0F - visible_fraction) +
                                  normalized * visible_fraction;
            (void)adapter.create_slice(static_cast<std::uint64_t>(
                absolute * sample.frames));
            publish(adapter.state(), window);
        });
        window->on_waveform_zoomed([&](float zoom) {
            (void)adapter.set_waveform_view(zoom, adapter.state().sample.offset);
            publish(adapter.state(), window);
        });
        window->on_slice_selected([&](int index) {
            (void)adapter.select_slice(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_slice_action([&](slint::SharedString operation) {
            if (operation == "delete")
                (void)adapter.delete_selected_slice();
            else if (operation == "assign")
                (void)adapter.assign_selected_slice(application.snapshot().selected_pad);
            publish(adapter.state(), window);
        });
        window->on_region_selected([&](int index) {
            (void)adapter.select_region(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_region_action([&](slint::SharedString operation) {
            const auto &sample = adapter.state().sample;
            const auto increment = std::max<std::uint64_t>(1, sample.frames / 1000);
            if (operation == "loop")
                (void)adapter.toggle_region_loop();
            else if (operation == "start-down")
                (void)adapter.set_region_bounds(sample.region_start > increment
                    ? sample.region_start - increment : 0, sample.region_end);
            else if (operation == "start-up")
                (void)adapter.set_region_bounds(sample.region_start + increment,
                                                sample.region_end);
            else if (operation == "end-down")
                (void)adapter.set_region_bounds(sample.region_start,
                    sample.region_end > increment ? sample.region_end - increment : 0);
            else if (operation == "end-up")
                (void)adapter.set_region_bounds(sample.region_start,
                    std::min(sample.frames, sample.region_end + increment));
            else if (operation == "fade-in")
                (void)adapter.set_region_fades(sample.fade_in + increment,
                                               sample.fade_out);
            else if (operation == "fade-out")
                (void)adapter.set_region_fades(sample.fade_in,
                                               sample.fade_out + increment);
            publish(adapter.state(), window);
        });
        window->on_sample_preview([&](slint::SharedString target) {
            if (target == "stop") (void)adapter.stop_preview();
            else (void)adapter.preview_sample(target.data());
            publish(adapter.state(), window);
        });
        window->on_sample_analysis([&](slint::SharedString operation) {
            if (operation == "start") (void)adapter.analyze_transients();
            else (void)adapter.apply_analysis(operation == "accept");
            publish(adapter.state(), window);
        });
        window->on_sample_processing([&](slint::SharedString operation) {
            (void)adapter.start_processing(operation.data());
            publish(adapter.state(), window);
        });
        window->on_processing_lifecycle(
            [&](slint::SharedString operation, slint::SharedString value) {
                (void)adapter.processing_action(operation.data(), value.data());
                publish(adapter.state(), window);
            });
        window->on_recording_mode_selected([&](slint::SharedString mode) {
            (void)adapter.set_recording_mode(mode.data());
            publish(adapter.state(), window);
        });
        window->on_recording_source_selected([&](slint::SharedString source) {
            const auto snapshot = application.snapshot();
            std::uint64_t source_id{};
            if (source == "track") source_id = snapshot.selected_track;
            else if (source == "group-post-fader") source_id = snapshot.selected_group;
            (void)adapter.set_recording_source(source.data(), source_id);
            publish(adapter.state(), window);
        });
        window->on_recording_quantization_selected(
            [&](slint::SharedString quantization) {
                (void)adapter.set_recording_quantization(quantization.data());
                publish(adapter.state(), window);
            });
        window->on_recording_action([&](slint::SharedString action) {
            (void)adapter.recording_action(action.data());
            publish(adapter.state(), window);
        });
        window->on_recording_closed([&] {
            adapter.close_recording();
            (void)adapter.refresh();
            publish(adapter.state(), window);
        });
        window->on_sound_subview_selected([&](slint::SharedString subview) {
            (void)adapter.select_sound_subview(subview.data());
            publish(adapter.state(), window);
        });
        window->on_sound_instrument_selected([&](slint::SharedString instrument) {
            (void)adapter.set_track_instrument(instrument.data());
            publish(adapter.state(), window);
        });
        window->on_sound_parameter_selected([&](int index) {
            (void)adapter.select_sound_parameter(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_sound_parameter_adjusted([&](int index, int steps) {
            (void)adapter.adjust_sound_parameter(
                static_cast<std::size_t>(index), steps);
            publish(adapter.state(), window);
        });
        window->on_sound_sample_action([&](slint::SharedString operation) {
            if (operation == "mode") (void)adapter.cycle_sample_playback_mode();
            else if (operation == "pitch-down")
                (void)adapter.adjust_sample_instrument("pitch", -1);
            else if (operation == "pitch-up")
                (void)adapter.adjust_sample_instrument("pitch", 1);
            else if (operation == "attack")
                (void)adapter.adjust_sample_instrument("attack", 1);
            else if (operation == "release")
                (void)adapter.adjust_sample_instrument("release", 1);
            publish(adapter.state(), window);
        });
        window->on_sound_modulation_selected([&](int index) {
            (void)adapter.select_sound_modulation(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_sound_modulation_action([&](slint::SharedString operation) {
            (void)adapter.edit_sound_modulation(operation.data());
            publish(adapter.state(), window);
        });
        window->on_sound_preset_action(
            [&](slint::SharedString operation, slint::SharedString library,
                slint::SharedString preset_id) {
                (void)adapter.preset_action(operation.data(), library.data(),
                                            preset_id.data());
                publish(adapter.state(), window);
            });
        window->on_mixer_channel_selected([&](int index) {
            (void)adapter.select_mixer_channel(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_mixer_bank_changed([&](int direction) {
            (void)adapter.bank_mixer(direction);
            publish(adapter.state(), window);
        });
        window->on_mixer_adjust(
            [&](slint::SharedString operation, int steps) {
                (void)adapter.adjust_mixer(operation.data(), steps);
                publish(adapter.state(), window);
            });
        window->on_mixer_toggle([&](slint::SharedString operation) {
            (void)adapter.toggle_mixer(operation.data());
            publish(adapter.state(), window);
        });
        window->on_mixer_route_cycle([&] {
            (void)adapter.cycle_mixer_route();
            publish(adapter.state(), window);
        });
        window->on_mixer_group_cycle([&] {
            (void)adapter.cycle_mixer_group();
            publish(adapter.state(), window);
        });
        window->on_mixer_fx_selected([&](int index) {
            (void)adapter.select_external_fx(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_mixer_fx_adjust(
            [&](slint::SharedString operation, int steps) {
                (void)adapter.adjust_external_fx(operation.data(), steps);
                publish(adapter.state(), window);
            });
        window->on_mixer_fx_toggle([&](slint::SharedString operation) {
            (void)adapter.toggle_external_fx(operation.data());
            publish(adapter.state(), window);
        });
        window->on_fx_subview_selected([&](slint::SharedString value) {
            (void)adapter.select_fx_subview(value.data()); publish(adapter.state(), window);
        });
        window->on_fx_owner_selected([&](int index) {
            (void)adapter.select_fx_owner(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_effect_selected([&](int index) {
            (void)adapter.select_effect(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_effect_add([&](slint::SharedString type) {
            (void)adapter.add_effect(type.data()); publish(adapter.state(), window);
        });
        window->on_fx_effect_action([&](slint::SharedString operation) {
            (void)adapter.edit_effect(operation.data()); publish(adapter.state(), window);
        });
        window->on_fx_parameter_selected([&](int index) {
            (void)adapter.select_effect_parameter(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_parameter_adjusted([&](int steps) {
            (void)adapter.adjust_effect_parameter(steps); publish(adapter.state(), window);
        });
        window->on_fx_modulator_selected([&](int index) {
            (void)adapter.select_fx_modulator(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_route_selected([&](int index) {
            (void)adapter.select_fx_route(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_route_action([&](slint::SharedString operation) {
            (void)adapter.edit_fx_route(operation.data()); publish(adapter.state(), window);
        });
        window->on_fx_route_create([&] {
            (void)adapter.create_fx_route(); publish(adapter.state(), window);
        });
        window->on_fx_macro_selected([&](int index) {
            (void)adapter.select_fx_macro(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_macro_adjusted([&](int steps) {
            (void)adapter.adjust_fx_macro(steps); publish(adapter.state(), window);
        });
        window->on_browser_category_selected([&](slint::SharedString category) {
            (void)adapter.select_browser_category(category.data()); publish(adapter.state(), window);
        });
        window->on_browser_parent([&] {
            (void)adapter.browser_parent(); publish(adapter.state(), window);
        });
        window->on_browser_entry_selected([&](int index) {
            (void)adapter.select_browser_entry(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_browser_activate([&] {
            (void)adapter.activate_browser_entry(); publish(adapter.state(), window);
        });
        window->on_browser_filter_requested([&](slint::SharedString filter) {
            (void)adapter.set_browser_filter(filter.data()); publish(adapter.state(), window);
        });
        window->on_browser_sort_requested([&] {
            (void)adapter.cycle_browser_sort(); publish(adapter.state(), window);
        });
        window->on_browser_project_action(
            [&](slint::SharedString action, slint::SharedString path) {
                (void)adapter.browser_project_action(action.data(), path.data());
                publish(adapter.state(), window);
            });
        window->on_browser_preset_action([&](slint::SharedString action) {
            (void)adapter.browser_preset_action(action.data()); publish(adapter.state(), window);
        });
        window->on_browser_confirm([&](bool accept) {
            (void)adapter.confirm_browser_action(accept); publish(adapter.state(), window);
        });
        window->on_setup_subview_selected([&](slint::SharedString value) {
            (void)adapter.select_setup_subview(value.data()); publish(adapter.state(), window);
        });
        window->on_setup_device_selected([&](slint::SharedString kind, int index) {
            (void)adapter.select_setup_device(kind.data(), static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_setup_value_adjusted([&](slint::SharedString property, int direction) {
            (void)adapter.adjust_setup_value(property.data(), direction); publish(adapter.state(), window);
        });
        window->on_setup_preference_toggled([&](slint::SharedString property) {
            (void)adapter.toggle_setup_preference(property.data()); publish(adapter.state(), window);
        });
        window->on_setup_preferences_action([&](slint::SharedString action) {
            (void)adapter.setup_preferences_action(action.data()); publish(adapter.state(), window);
        });
        window->on_setup_controller_selected([&](int index) {
            (void)adapter.select_setup_controller(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_setup_midi_learn([&](slint::SharedString operation,
                                        slint::SharedString control,
                                        slint::SharedString mode) {
            (void)adapter.setup_midi_learn(operation.data(), control.data(), mode.data()); publish(adapter.state(), window);
        });
        window->on_setup_profile_save([&](slint::SharedString path) {
            (void)adapter.save_setup_controller_profile(path.data()); publish(adapter.state(), window);
        });
        window->on_setup_exit([&] {
            if (adapter.request_application_exit()) window->hide();
        });
        window->on_notification_dismissed([&] {
            adapter.dismiss_notification();
            publish(adapter.state(), window);
        });
        slint::Timer refresh;
        refresh.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16),
                      [&] {
                          if (adapter.refresh())
                              publish(adapter.state(), window);
                          if (options.external_stop_requested &&
                              options.external_stop_requested())
                              window->hide();
                      });
        do {
            window->run();
        } while (options.request_close && !options.request_close());
        refresh.stop();
        adapter.shutdown();
        return 0;
    } catch (const std::exception &exception) {
        error = exception.what();
        return 1;
    }
}
} // namespace neurogenic
