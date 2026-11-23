#include "neurogenic/ui/slint_adapter.hpp"
#include "neurogenic/audio/slicing.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace neurogenic {
namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 8> pages{{
    {"performance", "Performance"}, {"sequencer", "Sequencer"},
    {"sample", "Sample"},           {"sound", "Sound"},
    {"mixer", "Mixer"},             {"fx", "FX"},
    {"browser", "Browser"},         {"setup", "Setup"}}};

std::string quantization_name(LaunchQuantization value) {
    switch (value) {
    case LaunchQuantization::immediate: return "Immediate";
    case LaunchQuantization::quarter: return "Beat";
    case LaunchQuantization::bar: return "Bar";
    case LaunchQuantization::two_bars: return "2 Bars";
    case LaunchQuantization::four_bars: return "4 Bars";
    case LaunchQuantization::eight_bars: return "8 Bars";
    case LaunchQuantization::subdivision: return "Subdivision";
    case LaunchQuantization::pattern_end: return "Pattern End";
    }
    return "Unavailable";
}

double numeric_value(const ApplicationValue &value) {
    if (const auto *real = std::get_if<double>(&value)) return *real;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto *boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    return 0.0;
}

std::string format_value(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::abs(value) >= 100.0 ? 0 : 2)
           << value;
    return stream.str();
}
} // namespace

std::string format_slint_musical_position(MusicalTick tick,
                                          std::uint16_t numerator,
                                          std::uint16_t denominator) {
    denominator = std::max<std::uint16_t>(1, denominator);
    numerator = std::max<std::uint16_t>(1, numerator);
    const auto beat_ticks =
        std::max<MusicalTick>(1, ticks_per_quarter * 4 / denominator);
    const auto bar_ticks = beat_ticks * numerator;
    return std::to_string(tick / bar_ticks + 1) + "." +
           std::to_string((tick % bar_ticks) / beat_ticks + 1) + "." +
           std::to_string(tick % beat_ticks);
}

std::string slint_recording_label(RecordingState state, bool playing) {
    if (state == RecordingState::recording)
        return "Recording";
    if (state == RecordingState::armed)
        return "Armed";
    return playing ? "Playing" : "Stopped";
}

SlintFrontendAdapter::SlintFrontendAdapter(ApplicationService &application)
    : application_(application), bindings_(application) {
    (void)refresh();
}

