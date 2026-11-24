#include "neurogenic/project/project.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neurogenic {
namespace {
constexpr std::uintmax_t maximum_metadata_size = 16U * 1024U * 1024U;
void bounded(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_metadata_size)
        throw std::runtime_error("project metadata is missing or oversized: " +
                                 path.string());
}
std::filesystem::path resolve_reference(const std::filesystem::path &root,
                                        const std::filesystem::path &value) {
    if (value.is_absolute())
        return value.lexically_normal();
    const auto normalized = value.lexically_normal();
    if (normalized.empty() || *normalized.begin() == "..")
        throw std::runtime_error("unsafe project-relative sample path: " +
                                 value.string());
    return (root / normalized).lexically_normal();
}
void checked_write(const std::filesystem::path &path,
                   const std::string &contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << contents) || !(output.flush()))
        throw std::runtime_error("cannot write project metadata: " +
                                 path.string());
}
std::string generation_name() {
    static std::atomic<std::uint64_t> sequence{};
    return "generation-" +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) +
           "-" + std::to_string(sequence.fetch_add(1));
}
std::string serialize_mixer(const MixerConfiguration &mixer) {
    std::ostringstream out;
    out << "NEUROGENIC_MIXER 4\n"
        << std::setprecision(9) << "master " << mixer.master.gain << ' '
        << static_cast<unsigned>(mixer.master.output_left) << ' '
        << static_cast<unsigned>(mixer.master.output_right) << ' '
        << mixer.master.mute << '\n';
    out << "tracks " << mixer.track_count << '\n';
    for (std::size_t i = 0; i < mixer.track_count; ++i) {
        const auto &c = mixer.tracks[i];
        out << c.track << ' ' << c.gain << ' ' << c.pan << ' ' << c.mute << ' '
            << c.solo << ' ' << static_cast<int>(c.output_pair) << ' '
            << c.destination_group;
        for (std::size_t a = 0; a < mixer_aux_count; ++a)
            out << ' ' << c.aux_send[a] << ' ' << c.aux_pre_fader[a];
        out << '\n';
    }
    out << "groups " << mixer.group_count << '\n';
    for (std::size_t i = 0; i < mixer.group_count; ++i) {
        const auto &group = mixer.groups[i];
        out << group.id << ' ' << std::quoted(std::string(group.name.data()))
            << ' ' << group.gain << ' ' << group.pan << ' ' << group.mute << ' '
            << group.solo << ' ' << group.destination_group << ' '
            << static_cast<int>(group.output_pair);
        for (std::size_t a = 0; a < mixer_aux_count; ++a)
            out << ' ' << group.aux_send[a] << ' '
                << group.aux_pre_fader[a];
        out << '\n';
    }
    out << "inputs " << mixer.input_count << '\n';
    for (std::size_t i = 0; i < mixer.input_count; ++i) {
        const auto &c = mixer.inputs[i];
        out << static_cast<unsigned>(c.physical_channel) << ' ' << c.gain << ' '
            << c.pan << ' ' << c.mute << ' ' << c.route_to_master;
        for (const auto send : c.aux_send)
            out << ' ' << send;
        out << '\n';
    }
    for (std::size_t a = 0; a < mixer_aux_count; ++a) {
        const auto &bus = mixer.aux[a];
        out << "aux " << a << ' ' << bus.gain << ' ' << bus.return_gain << ' '
            << static_cast<int>(bus.output_pair) << ' ' << bus.mute << ' '
            << bus.effect_enabled << '\n';
    }
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        const auto &loop = mixer.external_fx[i];
        out << "external " << i << ' ' << loop.id << ' '
            << std::quoted(std::string(loop.name.data())) << ' '
            << static_cast<unsigned>(loop.source) << ' ' << loop.source_id
            << ' ' << static_cast<unsigned>(loop.send_left) << ' '
            << static_cast<unsigned>(loop.send_right) << ' '
            << static_cast<unsigned>(loop.return_left) << ' '
            << static_cast<unsigned>(loop.return_right) << ' ' << loop.send_gain
            << ' ' << loop.return_gain << ' ' << loop.wet << ' '
            << loop.latency_frames << ' ' << loop.enabled << ' ' << loop.bypass
            << ' ' << loop.return_mute << ' ' << loop.mono_send << ' '
            << loop.mono_return << '\n';
    }
    out << "effects " << mixer.effects.instance_count << '\n';
    for (std::size_t i = 0; i < mixer.effects.instance_count; ++i) {
        const auto &effect = mixer.effects.instances[i];
        out << "effect " << effect.id << ' '
            << static_cast<unsigned>(effect.type) << ' ' << effect.enabled
            << ' ' << effect.bypass << ' ' << effect.wet << ' '
            << static_cast<unsigned>(effect.input_channels) << ' '
            << static_cast<unsigned>(effect.output_channels) << ' '
            << effect.parameter_count;
        for (std::size_t p = 0; p < effect.parameter_count; ++p)
            out << ' ' << effect.parameters[p].id << ' '
                << effect.parameters[p].value;
        out << '\n';
    }
    const auto chain = [&](std::string_view kind, std::size_t index,
                           const EffectChainConfiguration &value) {
        out << "chain " << kind << ' ' << index << ' ' << value.bypass << ' '
            << value.count;
        for (std::size_t i = 0; i < value.count; ++i)
            out << ' ' << value.instances[i];
        out << '\n';
    };
    for (std::size_t i = 0; i < mixer.effects.tracks.size(); ++i)
        chain("track", i, mixer.effects.tracks[i]);
    for (std::size_t i = 0; i < mixer.effects.aux.size(); ++i)
        chain("aux", i, mixer.effects.aux[i]);
    chain("master", 0, mixer.effects.master);
    for (std::size_t i = 0; i < mixer.effects.groups.size(); ++i)
        chain("group", i, mixer.effects.groups[i]);
    return out.str();
}
MixerConfiguration load_mixer(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{}, left{}, right{};
    MixerConfiguration result;
    int output{};
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MIXER" ||
        (version < 1 || version > 4) || !(input >> label) ||
        label != "master" ||
        !(input >> result.master.gain >> left >> right >> result.master.mute) ||
        left >= maximum_mixer_outputs || right >= maximum_mixer_outputs ||
        !(input >> label >> result.track_count) || label != "tracks" ||
        result.track_count > maximum_mixer_tracks)
        throw std::runtime_error("unsupported or malformed mixer metadata");
    result.master.output_left = static_cast<std::uint8_t>(left);
    result.master.output_right = static_cast<std::uint8_t>(right);
    for (std::size_t i = 0; i < result.track_count; ++i) {
        auto &c = result.tracks[i];
        if (!(input >> c.track >> c.gain >> c.pan >> c.mute >> c.solo >>
              output))
            throw std::runtime_error("malformed mixer track");
        c.output_pair = static_cast<std::int8_t>(output);
        if (version >= 4 && !(input >> c.destination_group))
            throw std::runtime_error("malformed mixer track destination");
        for (std::size_t a = 0; a < mixer_aux_count; ++a)
            if (!(input >> c.aux_send[a] >> c.aux_pre_fader[a]))
                throw std::runtime_error("malformed mixer send");
    }
    if (version >= 4) {
        if (!(input >> label >> result.group_count) || label != "groups" ||
            result.group_count > maximum_mixer_groups)
            throw std::runtime_error("malformed mixer group count");
        for (std::size_t i = 0; i < result.group_count; ++i) {
            auto &group = result.groups[i];
            std::string name;
            if (!(input >> group.id >> std::quoted(name) >> group.gain >>
                  group.pan >> group.mute >> group.solo >>
                  group.destination_group >> output) ||
                name.size() >= group.name.size())
                throw std::runtime_error("malformed mixer group");
            std::copy(name.begin(), name.end(), group.name.begin());
            group.output_pair = static_cast<std::int8_t>(output);
            for (std::size_t a = 0; a < mixer_aux_count; ++a)
                if (!(input >> group.aux_send[a] >>
                      group.aux_pre_fader[a]))
                    throw std::runtime_error("malformed group send");
        }
    }
    if (!(input >> label >> result.input_count) || label != "inputs" ||
        result.input_count > maximum_mixer_inputs)
        throw std::runtime_error("malformed mixer inputs");
    for (std::size_t i = 0; i < result.input_count; ++i) {
        auto &c = result.inputs[i];
        unsigned physical{};
        if (!(input >> physical >> c.gain >> c.pan >> c.mute >>
              c.route_to_master) ||
            physical >= maximum_mixer_inputs)
            throw std::runtime_error("malformed mixer input");
        c.physical_channel = static_cast<std::uint8_t>(physical);
        for (auto &send : c.aux_send)
            if (!(input >> send))
                throw std::runtime_error("malformed input send");
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i) {
        std::size_t index{};
        auto &bus = result.aux[i];
        if (!(input >> label >> index >> bus.gain >> bus.return_gain >>
              output >> bus.mute >> bus.effect_enabled) ||
            label != "aux" || index != i)
            throw std::runtime_error("malformed auxiliary bus");
        bus.output_pair = static_cast<std::int8_t>(output);
    }
    if (version >= 2)
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
            std::size_t index{};
            unsigned source{}, send_left{}, send_right{}, return_left{},
                return_right{};
            auto &loop = result.external_fx[i];
            std::string name;
            if (!(input >> label >> index >> loop.id >> std::quoted(name) >>
                  source >> loop.source_id >> send_left >> send_right >>
                  return_left >> return_right >> loop.send_gain >>
                  loop.return_gain >> loop.wet >> loop.latency_frames >>
                  loop.enabled >> loop.bypass >> loop.return_mute >>
                  loop.mono_send >> loop.mono_return) ||
                label != "external" || index != i ||
                source > (version >= 4 ? 3U : 2U) ||
                send_left >= maximum_mixer_outputs ||
                send_right >= maximum_mixer_outputs ||
                return_left >= maximum_mixer_inputs ||
                return_right >= maximum_mixer_inputs)
                throw std::runtime_error("malformed external FX loop");
            if (name.size() >= loop.name.size())
                throw std::runtime_error("external FX loop name is too long");
            std::copy(name.begin(), name.end(), loop.name.begin());
            loop.source = static_cast<ExternalFxSource>(source);
            loop.send_left = static_cast<std::uint8_t>(send_left);
            loop.send_right = static_cast<std::uint8_t>(send_right);
            loop.return_left = static_cast<std::uint8_t>(return_left);
            loop.return_right = static_cast<std::uint8_t>(return_right);
        }
    if (version >= 3) {
        if (!(input >> label >> result.effects.instance_count) ||
            label != "effects" ||
            result.effects.instance_count > maximum_effect_instances)
            throw std::runtime_error("malformed effect instance count");
        for (std::size_t i = 0; i < result.effects.instance_count; ++i) {
            auto &effect = result.effects.instances[i];
            unsigned type{}, inputs{}, outputs{};
            if (!(input >> label >> effect.id >> type >> effect.enabled >>
                  effect.bypass >> effect.wet >> inputs >> outputs >>
                  effect.parameter_count) ||
                label != "effect" || type < 1 || type > 4 || inputs < 1 ||
                inputs > 2 || outputs < 1 || outputs > 2 ||
                effect.parameter_count > maximum_effect_parameters)
                throw std::runtime_error("malformed effect instance");
            effect.type = static_cast<EffectProcessorType>(type);
            effect.input_channels = static_cast<std::uint8_t>(inputs);
            effect.output_channels = static_cast<std::uint8_t>(outputs);
            for (std::size_t p = 0; p < effect.parameter_count; ++p)
                if (!(input >> effect.parameters[p].id >>
                      effect.parameters[p].value))
                    throw std::runtime_error("malformed effect parameter");
        }
        const auto read_chain = [&](std::string_view expected,
                                    std::size_t expected_index,
                                    EffectChainConfiguration &chain) {
            std::string kind;
            std::size_t index{};
            if (!(input >> label >> kind >> index >> chain.bypass >>
                  chain.count) ||
                label != "chain" || kind != expected ||
                index != expected_index ||
                chain.count > maximum_effects_per_chain)
                throw std::runtime_error("malformed effect chain");
            for (std::size_t i = 0; i < chain.count; ++i)
                if (!(input >> chain.instances[i]))
                    throw std::runtime_error("malformed effect chain instance");
        };
        for (std::size_t i = 0; i < result.effects.tracks.size(); ++i)
            read_chain("track", i, result.effects.tracks[i]);
        for (std::size_t i = 0; i < result.effects.aux.size(); ++i)
            read_chain("aux", i, result.effects.aux[i]);
        read_chain("master", 0, result.effects.master);
        if (version >= 4)
            for (std::size_t i = 0; i < result.effects.groups.size(); ++i)
                read_chain("group", i, result.effects.groups[i]);
    }
    std::string error;
    if (!validate_mixer_configuration(result, maximum_mixer_inputs,
                                      maximum_mixer_outputs, error))
        throw std::runtime_error("invalid mixer metadata: " + error);
    result.revision = 1;
    return result;
}
bool validate_modulation_owners(const ModulationConfiguration &configuration,
                                const Timeline &timeline,
                                const MixerConfiguration &mixer,
                                std::string &error) {
    const auto owner_exists = [&](ModulatorOwnerKind kind, std::uint64_t id) {
        if (kind == ModulatorOwnerKind::master)
            return id == 0;
        if (kind == ModulatorOwnerKind::auxiliary)
            return id < mixer_aux_count;
        if (kind == ModulatorOwnerKind::group)
            return std::any_of(mixer.groups.begin(),
                               mixer.groups.begin() + mixer.group_count,
                               [&](const auto &group) { return group.id == id; });
        if (kind == ModulatorOwnerKind::track ||
            kind == ModulatorOwnerKind::voice_definition)
            return std::any_of(timeline.tracks.begin(), timeline.tracks.end(),
                               [&](const auto &track) { return track.id == id; });
        return false;
    };
    for (std::size_t i = 0; i < configuration.modulator_count; ++i) {
        const auto &modulator = configuration.modulators[i];
        if (!owner_exists(modulator.owner_kind, modulator.owner_id)) {
            error = "modulator " +
                    std::to_string(modulator.id) + " has a missing owner";
            return false;
        }
        if (modulator.type == ModulatorType::envelope_follower &&
            modulator.settings.follower_tap == FollowerTapKind::group &&
            std::none_of(mixer.groups.begin(),
                         mixer.groups.begin() + mixer.group_count,
                         [&](const auto &group) {
                             return group.id ==
                                    modulator.settings.follower_tap_id;
                         })) {
            error = "modulator " + std::to_string(modulator.id) +
                    " has a missing group follower tap";
            return false;
        }
    }
    for (std::size_t i = 0; i < configuration.route_count; ++i)
        if (configuration.routes[i].destination_owner_explicit &&
            !owner_exists(configuration.routes[i].destination_owner_kind,
                          configuration.routes[i].destination_owner_id)) {
            error = "connection " + std::to_string(configuration.routes[i].id) +
                    " has a missing destination owner";
            return false;
        }
    return true;
}
std::string serialize_modulation(const ModulationConfiguration &configuration) {
    std::ostringstream out;
    out << "NEUROGENIC_MODULATION 8\n"
        << std::setprecision(9) << "interval " << configuration.update_interval
        << '\n';
    for (std::size_t i = 0; i < configuration.source_count; ++i) {
        const auto &source = configuration.sources[i];
        out << "source " << i << ' ' << static_cast<unsigned>(source.waveform)
            << ' ' << source.rate_hz << ' ' << source.tempo_sync << ' '
            << source.beats_per_cycle << ' ' << source.retrigger << ' '
            << static_cast<unsigned>(source.scope) << ' '
            << source.attack_seconds << ' ' << source.decay_seconds << ' '
            << source.sustain << ' ' << source.release_seconds << ' '
            << source.seed << ' ' << source.follower_attack_seconds << ' '
            << source.follower_release_seconds << ' ' << source.follower_gain
            << ' ' << source.follower_threshold << '\n';
    }
    out << "modulators " << configuration.modulator_count << '\n';
    for (std::size_t i = 0; i < configuration.modulator_count; ++i) {
        const auto &instance = configuration.modulators[i];
        const auto &source = instance.settings;
        out << "modulator " << instance.id << ' '
            << static_cast<unsigned>(instance.type) << ' '
            << static_cast<unsigned>(instance.owner_kind) << ' '
            << instance.owner_id << ' ' << instance.enabled << ' '
            << std::quoted(std::string(instance.display_name.data())) << ' '
            << static_cast<unsigned>(source.waveform) << ' ' << source.rate_hz
            << ' ' << source.tempo_sync << ' ' << source.beats_per_cycle << ' '
            << source.retrigger << ' ' << source.attack_seconds << ' '
            << source.decay_seconds << ' ' << source.sustain << ' '
            << source.release_seconds << ' ' << source.seed << ' '
            << source.follower_attack_seconds << ' '
            << source.follower_release_seconds << ' ' << source.follower_gain
            << ' ' << source.follower_threshold << ' ' << instance.shared
            << ' ' << static_cast<unsigned>(source.follower_tap) << ' '
            << source.follower_tap_id << ' '
            << static_cast<unsigned>(source.step_count) << ' '
            << static_cast<unsigned>(source.pulses) << ' '
            << static_cast<unsigned>(source.rotation) << ' '
            << static_cast<unsigned>(source.order) << ' '
            << static_cast<unsigned>(source.distribution) << ' '
            << source.step_duration_beats << ' ' << source.swing << ' '
            << source.gate_length << ' ' << source.correlation << ' '
            << source.quantization << ' ' << source.slew_seconds << ' '
            << source.rise_seconds << ' ' << source.fall_seconds << ' '
            << source.curve << ' ' << source.scale << ' ' << source.offset << ' '
            << source.chaos << ' ' << source.unipolar << ' ' << source.looping;
        for (const auto value : source.step_values)
            out << ' ' << value;
        out << '\n';
    }
    out << "routes " << configuration.route_count << '\n';
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        out << route.id << ' ' << static_cast<unsigned>(route.source) << ' '
            << static_cast<unsigned>(route.destination.target) << ' '
            << route.destination.instance << ' ' << route.destination.parameter
            << ' ' << route.minimum << ' ' << route.maximum << ' '
            << route.base_value << ' ' << route.depth << ' ' << route.enabled
            << ' ' << static_cast<unsigned>(route.polarity) << ' '
            << static_cast<unsigned>(route.scope) << ' ' << route.track_id
            << ' ' << route.source_instance << ' ' << route.source_output
            << ' ' << static_cast<unsigned>(route.destination_kind) << ' '
            << route.destination_modulator << ' '
            << static_cast<unsigned>(route.destination_modulator_parameter)
            << ' ' << route.destination_input
            << ' ' << route.destination_owner_explicit << ' '
            << static_cast<unsigned>(route.destination_owner_kind) << ' '
            << route.destination_owner_id
            << '\n';
    }
    return out.str();
}
ModulationConfiguration load_modulation(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{};
    ModulationConfiguration result;
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MODULATION" ||
        (version == 0 || version > 8) ||
        !(input >> label >> result.update_interval) || label != "interval")
        throw std::runtime_error(
            "unsupported or malformed modulation metadata");
    const auto stored_source_count =
        version >= 3 ? result.source_count : result.source_count - 1;
    for (std::size_t i = 0; i < stored_source_count; ++i) {
        std::size_t index{};
        unsigned waveform{}, scope{};
        auto &source = result.sources[i];
        if (!(input >> label >> index >> waveform >> source.rate_hz >>
              source.tempo_sync >> source.beats_per_cycle >> source.retrigger >>
              scope >> source.attack_seconds >> source.decay_seconds >>
              source.sustain >> source.release_seconds >> source.seed) ||
            label != "source" || index != i ||
            waveform > static_cast<unsigned>(LfoWaveform::random) ||
            scope > static_cast<unsigned>(ModulationScope::voice))
            throw std::runtime_error("malformed modulation source");
        source.waveform = static_cast<LfoWaveform>(waveform);
        source.scope = static_cast<ModulationScope>(scope);
        if (version >= 3 &&
            !(input >> source.follower_attack_seconds >>
              source.follower_release_seconds >> source.follower_gain >>
              source.follower_threshold))
            throw std::runtime_error("malformed modulation follower source");
    }
    if (version >= 4) {
        if (!(input >> label >> result.modulator_count) ||
            label != "modulators" ||
            result.modulator_count > result.modulators.size())
            throw std::runtime_error("malformed modulator instance count");
        for (std::size_t i = 0; i < result.modulator_count; ++i) {
            auto &instance = result.modulators[i];
            auto &source = instance.settings;
            unsigned type{}, owner{}, waveform{};
            std::string name;
            if (!(input >> label >> instance.id >> type >> owner >>
                  instance.owner_id >> instance.enabled >> std::quoted(name) >>
                  waveform >> source.rate_hz >> source.tempo_sync >>
                  source.beats_per_cycle >> source.retrigger >>
                  source.attack_seconds >> source.decay_seconds >>
                  source.sustain >> source.release_seconds >> source.seed >>
                  source.follower_attack_seconds >>
                  source.follower_release_seconds >> source.follower_gain >>
                  source.follower_threshold) ||
                label != "modulator" ||
                type < static_cast<unsigned>(ModulatorType::lfo) ||
                type > static_cast<unsigned>(
                           version >= 7 ? ModulatorType::gate
                           : version >= 6 ? ModulatorType::logistic
                                          : ModulatorType::midi_cc) ||
                owner < static_cast<unsigned>(ModulatorOwnerKind::track) ||
                owner >
                    static_cast<unsigned>(ModulatorOwnerKind::voice_definition) ||
                waveform > static_cast<unsigned>(LfoWaveform::random) ||
                name.size() >= instance.display_name.size())
                throw std::runtime_error("malformed modulator instance");
            instance.type = static_cast<ModulatorType>(type);
            instance.owner_kind = static_cast<ModulatorOwnerKind>(owner);
            source.waveform = static_cast<LfoWaveform>(waveform);
            std::copy(name.begin(), name.end(), instance.display_name.begin());
            if (version >= 5) {
                unsigned tap{};
                if (!(input >> instance.shared >> tap >> source.follower_tap_id) ||
                    tap > static_cast<unsigned>(FollowerTapKind::group))
                    throw std::runtime_error("malformed modulator graph metadata");
                source.follower_tap = static_cast<FollowerTapKind>(tap);
            }
            if (version >= 6) {
                unsigned steps{}, pulses{}, rotation{}, order{}, distribution{};
                if (!(input >> steps >> pulses >> rotation >> order >>
                      distribution >>
                      source.step_duration_beats >> source.swing >>
                      source.gate_length >> source.correlation >>
                      source.quantization >> source.slew_seconds >>
                      source.rise_seconds >> source.fall_seconds >> source.curve >>
                      source.scale >> source.offset >> source.chaos >>
                      source.unipolar >> source.looping) ||
                    steps == 0 || steps > source.step_values.size() ||
                    pulses > steps || rotation >= steps || order > 3 ||
                    distribution > 2)
                    throw std::runtime_error("malformed native modulator settings");
                source.step_count = static_cast<std::uint8_t>(steps);
                source.pulses = static_cast<std::uint8_t>(pulses);
                source.rotation = static_cast<std::uint8_t>(rotation);
                source.order = static_cast<std::uint8_t>(order);
                source.distribution = static_cast<std::uint8_t>(distribution);
                for (auto &value : source.step_values)
                    if (!(input >> value))
                        throw std::runtime_error(
                            "malformed step modulator values");
            }
        }
    }
    if (!(input >> label >> result.route_count) || label != "routes" ||
        result.route_count > result.routes.size())
        throw std::runtime_error("malformed modulation route count");
    for (std::size_t i = 0; i < result.route_count; ++i) {
        unsigned source{}, target{}, polarity{}, scope{};
        auto &route = result.routes[i];
        if (!(input >> route.id >> source >> target) ||
            (version >= 2 && !(input >> route.destination.instance)) ||
            !(input >> route.destination.parameter >> route.minimum >>
              route.maximum >> route.base_value >> route.depth >>
              route.enabled >> polarity >> scope >> route.track_id) ||
            source >= static_cast<unsigned>(ModulationSourceId::count) ||
            target > static_cast<unsigned>(ParameterTarget::performance_macro) ||
            polarity > static_cast<unsigned>(ModulationPolarity::unipolar) ||
            scope > static_cast<unsigned>(ModulationScope::voice))
            throw std::runtime_error("malformed modulation route");
        route.source = static_cast<ModulationSourceId>(source);
        route.destination.target = static_cast<ParameterTarget>(target);
        route.polarity = static_cast<ModulationPolarity>(polarity);
        route.scope = static_cast<ModulationScope>(scope);
        if (version >= 4 &&
            !(input >> route.source_instance >> route.source_output))
            throw std::runtime_error("malformed modulator route source");
        if (version >= 5) {
            unsigned kind{}, parameter{}, owner{};
            if (!(input >> kind >> route.destination_modulator >> parameter) ||
                (version >= 6 && !(input >> route.destination_input)) ||
                !(input >>
                  route.destination_owner_explicit >> owner >>
                  route.destination_owner_id) ||
                kind > static_cast<unsigned>(
                           ModulationDestinationKind::modulator_input) ||
                owner > static_cast<unsigned>(ModulatorOwnerKind::voice_definition))
                throw std::runtime_error("malformed modulation graph connection");
            route.destination_kind =
                static_cast<ModulationDestinationKind>(kind);
            route.destination_modulator_parameter =
                static_cast<ModulatorParameterId>(parameter);
            route.destination_owner_kind = static_cast<ModulatorOwnerKind>(owner);
        }
        if (version == 1 &&
            route.destination.target == ParameterTarget::subtractive_synth &&
            route.scope != ModulationScope::global)
            route.destination.instance = default_synth_instance_id;
    }
    if (version < 4) {
        for (std::size_t i = 0; i < result.route_count; ++i) {
            auto &route = result.routes[i];
            if (route.source == ModulationSourceId::velocity)
                continue; // voice velocity remains on the compatibility path.
            if (result.modulator_count == result.modulators.size())
                throw std::runtime_error(
                    "legacy modulation exceeds instance capacity");
            auto &instance = result.modulators[result.modulator_count++];
            instance.id = static_cast<ModulatorInstanceId>(i + 1);
            instance.settings =
                result.sources[static_cast<std::size_t>(route.source)];
            instance.owner_kind = route.scope == ModulationScope::track
                                      ? ModulatorOwnerKind::track
                                  : route.scope == ModulationScope::voice
                                      ? ModulatorOwnerKind::voice_definition
                                      : ModulatorOwnerKind::master;
            instance.owner_id = route.scope == ModulationScope::global
                                    ? 0
                                    : route.track_id;
            switch (route.source) {
            case ModulationSourceId::lfo_1:
            case ModulationSourceId::lfo_2:
            case ModulationSourceId::lfo_3:
            case ModulationSourceId::lfo_4:
                instance.type = ModulatorType::lfo;
                break;
            case ModulationSourceId::envelope:
                instance.type = ModulatorType::adsr;
                break;
            case ModulationSourceId::sample_hold:
                instance.type = ModulatorType::sample_hold;
                break;
            case ModulationSourceId::smooth_random:
                instance.type = ModulatorType::smooth_random;
                break;
            case ModulationSourceId::envelope_follower:
                instance.type = ModulatorType::envelope_follower;
                break;
            case ModulationSourceId::midi_cc:
                instance.type = ModulatorType::midi_cc;
                break;
            default:
                instance.type = ModulatorType::xy_manual;
                break;
            }
            route.source_instance = instance.id;
        }
    }
    ModulationEngine validation;
    if (!validation.configure(result))
        throw std::runtime_error("invalid modulation configuration");
    return result;
}
std::string serialize_midi(const MidiProjectConfiguration &configuration) {
    std::ostringstream out;
    out << "NEUROGENIC_MIDI 2\nclock "
        << static_cast<unsigned>(configuration.clock_source) << ' '
        << configuration.clock_output_count << ' '
        << configuration.forward_external_clock;
    for (std::size_t i = 0; i < configuration.clock_output_count; ++i)
        out << ' ' << configuration.clock_outputs[i];
    out << "\nroutes " << configuration.route_count << '\n';
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        out << route.id << ' ' << route.input_port << ' ' << route.output_port
            << ' ' << int(route.input_channel) << ' '
            << int(route.output_channel) << ' ' << route.notes << ' '
            << route.control_change << ' ' << route.program_change << ' '
            << route.pitch_bend << ' ' << route.clock << ' ' << route.transport
            << ' ' << route.enabled << '\n';
    }
    for (std::size_t i = 0; i < maximum_midi_ports; ++i)
        out << "port " << i + 1 << ' ' << configuration.output_latency_frames[i]
            << ' ' << std::quoted(configuration.input_selectors[i].data())
            << ' ' << std::quoted(configuration.output_selectors[i].data())
            << '\n';
    return out.str();
}
MidiProjectConfiguration load_midi(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{}, source{};
    MidiProjectConfiguration result;
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MIDI" ||
        (version != 1 && version != 2) ||
        !(input >> label >> source >> result.clock_output_count) ||
        label != "clock" || source > 1 ||
        result.clock_output_count > maximum_midi_ports)
        throw std::runtime_error("unsupported or malformed MIDI metadata");
    result.clock_source = static_cast<MidiClockSource>(source);
    if (version >= 2 && !(input >> result.forward_external_clock))
        throw std::runtime_error("malformed MIDI forwarding preference");
    for (std::size_t i = 0; i < result.clock_output_count; ++i)
        if (!(input >> result.clock_outputs[i]) || !result.clock_outputs[i] ||
            result.clock_outputs[i] > maximum_midi_ports)
            throw std::runtime_error("malformed MIDI clock output");
    if (!(input >> label >> result.route_count) || label != "routes" ||
        result.route_count > maximum_midi_routes)
        throw std::runtime_error("malformed MIDI route count");
    for (std::size_t i = 0; i < result.route_count; ++i) {
        auto &route = result.routes[i];
        int input_channel{}, output_channel{};
        if (!(input >> route.id >> route.input_port >> route.output_port >>
              input_channel >> output_channel >> route.notes >>
              route.control_change >> route.program_change >>
              route.pitch_bend >> route.clock >> route.transport >>
              route.enabled))
            throw std::runtime_error("malformed MIDI route");
        route.input_channel = static_cast<std::int8_t>(input_channel);
        route.output_channel = static_cast<std::int8_t>(output_channel);
    }
    std::string error;
    if (!validate_midi_routes({result.routes.data(), result.route_count},
                              error))
        throw std::runtime_error("invalid MIDI routes: " + error);
    for (std::size_t i = 0; i < maximum_midi_ports; ++i) {
        std::size_t port{};
        std::string in, out;
        if (!(input >> label >> port >> result.output_latency_frames[i] >>
              std::quoted(in) >> std::quoted(out)) ||
            label != "port" || port != i + 1 || in.size() >= 64 ||
            out.size() >= 64)
            throw std::runtime_error("malformed MIDI port assignment");
        std::copy(in.begin(), in.end(), result.input_selectors[i].begin());
        std::copy(out.begin(), out.end(), result.output_selectors[i].begin());
    }
    return result;
}
std::string serialize_macros(const MacroProject &project) {
    std::ostringstream out;
    out << "NEUROGENIC_PERFORMANCE_MACROS 2\n" << std::setprecision(9)
        << "macros " << project.macros.size() << '\n';
    for (const auto &macro : project.macros) {
        out << "macro " << macro.id << ' ' << macro.source_instance << ' '
            << static_cast<unsigned>(macro.owner_kind) << ' ' << macro.owner_id
            << ' ' << macro.value << ' ' << macro.default_value << ' '
            << macro.enabled << ' ' << std::quoted(macro.name) << ' '
            << std::quoted(macro.description) << ' ' << macro.assignments.size()
            << '\n';
        for (const auto &assignment : macro.assignments)
            out << "assignment " << assignment.id << ' '
                << std::quoted(assignment.destination_id) << ' '
                << assignment.minimum << ' ' << assignment.maximum << ' '
                << static_cast<unsigned>(assignment.polarity) << ' '
                << static_cast<unsigned>(assignment.curve) << ' '
                << assignment.enabled << ' ' << assignment.smoothing_seconds
                << ' ' << assignment.steps << '\n';
    }
    out << "scenes " << project.scenes.size() << '\n';
    for (const auto &scene : project.scenes) {
        out << "scene " << scene.id << ' ' << scene.morph_duration << ' '
            << scene.tempo_sync << ' ' << static_cast<unsigned>(scene.curve)
            << ' ' << std::quoted(scene.name) << ' '
            << scene.macro_values.size() << ' '
            << scene.parameter_values.size() << '\n';
        for (const auto &[id, value] : scene.macro_values)
            out << "macro_value " << id << ' ' << value << '\n';
        for (const auto &[id, value] : scene.parameter_values)
            out << "parameter_value " << std::quoted(id) << ' ' << value
                << '\n';
    }
    return out.str();
}
MacroProject load_macros(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{};
    MacroProject result;
    std::size_t macro_count{};
    if (!(input >> magic >> version) ||
        magic != "NEUROGENIC_PERFORMANCE_MACROS" ||
        (version != 1 && version != 2) ||
        !(input >> label >> macro_count) || label != "macros" ||
        macro_count > MacroProject::default_macro_capacity)
        throw std::runtime_error("unsupported performance macro metadata");
    for (std::size_t i = 0; i < macro_count; ++i) {
        PerformanceMacro macro;
        unsigned owner{};
        std::size_t assignment_count{};
        if (!(input >> label >> macro.id >> macro.source_instance >> owner >>
              macro.owner_id >> macro.value >> macro.default_value >>
              macro.enabled >> std::quoted(macro.name) >>
              std::quoted(macro.description) >> assignment_count) ||
            label != "macro" || owner > static_cast<unsigned>(MacroOwnerKind::master) ||
            assignment_count > MacroProject::default_assignment_capacity)
            throw std::runtime_error("malformed performance macro");
        macro.owner_kind = static_cast<MacroOwnerKind>(owner);
        for (std::size_t assignment = 0; assignment < assignment_count;
             ++assignment) {
            MacroAssignment value;
            unsigned polarity{}, curve{};
            if (!(input >> label >> value.id >>
                  std::quoted(value.destination_id) >> value.minimum >>
                  value.maximum >> polarity >> curve >> value.enabled >>
                  value.smoothing_seconds >> value.steps) ||
                label != "assignment" ||
                polarity > static_cast<unsigned>(ModulationPolarity::unipolar) ||
                curve > static_cast<unsigned>(ModulationMappingCurve::stepped))
                throw std::runtime_error("malformed macro assignment");
            value.polarity = static_cast<ModulationPolarity>(polarity);
            value.curve = static_cast<ModulationMappingCurve>(curve);
            macro.assignments.push_back(std::move(value));
        }
        result.macros.push_back(std::move(macro));
    }
    std::size_t scene_count{};
    if (!(input >> label >> scene_count) || label != "scenes" ||
        scene_count > MacroProject::default_scene_capacity)
        throw std::runtime_error("malformed performance scene count");
    for (std::size_t i = 0; i < scene_count; ++i) {
        PerformanceScene scene;
        std::size_t macro_values{}, parameter_values{};
        unsigned curve{};
        if (!(input >> label >> scene.id >> scene.morph_duration >>
              scene.tempo_sync) ||
            (version >= 2 && !(input >> curve)) ||
            !(input >> std::quoted(scene.name) >> macro_values >>
              parameter_values) ||
            label != "scene" ||
            curve > static_cast<unsigned>(MacroMorphCurve::s_curve) ||
            macro_values > MacroProject::default_macro_capacity ||
            parameter_values > MacroProject::default_assignment_capacity)
            throw std::runtime_error("malformed performance scene");
        scene.curve = static_cast<MacroMorphCurve>(curve);
        for (std::size_t value = 0; value < macro_values; ++value) {
            PerformanceMacroId id{};
            float amount{};
            if (!(input >> label >> id >> amount) || label != "macro_value")
                throw std::runtime_error("malformed scene macro value");
            scene.macro_values.emplace_back(id, amount);
        }
        for (std::size_t value = 0; value < parameter_values; ++value) {
            std::string id;
            float amount{};
            if (!(input >> label >> std::quoted(id) >> amount) ||
                label != "parameter_value")
                throw std::runtime_error("malformed scene parameter value");
            scene.parameter_values.emplace_back(std::move(id), amount);
        }
        result.scenes.push_back(std::move(scene));
    }
    return result;
}
} // namespace

