#include "neurogenic/audio/mixer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
bool finite_range(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
std::pair<float, float> pan(float value) noexcept {
    const auto position = std::clamp(value, -1.0F, 1.0F);
    return {position > 0 ? 1.0F - position : 1.0F,
            position < 0 ? 1.0F + position : 1.0F};
}
bool prepare_group_order(MixerConfiguration &value, std::string &error) noexcept {
    value.prepared_group_count = 0;
    std::array<std::uint8_t, maximum_mixer_groups> indegree{};
    const auto find = [&](MixerBusId id) {
        for (std::size_t i = 0; i < value.group_count; ++i)
            if (value.groups[i].id == id)
                return i;
        return value.group_count;
    };
    for (std::size_t i = 0; i < value.group_count; ++i)
        if (value.groups[i].destination_group) {
            const auto destination = find(value.groups[i].destination_group);
            if (destination == value.group_count) {
                error = "group output destination does not exist";
                return false;
            }
            if (destination == i) {
                error = "group cannot route to itself";
                return false;
            }
            ++indegree[destination];
        }
    std::array<bool, maximum_mixer_groups> emitted{};
    for (std::size_t count = 0; count < value.group_count; ++count) {
        std::size_t selected = value.group_count;
        for (std::size_t i = 0; i < value.group_count; ++i)
            if (!emitted[i] && indegree[i] == 0 &&
                (selected == value.group_count ||
                 value.groups[i].id < value.groups[selected].id))
                selected = i;
        if (selected == value.group_count) {
            error = "cyclic group routing";
            return false;
        }
        emitted[selected] = true;
        value.prepared_group_order[value.prepared_group_count++] =
            static_cast<std::uint8_t>(selected);
        const auto destination = value.groups[selected].destination_group;
        if (destination)
            --indegree[find(destination)];
    }
    return true;
}
} // namespace

