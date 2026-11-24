#pragma once
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/dsp/effect_chain.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neurogenic {

enum class PresetType : std::uint8_t {
    synth_patch = 1,
    sample_instrument,
    fx_chain,
    track_sound
};
enum class PresetImportMode : std::uint8_t {
    replace_sound = 1,
    insert_fx_chain,
    merge_modulation,
    preview
};

struct PresetMetadata {
    static constexpr unsigned schema_version = 1;
    std::string id, name, author, description;
    PresetType type{PresetType::synth_patch};
    std::vector<std::string> tags, required_capabilities,
        processor_types, modulator_types;
};

struct PresetEffectChain {
    std::vector<EffectInstanceConfiguration> instances;
    EffectChainConfiguration chain;
};

struct SoundPreset {
    PresetMetadata metadata;
    std::map<std::string, double> synth_parameters;
    std::optional<SliceProject> samples;
    std::optional<PresetEffectChain> effects;
    ModulationConfiguration modulation;
    float track_gain{1.0F}, track_pan{};
};

struct PresetValidation {
    bool valid{};
    std::vector<std::string> errors, missing_assets;
};
struct PresetAssetRelink {
    std::filesystem::path missing, replacement;
};
struct PresetDirectoryRelink {
    std::filesystem::path missing_prefix, replacement_prefix;
};
struct PortablePresetExportResult {
    std::uintmax_t package_bytes{};
    std::size_t copied_assets{};
    std::vector<std::string> missing_assets;
};

struct PresetImportResult {
    EffectRackConfiguration effects;
    ModulationConfiguration modulation;
    std::map<EffectInstanceId, EffectInstanceId> effect_ids;
    std::map<ModulatorInstanceId, ModulatorInstanceId> modulator_ids;
    std::map<ModulationRouteId, ModulationRouteId> route_ids;
    EffectChainConfiguration chain;
};

[[nodiscard]] const char *preset_type_name(PresetType) noexcept;
[[nodiscard]] std::optional<PresetType> parse_preset_type(std::string_view);
[[nodiscard]] PresetValidation validate_preset(const SoundPreset &,
                                               bool require_assets = true);
[[nodiscard]] bool prepare_preset_import(
    const SoundPreset &, const EffectRackConfiguration &,
    const ModulationConfiguration &, PresetImportResult &,
    std::string &error);
[[nodiscard]] PresetValidation relink_preset_assets(
    SoundPreset &, const std::vector<PresetAssetRelink> &,
    const std::vector<PresetDirectoryRelink> & = {},
    std::uint32_t target_sample_rate = 0);
[[nodiscard]] const char *preset_import_mode_name(PresetImportMode) noexcept;
[[nodiscard]] std::optional<PresetImportMode>
parse_preset_import_mode(std::string_view) noexcept;

class PresetLibrary {
  public:
    explicit PresetLibrary(std::filesystem::path root)
        : root_(std::move(root)) {}
    [[nodiscard]] const std::filesystem::path &root() const noexcept {
        return root_;
    }
    void save(const SoundPreset &);
    [[nodiscard]] SoundPreset load(std::string_view id,
                                   bool require_assets = true) const;
    [[nodiscard]] std::vector<PresetMetadata>
    enumerate(std::optional<PresetType> type = std::nullopt) const;
    [[nodiscard]] bool contains(std::string_view id) const;
    void erase(std::string_view id);
    void duplicate(std::string_view source_id, std::string new_id,
                   std::string new_name);
    void export_package(std::string_view id,
                        const std::filesystem::path &destination) const;
    [[nodiscard]] PortablePresetExportResult export_self_contained(
        std::string_view id, const std::filesystem::path &destination) const;
    [[nodiscard]] PresetValidation repair_assets(
        std::string_view id, const std::vector<PresetAssetRelink> &,
        const std::vector<PresetDirectoryRelink> & = {},
        std::uint32_t target_sample_rate = 0);
    void import_package(const std::filesystem::path &source,
                        std::string_view id);

  private:
    [[nodiscard]] std::filesystem::path preset_path(std::string_view) const;
    std::filesystem::path root_;
};

} // namespace neurogenic