void save_modulation_configuration(
    const ModulationConfiguration &configuration,
    const std::filesystem::path &path) {
    ModulationGraphDiagnostics diagnostics;
    if (!validate_modulation_graph(configuration, diagnostics))
        throw std::invalid_argument("invalid modulation preset: " +
                                    diagnostics.message);
    checked_write(path, serialize_modulation(configuration));
}

ModulationConfiguration load_modulation_configuration(
    const std::filesystem::path &path) {
    return load_modulation(path);
}

void save_project(const Project &project,
                  const std::filesystem::path &directory,
                  ProjectIoMetrics *metrics) {
    const auto begin = std::chrono::steady_clock::now();
    PlaybackPlan validation;
    std::string validation_error;
    if (project.name.empty() ||
        !compile_timeline(project.timeline, validation, validation_error))
        throw std::invalid_argument("invalid project: " + validation_error);
    if (!validate_mixer_configuration(project.mixer, maximum_mixer_inputs,
                                      maximum_mixer_outputs, validation_error))
        throw std::invalid_argument("invalid project mixer: " +
                                    validation_error);
    if (project.routing_scenes.size() > 32)
        throw std::invalid_argument("too many routing scenes");
    for (std::size_t i = 0; i < project.routing_scenes.size(); ++i) {
        const auto &scene = project.routing_scenes[i];
        if (!scene.id || scene.name.empty() ||
            !validate_mixer_configuration(scene.mixer, maximum_mixer_inputs,
                                          maximum_mixer_outputs,
                                          validation_error))
            throw std::invalid_argument("invalid routing scene: " +
                                        validation_error);
        for (std::size_t j = 0; j < i; ++j)
            if (project.routing_scenes[j].id == scene.id)
                throw std::invalid_argument("duplicate routing scene ID");
    }
    if (project.active_routing_scene &&
        std::none_of(project.routing_scenes.begin(),
                     project.routing_scenes.end(), [&](const auto &scene) {
                         return scene.id == project.active_routing_scene;
                     }))
        throw std::invalid_argument("active routing scene does not exist");
    ModulationEngine modulation_validation;
    if (!modulation_validation.configure(project.modulation))
        throw std::invalid_argument("invalid project modulation configuration");
    if (!validate_modulation_owners(project.modulation, project.timeline,
                                    project.mixer,
                                    validation_error))
        throw std::invalid_argument("invalid project modulation owners: " +
                                    validation_error);
    PerformanceMacroService macro_validation;
    if (!macro_validation.replace(project.performance_macros, project.timeline,
                                  project.mixer, validation_error))
        throw std::invalid_argument("invalid performance macros: " +
                                    validation_error);
    ModulationConfiguration macro_modulation;
    if (!macro_validation.compile(project.modulation, macro_modulation,
                                  validation_error))
        throw std::invalid_argument("invalid macro modulation graph: " +
                                    validation_error);
    for (std::size_t i = 0; i < project.modulation.route_count; ++i)
        if (project.modulation.routes[i].destination_kind ==
                ModulationDestinationKind::parameter &&
            !valid_legacy_parameter(
                project.modulation.routes[i].destination.target,
                project.modulation.routes[i].destination.parameter))
            throw std::invalid_argument("invalid modulation destination");
    std::filesystem::create_directories(directory / "generations");
    const auto generation = generation_name();
    const auto temporary = directory / "generations" / (generation + ".tmp");
    const auto committed = directory / "generations" / generation;
    std::filesystem::create_directory(temporary);
    try {
        save_timeline(project.timeline, temporary / "timeline.ngt");
        auto samples = project.samples;
        for (auto &[slot, path] : samples.sample_paths) {
            (void)slot;
            if (!path.is_absolute())
                continue;
            const auto relative = path.lexically_normal().lexically_relative(
                directory.lexically_normal());
            if (!relative.empty() && *relative.begin() != "..")
                path = relative;
        }
        save_slice_project(samples, temporary / "samples.ngs");
        std::ostringstream parameters;
        parameters << "NEUROGENIC_PARAMETERS 1\n" << std::setprecision(17);
        for (const auto &[id, value] : project.parameters)
            parameters << std::quoted(id) << ' ' << value << '\n';
        checked_write(temporary / "parameters.ngp", parameters.str());
        checked_write(temporary / "mixer.ngm", serialize_mixer(project.mixer));
        std::ostringstream scenes;
        scenes << "NEUROGENIC_ROUTING_SCENES 1\nactive "
               << project.active_routing_scene << "\ncount "
               << project.routing_scenes.size() << '\n';
        for (const auto &scene : project.routing_scenes) {
            const auto file = "scene-" + std::to_string(scene.id) + ".ngm";
            scenes << scene.id << ' ' << std::quoted(scene.name) << ' '
                   << std::quoted(file) << '\n';
            checked_write(temporary / file, serialize_mixer(scene.mixer));
        }
        checked_write(temporary / "scenes.ngr", scenes.str());
        checked_write(temporary / "modulation.ngm",
                      serialize_modulation(project.modulation));
        checked_write(temporary / "macros.ngm",
                      serialize_macros(project.performance_macros));
        checked_write(temporary / "midi.ngm", serialize_midi(project.midi));
        std::filesystem::rename(temporary, committed);
        std::ostringstream manifest;
        manifest << "NEUROGENIC_PROJECT " << Project::format_version << '\n'
                 << "name " << std::quoted(project.name) << '\n'
                 << "generation " << std::quoted(generation) << '\n';
        checked_write(directory / "manifest.tmp", manifest.str());
        std::filesystem::rename(directory / "manifest.tmp",
                                directory / "manifest.ngp");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        throw;
    }
    if (metrics) {
        metrics->elapsed_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin)
                .count());
        metrics->metadata_bytes =
            std::filesystem::file_size(directory / "manifest.ngp") +
            std::filesystem::file_size(committed / "timeline.ngt") +
            std::filesystem::file_size(committed / "samples.ngs") +
            std::filesystem::file_size(committed / "parameters.ngp") +
            std::filesystem::file_size(committed / "mixer.ngm") +
            std::filesystem::file_size(committed / "scenes.ngr") +
            std::filesystem::file_size(committed / "modulation.ngm") +
            std::filesystem::file_size(committed / "macros.ngm");
    }
}

