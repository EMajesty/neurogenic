#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace neurogenic {

using EffectInstanceId = std::uint32_t;
inline constexpr std::size_t maximum_effect_instances = 32;
inline constexpr std::size_t maximum_effects_per_chain = 4;
inline constexpr std::size_t maximum_effect_parameters = 4;
inline constexpr std::size_t maximum_effect_block = 1024;
inline constexpr std::size_t maximum_effect_groups = 8;

enum class EffectProcessorType : std::uint8_t {
    gain = 1,
    stereo_delay,
    low_pass_filter,
    saturation
};
enum class EffectParameterKind : std::uint8_t { continuous, discrete };
struct EffectParameterDescriptor {
    std::uint32_t id{};
    std::string_view stable_id, name, unit;
    float minimum{}, maximum{}, default_value{};
    EffectParameterKind kind{EffectParameterKind::continuous};
    bool automatable{true}, modulatable{true};
};
[[nodiscard]] std::string_view
    effect_processor_type_id(EffectProcessorType) noexcept;
[[nodiscard]] std::span<const EffectParameterDescriptor>
    effect_parameter_descriptors(EffectProcessorType) noexcept;

struct EffectParameterValue {
    std::uint32_t id{};
    float value{};
};
struct EffectInstanceConfiguration {
    EffectInstanceId id{};
    EffectProcessorType type{EffectProcessorType::gain};
    std::array<EffectParameterValue, maximum_effect_parameters> parameters{};
    std::size_t parameter_count{};
    float wet{1.0F};
    std::uint8_t input_channels{2}, output_channels{2};
    bool enabled{true}, bypass{};
};
struct EffectChainConfiguration {
    std::array<EffectInstanceId, maximum_effects_per_chain> instances{};
    std::size_t count{};
    bool bypass{};
};
struct EffectRackConfiguration {
    std::array<EffectInstanceConfiguration, maximum_effect_instances>
        instances{};
    std::size_t instance_count{};
    std::array<EffectChainConfiguration, 32> tracks{};
    std::array<EffectChainConfiguration, 2> aux{};
    EffectChainConfiguration master;
    std::array<EffectChainConfiguration, maximum_effect_groups> groups{};
};

[[nodiscard]] bool validate_effect_rack(const EffectRackConfiguration &,
                                        std::string &error) noexcept;

class NativeEffectRack {
  public:
    explicit NativeEffectRack(std::uint32_t sample_rate = 48000);
    ~NativeEffectRack();
    NativeEffectRack(const NativeEffectRack &) = delete;
    NativeEffectRack &operator=(const NativeEffectRack &) = delete;
    void activate(const EffectRackConfiguration &) noexcept;
    void reset_state() noexcept;
    [[nodiscard]] bool set_parameter(EffectInstanceId, std::uint32_t,
                                     float) noexcept;
    void process(const EffectChainConfiguration &, std::span<float>,
                 std::span<float>) noexcept;
    [[nodiscard]] const EffectRackConfiguration &
    configuration() const noexcept {
        return configuration_;
    }

  private:
    struct Runtime;
    Runtime *runtime_{};
    EffectRackConfiguration configuration_{};
};
} // namespace neurogenic
