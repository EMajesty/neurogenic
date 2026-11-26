#include "neurogenic/dsp/effect_chain.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace neurogenic {
namespace {
constexpr std::array gain_parameters{
    EffectParameterDescriptor{1, "gain", "Gain", "x", 0, 4, 1}};
constexpr std::array delay_parameters{
    EffectParameterDescriptor{1, "time", "Time", "s", 0.001F, 2, 0.25F},
    EffectParameterDescriptor{2, "feedback", "Feedback", "", 0, 0.95F, 0.3F},
    EffectParameterDescriptor{3, "mix", "Mix", "", 0, 1, 0.25F}};
constexpr std::array filter_parameters{
    EffectParameterDescriptor{1, "cutoff", "Cutoff", "Hz", 20, 20000, 8000},
    EffectParameterDescriptor{2, "resonance", "Resonance", "", 0, 0.95F, 0.1F}};
constexpr std::array saturation_parameters{
    EffectParameterDescriptor{1, "drive", "Drive", "x", 1, 20, 1},
    EffectParameterDescriptor{2, "mix", "Mix", "", 0, 1, 1}};
bool finite(float value) noexcept { return std::isfinite(value); }
float default_parameter(EffectProcessorType type, std::uint32_t id) noexcept {
    for (const auto &descriptor : effect_parameter_descriptors(type))
        if (descriptor.id == id)
            return descriptor.default_value;
    return 0;
}
} // namespace

std::string_view effect_processor_type_id(EffectProcessorType type) noexcept {
    switch (type) {
    case EffectProcessorType::gain:
        return "native.gain.v1";
    case EffectProcessorType::stereo_delay:
        return "native.stereo-delay.v1";
    case EffectProcessorType::low_pass_filter:
        return "native.low-pass.v1";
    case EffectProcessorType::saturation:
        return "native.saturation.v1";
    }
    return {};
}
std::span<const EffectParameterDescriptor>
effect_parameter_descriptors(EffectProcessorType type) noexcept {
    switch (type) {
    case EffectProcessorType::gain:
        return gain_parameters;
    case EffectProcessorType::stereo_delay:
        return delay_parameters;
    case EffectProcessorType::low_pass_filter:
        return filter_parameters;
    case EffectProcessorType::saturation:
        return saturation_parameters;
    }
    return {};
}
bool validate_effect_rack(const EffectRackConfiguration &rack,
                          std::string &error) noexcept {
    if (rack.instance_count > rack.instances.size()) {
        error = "effect instance capacity exceeded";
        return false;
    }
    for (std::size_t i = 0; i < rack.instance_count; ++i) {
        const auto &instance = rack.instances[i];
        if (!instance.id || effect_processor_type_id(instance.type).empty() ||
            instance.parameter_count > instance.parameters.size() ||
            !finite(instance.wet) || instance.wet < 0 || instance.wet > 1 ||
            instance.input_channels == 0 || instance.input_channels > 2 ||
            instance.output_channels == 0 || instance.output_channels > 2) {
            error = "invalid effect instance";
            return false;
        }
        for (std::size_t other = 0; other < i; ++other)
            if (rack.instances[other].id == instance.id) {
                error = "duplicate effect instance ID";
                return false;
            }
        for (std::size_t p = 0; p < instance.parameter_count; ++p) {
            const auto &parameter = instance.parameters[p];
            const auto descriptors =
                effect_parameter_descriptors(instance.type);
            const auto found = std::find_if(
                descriptors.begin(), descriptors.end(),
                [&](const auto &value) { return value.id == parameter.id; });
            if (found == descriptors.end() || !finite(parameter.value) ||
                parameter.value < found->minimum ||
                parameter.value > found->maximum) {
                error = "invalid effect parameter";
                return false;
            }
            for (std::size_t q = 0; q < p; ++q)
                if (instance.parameters[q].id == parameter.id) {
                    error = "duplicate effect parameter";
                    return false;
                }
        }
    }
    const auto validate_chain = [&](const EffectChainConfiguration &chain) {
        if (chain.count > chain.instances.size())
            return false;
        for (std::size_t i = 0; i < chain.count; ++i) {
            if (std::find(chain.instances.begin(), chain.instances.begin() + i,
                          chain.instances[i]) != chain.instances.begin() + i)
                return false;
            if (std::none_of(rack.instances.begin(),
                             rack.instances.begin() + rack.instance_count,
                             [&](const auto &value) {
                                 return value.id == chain.instances[i];
                             }))
                return false;
        }
        return true;
    };
    for (const auto &chain : rack.tracks)
        if (!validate_chain(chain)) {
            error = "invalid track effect chain";
            return false;
        }
    for (const auto &chain : rack.aux)
        if (!validate_chain(chain)) {
            error = "invalid aux effect chain";
            return false;
        }
    for (const auto &chain : rack.groups)
        if (!validate_chain(chain)) {
            error = "invalid group effect chain";
            return false;
        }
    if (!validate_chain(rack.master)) {
        error = "invalid master effect chain";
        return false;
    }
    error.clear();
    return true;
}

