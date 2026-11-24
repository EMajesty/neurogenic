#include "neurogenic/project/preset.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/audio/sample.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace neurogenic {
namespace {
constexpr std::uintmax_t maximum_preset_metadata = 16U * 1024U * 1024U;

bool safe_id(std::string_view value) {
    return !value.empty() && value.size() <= 96 && value != "." &&
           value != ".." &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isalnum(c) || c == '-' || c == '_';
           });
}
void bounded(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_preset_metadata)
        throw std::runtime_error("preset metadata is missing or oversized: " +
                                 path.string());
}
void write_checked(const std::filesystem::path &path,
                   const std::string &contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << contents) || !output.flush())
        throw std::runtime_error("cannot write preset metadata: " +
                                 path.string());
}
std::string generation_name() {
    static std::atomic<std::uint64_t> sequence{};
    return "generation-" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) +
           "-" + std::to_string(sequence.fetch_add(1));
}
std::uint64_t stable_path_hash(std::string_view value) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}
bool below_prefix(const std::filesystem::path &path,
                  const std::filesystem::path &prefix,
                  std::filesystem::path &suffix) {
    const auto normalized_path = path.lexically_normal();
    const auto normalized_prefix = prefix.lexically_normal();
    auto p = normalized_path.begin();
    for (auto base = normalized_prefix.begin(); base != normalized_prefix.end();
         ++base, ++p)
        if (p == normalized_path.end() || *p != *base) return false;
    suffix.clear();
    for (; p != normalized_path.end(); ++p) suffix /= *p;
    return true;
}
void strings(std::ostringstream &out, std::string_view label,
             const std::vector<std::string> &values) {
    out << label << ' ' << values.size();
    for (const auto &value : values) out << ' ' << std::quoted(value);
    out << '\n';
}
std::vector<std::string> read_strings(std::istream &input,
                                      std::string_view expected) {
    std::string label;
    std::size_t count{};
    if (!(input >> label >> count) || label != expected || count > 256)
        throw std::runtime_error("malformed preset metadata list");
    std::vector<std::string> result(count);
    for (auto &value : result)
        if (!(input >> std::quoted(value)) || value.size() > 1024)
            throw std::runtime_error("malformed preset metadata value");
    return result;
}
std::string serialize_effects(const PresetEffectChain &value) {
    std::ostringstream out;
    out << "NEUROGENIC_PRESET_EFFECTS 1\n" << std::setprecision(9)
        << "instances " << value.instances.size() << '\n';
    for (const auto &instance : value.instances) {
        out << "instance " << instance.id << ' '
            << static_cast<unsigned>(instance.type) << ' ' << instance.wet
            << ' ' << instance.enabled << ' ' << instance.bypass << ' '
            << static_cast<unsigned>(instance.input_channels) << ' '
            << static_cast<unsigned>(instance.output_channels) << ' '
            << instance.parameter_count;
        for (std::size_t i = 0; i < instance.parameter_count; ++i)
            out << ' ' << instance.parameters[i].id << ' '
                << instance.parameters[i].value;
        out << '\n';
    }
    out << "chain " << value.chain.bypass << ' ' << value.chain.count;
    for (std::size_t i = 0; i < value.chain.count; ++i)
        out << ' ' << value.chain.instances[i];
    out << '\n';
    return out.str();
}
PresetEffectChain load_effects(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{};
    std::size_t count{};
    if (!(input >> magic >> version) || magic != "NEUROGENIC_PRESET_EFFECTS" ||
        version != 1 || !(input >> label >> count) || label != "instances" ||
        count > maximum_effect_instances)
        throw std::runtime_error("unsupported or malformed effect preset");
    PresetEffectChain result;
    std::set<EffectInstanceId> ids;
    for (std::size_t index = 0; index < count; ++index) {
        EffectInstanceConfiguration instance;
        unsigned type{}, input_channels{}, output_channels{};
        if (!(input >> label >> instance.id >> type >> instance.wet >>
              instance.enabled >> instance.bypass >> input_channels >>
              output_channels >> instance.parameter_count) ||
            label != "instance" || !instance.id || type < 1 || type > 4 ||
            !ids.insert(instance.id).second || instance.wet < 0 ||
            instance.wet > 1 || input_channels < 1 || input_channels > 2 ||
            output_channels < 1 || output_channels > 2 ||
            instance.parameter_count > instance.parameters.size())
            throw std::runtime_error("malformed effect preset instance");
        instance.type = static_cast<EffectProcessorType>(type);
        instance.input_channels = static_cast<std::uint8_t>(input_channels);
        instance.output_channels = static_cast<std::uint8_t>(output_channels);
        for (std::size_t p = 0; p < instance.parameter_count; ++p)
            if (!(input >> instance.parameters[p].id >>
                  instance.parameters[p].value) ||
                !std::isfinite(instance.parameters[p].value))
                throw std::runtime_error("malformed effect parameter");
        result.instances.push_back(instance);
    }
    if (!(input >> label >> result.chain.bypass >> result.chain.count) ||
        label != "chain" ||
        result.chain.count > result.chain.instances.size())
        throw std::runtime_error("malformed effect preset chain");
    for (std::size_t i = 0; i < result.chain.count; ++i)
        if (!(input >> result.chain.instances[i]) ||
            !ids.contains(result.chain.instances[i]))
            throw std::runtime_error("effect chain references missing instance");
    return result;
}
std::string serialize_parameters(const std::map<std::string, double> &values) {
    std::ostringstream out;
    out << "NEUROGENIC_PRESET_PARAMETERS 1\n" << std::setprecision(17);
    for (const auto &[id, value] : values)
        out << std::quoted(id) << ' ' << value << '\n';
    return out.str();
}
std::map<std::string, double> load_parameters(
    const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, id;
    unsigned version{};
    if (!(input >> magic >> version) ||
        magic != "NEUROGENIC_PRESET_PARAMETERS" || version != 1)
        throw std::runtime_error("unsupported preset parameters");
    std::map<std::string, double> result;
    while (input >> std::quoted(id)) {
        double value{};
        if (id.empty() || id.size() > 256 || !(input >> value) ||
            !std::isfinite(value) || !result.emplace(id, value).second ||
            result.size() > 4096)
            throw std::runtime_error("malformed preset parameter");
    }
    return result;
}
} // namespace