bool validate_mixer_configuration(const MixerConfiguration &value,
                                  std::uint8_t inputs, std::uint8_t outputs,
                                  std::string &error) noexcept {
    if (value.track_count > maximum_mixer_tracks ||
        value.input_count > maximum_mixer_inputs ||
        value.group_count > maximum_mixer_groups ||
        outputs > maximum_mixer_outputs || inputs > maximum_mixer_inputs) {
        error = "mixer capacity exceeded";
        return false;
    }
    if (outputs && (value.master.output_left >= outputs ||
                    value.master.output_right >= outputs ||
                    value.master.output_left == value.master.output_right)) {
        error = "master output mapping is unavailable";
        return false;
    }
    if (!finite_range(value.master.gain, 0, 2)) {
        error = "master gain must be 0..2";
        return false;
    }
    for (std::size_t i = 0; i < value.track_count; ++i) {
        const auto &channel = value.tracks[i];
        if (!channel.track || !finite_range(channel.gain, 0, 2) ||
            !finite_range(channel.pan, -1, 1)) {
            error = "invalid track mixer channel";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (value.tracks[j].track == channel.track) {
                error = "duplicate track mixer channel";
                return false;
            }
        for (const auto send : channel.aux_send)
            if (!finite_range(send, 0, 1)) {
                error = "aux send must be 0..1";
                return false;
            }
        if (channel.output_pair < -1 ||
            (channel.output_pair >= 0 &&
             (!outputs || channel.output_pair + 1 >= outputs))) {
            error = "track output pair is unavailable";
            return false;
        }
        if (channel.destination_group) {
            if (channel.output_pair >= 0 ||
                std::none_of(value.groups.begin(),
                             value.groups.begin() + value.group_count,
                             [&](const auto &group) {
                                 return group.id == channel.destination_group;
                             })) {
                error = "track group destination is unavailable";
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < value.group_count; ++i) {
        const auto &group = value.groups[i];
        if (!group.id || !finite_range(group.gain, 0, 2) ||
            !finite_range(group.pan, -1, 1) || group.output_pair < -1 ||
            std::find(group.name.begin(), group.name.end(), '\0') ==
                group.name.end() ||
            (group.output_pair >= 0 &&
             (!outputs || group.output_pair + 1 >= outputs)) ||
            (group.output_pair >= 0 && group.destination_group)) {
            error = "invalid group bus configuration";
            return false;
        }
        for (const auto send : group.aux_send)
            if (!finite_range(send, 0, 1)) {
                error = "group aux send must be 0..1";
                return false;
            }
        for (std::size_t j = 0; j < i; ++j)
            if (value.groups[j].id == group.id) {
                error = "duplicate group bus ID";
                return false;
            }
    }
    auto prepared = value;
    if (!prepare_group_order(prepared, error))
        return false;
    for (std::size_t i = 0; i < value.input_count; ++i) {
        const auto &channel = value.inputs[i];
        if (channel.physical_channel >= inputs ||
            !finite_range(channel.gain, 0, 2) ||
            !finite_range(channel.pan, -1, 1)) {
            error = "physical input mapping is unavailable";
            return false;
        }
    }
    for (const auto &aux : value.aux)
        if (!finite_range(aux.gain, 0, 2) ||
            !finite_range(aux.return_gain, 0, 2) || aux.output_pair < -1 ||
            (aux.output_pair >= 0 &&
             (!outputs || aux.output_pair + 1 >= outputs))) {
            error = "invalid auxiliary bus configuration";
            return false;
        }
    if (!validate_effect_rack(value.effects, error))
        return false;
    std::array<bool, maximum_mixer_outputs> claimed_outputs{};
    if (outputs) {
        claimed_outputs[value.master.output_left] = true;
        claimed_outputs[value.master.output_right] = true;
    }
    for (std::size_t i = 0; i < value.track_count; ++i)
        if (value.tracks[i].output_pair >= 0) {
            claimed_outputs[static_cast<std::size_t>(
                value.tracks[i].output_pair)] = true;
            claimed_outputs[static_cast<std::size_t>(
                                value.tracks[i].output_pair) +
                            1] = true;
        }
    for (const auto &aux : value.aux)
        if (aux.output_pair >= 0) {
            claimed_outputs[static_cast<std::size_t>(aux.output_pair)] = true;
            claimed_outputs[static_cast<std::size_t>(aux.output_pair) + 1] =
                true;
        }
    for (std::size_t i = 0; i < value.group_count; ++i)
        if (value.groups[i].output_pair >= 0) {
            claimed_outputs[static_cast<std::size_t>(
                value.groups[i].output_pair)] = true;
            claimed_outputs[static_cast<std::size_t>(
                                value.groups[i].output_pair) +
                            1] = true;
        }
    bool master_source{};
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        const auto &loop = value.external_fx[i];
        if (!loop.id && !loop.enabled)
            continue;
        if (!loop.id || !finite_range(loop.send_gain, 0, 2) ||
            !finite_range(loop.return_gain, 0, 2) ||
            !finite_range(loop.wet, 0, 1) ||
            loop.latency_frames > maximum_external_fx_latency_frames) {
            error = "invalid external FX loop";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (value.external_fx[j].id == loop.id) {
                error = "duplicate external FX loop ID";
                return false;
            }
        for (std::size_t j = 0; loop.enabled && j < i; ++j)
            if (value.external_fx[j].enabled &&
                value.external_fx[j].source == loop.source &&
                value.external_fx[j].source_id == loop.source_id) {
                error = "external FX loops cannot process the same source";
                return false;
            }
        if (loop.enabled && loop.source == ExternalFxSource::track) {
            const auto found = std::find_if(
                value.tracks.begin(), value.tracks.begin() + value.track_count,
                [&](const auto &channel) {
                    return channel.track == loop.source_id;
                });
            if (found == value.tracks.begin() + value.track_count) {
                error = "external FX track source is unavailable";
                return false;
            }
            if (found->destination_group) {
                error = "external FX cannot source a grouped track";
                return false;
            }
        } else if (loop.enabled && loop.source == ExternalFxSource::auxiliary &&
                   loop.source_id >= mixer_aux_count) {
            error = "external FX auxiliary source is unavailable";
            return false;
        } else if (loop.enabled && loop.source == ExternalFxSource::master) {
            if (master_source) {
                error =
                    "only one master-source external FX loop may be enabled";
                return false;
            }
            master_source = true;
        } else if (loop.enabled && loop.source == ExternalFxSource::group &&
                   std::none_of(value.groups.begin(),
                                value.groups.begin() + value.group_count,
                                [&](const auto &group) {
                                    return group.id == loop.source_id;
                                })) {
            error = "external FX group source is unavailable";
            return false;
        } else if (loop.enabled && loop.source == ExternalFxSource::group) {
            const auto group = std::find_if(
                value.groups.begin(), value.groups.begin() + value.group_count,
                [&](const auto &candidate) {
                    return candidate.id == loop.source_id;
                });
            if (group->destination_group) {
                error = "external FX cannot source a nested group";
                return false;
            }
        }
        if (!loop.enabled)
            continue;
        const auto send_right =
            loop.mono_send ? loop.send_left : loop.send_right;
        const auto return_right =
            loop.mono_return ? loop.return_left : loop.return_right;
        if (!outputs || loop.send_left >= outputs || send_right >= outputs ||
            (!loop.mono_send && loop.send_left == loop.send_right)) {
            error = "external FX send mapping is unavailable";
            return false;
        }
        if (!inputs || loop.return_left >= inputs || return_right >= inputs ||
            (!loop.mono_return && loop.return_left == loop.return_right)) {
            error = "external FX return mapping is unavailable";
            return false;
        }
        if (claimed_outputs[loop.send_left] || claimed_outputs[send_right]) {
            error = "external FX send conflicts with another physical output";
            return false;
        }
        claimed_outputs[loop.send_left] = true;
        claimed_outputs[send_right] = true;
    }
    error.clear();
    return true;
}

Mixer::Mixer(std::uint32_t sample_rate)
    : sample_rate_(sample_rate),
      group_buffers_(std::make_unique<GroupAudioBuffers>()),
      aux_effects_{StereoDelay(sample_rate), StereoDelay(sample_rate)},
      effect_rack_(std::make_unique<NativeEffectRack>(sample_rate)),
      modulation_lanes_(std::make_unique<ModulationLanes>()) {
    for (auto &effect : aux_effects_) {
        effect.set_parameter(static_cast<std::uint32_t>(DelayParameter::mix),
                             0.25F);
        effect.set_parameter(
            static_cast<std::uint32_t>(DelayParameter::feedback), 0.25F);
    }
}
void Mixer::clear_modulation() noexcept { modulation_ = {}; }
bool Mixer::set_modulation(ParameterDestination destination,
                           float value) noexcept {
    if (!std::isfinite(value))
        return false;
    const auto index = destination.instance
                           ? static_cast<std::size_t>(destination.instance - 1)
                           : std::numeric_limits<std::size_t>::max();
    if (destination.target == ParameterTarget::mixer_track &&
        index < active_.track_count) {
        switch (
            static_cast<MixerTrackModulationParameter>(destination.parameter)) {
        case MixerTrackModulationParameter::gain:
            modulation_.track_gain[index] = std::clamp(value, 0.0F, 2.0F);
            modulation_.has_track_gain[index] = true;
            return true;
        case MixerTrackModulationParameter::pan:
            modulation_.track_pan[index] = std::clamp(value, -1.0F, 1.0F);
            modulation_.has_track_pan[index] = true;
            return true;
        case MixerTrackModulationParameter::aux_send_1:
        case MixerTrackModulationParameter::aux_send_2: {
            const auto send = destination.parameter -
                              static_cast<std::uint32_t>(
                                  MixerTrackModulationParameter::aux_send_1);
            modulation_.track_send[index][send] = std::clamp(value, 0.0F, 1.0F);
            modulation_.has_track_send[index][send] = true;
            return true;
        }
        }
    } else if (destination.target == ParameterTarget::mixer_aux &&
               index < mixer_aux_count && destination.parameter == 1) {
        modulation_.aux_return[index] = std::clamp(value, 0.0F, 2.0F);
        modulation_.has_aux_return[index] = true;
        return true;
    } else if (destination.target == ParameterTarget::mixer_master &&
               destination.instance == 0 && destination.parameter == 1) {
        modulation_.master_gain = std::clamp(value, 0.0F, 2.0F);
        modulation_.has_master_gain = true;
        return true;
    } else if (destination.target == ParameterTarget::mixer_group) {
        const auto group = find_group(destination.instance);
        if (group >= active_.group_count)
            return false;
        switch (static_cast<MixerGroupModulationParameter>(
            destination.parameter)) {
        case MixerGroupModulationParameter::gain:
            modulation_.group_gain[group] = std::clamp(value, 0.0F, 2.0F);
            modulation_.has_group_gain[group] = true;
            return true;
        case MixerGroupModulationParameter::pan:
            modulation_.group_pan[group] = std::clamp(value, -1.0F, 1.0F);
            modulation_.has_group_pan[group] = true;
            return true;
        case MixerGroupModulationParameter::aux_send_1:
        case MixerGroupModulationParameter::aux_send_2: {
            const auto aux = destination.parameter - 3;
            modulation_.group_send[group][aux] = std::clamp(value, 0.0F, 1.0F);
            modulation_.has_group_send[group][aux] = true;
            return true;
        }
        }
    } else if (destination.target == ParameterTarget::external_fx &&
               index < mixer_external_fx_count) {
        switch (
            static_cast<ExternalFxModulationParameter>(destination.parameter)) {
        case ExternalFxModulationParameter::wet:
            modulation_.fx_wet[index] = std::clamp(value, 0.0F, 1.0F);
            modulation_.has_fx_wet[index] = true;
            return true;
        case ExternalFxModulationParameter::send_gain:
            modulation_.fx_send[index] = std::clamp(value, 0.0F, 2.0F);
            modulation_.has_fx_send[index] = true;
            return true;
        case ExternalFxModulationParameter::return_gain:
            modulation_.fx_return[index] = std::clamp(value, 0.0F, 2.0F);
            modulation_.has_fx_return[index] = true;
            return true;
        }
    }
    return false;
}
void Mixer::capture_modulation_frame(std::size_t frame) noexcept {
    if (frame >= frames_)
        return;
    for (std::size_t channel = 0; channel < active_.track_count; ++channel) {
        modulation_lanes_->track_gain[channel][frame] =
            modulation_.has_track_gain[channel]
                ? modulation_.track_gain[channel]
                : active_.tracks[channel].gain;
        modulation_lanes_->track_pan[channel][frame] =
            modulation_.has_track_pan[channel] ? modulation_.track_pan[channel]
                                               : active_.tracks[channel].pan;
        for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
            modulation_lanes_->track_send[channel][aux][frame] =
                modulation_.has_track_send[channel][aux]
                    ? modulation_.track_send[channel][aux]
                    : active_.tracks[channel].aux_send[aux];
    }
    for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
        modulation_lanes_->aux_return[aux][frame] =
            modulation_.has_aux_return[aux] ? modulation_.aux_return[aux]
                                            : active_.aux[aux].return_gain;
    for (std::size_t group = 0; group < active_.group_count; ++group) {
        modulation_lanes_->group_gain[group][frame] =
            modulation_.has_group_gain[group] ? modulation_.group_gain[group]
                                              : active_.groups[group].gain;
        modulation_lanes_->group_pan[group][frame] =
            modulation_.has_group_pan[group] ? modulation_.group_pan[group]
                                             : active_.groups[group].pan;
        for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
            modulation_lanes_->group_send[group][aux][frame] =
                modulation_.has_group_send[group][aux]
                    ? modulation_.group_send[group][aux]
                    : active_.groups[group].aux_send[aux];
    }
    for (std::size_t index = 0; index < mixer_external_fx_count; ++index) {
        const auto wet_target = modulation_.has_fx_wet[index]
                                    ? modulation_.fx_wet[index]
                                    : active_.external_fx[index].wet;
        const auto send_target = modulation_.has_fx_send[index]
                                     ? modulation_.fx_send[index]
                                     : active_.external_fx[index].send_gain;
        const auto return_target = modulation_.has_fx_return[index]
                                       ? modulation_.fx_return[index]
                                       : active_.external_fx[index].return_gain;
        constexpr float smoothing = 0.125F;
        smoothed_fx_wet_[index] +=
            (wet_target - smoothed_fx_wet_[index]) * smoothing;
        smoothed_fx_send_[index] +=
            (send_target - smoothed_fx_send_[index]) * smoothing;
        smoothed_fx_return_[index] +=
            (return_target - smoothed_fx_return_[index]) * smoothing;
        modulation_lanes_->fx_wet[index][frame] = smoothed_fx_wet_[index];
        modulation_lanes_->fx_send[index][frame] = smoothed_fx_send_[index];
        modulation_lanes_->fx_return[index][frame] = smoothed_fx_return_[index];
    }
    modulation_lanes_->master_gain[frame] = modulation_.has_master_gain
                                                ? modulation_.master_gain
                                                : active_.master.gain;
}
bool Mixer::publish(const MixerConfiguration &value,
                    std::string &error) noexcept {
    auto prepared = value;
    if (!prepare(prepared, error))
        return false;
    if (!publications_.push(prepared)) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        error = "mixer publication queue is full";
        return false;
    }
    {
        std::lock_guard lock(control_mutex_);
        control_ = prepared;
    }
    error.clear();
    return true;
}
bool Mixer::prepare(MixerConfiguration &value, std::string &error) const noexcept {
    if (!validate_mixer_configuration(value, maximum_mixer_inputs,
                                      maximum_mixer_outputs, error) ||
        !prepare_group_order(value, error))
        return false;
    error.clear();
    return true;
}
void Mixer::set_control_configuration(
    const MixerConfiguration &value) noexcept {
    std::lock_guard lock(control_mutex_);
    control_ = value;
}
void Mixer::apply_pending_publications() noexcept {
    MixerConfiguration pending;
    while (publications_.pop(pending)) activate_prepared(pending);
}
void Mixer::activate_prepared(const MixerConfiguration &value) noexcept {
    const auto changed = active_.revision != value.revision;
    active_ = value;
    if (changed) {
        effect_rack_->activate(active_.effects);
        external_delay_cursor_.fill(0);
        external_delay_valid_.fill(0);
        for (std::size_t index = 0; index < mixer_external_fx_count; ++index) {
            smoothed_fx_wet_[index] = active_.external_fx[index].wet;
            smoothed_fx_send_[index] = active_.external_fx[index].send_gain;
            smoothed_fx_return_[index] = active_.external_fx[index].return_gain;
        }
    }
    revision_.store(active_.revision, std::memory_order_release);
}
void Mixer::begin_block(std::size_t frames) noexcept {
    apply_pending_publications();
    begin_prepared_block(frames);
}
void Mixer::begin_prepared_block(std::size_t frames) noexcept {
    frames_ = std::min(frames, maximum_mixer_block);
    for (std::size_t channel = 0; channel < active_.track_count; ++channel) {
        std::fill_n(modulation_lanes_->track_gain[channel].begin(), frames_,
                    active_.tracks[channel].gain);
        std::fill_n(modulation_lanes_->track_pan[channel].begin(), frames_,
                    active_.tracks[channel].pan);
        for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
            std::fill_n(modulation_lanes_->track_send[channel][aux].begin(),
                        frames_, active_.tracks[channel].aux_send[aux]);
    }
    for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
        std::fill_n(modulation_lanes_->aux_return[aux].begin(), frames_,
                    active_.aux[aux].return_gain);
    for (std::size_t group = 0; group < active_.group_count; ++group) {
        std::fill_n(modulation_lanes_->group_gain[group].begin(), frames_,
                    active_.groups[group].gain);
        std::fill_n(modulation_lanes_->group_pan[group].begin(), frames_,
                    active_.groups[group].pan);
        for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
            std::fill_n(modulation_lanes_->group_send[group][aux].begin(),
                        frames_, active_.groups[group].aux_send[aux]);
    }
    for (std::size_t index = 0; index < mixer_external_fx_count; ++index) {
        std::fill_n(modulation_lanes_->fx_wet[index].begin(), frames_,
                    active_.external_fx[index].wet);
        std::fill_n(modulation_lanes_->fx_send[index].begin(), frames_,
                    active_.external_fx[index].send_gain);
        std::fill_n(modulation_lanes_->fx_return[index].begin(), frames_,
                    active_.external_fx[index].return_gain);
    }
    std::fill_n(modulation_lanes_->master_gain.begin(), frames_,
                active_.master.gain);
    physical_input_channels_ = 0;
    for (auto &buffer : track_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : track_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : physical_input_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : aux_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : aux_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto *collection : {&group_buffers_->pre_left,
                             &group_buffers_->pre_right,
                             &group_buffers_->post_effect_left,
                             &group_buffers_->post_effect_right,
                             &group_buffers_->left, &group_buffers_->right})
        for (auto &buffer : *collection)
            std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : external_return_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : external_return_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : external_mix_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : external_mix_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
}
std::size_t Mixer::find_track(TrackId track) const noexcept {
    for (std::size_t i = 0; i < active_.track_count; ++i)
        if (active_.tracks[i].track == track)
            return i;
    return maximum_mixer_tracks;
}
std::size_t Mixer::find_group(MixerBusId id) const noexcept {
    for (std::size_t i = 0; i < active_.group_count; ++i)
        if (active_.groups[i].id == id)
            return i;
    return maximum_mixer_groups;
}
bool Mixer::add_track(TrackId track, std::size_t frame, float left,
                      float right) noexcept {
    if (frame >= frames_)
        return false;
    const auto channel = find_track(track);
    if (channel == maximum_mixer_tracks)
        return false;
    track_left_[channel][frame] += left;
    track_right_[channel][frame] += right;
    return true;
}
void Mixer::add_input(std::uint8_t physical, std::size_t frame,
                      float value) noexcept {
    if (frame >= frames_)
        return;
    if (physical < maximum_mixer_inputs) {
        physical_input_[physical][frame] += value;
        physical_input_channels_ = std::max<std::uint8_t>(
            physical_input_channels_, static_cast<std::uint8_t>(physical + 1));
    }
}
void Mixer::update_meter(AtomicMeter &meter, float left, float right) noexcept {
    constexpr float decay = 0.92F;
    const auto l = std::max(std::fabs(left),
                            meter.left.load(std::memory_order_relaxed) * decay);
    const auto r = std::max(
        std::fabs(right), meter.right.load(std::memory_order_relaxed) * decay);
    meter.left.store(l, std::memory_order_relaxed);
    meter.right.store(r, std::memory_order_relaxed);
    if (l >= 1 || r >= 1)
        meter.clip.store(true, std::memory_order_relaxed);
}
bool Mixer::track_effectively_audible(TrackId track) const noexcept {
    const auto end = active_.tracks.begin() + active_.track_count;
    const auto found = std::find_if(active_.tracks.begin(), end,
                                    [track](const auto &item) {
                                        return item.track == track;
                                    });
    // A timeline may run before a matching mixer channel publication (headless
    // and parser-level clients rely on this). Absence is not an implicit mute.
    if (found == end)
        return true;
    if (found->mute)
        return false;
    const auto any_solo = std::any_of(active_.tracks.begin(), end,
                                      [](const auto &item) {
                                          return item.solo;
                                      });
    if (any_solo && !found->solo)
        return false;
    const auto group_end = active_.groups.begin() + active_.group_count;
    const auto any_group_solo = std::any_of(
        active_.groups.begin(), group_end,
        [](const auto &item) { return item.solo; });
    if (!any_group_solo)
        return true;
    auto group = found->destination_group;
    for (std::size_t depth = 0; group && depth < active_.group_count; ++depth) {
        const auto owner = std::find_if(active_.groups.begin(), group_end,
                                        [group](const auto &item) {
                                            return item.id == group;
                                        });
        if (owner == group_end)
            return false;
        if (owner->solo)
            return true;
        group = owner->destination_group;
    }
    return false;
}

void Mixer::reset_effect_state() noexcept {
    effect_rack_->reset_state();
    external_delay_cursor_.fill(0);
    external_delay_valid_.fill(0);
}
void Mixer::process(std::span<float> master_left, std::span<float> master_right,
                    std::span<float> physical, std::uint8_t channels) noexcept {
    const auto frames =
        std::min({frames_, master_left.size(), master_right.size()});
    bool any_solo{};
    for (std::size_t i = 0; i < active_.track_count; ++i)
        any_solo |= active_.tracks[i].solo;
    bool any_group_solo{};
    for (std::size_t i = 0; i < active_.group_count; ++i)
        any_group_solo |= active_.groups[i].solo;
    const auto reaches_solo_group = [&](MixerBusId id) {
        for (std::size_t depth = 0; id && depth < active_.group_count; ++depth) {
            const auto slot = find_group(id);
            if (slot >= active_.group_count)
                return false;
            if (active_.groups[slot].solo)
                return true;
            id = active_.groups[slot].destination_group;
        }
        return false;
    };
    const auto carries_solo_group = [&](MixerBusId id) {
        for (std::size_t source = 0; source < active_.group_count; ++source) {
            if (!active_.groups[source].solo)
                continue;
            auto current = active_.groups[source].id;
            for (std::size_t depth = 0;
                 current && depth <= active_.group_count; ++depth) {
                if (current == id)
                    return true;
                const auto slot = find_group(current);
                if (slot >= active_.group_count)
                    break;
                current = active_.groups[slot].destination_group;
            }
        }
        return false;
    };
    for (std::size_t channel = 0; channel < active_.track_count; ++channel) {
        const auto &settings = active_.tracks[channel];
        effect_rack_->process(active_.effects.tracks[channel],
                              {track_left_[channel].data(), frames},
                              {track_right_[channel].data(), frames});
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto [pan_l, pan_r] =
                pan(modulation_lanes_->track_pan[channel][frame]);
            const auto effective_gain =
                modulation_lanes_->track_gain[channel][frame];
            const auto raw_l = track_left_[channel][frame];
            const auto raw_r = track_right_[channel][frame];
            const auto pre_l = raw_l * pan_l, pre_r = raw_r * pan_r;
            const auto audible =
                !settings.mute && (!any_solo || settings.solo) &&
                (!any_group_solo ||
                 reaches_solo_group(settings.destination_group));
            const auto post_l = audible ? pre_l * effective_gain : 0.0F;
            const auto post_r = audible ? pre_r * effective_gain : 0.0F;
            track_left_[channel][frame] = post_l;
            track_right_[channel][frame] = post_r;
            for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                const auto send_l =
                    settings.aux_pre_fader[aux] ? pre_l : post_l;
                const auto send_r =
                    settings.aux_pre_fader[aux] ? pre_r : post_r;
                const auto amount =
                    modulation_lanes_->track_send[channel][aux][frame];
                aux_left_[aux][frame] += send_l * amount;
                aux_right_[aux][frame] += send_r * amount;
            }
            if (settings.destination_group) {
                const auto group = find_group(settings.destination_group);
                if (group < active_.group_count) {
                    group_buffers_->pre_left[group][frame] += post_l;
                    group_buffers_->pre_right[group][frame] += post_r;
                }
            } else if (settings.output_pair < 0) {
                master_left[frame] += post_l;
                master_right[frame] += post_r;
            } else if (channels && physical.size() >= frames * channels) {
                physical[frame * channels + settings.output_pair] += post_l;
                physical[frame * channels + settings.output_pair + 1] += post_r;
            }
            peak_l = std::max(peak_l, std::fabs(post_l));
            peak_r = std::max(peak_r, std::fabs(post_r));
        }
        update_meter(track_meters_[channel], peak_l, peak_r);
    }
    for (std::size_t order = 0; order < active_.prepared_group_count; ++order) {
        const auto group = active_.prepared_group_order[order];
        const auto &settings = active_.groups[group];
        std::copy_n(group_buffers_->pre_left[group].begin(), frames,
                    group_buffers_->post_effect_left[group].begin());
        std::copy_n(group_buffers_->pre_right[group].begin(), frames,
                    group_buffers_->post_effect_right[group].begin());
        effect_rack_->process(active_.effects.groups[group],
                              {group_buffers_->post_effect_left[group].data(),
                               frames},
                              {group_buffers_->post_effect_right[group].data(),
                               frames});
        float peak_l{}, peak_r{};
        const auto audible =
            !settings.mute &&
            (!any_group_solo || reaches_solo_group(settings.id) ||
             carries_solo_group(settings.id));
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto [pan_l, pan_r] =
                pan(modulation_lanes_->group_pan[group][frame]);
            const auto pre_l =
                group_buffers_->post_effect_left[group][frame] * pan_l;
            const auto pre_r =
                group_buffers_->post_effect_right[group][frame] * pan_r;
            const auto gain = modulation_lanes_->group_gain[group][frame];
            const auto post_l = audible ? pre_l * gain : 0.0F;
            const auto post_r = audible ? pre_r * gain : 0.0F;
            group_buffers_->left[group][frame] = post_l;
            group_buffers_->right[group][frame] = post_r;
            for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                const auto send_l = settings.aux_pre_fader[aux] ? pre_l : post_l;
                const auto send_r = settings.aux_pre_fader[aux] ? pre_r : post_r;
                const auto amount =
                    modulation_lanes_->group_send[group][aux][frame];
                aux_left_[aux][frame] += send_l * amount;
                aux_right_[aux][frame] += send_r * amount;
            }
            if (settings.destination_group) {
                const auto destination = find_group(settings.destination_group);
                if (destination < active_.group_count) {
                    group_buffers_->pre_left[destination][frame] += post_l;
                    group_buffers_->pre_right[destination][frame] += post_r;
                }
            } else if (settings.output_pair < 0) {
                master_left[frame] += post_l;
                master_right[frame] += post_r;
            } else if (channels && physical.size() >= frames * channels) {
                physical[frame * channels + settings.output_pair] += post_l;
                physical[frame * channels + settings.output_pair + 1] += post_r;
            }
            peak_l = std::max(peak_l, std::fabs(post_l));
            peak_r = std::max(peak_r, std::fabs(post_r));
        }
        update_meter(group_meters_[group], peak_l, peak_r);
    }
    for (std::size_t channel = 0; channel < active_.input_count; ++channel) {
        const auto &settings = active_.inputs[channel];
        const auto [pan_l, pan_r] = pan(settings.pan);
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto value =
                settings.mute
                    ? 0.0F
                    : physical_input_[settings.physical_channel][frame] *
                          settings.gain;
            const auto l = value * pan_l, r = value * pan_r;
            if (settings.route_to_master) {
                master_left[frame] += l;
                master_right[frame] += r;
            }
            for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                aux_left_[aux][frame] += l * settings.aux_send[aux];
                aux_right_[aux][frame] += r * settings.aux_send[aux];
            }
            peak_l = std::max(peak_l, std::fabs(l));
            peak_r = std::max(peak_r, std::fabs(r));
        }
        update_meter(input_meters_[channel], peak_l, peak_r);
    }
    for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
        const auto &settings = active_.aux[aux];
        effect_rack_->process(active_.effects.aux[aux],
                              {aux_left_[aux].data(), frames},
                              {aux_right_[aux].data(), frames});
        if (settings.effect_enabled)
            aux_effects_[aux].process({aux_left_[aux].data(), frames},
                                      {aux_right_[aux].data(), frames});
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto l =
                settings.mute ? 0.0F : aux_left_[aux][frame] * settings.gain;
            const auto r =
                settings.mute ? 0.0F : aux_right_[aux][frame] * settings.gain;
            aux_left_[aux][frame] = l;
            aux_right_[aux][frame] = r;
            if (settings.output_pair < 0) {
                const auto return_gain =
                    modulation_lanes_->aux_return[aux][frame];
                master_left[frame] += l * return_gain;
                master_right[frame] += r * return_gain;
            } else if (channels && physical.size() >= frames * channels) {
                physical[frame * channels + settings.output_pair] += l;
                physical[frame * channels + settings.output_pair + 1] += r;
            }
            peak_l = std::max(peak_l, std::fabs(l));
            peak_r = std::max(peak_r, std::fabs(r));
        }
        update_meter(aux_meters_[aux], peak_l, peak_r);
    }
    for (std::size_t index = 0; index < mixer_external_fx_count; ++index) {
        const auto &loop = active_.external_fx[index];
        const auto send_right_channel =
            loop.mono_send ? loop.send_left : loop.send_right;
        const auto return_right_channel =
            loop.mono_return ? loop.return_left : loop.return_right;
        const auto send_available = loop.enabled && channels &&
                                    loop.send_left < channels &&
                                    send_right_channel < channels &&
                                    physical.size() >= frames * channels;
        const auto return_available =
            loop.enabled && loop.return_left < physical_input_channels_ &&
            return_right_channel < physical_input_channels_;
        external_send_available_[index].store(send_available,
                                              std::memory_order_relaxed);
        external_return_available_[index].store(return_available,
                                                std::memory_order_relaxed);
        if (!loop.enabled || loop.bypass) {
            update_meter(external_meters_[index], 0, 0);
            continue;
        }
        const float *source_l = master_left.data(),
                    *source_r = master_right.data();
        bool source_routes_to_master = true;
        float source_master_gain = 1.0F;
        if (loop.source == ExternalFxSource::track) {
            const auto track = find_track(static_cast<TrackId>(loop.source_id));
            if (track >= active_.track_count)
                continue;
            source_l = track_left_[track].data();
            source_r = track_right_[track].data();
            source_routes_to_master = active_.tracks[track].output_pair < 0 &&
                                      !active_.tracks[track].destination_group;
        } else if (loop.source == ExternalFxSource::auxiliary) {
            const auto aux = static_cast<std::size_t>(loop.source_id);
            if (aux >= mixer_aux_count)
                continue;
            source_l = aux_left_[aux].data();
            source_r = aux_right_[aux].data();
            source_routes_to_master = active_.aux[aux].output_pair < 0;
            source_master_gain = active_.aux[aux].return_gain;
        } else if (loop.source == ExternalFxSource::group) {
            const auto group = find_group(static_cast<MixerBusId>(loop.source_id));
            if (group >= active_.group_count)
                continue;
            source_l = group_buffers_->left[group].data();
            source_r = group_buffers_->right[group].data();
            source_routes_to_master = active_.groups[group].output_pair < 0 &&
                                      !active_.groups[group].destination_group;
        }
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto send_gain = modulation_lanes_->fx_send[index][frame];
            const auto return_gain = modulation_lanes_->fx_return[index][frame];
            const auto wet = modulation_lanes_->fx_wet[index][frame];
            const auto dry_l = source_l[frame] * source_master_gain;
            const auto dry_r = source_r[frame] * source_master_gain;
            if (send_available) {
                if (loop.mono_send) {
                    physical[frame * channels + loop.send_left] +=
                        (source_l[frame] + source_r[frame]) * 0.5F * send_gain;
                } else {
                    physical[frame * channels + loop.send_left] +=
                        source_l[frame] * send_gain;
                    physical[frame * channels + loop.send_right] +=
                        source_r[frame] * send_gain;
                }
            }
            float returned_l{}, returned_r{};
            if (return_available && !loop.return_mute) {
                returned_l =
                    physical_input_[loop.return_left][frame] * return_gain;
                returned_r = loop.mono_return
                                 ? returned_l
                                 : physical_input_[loop.return_right][frame] *
                                       return_gain;
            }
            external_return_left_[index][frame] = returned_l;
            external_return_right_[index][frame] = returned_r;
            const auto cursor = external_delay_cursor_[index];
            external_delay_left_[index][cursor] = dry_l;
            external_delay_right_[index][cursor] = dry_r;
            external_delay_valid_[index] =
                std::min<std::size_t>(external_delay_valid_[index] + 1,
                                      maximum_external_fx_latency_frames + 1);
            const auto delay = std::min<std::size_t>(
                loop.latency_frames, maximum_external_fx_latency_frames);
            constexpr auto delay_capacity =
                maximum_external_fx_latency_frames + 1;
            const auto read =
                (cursor + delay_capacity - delay) % delay_capacity;
            const auto ready = !delay || external_delay_valid_[index] > delay;
            const auto aligned_l = !delay  ? dry_l
                                   : ready ? external_delay_left_[index][read]
                                           : 0.0F;
            const auto aligned_r = !delay  ? dry_r
                                   : ready ? external_delay_right_[index][read]
                                           : 0.0F;
            external_delay_cursor_[index] = (cursor + 1) % delay_capacity;
            const auto mixed_l = aligned_l * (1.0F - wet) + returned_l * wet;
            const auto mixed_r = aligned_r * (1.0F - wet) + returned_r * wet;
            external_mix_left_[index][frame] = mixed_l;
            external_mix_right_[index][frame] = mixed_r;
            if (source_routes_to_master) {
                master_left[frame] += mixed_l - dry_l;
                master_right[frame] += mixed_r - dry_r;
            } else {
                master_left[frame] += returned_l * wet;
                master_right[frame] += returned_r * wet;
            }
            peak_l = std::max(peak_l, std::fabs(returned_l));
            peak_r = std::max(peak_r, std::fabs(returned_r));
        }
        update_meter(external_meters_[index], peak_l, peak_r);
    }
}
void Mixer::apply_master_gain(std::span<float> left,
                              std::span<float> right) noexcept {
    const auto frames = std::min({frames_, left.size(), right.size()});
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (active_.master.mute)
            left[frame] = right[frame] = 0;
        else {
            left[frame] *= modulation_lanes_->master_gain[frame];
            right[frame] *= modulation_lanes_->master_gain[frame];
        }
    }
}
void Mixer::route_master_output(std::span<const float> left,
                                std::span<const float> right,
                                std::span<float> physical,
                                std::uint8_t channels) noexcept {
    const auto frames = std::min({frames_, left.size(), right.size()});
    float peak_l{}, peak_r{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        peak_l = std::max(peak_l, std::fabs(left[frame]));
        peak_r = std::max(peak_r, std::fabs(right[frame]));
        if (channels == 1 && physical.size() >= frames)
            physical[frame] += (left[frame] + right[frame]) * 0.5F;
        else if (channels && physical.size() >= frames * channels &&
                 active_.master.output_left < channels &&
                 active_.master.output_right < channels) {
            physical[frame * channels + active_.master.output_left] +=
                left[frame];
            physical[frame * channels + active_.master.output_right] +=
                right[frame];
        }
    }
    update_meter(master_meter_, peak_l, peak_r);
}