struct NativeEffectRack::Runtime {
    struct Slot {
        EffectInstanceId id{};
        EffectProcessorType type{EffectProcessorType::gain};
        std::array<float, maximum_effect_parameters> parameter{};
        float filter_l{}, filter_r{}, filter_band_l{}, filter_band_r{};
        std::vector<float> delay_l, delay_r;
        std::vector<std::uint32_t> delay_generation;
        std::size_t cursor{};
        std::uint32_t generation{1};
        explicit Slot(std::size_t delay_frames)
            : delay_l(delay_frames), delay_r(delay_frames),
              delay_generation(delay_frames) {}
        void reset(EffectInstanceId next_id,
                   EffectProcessorType next_type) noexcept {
            id = next_id;
            type = next_type;
            filter_l = filter_r = 0;
            filter_band_l = filter_band_r = 0;
            cursor = 0;
            if (++generation == 0) {
                std::fill(delay_generation.begin(), delay_generation.end(), 0);
                generation = 1;
            }
            parameter.fill(0);
            const auto descriptors = effect_parameter_descriptors(type);
            for (std::size_t i = 0; i < descriptors.size(); ++i)
                parameter[i] = descriptors[i].default_value;
        }
        void clear_state() noexcept {
            filter_l = filter_r = 0;
            filter_band_l = filter_band_r = 0;
            cursor = 0;
            if (++generation == 0) {
                std::fill(delay_generation.begin(), delay_generation.end(), 0);
                generation = 1;
            }
        }
    };
    std::uint32_t rate{};
    std::array<std::unique_ptr<Slot>, maximum_effect_instances> slots{};
    std::array<float, maximum_effect_block> dry_l{}, dry_r{};
    explicit Runtime(std::uint32_t sample_rate)
        : rate(std::max(1U, sample_rate)) {
        const auto delay_frames = static_cast<std::size_t>(rate) * 2 + 1;
        for (auto &slot : slots)
            slot = std::make_unique<Slot>(delay_frames);
    }
};

NativeEffectRack::NativeEffectRack(std::uint32_t rate)
    : runtime_(new Runtime(rate)) {}