const char *preset_type_name(PresetType type) noexcept {
    switch (type) {
    case PresetType::synth_patch: return "synth";
    case PresetType::sample_instrument: return "sample";
    case PresetType::fx_chain: return "fx-chain";
    case PresetType::track_sound: return "track-sound";
    }
    return "unknown";
}
std::optional<PresetType> parse_preset_type(std::string_view value) {
    if (value == "synth") return PresetType::synth_patch;
    if (value == "sample") return PresetType::sample_instrument;
    if (value == "fx-chain") return PresetType::fx_chain;
    if (value == "track-sound") return PresetType::track_sound;
    return std::nullopt;
}
const char *preset_import_mode_name(PresetImportMode mode) noexcept {
    switch (mode) {
    case PresetImportMode::replace_sound: return "replace";
    case PresetImportMode::insert_fx_chain: return "insert-fx";
    case PresetImportMode::merge_modulation: return "merge-modulation";
    case PresetImportMode::preview: return "preview";
    }
    return "unknown";
}
std::optional<PresetImportMode> parse_preset_import_mode(
    std::string_view value) noexcept {
    if (value == "replace") return PresetImportMode::replace_sound;
    if (value == "insert-fx") return PresetImportMode::insert_fx_chain;
    if (value == "merge-modulation")
        return PresetImportMode::merge_modulation;
    if (value == "preview") return PresetImportMode::preview;
    return std::nullopt;
}

