#include "neurogenic/ui/slint_adapter.hpp"
#include <algorithm>

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
                                    id == "performance" || id == "sequencer"});
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

    const auto context = build_controller_context(snapshot);
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
    state_.shell.notification = index < 2
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