MixerState Mixer::state() const noexcept {
    MixerState result;
    result.revision = revision_.load(std::memory_order_acquire);
    result.rejected_publications = rejected_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(control_mutex_);
        result.configuration = control_;
    }
    const auto read = [](const AtomicMeter &meter) {
        return StereoMeter{meter.left.load(std::memory_order_relaxed),
                           meter.right.load(std::memory_order_relaxed),
                           meter.clip.load(std::memory_order_relaxed)};
    };
    for (std::size_t i = 0; i < maximum_mixer_tracks; ++i)
        result.tracks[i] = read(track_meters_[i]);
    for (std::size_t i = 0; i < maximum_mixer_inputs; ++i)
        result.inputs[i] = read(input_meters_[i]);
    for (std::size_t i = 0; i < mixer_aux_count; ++i)
        result.aux[i] = read(aux_meters_[i]);
    for (std::size_t i = 0; i < maximum_mixer_groups; ++i)
        result.groups[i] = read(group_meters_[i]);
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        result.external_returns[i] = read(external_meters_[i]);
        result.external_send_available[i] =
            external_send_available_[i].load(std::memory_order_relaxed);
        result.external_return_available[i] =
            external_return_available_[i].load(std::memory_order_relaxed);
    }
    result.master = read(master_meter_);
    return result;
}
std::span<const float> Mixer::track_left(std::size_t index) const noexcept {
    return index < maximum_mixer_tracks
               ? std::span<const float>{track_left_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::track_right(std::size_t index) const noexcept {
    return index < maximum_mixer_tracks
               ? std::span<const float>{track_right_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::aux_left(std::size_t index) const noexcept {
    return index < mixer_aux_count
               ? std::span<const float>{aux_left_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::aux_right(std::size_t index) const noexcept {
    return index < mixer_aux_count
               ? std::span<const float>{aux_right_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::group_pre_left(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{
                     group_buffers_->pre_left[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::group_pre_right(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{
                     group_buffers_->pre_right[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::group_post_effect_left(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{
                     group_buffers_->post_effect_left[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::group_post_effect_right(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{
                     group_buffers_->post_effect_right[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::group_left(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{group_buffers_->left[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::group_right(std::size_t index) const noexcept {
    return index < active_.group_count
               ? std::span<const float>{group_buffers_->right[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::external_return_left(std::size_t index) const noexcept {
    return index < mixer_external_fx_count
               ? std::span<const float>{external_return_left_[index].data(),
                                        frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::external_return_right(std::size_t index) const noexcept {
    return index < mixer_external_fx_count
               ? std::span<const float>{external_return_right_[index].data(),
                                        frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::external_mix_left(std::size_t index) const noexcept {
    return index < mixer_external_fx_count
               ? std::span<const float>{external_mix_left_[index].data(),
                                        frames_}
               : std::span<const float>{};
}
std::span<const float>
Mixer::external_mix_right(std::size_t index) const noexcept {
    return index < mixer_external_fx_count
               ? std::span<const float>{external_mix_right_[index].data(),
                                        frames_}
               : std::span<const float>{};
}
} // namespace neurogenic