PresetValidation validate_preset(const SoundPreset &preset,
                                 bool require_assets) {
    PresetValidation result;
    if (!safe_id(preset.metadata.id)) result.errors.push_back("invalid preset ID");
    if (preset.metadata.name.empty() || preset.metadata.name.size() > 256)
        result.errors.push_back("invalid preset name");
    if (preset.synth_parameters.size() > 4096)
        result.errors.push_back("too many synth parameters");
    for (const auto &[id, value] : preset.synth_parameters)
        if (id.empty() || !std::isfinite(value))
            result.errors.push_back("invalid synth parameter");
    static const std::set<std::string_view> capabilities{
        "native-synth", "sample-instrument", "native-fx",
        "modulation-graph", "voice-modulation"};
    for (const auto &capability : preset.metadata.required_capabilities)
        if (!capabilities.contains(capability))
            result.errors.push_back("unsupported capability: " + capability);
    for (const auto &processor : preset.metadata.processor_types) {
        bool supported{};
        for (const auto type : {EffectProcessorType::gain,
                                EffectProcessorType::stereo_delay,
                                EffectProcessorType::low_pass_filter,
                                EffectProcessorType::saturation})
            supported = supported || effect_processor_type_id(type) == processor;
        if (!supported)
            result.errors.push_back("unsupported processor type: " + processor);
    }
    for (const auto &modulator : preset.metadata.modulator_types) {
        const auto supported = std::any_of(
            modulator_type_registry().begin(), modulator_type_registry().end(),
            [&](const auto &type) { return type.id == modulator; });
        if (!supported)
            result.errors.push_back("unsupported modulator type: " + modulator);
    }
    if (preset.effects) {
        EffectRackConfiguration rack;
        if (preset.effects->instances.size() > rack.instances.size())
            result.errors.push_back("effect instance capacity exceeded");
        else {
            rack.instance_count = preset.effects->instances.size();
            std::copy(preset.effects->instances.begin(),
                      preset.effects->instances.end(), rack.instances.begin());
            rack.master = preset.effects->chain;
            std::string error;
            if (!validate_effect_rack(rack, error)) result.errors.push_back(error);
        }
    }
    ModulationGraphDiagnostics diagnostics;
    if (!validate_modulation_graph(preset.modulation, diagnostics))
        result.errors.push_back(diagnostics.message);
    if (preset.samples)
        for (const auto &[slot, path] : preset.samples->sample_paths) {
            (void)slot;
            if (!std::filesystem::exists(path)) {
                result.missing_assets.push_back(path.string());
                if (require_assets) result.errors.push_back("missing sample: " + path.string());
            }
        }
    result.valid = result.errors.empty();
    return result;
}

bool prepare_preset_import(const SoundPreset &preset,
                           const EffectRackConfiguration &base_effects,
                           const ModulationConfiguration &base_modulation,
                           PresetImportResult &result, std::string &error) {
    const auto validation = validate_preset(preset, false);
    if (!validation.valid) {
        error = validation.errors.front();
        return false;
    }
    result = {};
    result.effects = base_effects;
    result.modulation = base_modulation;
    if (preset.effects) {
        if (result.effects.instance_count + preset.effects->instances.size() >
            result.effects.instances.size()) {
            error = "effect instance capacity is exhausted";
            return false;
        }
        EffectInstanceId next{1};
        for (std::size_t i = 0; i < result.effects.instance_count; ++i)
            next = std::max(next, result.effects.instances[i].id + 1);
        for (const auto &source : preset.effects->instances) {
            while (std::any_of(result.effects.instances.begin(),
                               result.effects.instances.begin() +
                                   result.effects.instance_count,
                               [&](const auto &v) { return v.id == next; }))
                ++next;
            auto copy = source;
            result.effect_ids[source.id] = next;
            copy.id = next++;
            result.effects.instances[result.effects.instance_count++] = copy;
        }
        result.chain = preset.effects->chain;
        for (std::size_t i = 0; i < result.chain.count; ++i) {
            const auto found = result.effect_ids.find(result.chain.instances[i]);
            if (found == result.effect_ids.end()) {
                error = "effect chain remapping failed";
                return false;
            }
            result.chain.instances[i] = found->second;
        }
    }
    if (result.modulation.modulator_count + preset.modulation.modulator_count >
            result.modulation.modulators.size() ||
        result.modulation.route_count + preset.modulation.route_count >
            result.modulation.routes.size()) {
        error = "modulation capacity is exhausted";
        return false;
    }
    ModulatorInstanceId next_mod{1};
    ModulationRouteId next_route{1};
    for (std::size_t i = 0; i < result.modulation.modulator_count; ++i)
        next_mod = std::max(next_mod, result.modulation.modulators[i].id + 1);
    for (std::size_t i = 0; i < result.modulation.route_count; ++i)
        next_route = std::max(next_route, result.modulation.routes[i].id + 1);
    for (std::size_t i = 0; i < preset.modulation.modulator_count; ++i) {
        auto copy = preset.modulation.modulators[i];
        result.modulator_ids[copy.id] = next_mod;
        copy.id = next_mod++;
        result.modulation.modulators[result.modulation.modulator_count++] = copy;
    }
    for (std::size_t i = 0; i < preset.modulation.route_count; ++i) {
        auto copy = preset.modulation.routes[i];
        result.route_ids[copy.id] = next_route;
        copy.id = next_route++;
        if (copy.source_instance) {
            const auto found = result.modulator_ids.find(copy.source_instance);
            if (found == result.modulator_ids.end()) {
                error = "modulation route references an external source";
                return false;
            }
            copy.source_instance = found->second;
        }
        if (copy.destination_modulator) {
            const auto found = result.modulator_ids.find(copy.destination_modulator);
            if (found == result.modulator_ids.end()) {
                error = "modulation route references an external modulator";
                return false;
            }
            copy.destination_modulator = found->second;
        }
        if (copy.destination.target == ParameterTarget::native_effect) {
            const auto found = result.effect_ids.find(copy.destination.instance);
            if (found == result.effect_ids.end()) {
                error = "modulation references an external effect instance";
                return false;
            }
            copy.destination.instance = found->second;
        }
        result.modulation.routes[result.modulation.route_count++] = copy;
    }
    ModulationGraphDiagnostics diagnostics;
    if (!prepare_modulation_graph(result.modulation, diagnostics) ||
        !validate_effect_rack(result.effects, error)) {
        if (error.empty()) error = diagnostics.message;
        return false;
    }
    error.clear();
    return true;
}

