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

SlintFrontendAdapter::SlintFrontendAdapter(ApplicationService &application)
    : application_(application), bindings_(application) {
    (void)refresh();
}

bool SlintFrontendAdapter::refresh() {
    if (!active_)
        return false;
    const auto snapshot = application_.snapshot();
    if (snapshot.revision == state_.revision)
        return false;
    SlintPerformancePageState next;
    next.revision = snapshot.revision;
    next.playing = snapshot.playing;
    next.recording = snapshot.recording.state != RecordingState::idle;
    next.project_dirty = snapshot.project_dirty;
    next.project_name = snapshot.project_name.empty() ? "Untitled"
                                                       : snapshot.project_name;
    next.tempo_bpm = snapshot.tempo_bpm;
    next.transport = next.recording ? "Recording"
                                    : snapshot.playing ? "Playing" : "Stopped";
    next.time_signature =
        std::to_string(snapshot.time_signature_numerator) + "/" +
        std::to_string(snapshot.time_signature_denominator);
    const auto quarter = snapshot.transport_tick / ticks_per_quarter;
    const auto beats_per_bar = std::max<std::uint16_t>(
        1, snapshot.time_signature_numerator);
    next.position = std::to_string(quarter / beats_per_bar + 1) + "." +
                    std::to_string(quarter % beats_per_bar + 1) + "." +
                    std::to_string(snapshot.transport_tick % ticks_per_quarter);
    next.page_id = state_.page_id.empty() ? "performance" : state_.page_id;
    next.page_title = state_.page_title.empty() ? "Performance"
                                                : state_.page_title;
    for (const auto &[id, title] : pages)
        next.pages.push_back(
            {std::string(id), std::string(title), id == "performance"});
    next.launch_quantization = quantization_name(snapshot.launch_quantization);
    for (const auto &scene : snapshot.performance_launching.scenes) {
        const auto queued = std::any_of(
            snapshot.performance_launching.pending.begin(),
            snapshot.performance_launching.pending.end(),
            [&](const auto &launch) { return launch.scene == scene.id; });
        next.scenes.push_back(
            {scene.name.empty() ? "Scene " + std::to_string(scene.id)
                                : scene.name,
             queued});
    }
    if (const auto track = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(), [&](const auto &t) {
                return t.id == snapshot.selected_track;
            });
        track != snapshot.tracks.end())
        next.selected_track = track->name.empty()
                                  ? "Track " + std::to_string(track->id)
                                  : track->name;
    if (snapshot.active_pattern != 0)
        next.active_content =
            "Pattern " + std::to_string(snapshot.active_pattern);
    else if (snapshot.playback_mode == TimelinePlaybackMode::song &&
             !snapshot.audio_clips.empty())
        next.active_content = "Arrangement";

    if (const auto owner = std::find_if(
            snapshot.performance_launching.ownership.begin(),
            snapshot.performance_launching.ownership.end(), [&](const auto &v) {
                return v.track == snapshot.selected_track;
            });
        owner != snapshot.performance_launching.ownership.end()) {
        switch (owner->owner) {
        case TrackPlaybackOwner::arrangement: next.track_owner = "Arrangement"; break;
        case TrackPlaybackOwner::session: next.track_owner = "Session"; break;
        case TrackPlaybackOwner::transitioning_to_session:
            next.track_owner = "Queued Session"; break;
        case TrackPlaybackOwner::transitioning_to_arrangement:
            next.track_owner = "Queued Arrangement"; break;
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
        next.encoders[i] = copy(context[i]);
        next.functions[i] = copy(context[8 + i]);
    }

    for (std::size_t i = 0; i < next.pads.size(); ++i) {
        next.pads[i].label = std::to_string(i + 1);
        next.pads[i].selected = snapshot.selected_pad == i;
        if (snapshot.pad_mode == PadMode::step) {
            const auto step = snapshot.step_page * 16 + i;
            next.pads[i].active = std::any_of(
                snapshot.tracks.begin(), snapshot.tracks.end(),
                [&](const auto &track) {
                    return track.id == snapshot.selected_track &&
                           std::any_of(track.events.begin(), track.events.end(),
                                       [&](const auto &event) {
                                           return snapshot.step_ticks > 0 &&
                                                  event.tick /
                                                          snapshot.step_ticks ==
                                                      static_cast<MusicalTick>(
                                                          step);
                                       });
                });
        } else
            next.pads[i].active = snapshot.region_pad_assignments[i].has_value() ||
                                  snapshot.slice_pad_assignments[i].has_value();
        if (i < snapshot.performance_launching.slots.size()) {
            const auto &slot = snapshot.performance_launching.slots[i];
            next.pads[i].slot_id = slot.id;
            next.pads[i].available = true;
            next.pads[i].label = "Slot " + std::to_string(slot.id);
            const auto pending = std::find_if(
                snapshot.performance_launching.pending.begin(),
                snapshot.performance_launching.pending.end(),
                [&](const auto &launch) { return launch.slot == slot.id; });
            next.pads[i].queued =
                pending != snapshot.performance_launching.pending.end();
            const auto ownership = std::find_if(
                snapshot.performance_launching.ownership.begin(),
                snapshot.performance_launching.ownership.end(),
                [&](const auto &owner) {
                    return owner.slot == slot.id &&
                           owner.owner == TrackPlaybackOwner::session;
                });
            next.pads[i].active =
                ownership != snapshot.performance_launching.ownership.end();
            next.pads[i].status = next.pads[i].queued
                                      ? "Queued"
                                      : next.pads[i].active ? "Playing"
                                                            : "Stopped";
        }
    }
    next.notification = last_error_;
    state_ = std::move(next);
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
    state_.notification = last_error_;
    return result.accepted;
}