bool SlintFrontendAdapter::refresh() {
    if (!active_)
        return false;
    const auto snapshot = application_.snapshot();
    if (snapshot.revision == state_.shell.revision &&
        local_revision_ == published_local_revision_)
        return false;
    SlintUiState next;
    next.shell.revision = snapshot.revision;
    next.shell.playing = snapshot.playing;
    next.shell.record_armed = snapshot.recording.state == RecordingState::armed;
    next.shell.recording = snapshot.recording.state == RecordingState::recording;
    next.shell.project_dirty = snapshot.project_dirty;
    next.shell.project_name = snapshot.project_name.empty() ? "Untitled"
                                                             : snapshot.project_name;
    next.shell.tempo_bpm = snapshot.tempo_bpm;
    next.shell.transport =
        slint_recording_label(snapshot.recording.state, snapshot.playing);
    next.shell.time_signature =
        std::to_string(snapshot.time_signature_numerator) + "/" +
        std::to_string(snapshot.time_signature_denominator);
    next.shell.position = format_slint_musical_position(
        snapshot.transport_tick, snapshot.time_signature_numerator,
        snapshot.time_signature_denominator);
    next.shell.page_id = state_.shell.page_id.empty() ? "performance"
                                                       : state_.shell.page_id;
    next.shell.page_title = state_.shell.page_title.empty()
                                ? "Performance"
                                : state_.shell.page_title;
    for (const auto &[id, title] : pages)
        next.shell.pages.push_back({std::string(id), std::string(title),
                                    id == "performance" || id == "sequencer" ||
                                    id == "sample" || id == "sound" ||
                                        id == "mixer" || id == "fx"});
    next.performance.launch_quantization =
        quantization_name(snapshot.launch_quantization);
    for (const auto &scene : snapshot.performance_launching.scenes) {
        const auto queued = std::any_of(
            snapshot.performance_launching.pending.begin(),
            snapshot.performance_launching.pending.end(),
            [&](const auto &launch) { return launch.scene == scene.id; });
        next.performance.scenes.push_back(
            {scene.name.empty() ? "Scene " + std::to_string(scene.id)
                                : scene.name,
             queued});
    }
    if (const auto track = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(), [&](const auto &t) {
                return t.id == snapshot.selected_track;
            });
        track != snapshot.tracks.end())
        next.performance.selected_track = next.sequencer.selected_track =
            track->name.empty() ? "Track " + std::to_string(track->id)
                                : track->name;
    if (snapshot.active_pattern != 0)
        next.performance.active_content =
            "Pattern " + std::to_string(snapshot.active_pattern);
    else if (snapshot.playback_mode == TimelinePlaybackMode::song &&
             !snapshot.audio_clips.empty())
        next.performance.active_content = "Arrangement";

    if (const auto owner = std::find_if(
            snapshot.performance_launching.ownership.begin(),
            snapshot.performance_launching.ownership.end(), [&](const auto &v) {
                return v.track == snapshot.selected_track;
            });
        owner != snapshot.performance_launching.ownership.end()) {
        switch (owner->owner) {
        case TrackPlaybackOwner::arrangement: next.performance.track_owner = "Arrangement"; break;
        case TrackPlaybackOwner::session: next.performance.track_owner = "Session"; break;
        case TrackPlaybackOwner::transitioning_to_session:
            next.performance.track_owner = "Queued Session"; break;
        case TrackPlaybackOwner::transitioning_to_arrangement:
            next.performance.track_owner = "Queued Arrangement"; break;
        }
    }

    const auto context_name = next.shell.page_id == "sample"
        ? std::string_view("samples")
        : next.shell.page_id == "sound" ? std::string_view("parameters")
        : next.shell.page_id == "mixer" ? std::string_view("mixer")
        : next.shell.page_id == "fx" ? std::string_view("parameters")
        : next.shell.page_id == "performance" ? std::string_view("performance")
                                                : std::string_view("sequencer");
    const auto context =
        resolve_controller_context(snapshot, context_name, snapshot.modifiers);
    for (std::size_t i = 0; i < 8; ++i) {
        const auto copy = [](const ContextControl &from) {
            return SlintContextItem{from.label.empty() ? "Unavailable"
                                                       : from.label,
                                    from.value.empty() ? "--" : from.value,
                                    from.unit,
                                    from.parameter_id,
                                    from.action_id,
                                    from.active};
        };
        next.context.encoders[i] = copy(context[i]);
        next.context.functions[i] = copy(context[8 + i]);
    }

    // The sixteen visual positions are filled by ascending stable slot ID.
    // Collection order is deliberately ignored and unfilled positions remain
    // unavailable, so project serialization order cannot change pad meaning.
    auto slots = snapshot.performance_launching.slots;
    std::sort(slots.begin(), slots.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (std::size_t i = 0; i < next.performance.pads.size(); ++i) {
        auto &pad = next.performance.pads[i];
        pad.label = std::to_string(i + 1);
        pad.selected = snapshot.selected_pad == i;
        if (i < slots.size()) {
            const auto &slot = slots[i];
            pad.slot_id = slot.id;
            pad.available = true;
            pad.label = "Slot " + std::to_string(slot.id);
            const auto pending = std::find_if(
                snapshot.performance_launching.pending.begin(),
                snapshot.performance_launching.pending.end(),
                [&](const auto &launch) { return launch.slot == slot.id; });
            pad.queued =
                pending != snapshot.performance_launching.pending.end();
            const auto ownership = std::find_if(
                snapshot.performance_launching.ownership.begin(),
                snapshot.performance_launching.ownership.end(),
                [&](const auto &owner) {
                    return owner.slot == slot.id &&
                           owner.owner == TrackPlaybackOwner::session;
                });
            pad.active =
                ownership != snapshot.performance_launching.ownership.end();
            pad.status = pad.queued ? "Queued" : pad.active ? "Playing" : "Stopped";
        }
    }

    next.sequencer.step_page = snapshot.step_page;
    next.sequencer.resolution = step_resolution_name(snapshot.step_resolution);
    next.sequencer.arrangement_view = state_.sequencer.arrangement_view;
    next.sequencer.selected_clip_id = state_.sequencer.selected_clip_id;
    const auto displayed_pattern = snapshot.selected_pattern
        ? snapshot.selected_pattern
        : snapshot.active_pattern
              ? snapshot.active_pattern
              : snapshot.patterns.empty() ? PatternId{} : snapshot.patterns.front().id;
    next.sequencer.selected_pattern_id = displayed_pattern;
    for (const auto &pattern : snapshot.patterns) {
        next.sequencer.patterns.push_back(
            {pattern.id, pattern.name.empty() ? "Pattern " + std::to_string(pattern.id)
                                               : pattern.name,
             pattern.id == displayed_pattern,
             pattern.id == snapshot.active_pattern,
             pattern.id == snapshot.queued_pattern});
        if (pattern.id == displayed_pattern) {
            next.sequencer.selected_pattern = next.sequencer.patterns.back().name;
            next.sequencer.pattern_length = std::to_string(pattern.length) + " ticks";
        }
    }
    const auto track = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                    [&](const auto &v) { return v.id == snapshot.selected_track; });
    const auto playhead = snapshot.step_ticks > 0
        ? (snapshot.transport_tick / snapshot.step_ticks) : MusicalTick{};
    for (std::size_t i = 0; i < next.sequencer.steps.size(); ++i) {
        const auto step = snapshot.step_page * 16 + i;
        auto &item = next.sequencer.steps[i];
        item.label = std::to_string(step + 1);
        item.selected = snapshot.selected_step == static_cast<std::int64_t>(step);
        item.playhead = playhead == static_cast<MusicalTick>(step);
        item.available = track != snapshot.tracks.end() && snapshot.step_ticks > 0 &&
                         static_cast<MusicalTick>(step) * snapshot.step_ticks <
                             track->pattern_length;
        item.active = item.available && std::any_of(
            track->events.begin(), track->events.end(), [&](const auto &event) {
                return event.type == MusicalEventType::note &&
                       event.tick / snapshot.step_ticks == static_cast<MusicalTick>(step);
            });
        item.status = item.active ? "Note" : "Empty";
    }
    if (snapshot.selected_event) {
        next.sequencer.selected_velocity =
            std::to_string(static_cast<int>(snapshot.selected_event->velocity * 127.0F));
        next.sequencer.selected_duration =
            std::to_string(snapshot.selected_event->duration) + " ticks";
    }
    for (const auto &clip : snapshot.clips)
        next.sequencer.arrangement.push_back(
            {clip.id, "Pattern " + std::to_string(clip.pattern),
             std::to_string(clip.start) + " + " + std::to_string(clip.duration),
             "Pattern", clip.id == next.sequencer.selected_clip_id, clip.enabled});
    for (const auto &clip : snapshot.audio_clips)
        next.sequencer.arrangement.push_back(
            {clip.clip.id, "Audio " + std::to_string(clip.clip.id),
             std::to_string(clip.clip.start) + " + " + std::to_string(clip.clip.duration),
             "Audio", clip.clip.id == next.sequencer.selected_clip_id,
             clip.clip.enabled});
    next.sequencer.loop_range = snapshot.arrangement_loop_enabled
        ? std::to_string(snapshot.arrangement_loop_start) + " - " +
              std::to_string(snapshot.arrangement_loop_end)
        : "Off";

    next.sample.slot = snapshot.selected_sample_slot.value;
    next.sample.zoom = state_.sample.zoom <= 0 ? 1.0F : state_.sample.zoom;
    next.sample.offset = state_.sample.offset;
    if (const auto slot = std::find_if(
            snapshot.sample_slots.begin(), snapshot.sample_slots.end(),
            [&](const auto &value) {
                return value.id == snapshot.selected_sample_slot;
            });
        slot != snapshot.sample_slots.end()) {
        next.sample.active_revision = slot->active_revision;
        next.sample.frames = slot->frames;
        next.sample.sample_rate = slot->source_sample_rate;
        next.sample.channels = slot->channels;
        next.sample.duration_seconds = slot->duration_seconds;
        next.sample.name = slot->source_path.empty()
            ? "Slot " + std::to_string(slot->id.value)
            : slot->source_path.filename().string();
        next.sample.error = slot->error;
        next.sample.ready = slot->state == SampleSlotLoadState::loaded;
        next.sample.loading = slot->state == SampleSlotLoadState::queued ||
                              slot->state == SampleSlotLoadState::preparing ||
                              slot->state == SampleSlotLoadState::prepared ||
                              slot->state == SampleSlotLoadState::pending_activation;
        next.sample.missing = slot->state == SampleSlotLoadState::error;
        switch (slot->state) {
        case SampleSlotLoadState::empty: next.sample.status = "Empty"; break;
        case SampleSlotLoadState::queued: next.sample.status = "Queued"; break;
        case SampleSlotLoadState::preparing: next.sample.status = "Loading"; break;
        case SampleSlotLoadState::prepared: next.sample.status = "Prepared"; break;
        case SampleSlotLoadState::pending_activation:
            next.sample.status = "Activating"; break;
        case SampleSlotLoadState::loaded: next.sample.status = "Ready"; break;
        case SampleSlotLoadState::error: next.sample.status = "Error"; break;
        }
        next.sample.metadata = slot->has_sample
            ? std::to_string(slot->source_sample_rate) + " Hz  " +
                  std::to_string(slot->channels) + " ch  " +
                  std::to_string(slot->frames) + " frames"
            : "No active sample";
    }
    next.sample.waveform.reserve(snapshot.waveform_peaks.size());
    for (const auto &peak : snapshot.waveform_peaks)
        next.sample.waveform.push_back(
            {peak.left_min, peak.left_max, peak.right_min, peak.right_max});
    for (const auto &slice : snapshot.slices) {
        const auto assigned = std::any_of(
            snapshot.slice_pad_assignments.begin(),
            snapshot.slice_pad_assignments.end(),
            [&](const auto &id) { return id && *id == slice.id; });
        next.sample.slices.push_back(
            {slice.id, slice.start_frame, slice.end_frame,
             slice.name.empty() ? "Slice " + std::to_string(slice.id)
                                : slice.name,
             slice.id == snapshot.selected_slice, assigned});
    }
    for (const auto &region : snapshot.regions) {
        next.sample.regions.push_back(
            {region.id, region.start_frame, region.end_frame,
             region.name.empty() ? "Region " + std::to_string(region.id)
                                 : region.name,
             std::to_string(region.fade_in_frames) + "/" +
                 std::to_string(region.fade_out_frames),
             region.loop_enabled
                 ? std::to_string(region.loop_start_frame) + ".." +
                       std::to_string(region.loop_end_frame)
                 : "Off",
             region.id == snapshot.selected_region});
        if (region.id == snapshot.selected_region) {
            next.sample.region_start = region.start_frame;
            next.sample.region_end = region.end_frame;
            next.sample.fade_in = region.fade_in_frames;
            next.sample.fade_out = region.fade_out_frames;
            next.sample.loop_enabled = region.loop_enabled;
            next.sample.loop_start = region.loop_start_frame;
            next.sample.loop_end = region.loop_end_frame;
        }
    }
    if (const auto analysis = std::find_if(
            snapshot.sample_analysis_sessions.begin(),
            snapshot.sample_analysis_sessions.end(), [&](const auto &value) {
                return value.id == snapshot.selected_analysis_session;
            });
        analysis != snapshot.sample_analysis_sessions.end()) {
        next.sample.analysis_status = sample_analysis_status_name(analysis->status);
        if (!analysis->error.empty())
            next.sample.error = analysis->error;
    }
    if (const auto processing = std::find_if(
            snapshot.sample_processing_jobs.begin(),
            snapshot.sample_processing_jobs.end(), [&](const auto &value) {
                return value.id == snapshot.selected_processing_job;
            });
        processing != snapshot.sample_processing_jobs.end()) {
        next.sample.processing_status =
            sample_processing_status_name(processing->status);
        if (!processing->error.empty())
            next.sample.error = processing->error;
    }

    next.sound.subview = state_.sound.subview.empty() ? "instrument"
                                                       : state_.sound.subview;
    next.sound.preset_library = state_.sound.preset_library;
    next.sound.preset_id = state_.sound.preset_id;
    next.sound.selected_parameter = state_.sound.selected_parameter;
    next.sound.selected_modulation = state_.sound.selected_modulation;
    if (const auto track = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(), [&](const auto &value) {
                return value.id == snapshot.selected_track;
            });
        track != snapshot.tracks.end()) {
        next.sound.track_id = track->id;
        next.sound.track = track->name.empty()
            ? "Track " + std::to_string(track->id) : track->name;
        next.sound.instrument_id = track->instrument_id;
        next.sound.instrument_instance = track->instrument_instance_id;
        next.sound.synth = track->instrument_id == subtractive_instrument_id;
        next.sound.sample = track->instrument_id == sample_instrument_id;
        next.sound.instrument_available = next.sound.synth || next.sound.sample;
        next.sound.instrument = next.sound.synth ? "Subtractive Synth"
            : next.sound.sample ? "Sample Instrument" : "Unavailable";
    }
    next.sound.groups = next.sound.synth
        ? std::vector<std::string>{"Oscillator", "Filter", "Envelope", "Output"}
        : std::vector<std::string>{"Playback", "Envelope", "Voice"};
    const auto prefix = next.sound.synth ? std::string_view("synth.")
                                         : std::string_view("sample.");
    for (const auto &parameter : snapshot.parameters) {
        if (parameter.id.rfind(prefix, 0) != 0) continue;
        const auto minimum = numeric_value(parameter.minimum);
        const auto maximum = numeric_value(parameter.maximum);
        const auto current = numeric_value(parameter.current_value);
        std::string group = "Output";
        if (parameter.id.find("attack") != std::string::npos ||
            parameter.id.find("decay") != std::string::npos ||
            parameter.id.find("sustain") != std::string::npos ||
            parameter.id.find("release") != std::string::npos)
            group = "Envelope";
        else if (parameter.id.find("cutoff") != std::string::npos ||
                 parameter.id.find("resonance") != std::string::npos)
            group = "Filter";
        else if (parameter.id.find("waveform") != std::string::npos ||
                 parameter.id.find("pitch") != std::string::npos ||
                 parameter.id.find("pulse") != std::string::npos)
            group = next.sound.synth ? "Oscillator" : "Playback";
        next.sound.parameters.push_back(
            {parameter.id, parameter.name, format_value(current), parameter.unit,
             group, minimum, maximum,
             maximum > minimum ? (current - minimum) / (maximum - minimum) : 0.0,
             true, parameter.automatable, parameter.modulatable});
    }
    next.sound.sample_settings = snapshot.selected_pad_settings;
    for (std::size_t index = 0; index < snapshot.modulation.route_count; ++index) {
        const auto &route = snapshot.modulation.routes[index];
        const auto source = std::find_if(
            snapshot.modulation.modulators.begin(),
            snapshot.modulation.modulators.begin() +
                snapshot.modulation.modulator_count,
            [&](const auto &value) { return value.id == route.source_instance; });
        const auto local = route.track_id == snapshot.selected_track ||
            (route.destination_owner_explicit &&
             route.destination_owner_id == snapshot.selected_track) ||
            (source != snapshot.modulation.modulators.begin() +
                           snapshot.modulation.modulator_count &&
             source->owner_kind == ModulatorOwnerKind::track &&
             source->owner_id == snapshot.selected_track);
        if (!local) continue;
        next.sound.modulation.push_back(
            {route.id, route.source_instance,
             route.source_instance ? "Modulator " + std::to_string(route.source_instance)
                                   : "Source " + std::to_string(static_cast<unsigned>(route.source)),
             "Parameter " + std::to_string(route.destination.parameter),
             format_value(route.depth), route.enabled});
    }
    next.sound.preset_status = snapshot.preset_application.status;
    next.sound.error = snapshot.preset_application.error;
    if (next.sound.selected_parameter >= next.sound.parameters.size())
        next.sound.selected_parameter = 0;
    if (next.sound.selected_modulation >= next.sound.modulation.size())
        next.sound.selected_modulation = 0;

    const auto route_name = [](MixerBusId group, std::int8_t output) {
        if (group) return "Group " + std::to_string(group);
        if (output >= 0)
            return "Output " + std::to_string(output + 1) + "/" +
                   std::to_string(output + 2);
        return std::string("Master");
    };
    next.mixer.bank = state_.mixer.bank;
    next.mixer.selected_channel = state_.mixer.selected_channel;
    const auto &configuration = snapshot.mixer.configuration;
    for (std::size_t i = 0; i < configuration.track_count; ++i) {
        const auto &channel = configuration.tracks[i];
        const auto summary = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(),
            [&](const auto &track) { return track.id == channel.track; });
        next.mixer.channels.push_back(
            {channel.track,
             summary != snapshot.tracks.end() && !summary->name.empty()
                 ? summary->name : "Track " + std::to_string(channel.track),
             "Track", route_name(channel.destination_group, channel.output_pair),
             channel.gain, channel.pan, snapshot.mixer.tracks[i].left,
             snapshot.mixer.tracks[i].right, channel.aux_send, channel.mute,
             channel.solo, snapshot.mixer.tracks[i].clip,
             false, true});
    }
    for (std::size_t i = 0; i < configuration.group_count; ++i) {
        const auto &group = configuration.groups[i];
        const auto name = std::string(group.name.data());
        next.mixer.channels.push_back(
            {group.id, name.empty() ? "Group " + std::to_string(group.id) : name,
             "Group", route_name(group.destination_group, group.output_pair),
             group.gain, group.pan, snapshot.mixer.groups[i].left,
             snapshot.mixer.groups[i].right, group.aux_send, group.mute,
             group.solo, snapshot.mixer.groups[i].clip,
             false, true});
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i) {
        const auto &aux = configuration.aux[i];
        next.mixer.channels.push_back(
            {i, "Aux " + std::to_string(i + 1), "Aux Return",
             route_name(0, aux.output_pair), aux.return_gain, 0.0F,
             snapshot.mixer.aux[i].left, snapshot.mixer.aux[i].right, {},
             aux.mute, false, snapshot.mixer.aux[i].clip, false, true});
    }
    next.mixer.channels.push_back(
        {0, "Master", "Master",
         "Output " + std::to_string(configuration.master.output_left + 1) +
             "/" + std::to_string(configuration.master.output_right + 1),
         configuration.master.gain, 0.0F, snapshot.mixer.master.left,
         snapshot.mixer.master.right, {}, configuration.master.mute, false,
         snapshot.mixer.master.clip, false, true});
    if (next.mixer.channels.empty())
        next.mixer.error = "No mixer channels are available";
    if (next.mixer.selected_channel >= next.mixer.channels.size())
        next.mixer.selected_channel = 0;
    if (!next.mixer.channels.empty()) {
        next.mixer.channels[next.mixer.selected_channel].selected = true;
        next.mixer.selected_id = next.mixer.channels[next.mixer.selected_channel].id;
    }
    constexpr std::size_t mixer_bank_size = 8;
    const auto maximum_bank = next.mixer.channels.empty()
        ? std::size_t{} : (next.mixer.channels.size() - 1) / mixer_bank_size;
    next.mixer.bank = std::min(next.mixer.bank, maximum_bank);
    next.mixer.bank_label = "Channels " +
        std::to_string(next.mixer.bank * mixer_bank_size + 1) + "-" +
        std::to_string(std::min(next.mixer.channels.size(),
                                (next.mixer.bank + 1) * mixer_bank_size));
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        const auto &loop = configuration.external_fx[i];
        const auto name = std::string(loop.name.data());
        next.mixer.external_fx.push_back(
            {i, loop.latency_frames,
             name.empty() ? "External FX " + std::to_string(i + 1) : name,
             loop.enabled ? loop.bypass ? "Bypassed" : "Active" : "Disabled",
             "Send " + std::to_string(loop.send_left + 1) + "/" +
                 std::to_string(loop.send_right + 1) + " Return " +
                 std::to_string(loop.return_left + 1) + "/" +
                 std::to_string(loop.return_right + 1),
             loop.send_gain, loop.return_gain, loop.wet,
             snapshot.mixer.external_returns[i].left,
             snapshot.mixer.external_returns[i].right, loop.enabled,
             loop.bypass, loop.return_mute,
             snapshot.mixer.external_send_available[i],
             snapshot.mixer.external_return_available[i]});
    }

    next.fx.subview = state_.fx.subview.empty() ? "chain" : state_.fx.subview;
    next.fx.selected_owner = state_.fx.selected_owner;
    next.fx.selected_effect = state_.fx.selected_effect;
    next.fx.selected_parameter = state_.fx.selected_parameter;
    next.fx.selected_modulator = state_.fx.selected_modulator;
    next.fx.selected_route = state_.fx.selected_route;
    next.fx.selected_macro = state_.fx.selected_macro;
    for (std::size_t i = 0; i < configuration.track_count; ++i) {
        const auto id = configuration.tracks[i].track;
        const auto summary = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
            [&](const auto &track) { return track.id == id; });
        next.fx.owners.push_back({"track",
            summary != snapshot.tracks.end() && !summary->name.empty()
                ? summary->name : "Track " + std::to_string(id),
            static_cast<std::int64_t>(i), false});
    }
    for (std::size_t i = 0; i < configuration.group_count; ++i) {
        const auto &group = configuration.groups[i];
        const auto name = std::string(group.name.data());
        next.fx.owners.push_back({"group", name.empty()
            ? "Group " + std::to_string(group.id) : name,
            static_cast<std::int64_t>(group.id), false});
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i)
        next.fx.owners.push_back({"aux", "Aux " + std::to_string(i + 1),
                                  static_cast<std::int64_t>(i), false});
    next.fx.owners.push_back({"master", "Master", 0, false});
    if (next.fx.selected_owner >= next.fx.owners.size()) next.fx.selected_owner = 0;
    const EffectChainConfiguration *fx_chain = nullptr;
    if (!next.fx.owners.empty()) {
        auto &owner = next.fx.owners[next.fx.selected_owner];
        owner.selected = true;
        next.fx.owner_kind = owner.kind;
        next.fx.owner_name = owner.name;
        next.fx.owner_index = owner.index;
        if (owner.kind == "track") fx_chain = &configuration.effects.tracks[owner.index];
        else if (owner.kind == "aux") fx_chain = &configuration.effects.aux[owner.index];
        else if (owner.kind == "master") fx_chain = &configuration.effects.master;
        else if (owner.kind == "group") {
            const auto group = std::find_if(configuration.groups.begin(),
                configuration.groups.begin() + configuration.group_count,
                [&](const auto &value) { return value.id == owner.index; });
            if (group != configuration.groups.begin() + configuration.group_count)
                fx_chain = &configuration.effects.groups[group - configuration.groups.begin()];
        }
    }
    const auto type_name = [](EffectProcessorType type) {
        switch (type) {
        case EffectProcessorType::gain: return std::string("Gain / Utility");
        case EffectProcessorType::stereo_delay: return std::string("Stereo Delay");
        case EffectProcessorType::low_pass_filter: return std::string("Low-pass Filter");
        case EffectProcessorType::saturation: return std::string("Saturation");
        }
        return std::string("Unknown");
    };
    if (fx_chain) {
        next.fx.chain_bypass = fx_chain->bypass;
        for (std::size_t position = 0; position < fx_chain->count; ++position) {
            const auto id = fx_chain->instances[position];
            const auto instance = std::find_if(configuration.effects.instances.begin(),
                configuration.effects.instances.begin() + configuration.effects.instance_count,
                [&](const auto &value) { return value.id == id; });
            if (instance == configuration.effects.instances.begin() +
                                configuration.effects.instance_count) continue;
            next.fx.effects.push_back({instance->id, type_name(instance->type),
                std::string(effect_processor_type_id(instance->type)),
                instance->bypass, false});
        }
    }
    if (next.fx.selected_effect >= next.fx.effects.size()) next.fx.selected_effect = 0;
    if (!next.fx.effects.empty()) {
        next.fx.effects[next.fx.selected_effect].selected = true;
        const auto id = next.fx.effects[next.fx.selected_effect].id;
        const auto instance = std::find_if(configuration.effects.instances.begin(),
            configuration.effects.instances.begin() + configuration.effects.instance_count,
            [&](const auto &value) { return value.id == id; });
        if (instance != configuration.effects.instances.begin() + configuration.effects.instance_count) {
            for (const auto &descriptor : effect_parameter_descriptors(instance->type)) {
                const auto value = std::find_if(instance->parameters.begin(),
                    instance->parameters.begin() + instance->parameter_count,
                    [&](const auto &item) { return item.id == descriptor.id; });
                const auto current = value == instance->parameters.begin() + instance->parameter_count
                    ? descriptor.default_value : value->value;
                next.fx.parameters.push_back({descriptor.id,
                    std::string(descriptor.stable_id), std::string(descriptor.name),
                    format_value(current), std::string(descriptor.unit),
                    descriptor.minimum, descriptor.maximum,
                    descriptor.maximum > descriptor.minimum
                        ? (current - descriptor.minimum) /
                              (descriptor.maximum - descriptor.minimum) : 0.0,
                    descriptor.automatable, descriptor.modulatable, false});
            }
        }
    }
    if (next.fx.selected_parameter >= next.fx.parameters.size()) next.fx.selected_parameter = 0;
    if (!next.fx.parameters.empty()) next.fx.parameters[next.fx.selected_parameter].selected = true;
    for (std::size_t i = 0; i < snapshot.modulation.modulator_count; ++i) {
        const auto &modulator = snapshot.modulation.modulators[i];
        const auto *descriptor = find_modulator_type(modulator.type);
        const auto custom = std::string(modulator.display_name.data());
        next.fx.modulators.push_back({modulator.id,
            custom.empty() ? "Modulator " + std::to_string(modulator.id) : custom,
            descriptor ? std::string(descriptor->name) : "Unknown",
            std::to_string(static_cast<unsigned>(modulator.owner_kind)) + ":" +
                std::to_string(modulator.owner_id), modulator.enabled, false});
    }
    if (next.fx.selected_modulator >= next.fx.modulators.size()) next.fx.selected_modulator = 0;
    if (!next.fx.modulators.empty()) next.fx.modulators[next.fx.selected_modulator].selected = true;
    for (std::size_t i = 0; i < snapshot.modulation.route_count; ++i) {
        const auto &route = snapshot.modulation.routes[i];
        next.fx.routes.push_back({route.id, route.source_instance,
            route.source_instance ? "Modulator " + std::to_string(route.source_instance)
                                  : "Source " + std::to_string(static_cast<unsigned>(route.source)),
            "Parameter " + std::to_string(route.destination.parameter),
            format_value(route.depth), route.enabled, false, true});
    }
    if (next.fx.selected_route >= next.fx.routes.size()) next.fx.selected_route = 0;
    if (!next.fx.routes.empty()) next.fx.routes[next.fx.selected_route].selected = true;
    for (const auto &macro : snapshot.performance_macros.macros) {
        const auto effective = std::find_if(snapshot.performance_macros.effective_values.begin(),
            snapshot.performance_macros.effective_values.end(),
            [&](const auto &value) { return value.first == macro.id; });
        next.fx.macros.push_back({macro.id, macro.name.empty()
            ? "Macro " + std::to_string(macro.id) : macro.name,
            std::to_string(macro.assignments.size()) + " assignments",
            effective == snapshot.performance_macros.effective_values.end()
                ? macro.value : effective->second, macro.enabled, false});
    }
    if (next.fx.selected_macro >= next.fx.macros.size()) next.fx.selected_macro = 0;
    if (!next.fx.macros.empty()) next.fx.macros[next.fx.selected_macro].selected = true;
    next.fx.error = last_error_;
    next.mixer.error = last_error_;
    next.shell.notification = last_error_;
    state_ = std::move(next);
    published_local_revision_ = local_revision_;
    return true;
}