PresetValidation relink_preset_assets(
    SoundPreset &preset, const std::vector<PresetAssetRelink> &exact,
    const std::vector<PresetDirectoryRelink> &directories,
    std::uint32_t target_sample_rate) {
    PresetValidation result;
    if (!preset.samples) {
        result.valid = true;
        return result;
    }
    auto replacement = preset.samples->sample_paths;
    for (auto &[slot, path] : replacement) {
        (void)slot;
        std::optional<std::filesystem::path> candidate;
        for (const auto &mapping : exact)
            if (path.lexically_normal() == mapping.missing.lexically_normal()) {
                candidate = mapping.replacement;
                break;
            }
        if (!candidate)
            for (const auto &mapping : directories) {
                std::filesystem::path suffix;
                if (below_prefix(path, mapping.missing_prefix, suffix)) {
                    candidate = mapping.replacement_prefix / suffix;
                    break;
                }
            }
        if (candidate) {
            const auto normalized = candidate->lexically_normal();
            if (!normalized.is_absolute() && !normalized.empty() &&
                *normalized.begin() == "..") {
                result.errors.push_back("unsafe relink target: " +
                                        normalized.string());
                continue;
            }
            try {
                (void)load_wav(normalized, target_sample_rate);
                path = normalized;
            } catch (const std::exception &exception) {
                result.errors.push_back("invalid replacement " +
                                        normalized.string() + ": " +
                                        exception.what());
            }
        }
        if (!std::filesystem::exists(path))
            result.missing_assets.push_back(path.string());
    }
    preset.samples->sample_paths = std::move(replacement);
    result.valid = result.errors.empty() && result.missing_assets.empty();
    return result;
}

std::filesystem::path PresetLibrary::preset_path(std::string_view id) const {
    if (!safe_id(id)) throw std::invalid_argument("unsafe preset ID");
    return root_ / std::string(id);
}