NativeEffectRack::~NativeEffectRack() { delete runtime_; }
void NativeEffectRack::reset_state() noexcept {
    for (std::size_t i = 0; i < configuration_.instance_count; ++i)
        runtime_->slots[i]->clear_state();
}
void NativeEffectRack::activate(
    const EffectRackConfiguration &configuration) noexcept {
    configuration_ = configuration;
    for (std::size_t i = 0; i < configuration_.instance_count; ++i) {
        const auto &instance = configuration_.instances[i];
        auto &slot = *runtime_->slots[i];
        if (slot.id != instance.id || slot.type != instance.type)
            slot.reset(instance.id, instance.type);
        const auto descriptors = effect_parameter_descriptors(instance.type);
        for (std::size_t p = 0; p < descriptors.size(); ++p)
            slot.parameter[p] =
                default_parameter(instance.type, descriptors[p].id);
        for (std::size_t p = 0; p < instance.parameter_count; ++p)
            (void)set_parameter(instance.id, instance.parameters[p].id,
                                instance.parameters[p].value);
    }
}
bool NativeEffectRack::set_parameter(EffectInstanceId id,
                                     std::uint32_t parameter,
                                     float value) noexcept {
    for (std::size_t i = 0; i < configuration_.instance_count; ++i) {
        if (configuration_.instances[i].id != id)
            continue;
        const auto descriptors =
            effect_parameter_descriptors(configuration_.instances[i].type);
        for (std::size_t p = 0; p < descriptors.size(); ++p)
            if (descriptors[p].id == parameter && finite(value)) {
                runtime_->slots[i]->parameter[p] = std::clamp(
                    value, descriptors[p].minimum, descriptors[p].maximum);
                return true;
            }
    }
    return false;
}
void NativeEffectRack::process(const EffectChainConfiguration &chain,
                               std::span<float> left,
                               std::span<float> right) noexcept {
    const auto frames =
        std::min({left.size(), right.size(), maximum_effect_block});
    if (chain.bypass || frames == 0)
        return;
    for (std::size_t entry = 0; entry < chain.count; ++entry) {
        const auto found = std::find_if(
            configuration_.instances.begin(),
            configuration_.instances.begin() + configuration_.instance_count,
            [&](const auto &value) {
                return value.id == chain.instances[entry];
            });
        if (found == configuration_.instances.begin() +
                         configuration_.instance_count ||
            !found->enabled || found->bypass)
            continue;
        const auto index =
            static_cast<std::size_t>(found - configuration_.instances.begin());
        auto &slot = *runtime_->slots[index];
        std::copy_n(left.begin(), frames, runtime_->dry_l.begin());
        std::copy_n(right.begin(), frames, runtime_->dry_r.begin());
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (slot.type == EffectProcessorType::gain) {
                left[frame] *= slot.parameter[0];
                right[frame] *= slot.parameter[0];
            } else if (slot.type == EffectProcessorType::stereo_delay) {
                const auto delay = std::clamp<std::size_t>(
                    static_cast<std::size_t>(slot.parameter[0] *
                                             runtime_->rate),
                    1, slot.delay_l.size() - 1);
                const auto read = (slot.cursor + slot.delay_l.size() - delay) %
                                  slot.delay_l.size();
                const auto valid =
                    slot.delay_generation[read] == slot.generation;
                const auto dl = valid ? slot.delay_l[read] : 0.0F;
                const auto dr = valid ? slot.delay_r[read] : 0.0F;
                slot.delay_l[slot.cursor] =
                    left[frame] + dr * slot.parameter[1];
                slot.delay_r[slot.cursor] =
                    right[frame] + dl * slot.parameter[1];
                slot.delay_generation[slot.cursor] = slot.generation;
                left[frame] = left[frame] * (1 - slot.parameter[2]) +
                              dl * slot.parameter[2];
                right[frame] = right[frame] * (1 - slot.parameter[2]) +
                               dr * slot.parameter[2];
                slot.cursor = (slot.cursor + 1) % slot.delay_l.size();
            } else if (slot.type == EffectProcessorType::low_pass_filter) {
                const auto coefficient = std::clamp(
                    2.0F * std::sin(3.14159265358979323846F *
                                    std::min(slot.parameter[0],
                                             runtime_->rate * 0.45F) /
                                    runtime_->rate),
                    0.0001F, 0.99F);
                const auto damping = 2.0F - 1.9F * slot.parameter[1];
                slot.filter_band_l +=
                    coefficient * (left[frame] - slot.filter_l -
                                   damping * slot.filter_band_l);
                slot.filter_l += coefficient * slot.filter_band_l;
                slot.filter_band_r +=
                    coefficient * (right[frame] - slot.filter_r -
                                   damping * slot.filter_band_r);
                slot.filter_r += coefficient * slot.filter_band_r;
                left[frame] = slot.filter_l;
                right[frame] = slot.filter_r;
            } else {
                const auto normal =
                    std::max(0.0001F, std::tanh(slot.parameter[0]));
                const auto wet_l =
                    std::tanh(left[frame] * slot.parameter[0]) / normal;
                const auto wet_r =
                    std::tanh(right[frame] * slot.parameter[0]) / normal;
                left[frame] += (wet_l - left[frame]) * slot.parameter[1];
                right[frame] += (wet_r - right[frame]) * slot.parameter[1];
            }
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            left[frame] = runtime_->dry_l[frame] +
                          (left[frame] - runtime_->dry_l[frame]) * found->wet;
            right[frame] = runtime_->dry_r[frame] +
                           (right[frame] - runtime_->dry_r[frame]) * found->wet;
        }
    }
}
} // namespace neurogenic