Project load_project(const std::filesystem::path &directory,
                     ProjectIoMetrics *metrics) {
    const auto begin = std::chrono::steady_clock::now();
    const auto manifest_path = directory / "manifest.ngp";
    bounded(manifest_path);
    std::ifstream manifest(manifest_path);
    std::string magic, kind, name, generation;
    unsigned version{};
    if (!(manifest >> magic >> version) || magic != "NEUROGENIC_PROJECT" ||
        version == 0 || version > Project::format_version ||
        !(manifest >> kind >> std::quoted(name)) || kind != "name" ||
        !(manifest >> kind >> std::quoted(generation)) ||
        kind != "generation" || generation.empty() ||
        generation.find('/') != std::string::npos || generation == "..")
        throw std::runtime_error("unsupported or malformed project manifest");
    const auto files = directory / "generations" / generation;
    bounded(files / "timeline.ngt");
    bounded(files / "samples.ngs");
    bounded(files / "parameters.ngp");
    Project result;
    result.name = std::move(name);
    result.timeline = load_timeline(files / "timeline.ngt");
    result.samples = load_slice_project(files / "samples.ngs", false);
    if (std::filesystem::exists(files / "mixer.ngm"))
        result.mixer = load_mixer(files / "mixer.ngm");
    if (std::filesystem::exists(files / "scenes.ngr")) {
        bounded(files / "scenes.ngr");
        std::ifstream scenes(files / "scenes.ngr");
        std::size_t count{};
        if (!(scenes >> magic >> version) ||
            magic != "NEUROGENIC_ROUTING_SCENES" || version != 1 ||
            !(scenes >> kind >> result.active_routing_scene) ||
            kind != "active" || !(scenes >> kind >> count) || kind != "count" ||
            count > 32)
            throw std::runtime_error("unsupported or malformed routing scenes");
        for (std::size_t i = 0; i < count; ++i) {
            RoutingScene scene;
            std::string file;
            if (!(scenes >> scene.id >> std::quoted(scene.name) >>
                  std::quoted(file)) ||
                !scene.id || scene.name.empty() || file.empty() ||
                file.find('/') != std::string::npos ||
                file.find("..") != std::string::npos)
                throw std::runtime_error("malformed routing scene entry");
            scene.mixer = load_mixer(files / file);
            result.routing_scenes.push_back(std::move(scene));
        }
        if (result.active_routing_scene &&
            std::none_of(result.routing_scenes.begin(),
                         result.routing_scenes.end(), [&](const auto &scene) {
                             return scene.id == result.active_routing_scene;
                         }))
            throw std::runtime_error("active routing scene was not found");
    }
    if (std::filesystem::exists(files / "modulation.ngm"))
        result.modulation = load_modulation(files / "modulation.ngm");
    if (std::filesystem::exists(files / "macros.ngm"))
        result.performance_macros = load_macros(files / "macros.ngm");
    std::string modulation_owner_error;
    if (!validate_modulation_owners(result.modulation, result.timeline,
                                    result.mixer,
                                    modulation_owner_error))
        throw std::runtime_error("invalid project modulation owners: " +
                                 modulation_owner_error);
    PerformanceMacroService macro_validation;
    if (!macro_validation.replace(result.performance_macros, result.timeline,
                                  result.mixer, modulation_owner_error))
        throw std::runtime_error("invalid performance macros: " +
                                 modulation_owner_error);
    if (std::filesystem::exists(files / "midi.ngm"))
        result.midi = load_midi(files / "midi.ngm");
    for (auto &[slot, path] : result.samples.sample_paths) {
        path = resolve_reference(directory, path);
        if (!std::filesystem::exists(path))
            result.missing_assets.push_back(path);
    }
    std::ifstream parameters(files / "parameters.ngp");
    if (!(parameters >> magic >> version) || magic != "NEUROGENIC_PARAMETERS" ||
        version != 1)
        throw std::runtime_error("unsupported parameter metadata");
    while (parameters >> std::quoted(name)) {
        double value{};
        if (!(parameters >> value) || !std::isfinite(value))
            throw std::runtime_error("invalid project parameter");
        result.parameters.emplace(name, value);
        if (result.parameters.size() > 4096)
            throw std::runtime_error("too many project parameters");
    }
    if (metrics) {
        metrics->elapsed_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin)
                .count());
        metrics->metadata_bytes =
            std::filesystem::file_size(manifest_path) +
            std::filesystem::file_size(files / "timeline.ngt") +
            std::filesystem::file_size(files / "samples.ngs") +
            std::filesystem::file_size(files / "parameters.ngp") +
            (std::filesystem::exists(files / "mixer.ngm")
                 ? std::filesystem::file_size(files / "mixer.ngm")
                 : 0) +
            (std::filesystem::exists(files / "scenes.ngr")
                 ? std::filesystem::file_size(files / "scenes.ngr")
                 : 0) +
            (std::filesystem::exists(files / "modulation.ngm")
                 ? std::filesystem::file_size(files / "modulation.ngm")
                 : 0);
    }
    return result;
}

