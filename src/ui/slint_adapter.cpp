#include "neurogenic/ui/slint_adapter.hpp"
#include <algorithm>

namespace neurogenic {

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
    next.tempo_bpm = snapshot.tempo_bpm;
    next.transport = snapshot.playing ? "Playing" : "Stopped";
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

bool SlintFrontendAdapter::keyboard_action(std::string_view key, bool pressed) {
    if (key == "space" && pressed)
        return invoke_action(state_.playing ? "transport.stop.v1"
                                           : "transport.play.v1");
    if (key == "shift")
        return execute(
            {"modifier.shift.v1", {{"active", pressed}}, {}, 0});
    if (pressed && key.size() == 1 && key[0] >= '1' && key[0] <= '8')
        return press_pad(static_cast<std::size_t>(key[0] - '1'));
    last_error_ = "Keyboard action is unavailable";
    return false;
}

} // namespace neurogenic