void PresetLibrary::save(const SoundPreset &preset) {
    const auto validation = validate_preset(preset, false);
    if (!validation.valid) throw std::invalid_argument(validation.errors.front());
    const auto directory = preset_path(preset.metadata.id);
    std::filesystem::create_directories(directory / "generations");
    const auto generation = generation_name();
    const auto temporary = directory / "generations" / (generation + ".tmp");
    const auto committed = directory / "generations" / generation;
    std::filesystem::create_directory(temporary);
    try {
        write_checked(temporary / "parameters.ngp",
                      serialize_parameters(preset.synth_parameters));
        if (preset.samples) save_slice_project(*preset.samples, temporary / "samples.ngs");
        if (preset.effects) write_checked(temporary / "effects.ngp", serialize_effects(*preset.effects));
        save_modulation_configuration(preset.modulation,
                                      temporary / "modulation.ngm");
        std::filesystem::rename(temporary, committed);
        std::ostringstream manifest;
        manifest << "NEUROGENIC_PRESET " << PresetMetadata::schema_version << '\n'
                 << "id " << std::quoted(preset.metadata.id) << '\n'
                 << "type " << preset_type_name(preset.metadata.type) << '\n'
                 << "name " << std::quoted(preset.metadata.name) << '\n'
                 << "author " << std::quoted(preset.metadata.author) << '\n'
                 << "description " << std::quoted(preset.metadata.description) << '\n'
                 << "generation " << std::quoted(generation) << '\n'
                 << "track " << preset.track_gain << ' ' << preset.track_pan << '\n';
        strings(manifest, "tags", preset.metadata.tags);
        strings(manifest, "capabilities", preset.metadata.required_capabilities);
        strings(manifest, "processors", preset.metadata.processor_types);
        strings(manifest, "modulators", preset.metadata.modulator_types);
        write_checked(directory / "manifest.tmp", manifest.str());
        std::filesystem::rename(directory / "manifest.tmp", directory / "manifest.ngp");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        throw;
    }
}

SoundPreset PresetLibrary::load(std::string_view id, bool require_assets) const {
    const auto directory = preset_path(id);
    bounded(directory / "manifest.ngp");
    std::ifstream input(directory / "manifest.ngp");
    SoundPreset result;
    std::string magic, label, type, generation;
    unsigned version{};
    if (!(input >> magic >> version) || magic != "NEUROGENIC_PRESET" ||
        version != PresetMetadata::schema_version ||
        !(input >> label >> std::quoted(result.metadata.id)) || label != "id" ||
        !safe_id(result.metadata.id) || !(input >> label >> type) || label != "type")
        throw std::runtime_error("unsupported or malformed preset manifest");
    const auto parsed = parse_preset_type(type);
    if (!parsed) throw std::runtime_error("unsupported preset type");
    result.metadata.type = *parsed;
    if (!(input >> label >> std::quoted(result.metadata.name)) || label != "name" ||
        !(input >> label >> std::quoted(result.metadata.author)) || label != "author" ||
        !(input >> label >> std::quoted(result.metadata.description)) || label != "description" ||
        !(input >> label >> std::quoted(generation)) || label != "generation" ||
        !safe_id(generation) || !(input >> label >> result.track_gain >> result.track_pan) ||
        label != "track" || !std::isfinite(result.track_gain) ||
        !std::isfinite(result.track_pan))
        throw std::runtime_error("malformed preset manifest");
    result.metadata.tags = read_strings(input, "tags");
    result.metadata.required_capabilities = read_strings(input, "capabilities");
    result.metadata.processor_types = read_strings(input, "processors");
    result.metadata.modulator_types = read_strings(input, "modulators");
    const auto files = directory / "generations" / generation;
    result.synth_parameters = load_parameters(files / "parameters.ngp");
    if (std::filesystem::exists(files / "samples.ngs")) {
        result.samples = load_slice_project(files / "samples.ngs", false);
        for (auto &[slot, path] : result.samples->sample_paths) {
            (void)slot;
            if (!path.is_absolute()) {
                const auto normalized = path.lexically_normal();
                if (normalized.empty() || *normalized.begin() == "..")
                    throw std::runtime_error("unsafe preset-relative sample path");
                path = (directory / normalized).lexically_normal();
            }
        }
    }
    if (std::filesystem::exists(files / "effects.ngp")) result.effects = load_effects(files / "effects.ngp");
    result.modulation = load_modulation_configuration(files / "modulation.ngm");
    const auto validation = validate_preset(result, require_assets);
    if (!validation.valid) throw std::runtime_error(validation.errors.front());
    return result;
}