bool relink_project_sample(Project &project,
                           const std::filesystem::path &missing,
                           const std::filesystem::path &replacement,
                           std::string &error) {
    if (!std::filesystem::is_regular_file(replacement)) {
        error = "replacement sample does not exist";
        return false;
    }
    for (auto &[slot, path] : project.samples.sample_paths)
        if (path == missing) {
            path = replacement;
            std::erase(project.missing_assets, missing);
            error.clear();
            return true;
        }
    error = "missing sample reference was not found";
    return false;
}

bool relink_project_assets(
    Project &project, const std::vector<ProjectAssetRelink> &exact,
    const std::vector<ProjectDirectoryRelink> &directories,
    std::uint32_t target_sample_rate,
    std::vector<std::filesystem::path> &unresolved, std::string &error) {
    auto candidate = project;
    unresolved.clear();
    const auto beneath = [](const std::filesystem::path &path,
                            const std::filesystem::path &prefix,
                            std::filesystem::path &suffix) {
        const auto normalized = path.lexically_normal();
        const auto base = prefix.lexically_normal();
        auto value = normalized.begin();
        auto root = base.begin();
        for (; root != base.end(); ++root, ++value)
            if (value == normalized.end() || *value != *root) return false;
        suffix.clear();
        for (; value != normalized.end(); ++value) suffix /= *value;
        return true;
    };
    for (auto &[slot, path] : candidate.samples.sample_paths) {
        (void)slot;
        const auto original = path.lexically_normal();
        std::optional<std::filesystem::path> replacement;
        for (const auto &mapping : exact)
            if (original == mapping.missing.lexically_normal()) {
                replacement = mapping.replacement;
                break;
            }
        if (!replacement)
            for (const auto &mapping : directories) {
                std::filesystem::path suffix;
                if (beneath(original, mapping.missing_prefix, suffix)) {
                    replacement = mapping.replacement_prefix / suffix;
                    break;
                }
            }
        if (replacement) {
            const auto normalized = replacement->lexically_normal();
            if (!normalized.is_absolute() && !normalized.empty() &&
                *normalized.begin() == "..") {
                error = "unsafe project relink target: " + normalized.string();
                return false;
            }
            try {
                (void)load_wav(normalized, target_sample_rate);
            } catch (const std::exception &exception) {
                error = "invalid replacement " + normalized.string() + ": " +
                        exception.what();
                return false;
            }
            path = normalized;
        }
        if (!std::filesystem::exists(path)) unresolved.push_back(path);
    }
    if (!unresolved.empty()) {
        error = "project recovery has unresolved sample dependencies";
        return false;
    }
    candidate.missing_assets.clear();
    project = std::move(candidate);
    error.clear();
    return true;
}
} // namespace neurogenic
