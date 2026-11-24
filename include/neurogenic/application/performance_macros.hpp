#pragma once
#include "neurogenic/audio/mixer.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/dsp/macro_morph.hpp"
#include "neurogenic/timeline/model.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
using PerformanceMacroId = std::uint64_t;
using MacroAssignmentId = std::uint64_t;
using PerformanceSceneId = std::uint64_t;

enum class MacroOwnerKind : std::uint8_t { project, track, group, master };

struct MacroAssignment {
    MacroAssignmentId id{};
    std::string destination_id;
    float minimum{}, maximum{1.0F};
    ModulationPolarity polarity{ModulationPolarity::unipolar};
    ModulationMappingCurve curve{ModulationMappingCurve::linear};
    bool enabled{true};
    float smoothing_seconds{};
    std::uint16_t steps{2};
};

struct PerformanceMacro {
    PerformanceMacroId id{};
    ModulatorInstanceId source_instance{};
    std::string name, description;
    MacroOwnerKind owner_kind{MacroOwnerKind::project};
    std::uint64_t owner_id{};
    float value{}, default_value{};
    bool enabled{true};
    std::vector<MacroAssignment> assignments;
};

struct PerformanceScene {
    PerformanceSceneId id{};
    std::string name;
    std::vector<std::pair<PerformanceMacroId, float>> macro_values;
    std::vector<std::pair<std::string, float>> parameter_values;
    float morph_duration{};
    bool tempo_sync{};
    MacroMorphCurve curve{MacroMorphCurve::linear};
};

struct MacroProject {
    static constexpr std::size_t default_macro_capacity = 32;
    static constexpr std::size_t default_assignment_capacity = 128;
    static constexpr std::size_t default_scene_capacity = 32;
    std::vector<PerformanceMacro> macros;
    std::vector<PerformanceScene> scenes;
};

struct MacroMorphStatus {
    bool active{}, tempo_sync{};
    PerformanceSceneId scene{};
    float progress{}, duration{};
    MusicalTick start_tick{}, duration_ticks{};
    std::uint64_t start_frame{}, duration_frames{};
    std::string error;
};

struct MacroDiscoverySnapshot {
    std::uint32_t api_version{2};
    std::vector<PerformanceMacro> macros;
    std::vector<PerformanceScene> scenes;
    MacroMorphStatus morph;
    std::size_t macro_capacity{MacroProject::default_macro_capacity};
    std::size_t assignment_capacity{MacroProject::default_assignment_capacity};
    std::size_t scene_capacity{MacroProject::default_scene_capacity};
    std::vector<std::string> validation_errors;
    std::vector<MacroMorphRuntime> active_morphs;
    std::vector<std::pair<PerformanceMacroId, float>> effective_values;
    std::uint32_t morph_control_interval{MacroMorphProcessor::control_interval};
    std::uint64_t dropped_morph_commands{};
    bool absolute_frame_scheduling{true}, seconds_duration{true},
        beats_duration{true};
};

class PerformanceMacroService {
  public:
    explicit PerformanceMacroService(MacroProject project = {});
    [[nodiscard]] const MacroProject &project() const noexcept { return project_; }
    bool replace(MacroProject, const Timeline &, const MixerConfiguration &,
                 std::string &);
    bool create(std::string, MacroOwnerKind, std::uint64_t, float,
                PerformanceMacroId &, std::string &);
    bool erase(PerformanceMacroId, std::string &);
    bool rename(PerformanceMacroId, std::string, std::string, std::string &);
    bool set_value(PerformanceMacroId, float, std::string &);
    bool assign(PerformanceMacroId, MacroAssignment, MacroAssignmentId &,
                std::string &);
    bool update_assignment(PerformanceMacroId, MacroAssignmentId,
                           MacroAssignment, std::string &);
    bool remove_assignment(PerformanceMacroId, MacroAssignmentId,
                           std::string &);
    bool capture_scene(std::string, float, bool, MacroMorphCurve,
                       PerformanceSceneId &,
                       std::string &);
    bool erase_scene(PerformanceSceneId, std::string &);
    bool rename_scene(PerformanceSceneId, std::string, std::string &);
    bool update_scene(PerformanceSceneId, std::string, float, bool,
                      MacroMorphCurve, std::string &);
    bool duplicate_scene(PerformanceSceneId, std::string,
                         PerformanceSceneId &, std::string &);
    bool start_morph(PerformanceSceneId, float, bool, MusicalTick,
                     std::uint64_t, std::uint32_t, double, std::string &);
    void stop_morph() noexcept;
    bool advance(MusicalTick, std::uint64_t,
                 std::vector<std::pair<ModulatorInstanceId, float>> &) noexcept;
    bool prepare_scene_morph(
        PerformanceSceneId, float, bool, MacroMorphCurve, std::uint64_t,
        std::span<const MacroMorphRuntime>, std::uint64_t &,
        std::vector<MacroMorphCommand> &, std::string &);
    bool compile(const ModulationConfiguration &, ModulationConfiguration &,
                 std::string &) const;
    [[nodiscard]] MacroDiscoverySnapshot snapshot() const;

  private:
    PerformanceMacro *find(PerformanceMacroId) noexcept;
    const PerformanceMacro *find(PerformanceMacroId) const noexcept;
    MacroProject project_;
    MacroMorphStatus morph_;
    std::vector<std::pair<PerformanceMacroId, float>> morph_start_;
    std::vector<std::pair<PerformanceMacroId, float>> morph_target_;
};
} // namespace neurogenic