std::vector<PresetMetadata> PresetLibrary::enumerate(
    std::optional<PresetType> type) const {
    std::vector<PresetMetadata> result;
    std::error_code error;
    if (!std::filesystem::exists(root_, error)) return result;
    for (const auto &entry : std::filesystem::directory_iterator(root_)) {
        if (!entry.is_directory()) continue;
        try {
            auto preset = load(entry.path().filename().string(), false);
            if (!type || preset.metadata.type == *type)
                result.push_back(std::move(preset.metadata));
        } catch (...) {}
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}
bool PresetLibrary::contains(std::string_view id) const {
    return std::filesystem::exists(preset_path(id));
}
void PresetLibrary::erase(std::string_view id) {
    const auto path = preset_path(id);
    if (!std::filesystem::exists(path)) throw std::runtime_error("preset does not exist");
    std::filesystem::remove_all(path);
}
void PresetLibrary::duplicate(std::string_view source_id, std::string new_id,
                              std::string new_name) {
    auto value = load(source_id, false);
    value.metadata.id = std::move(new_id);
    value.metadata.name = std::move(new_name);
    save(value);
}
void PresetLibrary::export_package(
    std::string_view id, const std::filesystem::path &destination) const {
    (void)load(id, false);
    if (std::filesystem::exists(destination))
        throw std::runtime_error("preset export destination already exists");
    std::filesystem::copy(preset_path(id), destination,
                          std::filesystem::copy_options::recursive);
}
PortablePresetExportResult PresetLibrary::export_self_contained(
    std::string_view id, const std::filesystem::path &destination) const {
    if (std::filesystem::exists(destination))
        throw std::runtime_error("preset export destination already exists");
    auto value = load(id, false);
    PortablePresetExportResult result;
    if (value.samples)
        for (const auto &[slot, path] : value.samples->sample_paths) {
            (void)slot;
            if (!std::filesystem::exists(path))
                result.missing_assets.push_back(path.string());
            else
                try {
                    (void)load_wav(path);
                } catch (const std::exception &exception) {
                    throw std::runtime_error("invalid preset asset " +
                                             path.string() + ": " +
                                             exception.what());
                }
        }
    if (!result.missing_assets.empty()) return result;
    const auto temporary_root = destination.parent_path() /
                                (destination.filename().string() + "-tmp-" +
                                 generation_name());
    const auto staged_package = temporary_root / value.metadata.id;
    try {
        if (value.samples) {
            std::map<std::filesystem::path, std::filesystem::path> copied;
            for (auto &[slot, path] : value.samples->sample_paths) {
                (void)slot;
                const auto canonical = std::filesystem::weakly_canonical(path);
                auto found = copied.find(canonical);
                if (found == copied.end()) {
                    std::ostringstream name;
                    name << std::hex << std::setw(16) << std::setfill('0')
                         << stable_path_hash(canonical.string()) << '-'
                         << canonical.filename().string();
                    const auto relative = std::filesystem::path("assets") /
                                          name.str();
                    std::filesystem::create_directories(staged_package /
                                                        "assets");
                    std::filesystem::copy_file(
                        canonical, staged_package / relative,
                        std::filesystem::copy_options::overwrite_existing);
                    ++result.copied_assets;
                    found = copied.emplace(canonical, relative).first;
                }
                path = found->second;
            }
        }
        PresetLibrary staging(temporary_root);
        staging.save(value);
        (void)staging.load(value.metadata.id, true);
        std::filesystem::rename(staged_package, destination);
        std::filesystem::remove(temporary_root);
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(destination))
            if (entry.is_regular_file())
                result.package_bytes += entry.file_size();
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary_root, ignored);
        throw;
    }
    return result;
}
void PresetLibrary::import_package(const std::filesystem::path &source,
                                   std::string_view id) {
    if (!safe_id(id)) throw std::invalid_argument("unsafe preset ID");
    PresetLibrary external(source.parent_path());
    auto value = external.load(source.filename().string(), false);
    value.metadata.id = std::string(id);
    save(value);
}
} // namespace neurogenic
