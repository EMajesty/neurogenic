#include "neurogenic/ui/slint_adapter.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "performance.h"
#include <slint.h>
#include <slint-platform.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>

namespace neurogenic {
namespace {
SlintPublicationDiagnostics publication_diagnostics;

struct PublicationModels {
    std::shared_ptr<slint::VectorModel<PageItem>> pages;
    std::shared_ptr<slint::VectorModel<SceneItem>> scenes;
    std::shared_ptr<slint::VectorModel<ContextItem>> encoders, functions;
    std::shared_ptr<slint::VectorModel<PadItem>> pads;
    std::shared_ptr<slint::VectorModel<StepItem>> steps;
    std::shared_ptr<slint::VectorModel<TrackItem>> sequencer_tracks;
    std::shared_ptr<slint::VectorModel<PatternItem>> patterns;
    std::shared_ptr<slint::VectorModel<ArrangementItem>> arrangement;
    std::shared_ptr<slint::VectorModel<float>> waveform_left, waveform_right,
        slice_markers;
    std::shared_ptr<slint::VectorModel<SliceItem>> slices;
    std::shared_ptr<slint::VectorModel<RegionItem>> regions;
    std::shared_ptr<slint::VectorModel<SoundParameterItem>> sound_parameters;
    std::shared_ptr<slint::VectorModel<SoundModulationItem>> sound_modulation;
    std::shared_ptr<slint::VectorModel<MixerChannelItem>> mixer_channels;
    std::shared_ptr<slint::VectorModel<MixerFxItem>> mixer_fx;
    std::shared_ptr<slint::VectorModel<FxOwnerItem>> fx_owners;
    std::shared_ptr<slint::VectorModel<FxInstanceItem>> fx_effects;
    std::shared_ptr<slint::VectorModel<FxParameterItem>> fx_parameters;
    std::shared_ptr<slint::VectorModel<FxModulatorItem>> fx_modulators;
    std::shared_ptr<slint::VectorModel<FxModulatorTypeItem>> fx_modulator_types;
    std::shared_ptr<slint::VectorModel<FxModulatorParameterItem>> fx_modulator_parameters;
    std::shared_ptr<slint::VectorModel<FxDestinationItem>> fx_destinations;
    std::shared_ptr<slint::VectorModel<FxRouteItem>> fx_routes;
    std::shared_ptr<slint::VectorModel<FxMacroItem>> fx_macros;
    std::shared_ptr<slint::VectorModel<FxMacroAssignmentItem>> fx_macro_assignments;
    std::shared_ptr<slint::VectorModel<FxSceneItem>> fx_macro_scenes;
    std::shared_ptr<slint::VectorModel<BrowserEntryItem>> browser_entries;
    std::shared_ptr<slint::VectorModel<slint::SharedString>> browser_missing_assets;
    std::shared_ptr<slint::VectorModel<SetupDeviceItem>> audio_inputs,
        audio_outputs, midi_inputs, midi_outputs;
    std::shared_ptr<slint::VectorModel<SetupControllerItem>> controllers;
    std::shared_ptr<slint::VectorModel<SetupMidiRouteItem>> midi_routes;
    std::shared_ptr<slint::VectorModel<SetupMappingItem>> mappings;
    std::shared_ptr<slint::VectorModel<SetupMappingTargetItem>> mapping_targets;
    std::shared_ptr<slint::VectorModel<LiveLoopItem>> live_loops;
    std::shared_ptr<slint::VectorModel<PerformanceBankItem>> performance_banks;
};

class HeadlessWindowAdapter final : public slint::platform::WindowAdapter {
  public:
    slint::platform::AbstractRenderer &renderer() override { return renderer_; }
    slint::PhysicalSize size() override { return size_; }
    void set_size(slint::PhysicalSize value) override {
        size_ = value;
        pixels_.resize(static_cast<std::size_t>(size_.width) * size_.height);
        window().dispatch_resize_event(slint::LogicalSize(
            {static_cast<float>(size_.width), static_cast<float>(size_.height)}));
    }
    void set_visible(bool) override {}
    void request_redraw() override { redraw_requested_ = true; }
    void render() {
        slint::platform::update_timers_and_animations();
        renderer_.render(pixels_, size_.width);
        redraw_requested_ = false;
    }
    [[nodiscard]] const std::vector<slint::Rgb8Pixel> &pixels() const noexcept {
        return pixels_;
    }
  private:
    slint::platform::SoftwareRenderer renderer_{
        slint::platform::SoftwareRenderer::RepaintBufferType::NewBuffer};
    slint::PhysicalSize size_{{1280, 800}};
    std::vector<slint::Rgb8Pixel> pixels_{1280U * 800U};
    bool redraw_requested_{};
};

HeadlessWindowAdapter *headless_window{};

class HeadlessPlatform final : public slint::platform::Platform {
  public:
    std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override {
        auto result = std::make_unique<HeadlessWindowAdapter>();
        headless_window = result.get();
        return result;
    }
};

template <typename T, typename Setter>
void publish_model(std::shared_ptr<slint::VectorModel<T>> &model,
                   std::vector<T> values, Setter setter) {
    if (!model) {
        model = std::make_shared<slint::VectorModel<T>>(std::move(values));
        setter(model);
        ++publication_diagnostics.model_creations;
    } else if (model->row_count() == values.size()) {
        for (std::size_t row = 0; row < values.size(); ++row) {
            model->set_row_data(row, std::move(values[row]));
            ++publication_diagnostics.model_row_updates;
        }
    } else {
        model->set_vector(std::move(values));
        ++publication_diagnostics.model_resets;
    }
}

slint::SharedString text(const std::string &value) {
    return slint::SharedString(std::string_view(value));
}

void publish(const SlintUiState &state,
             const slint::ComponentHandle<PerformanceWindow> &window,
             bool reset_models = false,
             SlintRefreshKind kind = SlintRefreshKind::full) {
    static PublicationModels models;
    if (reset_models)
        models = {};
    ++publication_diagnostics.passes;
    if (kind == SlintRefreshKind::telemetry) {
        window->set_transport_state(text(state.shell.transport));
        window->set_position(text(state.shell.position));
        window->set_history_pending(state.shell.history_pending);
        window->set_can_undo(state.shell.can_undo);
        window->set_can_redo(state.shell.can_redo);
        window->set_undo_label(text(state.shell.undo_label));
        window->set_redo_label(text(state.shell.redo_label));
        window->set_recording_phase(text(state.recording.phase));
        window->set_recording_progress(state.recording.progress);
        window->set_recording_overflows(static_cast<int>(state.recording.overflow_count));
        window->set_recording_can_overdub(state.recording.can_overdub);
        window->set_recording_can_replace(state.recording.can_replace);
        window->set_recording_can_clear(state.recording.can_clear);
        window->set_recording_can_save(state.recording.can_save);
        window->set_recording_pass(text(state.recording.pass));
        window->set_recording_loop_iteration(
            static_cast<int>(state.recording.loop_iteration));
        std::vector<LiveLoopItem> live_loops;
        for (const auto &loop : state.recording.loops)
            live_loops.push_back({text(loop.label), text(loop.state),
                                  text(loop.mode), loop.selected,
                                  loop.audio_active});
        publish_model(models.live_loops, std::move(live_loops),
                      [&](auto value) { window->set_recording_loops(value); });
        window->set_processing_status(text(state.sample.processing_status));
        window->set_processing_progress(state.sample.processing_progress);
        window->set_processing_can_cancel(state.sample.processing_can_cancel);
        window->set_processing_can_publish(state.sample.processing_can_publish);
        window->set_processing_audio_active(state.sample.processing_audio_active);
        std::vector<ContextItem> encoders, functions;
        for (const auto &item : state.context.encoders)
            encoders.push_back({text(item.label), text(item.value), text(item.unit), item.available});
        for (const auto &item : state.context.functions)
            functions.push_back({text(item.label), text(item.value), text(item.unit), item.available});
        publish_model(models.encoders, std::move(encoders), [&](auto value) { window->set_encoders(value); });
        publish_model(models.functions, std::move(functions), [&](auto value) { window->set_functions(value); });
        window->set_context_identity(text(state.context.identity));
        window->set_context_bank(text(state.context.bank));
        if (state.shell.page_id == "performance") {
            std::vector<PadItem> pads;
            for (const auto &pad : state.performance.pads)
                pads.push_back({text(pad.label), text(pad.status), pad.active,
                                pad.queued, pad.selected, pad.available});
            publish_model(models.pads, std::move(pads), [&](auto value) { window->set_pads(value); });
        } else if (state.shell.page_id == "sequencer") {
            std::vector<StepItem> steps;
            for (const auto &step : state.sequencer.steps)
                steps.push_back({text(step.label), text(step.status), step.active,
                                 step.selected, step.playhead, step.available});
            publish_model(models.steps, std::move(steps), [&](auto value) { window->set_steps(value); });
        } else if (state.shell.page_id == "sound") {
            std::vector<SoundParameterItem> parameters;
            for (const auto &parameter : state.sound.parameters)
                parameters.push_back({text(parameter.id), text(parameter.name),
                    text(parameter.value), text(parameter.unit), text(parameter.group),
                    static_cast<float>(parameter.normalized), parameter.available,
                    parameter.automatable, parameter.modulatable});
            publish_model(models.sound_parameters, std::move(parameters), [&](auto value) { window->set_sound_parameters(value); });
        } else if (state.shell.page_id == "mixer") {
            std::vector<MixerChannelItem> channels;
            for (const auto &channel : state.mixer.channels)
                channels.push_back({static_cast<int>(channel.id), text(channel.name),
                    text(channel.type), text(channel.route), channel.gain, channel.pan,
                    channel.meter_left, channel.meter_right, channel.aux_sends[0],
                    channel.aux_sends[1], channel.mute, channel.solo, channel.clip,
                    channel.selected, channel.available});
            publish_model(models.mixer_channels, std::move(channels), [&](auto value) { window->set_mixer_channels(value); });
        } else if (state.shell.page_id == "setup") {
            window->set_setup_callback_load(static_cast<float>(state.setup.callback_load));
            window->set_setup_callback_peak(static_cast<float>(state.setup.callback_peak_load));
            window->set_setup_xruns(static_cast<int>(state.setup.xruns));
        }
        return;
    }
    window->set_transport_state(text(state.shell.transport));
    window->set_tempo(text(std::to_string(state.shell.tempo_bpm) + " BPM"));
    window->set_project_name(text(state.shell.project_name));
    window->set_project_dirty(state.shell.project_dirty);
    window->set_history_pending(state.shell.history_pending);
    window->set_can_undo(state.shell.can_undo);
    window->set_can_redo(state.shell.can_redo);
    window->set_undo_label(text(state.shell.undo_label));
    window->set_redo_label(text(state.shell.redo_label));
    window->set_time_signature(text(state.shell.time_signature));
    window->set_position(text(state.shell.position));
    window->set_page_id(text(state.shell.page_id));
    window->set_destination_id(text(state.shell.destination_id));
    window->set_effective_shift(state.shell.effective_shift);
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
    window->set_selected_pitch(text(state.sequencer.selected_pitch));
    window->set_selected_probability(text(state.sequencer.selected_probability));
    window->set_selected_condition(text(state.sequencer.selected_condition));
    window->set_selected_ratchet(text(state.sequencer.selected_ratchet));
    window->set_selected_timing(text(state.sequencer.selected_timing));
    window->set_step_page_label(text(state.sequencer.page_label));
    window->set_step_lock_parameter(text(state.sequencer.lock_parameter));
    window->set_step_lock_value(text(state.sequencer.lock_value));
    window->set_step_lock_available(state.sequencer.lock_available);
    window->set_playhead_follow(state.sequencer.playhead_follow);
    window->set_can_page_previous(state.sequencer.can_page_previous);
    window->set_can_page_next(state.sequencer.can_page_next);
    window->set_confirm_delete_track(state.sequencer.confirm_delete_track);
    window->set_confirm_delete_pattern(state.sequencer.confirm_delete_pattern);
    window->set_confirm_delete_clip(state.sequencer.confirm_delete_clip);
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
    window->set_recording_can_overdub(state.recording.can_overdub);
    window->set_recording_can_replace(state.recording.can_replace);
    window->set_recording_can_clear(state.recording.can_clear);
    window->set_recording_can_save(state.recording.can_save);
    window->set_recording_pass(text(state.recording.pass));
    window->set_recording_loop_iteration(
        static_cast<int>(state.recording.loop_iteration));
    window->set_recording_progress(state.recording.progress);
    window->set_recording_overflows(
        static_cast<int>(state.recording.overflow_count));
    std::vector<LiveLoopItem> live_loops;
    live_loops.reserve(state.recording.loops.size());
    for (const auto &loop : state.recording.loops)
        live_loops.push_back({text(loop.label), text(loop.state),
                              text(loop.mode), loop.selected,
                              loop.audio_active});
    publish_model(models.live_loops, std::move(live_loops),
                  [&](auto value) { window->set_recording_loops(value); });
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
    std::vector<PerformanceBankItem> performance_banks;
    for (const auto &bank : state.performance.banks)
        performance_banks.push_back(
            {static_cast<int>(bank.id), text(bank.name), bank.selected});
    pads.reserve(state.performance.pads.size());
    for (const auto &pad : state.performance.pads)
        pads.push_back({text(pad.label), text(pad.status), pad.active,
                        pad.queued, pad.selected, pad.available});
    publish_model(models.pages, std::move(pages), [&](auto value) { window->set_pages(value); });
    publish_model(models.scenes, std::move(scenes), [&](auto value) { window->set_scenes(value); });
    publish_model(models.encoders, std::move(encoders), [&](auto value) { window->set_encoders(value); });
    publish_model(models.functions, std::move(functions), [&](auto value) { window->set_functions(value); });
    window->set_context_identity(text(state.context.identity));
    window->set_context_bank(text(state.context.bank));
    publish_model(models.pads, std::move(pads), [&](auto value) { window->set_pads(value); });
    publish_model(models.performance_banks, std::move(performance_banks),
                  [&](auto value) { window->set_performance_banks(value); });
    window->set_performance_bank_name(text(state.performance.bank_name));
    std::vector<StepItem> steps;
    for (const auto &step : state.sequencer.steps)
        steps.push_back({text(step.label), text(step.status), step.active,
                         step.selected, step.playhead, step.available});
    publish_model(models.steps, std::move(steps), [&](auto value) { window->set_steps(value); });
    std::vector<TrackItem> sequencer_tracks;
    for (const auto &track : state.sequencer.tracks)
        sequencer_tracks.push_back({static_cast<int>(track.id), text(track.name),
            text(track.instrument), track.selected, track.muted});
    publish_model(models.sequencer_tracks, std::move(sequencer_tracks),
        [&](auto value) { window->set_sequencer_tracks(value); });
    std::vector<PatternItem> patterns;
    for (const auto &pattern : state.sequencer.patterns)
        patterns.push_back({static_cast<int>(pattern.id), text(pattern.name),
                            pattern.selected, pattern.active, pattern.queued});
    publish_model(models.patterns, std::move(patterns), [&](auto value) { window->set_patterns(value); });
    std::vector<ArrangementItem> arrangement;
    for (const auto &item : state.sequencer.arrangement)
        arrangement.push_back({static_cast<int>(item.id), text(item.label),
                               text(item.range), text(item.kind), item.selected,
                               item.enabled});
    publish_model(models.arrangement, std::move(arrangement), [&](auto value) { window->set_arrangement_items(value); });
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
    publish_model(models.waveform_left, std::move(waveform_left), [&](auto value) { window->set_waveform_left(value); });
    publish_model(models.waveform_right, std::move(waveform_right), [&](auto value) { window->set_waveform_right(value); });
    publish_model(models.slice_markers, std::move(markers), [&](auto value) { window->set_slice_markers(value); });
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
    publish_model(models.slices, std::move(slices), [&](auto value) { window->set_sample_slices(value); });
    std::vector<RegionItem> regions;
    for (const auto &region : state.sample.regions)
        regions.push_back({static_cast<int>(region.id), text(region.label),
                           text(std::to_string(region.start_frame) + ".." +
                                std::to_string(region.end_frame)),
                           text(region.fade), text(region.loop), region.selected});
    publish_model(models.regions, std::move(regions), [&](auto value) { window->set_sample_regions(value); });
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
    publish_model(models.sound_parameters, std::move(sound_parameters), [&](auto value) { window->set_sound_parameters(value); });
    std::vector<SoundModulationItem> sound_modulation;
    for (const auto &route : state.sound.modulation)
        sound_modulation.push_back(
            {static_cast<int>(route.id), text(route.source),
             text(route.destination), text(route.amount), route.enabled});
    publish_model(models.sound_modulation, std::move(sound_modulation), [&](auto value) { window->set_sound_modulation(value); });
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
    publish_model(models.mixer_channels, std::move(mixer_channels), [&](auto value) { window->set_mixer_channels(value); });
    std::vector<MixerFxItem> mixer_fx;
    for (const auto &fx : state.mixer.external_fx)
        mixer_fx.push_back(
            {static_cast<int>(fx.index), static_cast<int>(fx.latency_frames),
             text(fx.name), text(fx.status),
             text(fx.routing), fx.send_gain, fx.return_gain, fx.wet,
             fx.meter_left, fx.meter_right, fx.enabled, fx.bypass,
             fx.return_mute, fx.send_available, fx.return_available});
    publish_model(models.mixer_fx, std::move(mixer_fx), [&](auto value) { window->set_mixer_external_fx(value); });
    window->set_fx_subview(text(state.fx.subview));
    window->set_fx_owner_name(text(state.fx.owner_name));
    window->set_fx_chain_bypass(state.fx.chain_bypass);
    window->set_fx_error(text(state.fx.error));
    std::vector<FxOwnerItem> fx_owners;
    for (const auto &owner : state.fx.owners)
        fx_owners.push_back({text(owner.name), text(owner.kind), owner.selected});
    publish_model(models.fx_owners, std::move(fx_owners), [&](auto value) { window->set_fx_owners(value); });
    std::vector<FxInstanceItem> fx_effects;
    for (const auto &effect : state.fx.effects)
        fx_effects.push_back({static_cast<int>(effect.id), text(effect.name),
                              text(effect.type), effect.bypass, effect.selected});
    publish_model(models.fx_effects, std::move(fx_effects), [&](auto value) { window->set_fx_effects(value); });
    std::vector<FxParameterItem> fx_parameters;
    for (const auto &parameter : state.fx.parameters)
        fx_parameters.push_back({static_cast<int>(parameter.id), text(parameter.name),
            text(parameter.value), text(parameter.unit), static_cast<float>(parameter.normalized),
            parameter.automatable, parameter.modulatable, parameter.selected});
    publish_model(models.fx_parameters, std::move(fx_parameters), [&](auto value) { window->set_fx_parameters(value); });
    std::vector<FxModulatorItem> fx_modulators;
    for (const auto &modulator : state.fx.modulators)
        fx_modulators.push_back({static_cast<int>(modulator.id), text(modulator.name),
            text(modulator.type), text(modulator.owner), modulator.enabled, modulator.selected});
    publish_model(models.fx_modulators, std::move(fx_modulators), [&](auto value) { window->set_fx_modulators(value); });
    std::vector<FxModulatorTypeItem> fx_modulator_types;
    for (const auto &type : state.fx.modulator_types)
        fx_modulator_types.push_back({static_cast<int>(type.id), text(type.name),
            text(type.category), type.selected});
    publish_model(models.fx_modulator_types, std::move(fx_modulator_types),
        [&](auto value) { window->set_fx_modulator_types(value); });
    std::vector<FxModulatorParameterItem> fx_modulator_parameters;
    for (const auto &parameter : state.fx.modulator_parameters)
        fx_modulator_parameters.push_back({text(parameter.id), text(parameter.name),
            text(parameter.value), text(parameter.unit), parameter.selected});
    publish_model(models.fx_modulator_parameters,
        std::move(fx_modulator_parameters),
        [&](auto value) { window->set_fx_modulator_parameters(value); });
    std::vector<FxDestinationItem> fx_destinations;
    for (const auto &destination : state.fx.destinations)
        fx_destinations.push_back({text(destination.id), text(destination.name),
            text(destination.range), destination.selected});
    publish_model(models.fx_destinations, std::move(fx_destinations),
        [&](auto value) { window->set_fx_destinations(value); });
    std::vector<FxRouteItem> fx_routes;
    for (const auto &route : state.fx.routes)
        fx_routes.push_back({static_cast<int>(route.id), text(route.source), text(route.destination),
            text(route.depth), route.enabled, route.selected, route.valid});
    publish_model(models.fx_routes, std::move(fx_routes), [&](auto value) { window->set_fx_routes(value); });
    std::vector<FxMacroItem> fx_macros;
    for (const auto &macro : state.fx.macros)
        fx_macros.push_back({static_cast<int>(macro.id), text(macro.name), text(macro.assignments),
            macro.value, macro.enabled, macro.selected});
    publish_model(models.fx_macros, std::move(fx_macros), [&](auto value) { window->set_fx_macros(value); });
    std::vector<FxMacroAssignmentItem> fx_macro_assignments;
    for (const auto &assignment : state.fx.macro_assignments)
        fx_macro_assignments.push_back({static_cast<int>(assignment.id),
            text(assignment.destination), text(assignment.range),
            text(assignment.curve), assignment.enabled, assignment.selected});
    publish_model(models.fx_macro_assignments, std::move(fx_macro_assignments),
        [&](auto value) { window->set_fx_macro_assignments(value); });
    std::vector<FxSceneItem> fx_macro_scenes;
    for (const auto &scene : state.fx.scenes)
        fx_macro_scenes.push_back({static_cast<int>(scene.id), text(scene.name),
            text(scene.summary), scene.selected});
    publish_model(models.fx_macro_scenes, std::move(fx_macro_scenes),
        [&](auto value) { window->set_fx_macro_scenes(value); });
    window->set_browser_category(text(state.browser.category));
    window->set_browser_location(text(state.browser.location));
    window->set_browser_destination(text(state.browser.destination));
    window->set_browser_preview(text(state.browser.preview));
    window->set_browser_sort(text(state.browser.sort));
    window->set_browser_filter(text(state.browser.filter));
    window->set_browser_error(text(state.browser.error));
    window->set_browser_truncated(state.browser.truncated);
    window->set_browser_loading(state.browser.loading);
    window->set_browser_page_label(text("Page " + std::to_string(state.browser.page + 1) +
        "/" + std::to_string(state.browser.page_count)));
    window->set_browser_can_previous_page(state.browser.can_previous_page);
    window->set_browser_can_next_page(state.browser.can_next_page);
    window->set_browser_selected_missing(static_cast<int>(state.browser.selected_missing));
    window->set_browser_confirmation_pending(state.browser.confirmation_pending);
    window->set_browser_confirmation_message(text(state.browser.confirmation_message));
    std::vector<BrowserEntryItem> browser_entries;
    browser_entries.reserve(state.browser.entries.size());
    for (const auto &entry : state.browser.entries)
        browser_entries.push_back({text(entry.name), text(entry.type),
            text(entry.metadata), entry.directory, entry.compatible, entry.selected});
    publish_model(models.browser_entries, std::move(browser_entries), [&](auto value) { window->set_browser_entries(value); });
    std::vector<slint::SharedString> missing_assets;
    for (const auto &missing : state.browser.missing_assets)
        missing_assets.push_back(text(missing));
    publish_model(models.browser_missing_assets, std::move(missing_assets),
        [&](auto value) { window->set_browser_missing_assets(value); });
    window->set_text_entry_visible(state.text_entry.visible);
    window->set_text_entry_title(text(state.text_entry.title));
    window->set_text_entry_value(text(state.text_entry.text));
    window->set_text_entry_cursor(static_cast<int>(state.text_entry.cursor));
    window->set_text_entry_error(text(state.text_entry.error));
    window->set_text_entry_uppercase(state.text_entry.uppercase);
    window->set_text_entry_symbols(state.text_entry.symbols);
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
    publish_model(models.audio_inputs, std::move(audio_inputs), [&](auto value) { window->set_setup_audio_inputs(value); });
    publish_model(models.audio_outputs, std::move(audio_outputs), [&](auto value) { window->set_setup_audio_outputs(value); });
    publish_model(models.midi_inputs, std::move(midi_inputs), [&](auto value) { window->set_setup_midi_inputs(value); });
    publish_model(models.midi_outputs, std::move(midi_outputs), [&](auto value) { window->set_setup_midi_outputs(value); });
    std::vector<SetupControllerItem> controllers;
    for (const auto &item : state.setup.controllers)
        controllers.push_back({text(item.id), text(item.transport), text(item.device),
            text(item.profile), text(item.protocol), text(item.status), text(item.error),
            text(item.learn_status), item.connected, item.feedback, item.selected});
    publish_model(models.controllers, std::move(controllers), [&](auto value) { window->set_setup_controllers(value); });
    std::vector<SetupMidiRouteItem> midi_routes;
    for (const auto &route : state.setup.midi_routes) {
        std::string flags;
        if (route.notes) flags += "Notes ";
        if (route.control_change) flags += "CC ";
        if (route.clock) flags += "Clock ";
        if (route.transport) flags += "Transport";
        const auto channel = [](std::int8_t value) {
            return value < 0 ? std::string("Omni") : std::to_string(value + 1);
        };
        midi_routes.push_back({static_cast<int>(route.id),
            text("Port " + std::to_string(route.input_port)),
            text("Port " + std::to_string(route.output_port)),
            text(channel(route.input_channel) + " → " + channel(route.output_channel)),
            text(flags), route.enabled, route.selected});
    }
    publish_model(models.midi_routes, std::move(midi_routes),
        [&](auto value) { window->set_setup_midi_routes(value); });
    std::vector<SetupMappingItem> mappings;
    for (const auto &mapping : state.setup.mappings)
        mappings.push_back({text(mapping.endpoint_id), text(mapping.control_id),
            text(mapping.source), text(mapping.mode), mapping.feedback,
            mapping.selected});
    publish_model(models.mappings, std::move(mappings),
        [&](auto value) { window->set_setup_mappings(value); });
    std::vector<SetupMappingTargetItem> mapping_targets;
    for (const auto &target : state.setup.mapping_targets)
        mapping_targets.push_back({text(target.kind), text(target.id),
                                   text(target.label), target.selected});
    publish_model(models.mapping_targets, std::move(mapping_targets),
        [&](auto value) { window->set_setup_mapping_targets(value); });
}
} // namespace

SlintPublicationDiagnostics slint_publication_diagnostics() noexcept {
    return publication_diagnostics;
}

void reset_slint_publication_diagnostics() noexcept {
    publication_diagnostics = {};
}

bool slint_available() noexcept { return true; }

int run_slint_frontend(ApplicationService &application,
                       const SlintOptions &options, std::string &error) {
    if (options.interaction_test && options.interaction_test_backend)
        slint::platform::set_platform(std::make_unique<HeadlessPlatform>());
#if defined(__linux__)
    const auto wayland = std::getenv("WAYLAND_DISPLAY");
    const auto display = std::getenv("DISPLAY");
    if (!options.interaction_test &&
        (wayland == nullptr || *wayland == '\0') &&
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
        if (options.interaction_test)
            window->window().dispatch_resize_event(
                slint::LogicalSize({static_cast<float>(options.width),
                                    static_cast<float>(options.height)}));
        window->window().set_fullscreen(options.fullscreen);
        reset_slint_publication_diagnostics();
        publish(adapter.state(), window, true);
        window->on_encoder_turned([&](int index, int delta) {
            (void)adapter.turn_encoder(static_cast<std::size_t>(index), delta);
            publish(adapter.state(), window);
        });
        window->on_function_invoked([&](int index) {
            (void)adapter.invoke_function(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_context_bank_changed([&](int direction) {
            (void)adapter.change_context_bank(direction);
            publish(adapter.state(), window);
        });
        window->on_history_action([&](slint::SharedString operation) {
            (void)adapter.invoke_action(
                std::string_view(operation.data()) == "undo" ? "edit.undo.v1"
                                                               : "edit.redo.v1");
            publish(adapter.state(), window);
        });
        window->on_pad_invoked([&](int index, bool pressed) {
            (void)adapter.press_pad(static_cast<std::size_t>(index), pressed);
            publish(adapter.state(), window);
        });
        window->on_pad_assignment([&](int position, int slot) {
            (void)adapter.assign_performance_pad(
                static_cast<std::size_t>(position),
                static_cast<std::uint64_t>(slot));
            publish(adapter.state(), window);
        });
        window->on_pad_assignment_clear([&](int position) {
            (void)adapter.clear_performance_pad(
                static_cast<std::size_t>(position));
            publish(adapter.state(), window);
        });
        window->on_performance_bank_selected([&](int index) {
            (void)adapter.select_performance_bank(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_performance_bank_action([&](slint::SharedString operation) {
            (void)adapter.manage_performance_bank(operation.data());
            publish(adapter.state(), window);
        });
        window->on_keyboard_input([&](slint::SharedString key, bool pressed) {
            (void)adapter.keyboard_action(key.data(), pressed);
            publish(adapter.state(), window);
        });
        window->on_page_selected([&](int index) {
            (void)adapter.select_reference_page(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_reference_shift_changed([&](bool effective) {
            (void)adapter.set_reference_shift(effective);
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
        window->on_arrangement_manage([&](slint::SharedString operation) {
            (void)adapter.manage_arrangement(operation.data());
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
        window->on_step_page([&](int direction) {
            (void)adapter.change_step_page(direction);
            publish(adapter.state(), window);
        });
        window->on_playhead_follow_changed([&](bool enabled) {
            (void)adapter.set_playhead_follow(enabled);
            publish(adapter.state(), window);
        });
        window->on_advanced_step_edit(
            [&](slint::SharedString property, int direction) {
                (void)adapter.edit_step_property(property.data(), direction);
                publish(adapter.state(), window);
            });
        window->on_condition_cycle([&] {
            (void)adapter.cycle_step_condition();
            publish(adapter.state(), window);
        });
        window->on_lock_parameter_cycle([&](int direction) {
            (void)adapter.cycle_lock_parameter(direction);
            publish(adapter.state(), window);
        });
        window->on_lock_edit(
            [&](slint::SharedString operation, int direction) {
                (void)adapter.edit_selected_lock(operation.data(), direction);
                publish(adapter.state(), window);
            });
        window->on_track_item_selected([&](int index) {
            (void)adapter.select_track_index(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_track_manage([&](slint::SharedString operation) {
            (void)adapter.manage_track(operation.data());
            publish(adapter.state(), window);
        });
        window->on_pattern_manage([&](slint::SharedString operation) {
            (void)adapter.manage_pattern(operation.data());
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
        window->on_sample_browse_requested([&] {
            (void)adapter.select_page(6);
            (void)adapter.select_browser_category("samples");
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
        window->on_recording_loop_selected([&](int index) {
            if (index >= 0)
                (void)adapter.select_live_loop(static_cast<std::size_t>(index));
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
        window->on_fx_modulator_type_selected([&](int index) {
            (void)adapter.select_fx_modulator_type(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_modulator_action([&](slint::SharedString operation) {
            (void)adapter.manage_fx_modulator(operation.data()); publish(adapter.state(), window);
        });
        window->on_fx_modulator_parameter_selected([&](int index) {
            (void)adapter.select_fx_modulator_parameter(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_modulator_parameter_adjusted([&](int steps) {
            (void)adapter.adjust_fx_modulator_parameter(steps); publish(adapter.state(), window);
        });
        window->on_fx_destination_selected([&](int index) {
            (void)adapter.select_fx_destination(static_cast<std::size_t>(index)); publish(adapter.state(), window);
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
        window->on_fx_macro_action([&](slint::SharedString operation) {
            (void)adapter.manage_fx_macro(operation.data()); publish(adapter.state(), window);
        });
        window->on_fx_macro_assignment_selected([&](int index) {
            (void)adapter.select_fx_macro_assignment(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_macro_assignment_action([&](slint::SharedString operation) {
            (void)adapter.manage_fx_macro_assignment(operation.data()); publish(adapter.state(), window);
        });
        window->on_fx_macro_scene_selected([&](int index) {
            (void)adapter.select_fx_scene(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_fx_macro_scene_action([&](slint::SharedString operation) {
            (void)adapter.manage_fx_scene(operation.data()); publish(adapter.state(), window);
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
        window->on_browser_page_requested([&](int direction) {
            (void)adapter.change_browser_page(direction); publish(adapter.state(), window);
        });
        window->on_browser_refresh_requested([&] {
            (void)adapter.refresh_browser(); publish(adapter.state(), window);
        });
        window->on_browser_missing_selected([&](int index) {
            (void)adapter.select_missing_asset(static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_browser_relink_requested([&](bool prefix) {
            (void)adapter.relink_missing_asset(prefix); publish(adapter.state(), window);
        });
        window->on_text_entry_requested(
            [&](slint::SharedString operation, slint::SharedString current) {
                (void)adapter.request_text_entry(operation.data(), current.data());
                publish(adapter.state(), window);
            });
        window->on_text_entry_insert([&](slint::SharedString value) {
            (void)adapter.text_entry_insert(value.data()); publish(adapter.state(), window);
        });
        window->on_text_entry_action([&](slint::SharedString action) {
            (void)adapter.text_entry_action(action.data()); publish(adapter.state(), window);
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
        window->on_setup_midi_route_selected([&](int index) {
            (void)adapter.select_setup_midi_route(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_setup_midi_route_action([&](slint::SharedString operation) {
            (void)adapter.edit_setup_midi_route(operation.data()); publish(adapter.state(), window);
        });
        window->on_setup_mapping_selected([&](int index) {
            (void)adapter.select_setup_mapping(static_cast<std::size_t>(index)); publish(adapter.state(), window);
        });
        window->on_setup_mapping_remove([&] {
            (void)adapter.remove_setup_mapping(); publish(adapter.state(), window);
        });
        window->on_setup_mapping_target_selected([&](int index) {
            (void)adapter.select_setup_mapping_target(
                static_cast<std::size_t>(index));
            publish(adapter.state(), window);
        });
        window->on_setup_mapping_target_apply([&] {
            (void)adapter.apply_setup_mapping_target();
            publish(adapter.state(), window);
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
        if (options.interaction_test) {
            // Exercise the generated component callbacks after the production
            // bindings above are installed. This intentionally tests the same
            // callback path used by mouse/touch controls, without a display.
            window->invoke_transport_invoked("play");
            if (!application.snapshot().playing)
                throw std::runtime_error("generated transport callback did not start playback");
            window->invoke_transport_invoked("stop");
            const auto initial_track = application.snapshot().selected_track;
            window->invoke_track_selected(1);
            if (application.snapshot().selected_track == initial_track)
                throw std::runtime_error(
                    "generated track-navigation callback did not change the stable target");
            window->show();
            const auto capture_headless = [&](const std::string &path) {
                if (path.empty() || !options.interaction_test_backend ||
                    !headless_window)
                    return;
                headless_window->render();
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                if (!output)
                    throw std::runtime_error("could not create rendered test artifact");
                const auto size = headless_window->size();
                output << "P6\n" << size.width << ' ' << size.height << "\n255\n";
                for (const auto &pixel : headless_window->pixels()) {
                    output.put(static_cast<char>(pixel.r));
                    output.put(static_cast<char>(pixel.g));
                    output.put(static_cast<char>(pixel.b));
                }
            };
            constexpr std::array<std::string_view, 8> page_ids{
                "performance", "sequencer", "sample", "sound", "mixer",
                "fx", "fx", "browser"};
            constexpr std::array<std::string_view, 8> destination_ids{
                "performance", "sequencer", "sample", "sound", "mixer",
                "modulation", "macros", "browser"};
            for (int page = 0; page < 8; ++page) {
                window->invoke_page_selected(page);
                if (std::string_view(window->get_page_id().data()) !=
                    page_ids[static_cast<std::size_t>(page)])
                    throw std::runtime_error("generated page callback did not publish selection");
                if (std::string_view(window->get_destination_id().data()) !=
                    destination_ids[static_cast<std::size_t>(page)])
                    throw std::runtime_error("generated page callback lost destination identity");
                switch (page) {
                case 0: window->invoke_pad_invoked(0, true); break;
                case 1: {
                    window->invoke_step_page(1);
                    if (std::string_view(window->get_step_page_label().data()) !=
                        "Page 2/2")
                        throw std::runtime_error("generated step paging did not publish");
                    window->invoke_advanced_step_edit("probability", -1);
                    const auto encoder0 = window->get_encoders()
                        ? window->get_encoders()->row_data(0)
                        : std::optional<ContextItem>{};
                    const auto encoder7 = window->get_encoders()
                        ? window->get_encoders()->row_data(7)
                        : std::optional<ContextItem>{};
                    if (!encoder0 || encoder0->label != "PITCH" ||
                        !encoder7 ||
                        !std::string_view(encoder7->label.data()).starts_with("LOCK"))
                        throw std::runtime_error(
                            "generated contextual sequencer descriptors are not V2-aligned");
                    window->invoke_encoder_turned(1, -1);
                    window->invoke_pattern_create(false);
                    if (!window->get_patterns() ||
                        window->get_patterns()->row_count() == 0)
                        throw std::runtime_error(
                            "generated reusable-pattern creation did not publish");
                    window->invoke_arrangement_toggle();
                    window->invoke_arrangement_manage("create-pattern");
                    if (!window->get_arrangement_items() ||
                        window->get_arrangement_items()->row_count() == 0)
                        throw std::runtime_error(
                            "generated arrangement creation did not publish");
                    window->invoke_arrangement_item_invoked(0);
                    window->invoke_arrangement_manage("move-right");
                    const auto moved = application.snapshot();
                    if (moved.clips.empty())
                        throw std::runtime_error(
                            "generated arrangement move lost its clip");
                    const auto moved_start = moved.clips.front().start;
                    if (!window->get_can_undo() ||
                        std::string_view(window->get_undo_label().data()).empty())
                        throw std::runtime_error(
                            "generated history availability did not publish");
                    window->invoke_history_action("undo");
                    const auto undone = application.snapshot();
                    if (undone.undo_depth >= moved.undo_depth ||
                        undone.redo_depth <= moved.redo_depth ||
                        undone.clips.empty() ||
                        undone.clips.front().start == moved_start)
                        throw std::runtime_error(
                            "generated Undo did not advance authoritative history: " +
                            std::to_string(moved.undo_depth) + "/" +
                            std::to_string(moved.redo_depth) + " -> " +
                            std::to_string(undone.undo_depth) + "/" +
                            std::to_string(undone.redo_depth) + " error=" +
                            std::string(window->get_notification().data()));
                    window->invoke_history_action("redo");
                    const auto redone = application.snapshot();
                    if (redone.undo_depth != moved.undo_depth ||
                        redone.redo_depth != moved.redo_depth ||
                        redone.clips.empty() ||
                        redone.clips.front().start != moved_start)
                        throw std::runtime_error(
                            "generated Redo did not restore authoritative history");
                    break;
                }
                case 2: window->invoke_sample_slot_selected(0); break;
                case 3:
                    window->invoke_sound_subview_selected("presets");
                    window->invoke_sound_preset_action("save", "", "");
                    if (!window->get_text_entry_visible())
                        throw std::runtime_error("Sound preset naming did not open keyboard");
                    window->invoke_text_entry_action("cancel");
                    break;
                case 4: window->invoke_mixer_bank_changed(0); break;
                case 5:
                    window->invoke_fx_subview_selected("modulation");
                    window->invoke_fx_modulator_action("create");
                    if (!window->get_fx_modulators() ||
                        window->get_fx_modulators()->row_count() == 0)
                        throw std::runtime_error(
                            "generated modulator creation did not publish");
                    window->invoke_fx_modulator_selected(0);
                    if (window->get_fx_modulator_parameters() &&
                        window->get_fx_modulator_parameters()->row_count() > 0) {
                        window->invoke_fx_modulator_parameter_selected(0);
                        window->invoke_fx_modulator_parameter_adjusted(1);
                    }
                    window->invoke_fx_subview_selected("macros");
                    window->invoke_fx_macro_action("create");
                    if (!window->get_text_entry_visible())
                        throw std::runtime_error(
                            "generated macro creation did not open keyboard");
                    window->invoke_text_entry_action("confirm");
                    window->invoke_fx_macro_scene_action("capture");
                    if (!window->get_text_entry_visible())
                        throw std::runtime_error(
                            "generated scene capture did not open keyboard");
                    window->invoke_text_entry_action("confirm");
                    break;
                case 6: window->invoke_browser_category_selected("instrument-presets"); break;
                case 7:
                    window->invoke_setup_subview_selected("midi");
                    window->invoke_setup_midi_route_action("add");
                    if (!window->get_setup_midi_routes() ||
                        window->get_setup_midi_routes()->row_count() == 0)
                        throw std::runtime_error("generated MIDI route callback did not publish");
                    window->invoke_setup_midi_route_selected(0);
                    window->invoke_setup_midi_route_action("delete");
                    if (window->get_setup_mapping_targets() &&
                        window->get_setup_mapping_targets()->row_count() > 0) {
                        window->invoke_setup_mapping_target_selected(0);
                        window->invoke_setup_mapping_target_apply();
                    }
                    break;
                }
                if (!options.screenshot_path.empty())
                    capture_headless(options.screenshot_path + "-" +
                                     std::string(page_ids[static_cast<std::size_t>(page)]) +
                                     ".ppm");
            }
            constexpr std::array<std::string_view, 8> secondary_ids{
                "xy", "song", "slice", "presets", "fx",
                "modulation-routing", "scenes", "setup"};
            window->invoke_reference_shift_changed(true);
            if (!window->get_effective_shift())
                throw std::runtime_error("generated SHIFT callback did not change label layer");
            for (int page = 0; page < 8; ++page) {
                window->invoke_page_selected(page);
                if (std::string_view(window->get_destination_id().data()) !=
                    secondary_ids[static_cast<std::size_t>(page)])
                    throw std::runtime_error("generated secondary destination mismatch");
            }
            window->invoke_reference_shift_changed(false);
            if (window->get_effective_shift() ||
                std::string_view(window->get_destination_id().data()) != "setup")
                throw std::runtime_error("SHIFT release redirected secondary destination");
            window->invoke_page_selected(0);
            window->invoke_performance_bank_action("create");
            if (!window->get_performance_banks() ||
                window->get_performance_banks()->row_count() == 0)
                throw std::runtime_error(
                    "generated Performance bank creation did not publish");
            window->invoke_performance_bank_selected(0);
            window->invoke_pad_assignment_clear(0);
            window->invoke_pad_assignment(0, 77);
            const auto assigned_pad = window->get_pads()
                ? window->get_pads()->row_data(0) : std::optional<PadItem>{};
            if (!assigned_pad || !assigned_pad->available)
                throw std::runtime_error(
                    "generated logical pad assignment did not publish");
            window->invoke_context_bank_changed(1);
            if (std::string_view(window->get_context_identity().data()) !=
                "performance:secondary")
                throw std::runtime_error(
                    "generated contextual bank callback did not publish");
            window->invoke_context_bank_changed(-1);
            window->invoke_transport_invoked("record");
            if (!window->get_recording_visible())
                throw std::runtime_error("recording modal did not become visible");
            window->invoke_recording_mode_selected("live-loop");
            window->invoke_recording_quantization_selected("immediate");
            window->invoke_recording_action("arm");
            if (!window->get_recording_loops() ||
                window->get_recording_loops()->row_count() == 0)
                throw std::runtime_error(
                    "generated live-loop arm did not publish a session");
            window->invoke_recording_loop_selected(0);
            if (!options.screenshot_path.empty())
                capture_headless(options.screenshot_path + "-recording.ppm");
            window->invoke_recording_action("cancel");
            if (std::string_view(window->get_recording_phase().data()) !=
                "Cancelled")
                throw std::runtime_error(
                    "generated live-loop cancellation did not round trip");
            window->invoke_recording_closed();
            window->invoke_text_entry_requested("browser-filter", "kick");
            window->invoke_text_entry_insert(" ");
            window->invoke_text_entry_insert("\xC3\x84");
            if (!window->get_text_entry_visible())
                throw std::runtime_error("touch keyboard did not become visible");
            if (!options.screenshot_path.empty())
                capture_headless(options.screenshot_path + "-keyboard.ppm");
            window->invoke_text_entry_action("left");
            window->invoke_text_entry_action("backspace");
            window->invoke_text_entry_action("cancel");
            if (window->get_text_entry_visible())
                throw std::runtime_error("touch keyboard cancellation did not close overlay");
            if (options.interaction_test_backend && headless_window)
                headless_window->render();
            const auto pixels = options.interaction_test_backend
                ? std::optional<slint::SharedPixelBuffer<slint::Rgba8Pixel>>{}
                : window->window().take_snapshot();
            const auto rendered_width = options.interaction_test_backend
                ? headless_window->size().width : pixels ? pixels->width() : 0U;
            const auto rendered_height = options.interaction_test_backend
                ? headless_window->size().height : pixels ? pixels->height() : 0U;
            if (rendered_width != static_cast<std::uint32_t>(options.width) ||
                rendered_height != static_cast<std::uint32_t>(options.height))
                throw std::runtime_error("renderer produced unexpected layout size");
            if (!window->get_test_shell_layout_valid())
                throw std::runtime_error(
                    "essential shell controls have invalid geometry");
            if (!options.screenshot_path.empty()) {
                std::ofstream output(options.screenshot_path,
                                     std::ios::binary | std::ios::trunc);
                if (!output)
                    throw std::runtime_error("could not create rendered test artifact");
                output << "P6\n" << rendered_width << ' ' << rendered_height
                       << "\n255\n";
                if (options.interaction_test_backend) {
                    for (const auto &pixel : headless_window->pixels()) {
                        output.put(static_cast<char>(pixel.r));
                        output.put(static_cast<char>(pixel.g));
                        output.put(static_cast<char>(pixel.b));
                    }
                } else {
                    for (const auto &pixel : *pixels) {
                        output.put(static_cast<char>(pixel.r));
                        output.put(static_cast<char>(pixel.g));
                        output.put(static_cast<char>(pixel.b));
                    }
                }
            }
            window->hide();
            adapter.shutdown();
            return 0;
        }
        slint::Timer refresh;
        refresh.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16),
                      [&] {
                          if (adapter.refresh())
                              publish(adapter.state(), window, false,
                                      adapter.refresh_diagnostics().last_kind);
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