bool SlintFrontendAdapter::invoke_action(std::string action_id) {
    return execute({std::move(action_id), {}, {}, 0});
}

bool SlintFrontendAdapter::turn_encoder(std::size_t index, double steps,
                                        bool fine) {
    if (index >= state_.encoders.size() || !state_.encoders[index].available ||
        state_.encoders[index].parameter_id.empty()) {
        last_error_ = "Encoder assignment is unavailable";
        state_.notification = last_error_;
        return false;
    }
    const auto discovery = discover_application(application_);
    const auto descriptor = std::find_if(
        discovery.parameters.begin(), discovery.parameters.end(),
        [&](const auto &parameter) {
            return parameter.id == state_.encoders[index].parameter_id;
        });
    if (descriptor == discovery.parameters.end()) {
        last_error_ = "Encoder parameter is no longer discoverable";
        state_.notification = last_error_;
        return false;
    }
    BindingReference reference{BindingTargetKind::explicit_object,
                               descriptor->parent,
                               state_.encoders[index].parameter_id, {},
                               0};
    const auto error = bindings_.apply(
        reference,
        {BindingOperation::relative_steps, steps * (fine ? 0.1 : 1.0)});
    last_error_ = error ? error.message : std::string{};
    (void)refresh();
    state_.notification = last_error_;
    return !error;
}

bool SlintFrontendAdapter::invoke_function(std::size_t index) {
    if (index >= state_.functions.size() || !state_.functions[index].available ||
        state_.functions[index].action_id.empty()) {
        last_error_ = "Function assignment is unavailable";
        state_.notification = last_error_;
        return false;
    }
    return invoke_action(state_.functions[index].action_id);
}

bool SlintFrontendAdapter::press_pad(std::size_t index, bool pressed) {
    if (index >= state_.pads.size()) {
        last_error_ = "Pad index is out of range";
        return false;
    }
    if (!pressed)
        return execute({"pad.release.v1",
                        {{"pad", static_cast<std::int64_t>(index)}}, {}, 0});
    if (state_.page_id == "performance" && state_.pads[index].available)
        return execute({"performance.slot-launch.v1",
                        {{"slot", static_cast<std::int64_t>(
                                      state_.pads[index].slot_id)}}, {}, 0});
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
        state_.notification = last_error_;
        return false;
    }
    state_.page_id = std::string(pages[index].first);
    state_.page_title = std::string(pages[index].second);
    state_.notification = index == 0 ? std::string{}
                                     : state_.page_title + " is not implemented yet";
    last_error_ = state_.notification;
    return true;
}

bool SlintFrontendAdapter::select_track(int direction) {
    return invoke_action(direction < 0 ? "track.previous.v1" : "track.next.v1");
}

bool SlintFrontendAdapter::launch_scene(std::size_t index) {
    const auto snapshot = application_.snapshot();
    if (index >= snapshot.performance_launching.scenes.size()) {
        last_error_ = "Performance scene is unavailable";
        state_.notification = last_error_;
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
    state_.notification = last_error_;
    return false;
}

void SlintFrontendAdapter::dismiss_notification() noexcept {
    last_error_.clear();
    state_.notification.clear();
}

bool SlintFrontendAdapter::keyboard_action(std::string_view key, bool pressed) {
    if (key == "space" && pressed)
        return invoke_action(state_.playing ? "transport.stop.v1"
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