bool SlintFrontendAdapter::execute(ApplicationCommand command) {
    if (!active_) {
        last_error_ = "Slint frontend is shutting down";
        return false;
    }
    command.source_id = "slint-development-ui";
    const auto result = application_.execute(command);
    last_error_ = result.accepted ? std::string{} : result.error;
    (void)refresh();
    state_.shell.notification = last_error_;
    return result.accepted;
}

bool SlintFrontendAdapter::invoke_action(std::string action_id) {
    return execute({std::move(action_id), {}, {}, 0});
}

bool SlintFrontendAdapter::turn_encoder(std::size_t index, double steps,
                                        bool fine) {
    if (index >= state_.context.encoders.size() ||
        !state_.context.encoders[index].available ||
        state_.context.encoders[index].parameter_id.empty()) {
        last_error_ = "Encoder assignment is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto discovery = discover_application(application_);
    const auto descriptor = std::find_if(
        discovery.parameters.begin(), discovery.parameters.end(),
        [&](const auto &parameter) {
            return parameter.id == state_.context.encoders[index].parameter_id;
        });
    if (descriptor == discovery.parameters.end()) {
        last_error_ = "Encoder parameter is no longer discoverable";
        state_.shell.notification = last_error_;
        return false;
    }
    BindingReference reference{BindingTargetKind::explicit_object,
                               descriptor->parent,
                               state_.context.encoders[index].parameter_id, {},
                               0};
    const auto error = bindings_.apply(
        reference,
        {BindingOperation::relative_steps, steps * (fine ? 0.1 : 1.0)});
    last_error_ = error ? error.message : std::string{};
    (void)refresh();
    state_.shell.notification = last_error_;
    return !error;
}

bool SlintFrontendAdapter::invoke_function(std::size_t index) {
    if (index >= state_.context.functions.size() ||
        !state_.context.functions[index].available ||
        state_.context.functions[index].action_id.empty()) {
        last_error_ = "Function assignment is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return invoke_action(state_.context.functions[index].action_id);
}

bool SlintFrontendAdapter::press_pad(std::size_t index, bool pressed) {
    if (index >= state_.performance.pads.size()) {
        last_error_ = "Pad index is out of range";
        return false;
    }
    if (!pressed)
        return execute({"pad.release.v1",
                        {{"pad", static_cast<std::int64_t>(index)}}, {}, 0});
    if (state_.shell.page_id == "performance" &&
        state_.performance.pads[index].available)
        return execute({"performance.slot-launch.v1",
                        {{"slot", static_cast<std::int64_t>(
                                      state_.performance.pads[index].slot_id)}}, {}, 0});
    PerformanceGrid grid;
    const auto snapshot = application_.snapshot();
    grid.set_mode(snapshot.pad_mode);
    grid.set_step_page(snapshot.step_page);
    grid.set_selected_pad(snapshot.selected_pad);
    grid.set_selected_slot(snapshot.selected_sample_slot);
    const auto command = grid_application_command(
        grid.press(index, snapshot.modifiers.select), "slint-development-ui");
    if (!command) {
        last_error_ = "Pad action is unavailable";
        return false;
    }
    return execute(*command);
}

bool SlintFrontendAdapter::select_page(std::size_t index) {
    if (index >= pages.size()) {
        last_error_ = "Page is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.shell.page_id = std::string(pages[index].first);
    state_.shell.page_title = std::string(pages[index].second);
    state_.shell.notification = index < 5
        ? std::string{}
        : state_.shell.page_title + " is not implemented yet";
    last_error_ = state_.shell.notification;
    ++local_revision_;
    return true;
}

bool SlintFrontendAdapter::select_track(int direction) {
    return invoke_action(direction < 0 ? "track.previous.v1" : "track.next.v1");
}

bool SlintFrontendAdapter::launch_scene(std::size_t index) {
    const auto snapshot = application_.snapshot();
    if (index >= snapshot.performance_launching.scenes.size()) {
        last_error_ = "Performance scene is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"performance.scene-launch.v1",
                    {{"scene", static_cast<std::int64_t>(
                                   snapshot.performance_launching.scenes[index].id)}},
                    {}, 0});
}

bool SlintFrontendAdapter::return_to_arrangement() {
    return execute({"performance.return-arrangement.v1",
                    {{"track", static_cast<std::int64_t>(
                                   application_.snapshot().selected_track)}},
                    {}, 0});
}

bool SlintFrontendAdapter::set_launch_quantization(std::string value) {
    return execute({"pattern.launch-quantization.v1",
                    {{"quantization", std::move(value)}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_launch_quantization() {
    const auto current = application_.snapshot().launch_quantization;
    switch (current) {
    case LaunchQuantization::immediate:
        return set_launch_quantization("quarter");
    case LaunchQuantization::quarter:
        return set_launch_quantization("bar");
    case LaunchQuantization::bar:
        return set_launch_quantization("two-bars");
    default: return set_launch_quantization("immediate");
    }
}

bool SlintFrontendAdapter::transport_record() {
    last_error_ = "Transport record has no single semantic action";
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::select_pattern(std::size_t index, bool launch) {
    if (index >= state_.sequencer.patterns.size()) {
        last_error_ = "Pattern is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto id = static_cast<std::int64_t>(state_.sequencer.patterns[index].id);
    if (!execute({"pattern.select.v1", {{"pattern_id", id}}, {}, 0}))
        return false;
    return !launch || execute({"pattern.launch.v1", {{"id", id}, {"queued", true}}, {}, 0});
}

bool SlintFrontendAdapter::create_pattern(bool duplicate) {
    const auto snapshot = application_.snapshot();
    const auto name = duplicate ? "Pattern Copy" : "New Pattern";
    if (duplicate) {
        if (!snapshot.selected_pattern) {
            last_error_ = "No pattern is selected";
            state_.shell.notification = last_error_;
            return false;
        }
        return execute({"pattern.duplicate.v1",
                        {{"source", static_cast<std::int64_t>(snapshot.selected_pattern)},
                         {"name", std::string(name)}}, {}, 0});
    }
    const auto length = snapshot.step_ticks > 0 ? snapshot.step_ticks * 16
                                                 : ticks_per_quarter * 4;
    return execute({"pattern.create.v1",
                    {{"name", std::string(name)},
                     {"length", static_cast<std::int64_t>(length)}}, {}, 0});
}

bool SlintFrontendAdapter::select_step(std::size_t index) {
    if (index >= state_.sequencer.steps.size() ||
        !state_.sequencer.steps[index].available) {
        last_error_ = "Step is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto step = static_cast<std::int64_t>(
        state_.sequencer.step_page * 16 + index);
    if (!execute({"step.toggle.v1", {{"step", step}}, {}, 0}))
        return false;
    // A newly inserted note becomes the editable event. Removal intentionally
    // leaves no selected event rather than targeting a stale identity.
    if (state_.sequencer.steps[index].active)
        return execute({"step.event-select.v1", {{"step", step}}, {}, 0});
    return true;
}

bool SlintFrontendAdapter::set_step_velocity(double value) {
    return execute({"step.velocity-set.v1", {{"velocity", value}}, {}, 0});
}

bool SlintFrontendAdapter::set_step_duration(std::int64_t ticks) {
    return execute({"step.duration-set.v1", {{"duration_ticks", ticks}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_step_resolution() {
    const auto current = application_.snapshot().step_resolution;
    const auto next = static_cast<StepResolution>((static_cast<unsigned>(current) + 1) % 8);
    return execute({"step.resolution-set.v1",
                    {{"resolution", std::string(step_resolution_name(next))}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_arrangement_view() {
    state_.sequencer.arrangement_view = !state_.sequencer.arrangement_view;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_arrangement_clip(std::size_t index) {
    if (index >= state_.sequencer.arrangement.size()) {
        last_error_ = "Arrangement clip is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.sequencer.selected_clip_id = state_.sequencer.arrangement[index].id;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_sample_slot(int direction) {
    const auto snapshot = application_.snapshot();
    if (snapshot.sample_slots.empty()) {
        last_error_ = "No sample slots are available";
        state_.shell.notification = last_error_;
        return false;
    }
    auto found = std::find_if(snapshot.sample_slots.begin(),
                              snapshot.sample_slots.end(), [&](const auto &slot) {
                                  return slot.id == snapshot.selected_sample_slot;
                              });
    auto index = found == snapshot.sample_slots.end()
        ? std::size_t{}
        : static_cast<std::size_t>(found - snapshot.sample_slots.begin());
    index = direction < 0
        ? (index + snapshot.sample_slots.size() - 1) % snapshot.sample_slots.size()
        : (index + 1) % snapshot.sample_slots.size();
    return execute({"sample.slot-select.v1",
                    {{"slot_id", static_cast<std::int64_t>(
                                     snapshot.sample_slots[index].id.value)}}, {}, 0});
}

bool SlintFrontendAdapter::load_sample(std::string path) {
    if (path.empty()) {
        last_error_ = "A WAV path is required";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({state_.sample.ready ? "sample.replace.v1" : "sample.load.v1",
                    {{"slot_id", static_cast<std::int64_t>(state_.sample.slot)},
                     {"path", std::move(path)}}, {}, 0});
}

bool SlintFrontendAdapter::select_slice(std::size_t index) {
    if (index >= state_.sample.slices.size()) {
        last_error_ = "Slice is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"slice.select.v1",
                    {{"slice_id", static_cast<std::int64_t>(
                                      state_.sample.slices[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::create_slice(std::uint64_t frame) {
    if (!state_.sample.ready || frame >= state_.sample.frames) {
        last_error_ = "Slice position is outside the active sample";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto length = std::max<std::uint64_t>(
        1, state_.sample.sample_rate ? state_.sample.sample_rate / 4 : 1);
    const auto end = std::min(state_.sample.frames, frame + length);
    return execute({"slice.create.v1",
                    {{"start_frame", static_cast<std::int64_t>(frame)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::move_selected_slice(std::uint64_t start,
                                                std::uint64_t end) {
    return execute({"slice.bounds-set.v1",
                    {{"start_frame", static_cast<std::int64_t>(start)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::delete_selected_slice() {
    return invoke_action("slice.delete.v1");
}

bool SlintFrontendAdapter::assign_selected_slice(std::size_t pad) {
    return execute({"slice.pad-assign.v1",
                    {{"pad", static_cast<std::int64_t>(pad)}}, {}, 0});
}

bool SlintFrontendAdapter::preview_sample(std::string_view target) {
    if (target == "sample")
        return invoke_action("sample.preview.v1");
    if (target == "slice")
        return invoke_action("slice.preview.v1");
    if (target == "region")
        return invoke_action("region.preview.v1");
    last_error_ = "Preview target is unavailable";
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::stop_preview() {
    return invoke_action("slice.preview-stop.v1");
}

bool SlintFrontendAdapter::analyze_transients() {
    return execute({"sample-analysis.start.v1",
                    {{"algorithm", std::string("transients")},
                     {"sensitivity", 0.5},
                     {"minimum_frames", static_cast<std::int64_t>(
                                            std::max<std::uint32_t>(1, state_.sample.sample_rate / 20))}},
                    {}, 0});
}

bool SlintFrontendAdapter::apply_analysis(bool accept) {
    if (!accept)
        return invoke_action("sample-analysis.discard.v1");
    return execute({"sample-analysis.apply.v1",
                    {{"target", std::string("slices")},
                     {"mode", std::string("replace")},
                     {"assign_pads", false}}, {}, 0});
}

bool SlintFrontendAdapter::select_region(std::size_t index) {
    if (index >= state_.sample.regions.size()) {
        last_error_ = "Region is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"region.select.v1",
                    {{"region_id", static_cast<std::int64_t>(
                                       state_.sample.regions[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::set_region_bounds(std::uint64_t start,
                                              std::uint64_t end) {
    return execute({"region.bounds-set.v1",
                    {{"start_frame", static_cast<std::int64_t>(start)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_region_loop() {
    return execute({"region.loop-set.v1",
                    {{"enabled", !state_.sample.loop_enabled},
                     {"start_frame", static_cast<std::int64_t>(state_.sample.region_start)},
                     {"end_frame", static_cast<std::int64_t>(state_.sample.region_end)},
                     {"crossfade_frames", std::int64_t{0}}}, {}, 0});
}

bool SlintFrontendAdapter::set_region_fades(std::uint64_t fade_in,
                                             std::uint64_t fade_out) {
    return execute({"region.fades-set.v1",
                    {{"fade_in_frames", static_cast<std::int64_t>(fade_in)},
                     {"fade_out_frames", static_cast<std::int64_t>(fade_out)},
                     {"curve", std::string("linear")}}, {}, 0});
}

bool SlintFrontendAdapter::start_processing(std::string operation) {
    if (operation != "normalize-peak" && operation != "reverse" &&
        operation != "trim" && operation != "fade") {
        last_error_ = "Processing operation is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto source = state_.sample.region_end > state_.sample.region_start
        ? std::string("region") : std::string("sample");
    std::map<std::string, ApplicationValue> create{{"source", source}};
    if (source == "region")
        create.emplace("source_id", static_cast<std::int64_t>(
            application_.snapshot().selected_region));
    if (!execute({"sample-processing.create.v1", std::move(create), {}, 0}))
        return false;
    std::map<std::string, ApplicationValue> arguments{{"operation", operation}};
    if (operation == "trim") {
        arguments.emplace("start_frame", static_cast<std::int64_t>(state_.sample.region_start));
        arguments.emplace("end_frame", static_cast<std::int64_t>(state_.sample.region_end));
    } else if (operation == "fade") {
        arguments.emplace("start_frame", static_cast<std::int64_t>(state_.sample.fade_in));
        arguments.emplace("end_frame", static_cast<std::int64_t>(state_.sample.fade_out));
    }
    return execute({"sample-processing.operation-add.v1", std::move(arguments), {}, 0}) &&
           invoke_action("sample-processing.start.v1");
}

bool SlintFrontendAdapter::set_waveform_view(float zoom, float offset) {
    if (!std::isfinite(zoom) || !std::isfinite(offset))
        return false;
    state_.sample.zoom = std::clamp(zoom, 1.0F, 64.0F);
    state_.sample.offset = std::clamp(offset, 0.0F, 1.0F);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_sound_subview(std::string subview) {
    if (subview != "instrument" && subview != "modulation" &&
        subview != "presets") {
        last_error_ = "Sound subview is unavailable";
        return false;
    }
    state_.sound.subview = std::move(subview);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_track_instrument(std::string instrument_id) {
    if (instrument_id != sample_instrument_id &&
        instrument_id != subtractive_instrument_id) {
        last_error_ = "Instrument type is unsupported";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"track.instrument-set.v1",
                    {{"instrument_id", std::move(instrument_id)}}, {}, 0});
}

bool SlintFrontendAdapter::select_sound_parameter(std::size_t index) {
    if (index >= state_.sound.parameters.size()) {
        last_error_ = "Sound parameter is unavailable";
        return false;
    }
    state_.sound.selected_parameter = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_sound_parameter(std::size_t index,
                                                   double steps) {
    if (index >= state_.sound.parameters.size() || !std::isfinite(steps)) {
        last_error_ = "Sound parameter is unavailable";
        return false;
    }
    const auto &parameter = state_.sound.parameters[index];
    const auto descriptor = application_.parameters().get(parameter.id);
    if (!descriptor) {
        last_error_ = "Sound parameter is stale";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto value = std::clamp(numeric_value(descriptor->current_value) +
                                      descriptor->step * steps,
                                  parameter.minimum, parameter.maximum);
    if (state_.sound.synth)
        return execute({"instrument.instance-parameter-set.v1",
                        {{"instance_id", static_cast<std::int64_t>(
                                             state_.sound.instrument_instance)},
                         {"parameter_id", parameter.id}, {"value", value}}, {}, 0});
    return execute({"parameter.set-normalized.v1",
                    {{"parameter_id", parameter.id},
                     {"value", parameter.maximum > parameter.minimum
                                   ? (value - parameter.minimum) /
                                         (parameter.maximum - parameter.minimum)
                                   : 0.0}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_sample_playback_mode() {
    auto mode = std::string("one-shot");
    switch (state_.sound.sample_settings.mode) {
    case SamplePlaybackMode::one_shot: mode = "gate"; break;
    case SamplePlaybackMode::gate: mode = "toggle"; break;
    case SamplePlaybackMode::toggle: mode = "one-shot"; break;
    }
    return execute({"pad.playback-mode-set.v1", {{"mode", mode}}, {}, 0});
}

bool SlintFrontendAdapter::adjust_sample_instrument(std::string property,
                                                     double steps) {
    const auto settings = state_.sound.sample_settings;
    if (property == "pitch")
        return execute({"pad.pitch-set.v1",
                        {{"semitones", std::clamp<double>(
                            settings.semitones + steps, -48.0, 48.0)}}, {}, 0});
    if (property == "fine")
        return execute({"pad.fine-set.v1",
                        {{"cents", std::clamp<double>(
                            settings.fine_cents + steps, -100.0, 100.0)}}, {}, 0});
    if (property == "gain")
        return execute({"pad.gain-set.v1",
                        {{"gain", std::clamp<double>(
                            settings.gain + steps * 0.05, 0.0, 4.0)}}, {}, 0});
    const auto delta = steps * 0.01;
    auto attack = settings.attack_seconds;
    auto decay = settings.decay_seconds;
    auto sustain = settings.sustain;
    auto release = settings.release_seconds;
    if (property == "attack") attack = std::clamp<float>(attack + delta, 0, 60);
    else if (property == "decay") decay = std::clamp<float>(decay + delta, 0, 60);
    else if (property == "sustain") sustain = std::clamp<float>(sustain + delta, 0, 1);
    else if (property == "release") release = std::clamp<float>(release + delta, 0, 60);
    else {
        last_error_ = "Sample instrument property is unavailable";
        return false;
    }
    return execute({"pad.envelope-set.v1",
                    {{"attack", static_cast<double>(attack)},
                     {"decay", static_cast<double>(decay)},
                     {"sustain", static_cast<double>(sustain)},
                     {"release", static_cast<double>(release)}}, {}, 0});
}

bool SlintFrontendAdapter::select_sound_modulation(std::size_t index) {
    if (index >= state_.sound.modulation.size()) {
        last_error_ = "Modulation assignment is unavailable";
        return false;
    }
    state_.sound.selected_modulation = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_sound_modulation(std::string operation) {
    if (state_.sound.selected_modulation >= state_.sound.modulation.size()) {
        last_error_ = "Modulation assignment is unavailable";
        return false;
    }
    const auto &route = state_.sound.modulation[state_.sound.selected_modulation];
    if (operation == "delete")
        return execute({"modulation.route-delete.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)}}, {}, 0});
    if (operation == "toggle")
        return execute({"modulation.route-enable.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)},
                         {"enabled", !route.enabled}}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto found = std::find_if(snapshot.modulation.routes.begin(),
        snapshot.modulation.routes.begin() + snapshot.modulation.route_count,
        [&](const auto &value) { return value.id == route.id; });
    if (found == snapshot.modulation.routes.begin() + snapshot.modulation.route_count) {
        last_error_ = "Modulation assignment is stale";
        return false;
    }
    const auto delta = operation == "depth-up" ? 0.05F
                     : operation == "depth-down" ? -0.05F : 0.0F;
    if (delta == 0) {
        last_error_ = "Modulation operation is unavailable";
        return false;
    }
    return execute({"modulation.route-depth.v1",
                    {{"route_id", static_cast<std::int64_t>(route.id)},
                     {"depth", static_cast<double>(
                         std::clamp(found->depth + delta, -1.0F, 1.0F))}}, {}, 0});
}

bool SlintFrontendAdapter::preset_action(std::string operation,
                                          std::string library,
                                          std::string preset_id) {
    state_.sound.preset_library = library;
    state_.sound.preset_id = preset_id;
    if (operation == "apply") return invoke_action("preset.apply.v1");
    if (operation == "cancel") return invoke_action("preset.cancel.v1");
    if (operation == "save") {
        last_error_ = "Saving the current instrument has no semantic action";
        state_.shell.notification = last_error_;
        return false;
    }
    if (library.empty() || preset_id.empty()) {
        last_error_ = "Preset library and ID are required";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto action = operation == "validate" ? "preset.validate.v1"
                                                 : "preset.prepare.v1";
    return execute({action,
                    {{"library", std::move(library)},
                     {"preset_id", std::move(preset_id)},
                     {"mode", std::string("replace")},
                     {"track_id", static_cast<std::int64_t>(state_.sound.track_id)}},
                    {}, 0});
}

bool SlintFrontendAdapter::select_mixer_channel(std::size_t index) {
    if (index >= state_.mixer.channels.size()) {
        last_error_ = "Mixer channel is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.mixer.selected_channel = index;
    ++local_revision_;
    const auto &channel = state_.mixer.channels[index];
    if (channel.type == "Track") {
        const auto snapshot = application_.snapshot();
        const auto found = std::find_if(
            snapshot.mixer.configuration.tracks.begin(),
            snapshot.mixer.configuration.tracks.begin() +
                snapshot.mixer.configuration.track_count,
            [&](const auto &track) { return track.track == channel.id; });
        if (found == snapshot.mixer.configuration.tracks.begin() +
                         snapshot.mixer.configuration.track_count)
            return false;
        return execute({"mixer.channel-select.v1",
                        {{"index", static_cast<std::int64_t>(found -
                            snapshot.mixer.configuration.tracks.begin())}}, {}, 0});
    }
    if (channel.type == "Group")
        return execute({"mixer.group-select.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)}}, {}, 0});
    (void)refresh();
    return true;
}

bool SlintFrontendAdapter::bank_mixer(int direction) {
    constexpr std::size_t bank_size = 8;
    const auto count = state_.mixer.channels.size();
    const auto maximum = count ? (count - 1) / bank_size : 0;
    if (direction < 0 && state_.mixer.bank) --state_.mixer.bank;
    else if (direction > 0 && state_.mixer.bank < maximum) ++state_.mixer.bank;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_mixer(std::string operation, double steps) {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size() ||
        !std::isfinite(steps)) {
        last_error_ = "Selected mixer channel is unavailable";
        return false;
    }
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    const auto delta = operation == "pan" ? 0.05 * steps : 0.05 * steps;
    const auto gain = std::clamp<double>(channel.gain + delta, 0.0, 2.0);
    const auto pan = std::clamp<double>(channel.pan + delta, -1.0, 1.0);
    const auto aux_index = operation == "aux-2" ? 1U : 0U;
    const auto aux = std::clamp<double>(channel.aux_sends[aux_index] + delta,
                                        0.0, 1.0);
    if (channel.type == "Track") {
        if (operation == "gain")
            return execute({"mixer.gain-set.v1", {{"value", gain}}, {}, 0});
        if (operation == "pan")
            return execute({"mixer.pan-set.v1", {{"value", pan}}, {}, 0});
        if (operation == "aux-1" || operation == "aux-2")
            return execute({"mixer.aux-send-set.v1",
                            {{"aux", static_cast<std::int64_t>(aux_index)},
                             {"value", aux}}, {}, 0});
    } else if (channel.type == "Group") {
        if (operation == "gain" || operation == "pan")
            return execute({"mixer.group-levels.v1",
                            {{"id", static_cast<std::int64_t>(channel.id)},
                             {"gain", gain}, {"pan", pan}}, {}, 0});
        if (operation == "aux-1" || operation == "aux-2")
            return execute({"mixer.group-aux-send.v1",
                            {{"id", static_cast<std::int64_t>(channel.id)},
                             {"aux", static_cast<std::int64_t>(aux_index)},
                             {"value", aux}}, {}, 0});
    } else if (channel.type == "Aux Return" && operation == "gain") {
        const auto snapshot = application_.snapshot();
        const auto &bus = snapshot.mixer.configuration.aux[channel.id];
        return execute({"mixer.aux-set.v1",
                        {{"aux", static_cast<std::int64_t>(channel.id)},
                         {"gain", static_cast<double>(bus.gain)},
                         {"return_gain", gain},
                         {"effect_enabled", bus.effect_enabled}}, {}, 0});
    } else if (channel.type == "Master" && operation == "gain")
        return execute({"mixer.master-gain-set.v1", {{"value", gain}}, {}, 0});
    last_error_ = operation + " is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::toggle_mixer(std::string operation) {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.type == "Track")
        return invoke_action(operation == "mute" ? "mixer.mute-toggle.v1"
                                                  : "mixer.solo-toggle.v1");
    if (channel.type == "Group")
        return execute({operation == "mute" ? "mixer.group-mute-toggle.v1"
                                             : "mixer.group-solo-toggle.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)}}, {}, 0});
    if (channel.type == "Master" && operation == "mute")
        return invoke_action("mixer.master-mute-toggle.v1");
    last_error_ = operation + " is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_mixer_route() {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.route.rfind("Group ", 0) == 0) {
        last_error_ = "Return the track to Master before selecting a direct output";
        state_.shell.notification = last_error_;
        return false;
    }
    // Skip the default master pair (1/2) when leaving Master; the mixer
    // validator prevents an implicit duplicate path onto the master outputs.
    const auto next = channel.route == "Master" ? 2 :
        channel.route == "Output 3/4" ? 4 : -1;
    if (channel.type == "Track")
        return execute({"mixer.output-pair-set.v1",
                        {{"pair", static_cast<std::int64_t>(next)}}, {}, 0});
    if (channel.type == "Group")
        return execute({"mixer.group-output.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)},
                         {"group_id", std::int64_t{0}},
                         {"output_pair", static_cast<std::int64_t>(next)}}, {}, 0});
    last_error_ = "Output routing is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_mixer_group() {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.type != "Track") {
        last_error_ = "Group assignment is available only for tracks";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto snapshot = application_.snapshot();
    const auto track = std::find_if(
        snapshot.mixer.configuration.tracks.begin(),
        snapshot.mixer.configuration.tracks.begin() +
            snapshot.mixer.configuration.track_count,
        [&](const auto &value) { return value.track == channel.id; });
    if (track == snapshot.mixer.configuration.tracks.begin() +
                     snapshot.mixer.configuration.track_count)
        return false;
    MixerBusId destination{};
    if (!snapshot.mixer.configuration.group_count) {
        last_error_ = "No group buses are available";
        state_.shell.notification = last_error_;
        return false;
    }
    for (std::size_t i = 0; i < snapshot.mixer.configuration.group_count; ++i) {
        if (snapshot.mixer.configuration.groups[i].id == track->destination_group) {
            if (i + 1 < snapshot.mixer.configuration.group_count)
                destination = snapshot.mixer.configuration.groups[i + 1].id;
            break;
        }
        if (!track->destination_group && i == 0)
            destination = snapshot.mixer.configuration.groups[0].id;
    }
    return execute({"mixer.track-destination.v1",
                    {{"track_id", static_cast<std::int64_t>(channel.id)},
                     {"group_id", static_cast<std::int64_t>(destination)}}, {}, 0});
}

bool SlintFrontendAdapter::select_external_fx(std::size_t index) {
    if (index >= state_.mixer.external_fx.size()) return false;
    return execute({"mixer.fx-loop-select.v1",
                    {{"index", static_cast<std::int64_t>(index)}}, {}, 0});
}

bool SlintFrontendAdapter::adjust_external_fx(std::string operation,
                                               double steps) {
    const auto snapshot = application_.snapshot();
    const auto index = snapshot.selected_external_fx_loop;
    if (index >= mixer_external_fx_count) return false;
    const auto &loop = snapshot.mixer.configuration.external_fx[index];
    auto send = static_cast<double>(loop.send_gain);
    auto receive = static_cast<double>(loop.return_gain);
    auto wet = static_cast<double>(loop.wet);
    if (operation == "send") send = std::clamp(send + steps * .05, 0.0, 2.0);
    else if (operation == "return") receive = std::clamp(receive + steps * .05, 0.0, 2.0);
    else if (operation == "wet") wet = std::clamp(wet + steps * .05, 0.0, 1.0);
    else if (operation == "latency")
        return execute({"mixer.fx-loop-latency.v1",
                        {{"index", static_cast<std::int64_t>(index)},
                         {"frames", static_cast<std::int64_t>(std::clamp<double>(
                             loop.latency_frames + steps * 16.0, 0.0,
                             maximum_external_fx_latency_frames))}}, {}, 0});
    else return false;
    return execute({"mixer.fx-loop-levels.v1",
                    {{"index", static_cast<std::int64_t>(index)},
                     {"send_gain", send}, {"return_gain", receive},
                     {"wet", wet}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_external_fx(std::string operation) {
    const auto snapshot = application_.snapshot();
    const auto index = snapshot.selected_external_fx_loop;
    if (index >= mixer_external_fx_count) return false;
    const auto &loop = snapshot.mixer.configuration.external_fx[index];
    const auto source = loop.source == ExternalFxSource::track ? "track" :
        loop.source == ExternalFxSource::auxiliary ? "aux" :
        loop.source == ExternalFxSource::group ? "group" : "master";
    if (operation == "return-mute")
        return execute({"mixer.fx-loop-levels.v1",
                        {{"index", static_cast<std::int64_t>(index)},
                         {"send_gain", static_cast<double>(loop.send_gain)},
                         {"return_gain", static_cast<double>(loop.return_gain)},
                         {"wet", static_cast<double>(loop.wet)},
                         {"return_mute", !loop.return_mute}}, {}, 0});
    const auto cycle_pair = [](std::uint8_t left) {
        return static_cast<std::uint8_t>(left < 2 ? 2 : left < 4 ? 4 : 0);
    };
    const auto send_left = operation == "send-pair" ? cycle_pair(loop.send_left)
                                                     : loop.send_left;
    const auto return_left = operation == "return-pair"
        ? cycle_pair(loop.return_left) : loop.return_left;
    return execute({"mixer.fx-loop-set.v1",
                    {{"index", static_cast<std::int64_t>(index)},
                     {"source", std::string(source)},
                     {"source_id", static_cast<std::int64_t>(loop.source_id)},
                     {"send_left", static_cast<std::int64_t>(send_left)},
                     {"send_right", static_cast<std::int64_t>(send_left + 1)},
                     {"return_left", static_cast<std::int64_t>(return_left)},
                     {"return_right", static_cast<std::int64_t>(return_left + 1)},
                     {"enabled", operation == "enabled" ? !loop.enabled : loop.enabled},
                     {"bypass", operation == "bypass" ? !loop.bypass : loop.bypass}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_subview(std::string subview) {
    static constexpr std::array<std::string_view, 5> valid{
        "chain", "parameters", "modulation", "macros", "external"};
    if (std::find(valid.begin(), valid.end(), subview) == valid.end()) {
        last_error_ = "Unknown FX subview";
        return false;
    }
    state_.fx.subview = std::move(subview);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_fx_owner(std::size_t index) {
    if (index >= state_.fx.owners.size()) return false;
    state_.fx.selected_owner = index;
    state_.fx.selected_effect = state_.fx.selected_parameter = 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_effect(std::size_t index) {
    if (index >= state_.fx.effects.size()) return false;
    state_.fx.selected_effect = index;
    state_.fx.selected_parameter = 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::add_effect(std::string type) {
    const auto snapshot = application_.snapshot();
    EffectInstanceId id = 1;
    for (std::size_t i = 0; i < snapshot.mixer.configuration.effects.instance_count; ++i)
        id = std::max(id, snapshot.mixer.configuration.effects.instances[i].id + 1);
    if (!execute({"effect.create.v1",
                  {{"id", static_cast<std::int64_t>(id)}, {"type", std::move(type)}}, {}, 0}))
        return false;
    return execute({"effect.insert.v1",
                    {{"id", static_cast<std::int64_t>(id)},
                     {"chain", state_.fx.owner_kind},
                     {"index", state_.fx.owner_index},
                     {"position", static_cast<std::int64_t>(state_.fx.effects.size())}}, {}, 0});
}

bool SlintFrontendAdapter::edit_effect(std::string operation) {
    if (operation == "chain-bypass")
        return execute({"effect.chain-bypass.v1",
                        {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                         {"bypass", !state_.fx.chain_bypass}}, {}, 0});
    if (state_.fx.selected_effect >= state_.fx.effects.size()) return false;
    const auto effect = state_.fx.effects[state_.fx.selected_effect];
    if (operation == "bypass")
        return execute({"effect.bypass.v1",
                        {{"id", static_cast<std::int64_t>(effect.id)},
                         {"bypass", !effect.bypass}}, {}, 0});
    const auto position = static_cast<std::int64_t>(state_.fx.selected_effect);
    if (operation == "up" || operation == "down") {
        const auto to = position + (operation == "up" ? -1 : 1);
        if (to < 0 || to >= static_cast<std::int64_t>(state_.fx.effects.size())) return false;
        return execute({"effect.reorder.v1",
                        {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                         {"from", position}, {"to", to}}, {}, 0});
    }
    if (operation == "remove") {
        if (!execute({"effect.remove.v1",
                      {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                       {"position", position}}, {}, 0})) return false;
        return execute({"effect.delete.v1",
                        {{"id", static_cast<std::int64_t>(effect.id)}}, {}, 0});
    }
    last_error_ = "Unsupported effect operation";
    return false;
}

bool SlintFrontendAdapter::select_effect_parameter(std::size_t index) {
    if (index >= state_.fx.parameters.size()) return false;
    state_.fx.selected_parameter = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_effect_parameter(double steps) {
    if (state_.fx.selected_effect >= state_.fx.effects.size() ||
        state_.fx.selected_parameter >= state_.fx.parameters.size() ||
        !std::isfinite(steps)) return false;
    const auto &effect = state_.fx.effects[state_.fx.selected_effect];
    const auto &parameter = state_.fx.parameters[state_.fx.selected_parameter];
    const auto current = parameter.minimum + parameter.normalized *
        (parameter.maximum - parameter.minimum);
    const auto value = std::clamp(current + steps *
        (parameter.maximum - parameter.minimum) / 100.0,
        parameter.minimum, parameter.maximum);
    return execute({"effect.parameter-set.v1",
                    {{"id", static_cast<std::int64_t>(effect.id)},
                     {"parameter", static_cast<std::int64_t>(parameter.id)},
                     {"value", value}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_modulator(std::size_t index) {
    if (index >= state_.fx.modulators.size()) return false;
    state_.fx.selected_modulator = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_fx_route(std::size_t index) {
    if (index >= state_.fx.routes.size()) return false;
    state_.fx.selected_route = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_fx_route(std::string operation) {
    if (state_.fx.selected_route >= state_.fx.routes.size()) return false;
    const auto &route = state_.fx.routes[state_.fx.selected_route];
    if (operation == "remove")
        return execute({"modulation.route-delete.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)}}, {}, 0});
    if (operation == "toggle")
        return execute({"modulation.route-enable.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)},
                         {"enabled", !route.enabled}}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto found = std::find_if(snapshot.modulation.routes.begin(),
        snapshot.modulation.routes.begin() + snapshot.modulation.route_count,
        [&](const auto &value) { return value.id == route.id; });
    if (found == snapshot.modulation.routes.begin() + snapshot.modulation.route_count)
        return false;
    const auto delta = operation == "depth-down" ? -0.05 : 0.05;
    return execute({"modulation.route-depth.v1",
                    {{"route_id", static_cast<std::int64_t>(route.id)},
                     {"depth", std::clamp<double>(found->depth + delta, -1.0, 1.0)}}, {}, 0});
}

bool SlintFrontendAdapter::create_fx_route() {
    if (state_.fx.selected_modulator >= state_.fx.modulators.size() ||
        state_.fx.selected_effect >= state_.fx.effects.size() ||
        state_.fx.selected_parameter >= state_.fx.parameters.size()) {
        last_error_ = "Select a modulator and effect parameter first";
        return false;
    }
    const auto &source = state_.fx.modulators[state_.fx.selected_modulator];
    const auto &effect = state_.fx.effects[state_.fx.selected_effect];
    const auto &parameter = state_.fx.parameters[state_.fx.selected_parameter];
    return execute({"modulator.route-create.v1",
                    {{"source_id", static_cast<std::int64_t>(source.id)},
                     {"destination", "fx." + std::to_string(effect.id) + "." +
                                         std::to_string(parameter.id)},
                     {"depth", 0.5}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_macro(std::size_t index) {
    if (index >= state_.fx.macros.size()) return false;
    state_.fx.selected_macro = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_fx_macro(double steps) {
    if (state_.fx.selected_macro >= state_.fx.macros.size()) return false;
    const auto &macro = state_.fx.macros[state_.fx.selected_macro];
    return execute({"macro.value-set.v1",
                    {{"id", static_cast<std::int64_t>(macro.id)},
                     {"value", std::clamp<double>(macro.value + steps * .05, 0.0, 1.0)}}, {}, 0});
}

void SlintFrontendAdapter::dismiss_notification() noexcept {
    last_error_.clear();
    state_.shell.notification.clear();
    ++local_revision_;
}

bool SlintFrontendAdapter::keyboard_action(std::string_view key, bool pressed) {
    if (key == "space" && pressed)
        return invoke_action(state_.shell.playing ? "transport.stop.v1"
                                                 : "transport.play.v1");
    if (key == "shift")
        return execute(
            {"modifier.shift.v1", {{"active", pressed}}, {}, 0});
    if (pressed && key.size() == 1 && key[0] >= '1' && key[0] <= '8')
        return press_pad(static_cast<std::size_t>(key[0] - '1'));
    if (pressed && key.size() == 2 && key[0] == 'f' && key[1] >= '1' &&
        key[1] <= '8')
        return select_page(static_cast<std::size_t>(key[1] - '1'));
    last_error_ = "Keyboard action is unavailable";
    return false;
}

} // namespace neurogenic
