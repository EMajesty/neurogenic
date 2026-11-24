#pragma once
#include "neurogenic/application/performance_macros.hpp"
#include "neurogenic/audio/mixer.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/timeline/model.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace neurogenic {
struct Project {
    static constexpr unsigned format_version = 5;
    std::string name{"Untitled"};
    Timeline timeline;
    SliceProject samples;
    std::map<std::string, double> parameters;
    MixerConfiguration mixer;
    std::vector<RoutingScene> routing_scenes;
    std::uint64_t active_routing_scene{};
    ModulationConfiguration modulation;
    MacroProject performance_macros;
    MidiProjectConfiguration midi;
    std::vector<std::filesystem::path> missing_assets;
};

struct ProjectIoMetrics {
    std::uint64_t elapsed_microseconds{};
    std::uintmax_t metadata_bytes{};
};
struct ProjectAssetRelink {
    std::filesystem::path missing, replacement;
};
struct ProjectDirectoryRelink {
    std::filesystem::path missing_prefix, replacement_prefix;
};

void save_project(const Project &, const std::filesystem::path &directory,
                  ProjectIoMetrics *metrics = nullptr);
Project load_project(const std::filesystem::path &directory,
                     ProjectIoMetrics *metrics = nullptr);
void save_modulation_configuration(const ModulationConfiguration &,
                                   const std::filesystem::path &);
ModulationConfiguration
load_modulation_configuration(const std::filesystem::path &);
bool relink_project_sample(Project &, const std::filesystem::path &missing,
                           const std::filesystem::path &replacement,
                           std::string &error);
bool relink_project_assets(
    Project &, const std::vector<ProjectAssetRelink> &,
    const std::vector<ProjectDirectoryRelink> &, std::uint32_t target_sample_rate,
    std::vector<std::filesystem::path> &unresolved, std::string &error);
} // namespace neurogenic
