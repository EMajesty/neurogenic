#include "neurogenic/application/performance_macros.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
bool owner_exists(MacroOwnerKind kind, std::uint64_t id, const Timeline &timeline,
                  const MixerConfiguration &mixer) {
    if (kind == MacroOwnerKind::project || kind == MacroOwnerKind::master)
        return id == 0;
    if (!id)
        return false;
    if (kind == MacroOwnerKind::track)
        return std::any_of(timeline.tracks.begin(), timeline.tracks.end(),
                           [=](const auto &track) { return track.id == id; });
    return std::any_of(mixer.groups.begin(),
                       mixer.groups.begin() + mixer.group_count,
                       [=](const auto &group) { return group.id == id; });
}
ModulatorOwnerKind modulation_owner(MacroOwnerKind kind) {
    if (kind == MacroOwnerKind::track)
        return ModulatorOwnerKind::track;
    if (kind == MacroOwnerKind::group)
        return ModulatorOwnerKind::group;
    return ModulatorOwnerKind::master;
}
float apply_mapping_curve(ModulationMappingCurve curve, float input,
                          std::uint16_t steps) noexcept {
    input = std::clamp(input, 0.0F, 1.0F);
    switch (curve) {
    case ModulationMappingCurve::exponential: return input * input;
    case ModulationMappingCurve::logarithmic: return std::sqrt(input);
    case ModulationMappingCurve::s_curve:
        return input * input * (3.0F - 2.0F * input);
    case ModulationMappingCurve::stepped:
        steps = std::max<std::uint16_t>(2, steps);
        return std::round(input * (steps - 1)) /
               static_cast<float>(steps - 1);
    case ModulationMappingCurve::linear: return input;
    }
    return input;
}
bool modulator_destination(std::string_view id, ModulatorInstanceId &instance,
                           ModulatorParameterId &parameter) {
    constexpr std::string_view prefix{"modulator."};
    if (!id.starts_with(prefix))
        return false;
    id.remove_prefix(prefix.size());
    const auto separator = id.find('.');
    if (separator == std::string_view::npos)
        return false;
    std::uint32_t raw_parameter{};
    const auto first = id.substr(0, separator);
    const auto second = id.substr(separator + 1);
    const auto a = std::from_chars(first.data(), first.data() + first.size(),
                                   instance);
    const auto b = std::from_chars(second.data(), second.data() + second.size(),
                                   raw_parameter);
    if (a.ec != std::errc{} || a.ptr != first.end() || !instance ||
        b.ec != std::errc{} || b.ptr != second.end() || !raw_parameter)
        return false;
    parameter = static_cast<ModulatorParameterId>(raw_parameter);
    return true;
}
} // namespace

PerformanceMacroService::PerformanceMacroService(MacroProject project)
    : project_(std::move(project)) {}

PerformanceMacro *PerformanceMacroService::find(PerformanceMacroId id) noexcept {
    const auto found = std::find_if(project_.macros.begin(), project_.macros.end(),
                                    [=](const auto &macro) {
                                        return macro.id == id;
                                    });
    return found == project_.macros.end() ? nullptr : &*found;
}
const PerformanceMacro *
PerformanceMacroService::find(PerformanceMacroId id) const noexcept {
    const auto found = std::find_if(project_.macros.begin(), project_.macros.end(),
                                    [=](const auto &macro) {
                                        return macro.id == id;
                                    });
    return found == project_.macros.end() ? nullptr : &*found;
}

bool PerformanceMacroService::replace(MacroProject project,
                                      const Timeline &timeline,
                                      const MixerConfiguration &mixer,
                                      std::string &error) {
    if (project.macros.size() > MacroProject::default_macro_capacity ||
        project.scenes.size() > MacroProject::default_scene_capacity) {
        error = "performance macro capacity exceeded";
        return false;
    }
    std::size_t assignments{};
    for (std::size_t i = 0; i < project.macros.size(); ++i) {
        const auto &macro = project.macros[i];
        assignments += macro.assignments.size();
        if (!macro.id || !macro.source_instance || macro.name.empty() ||
            !std::isfinite(macro.value) || macro.value < 0 || macro.value > 1 ||
            !std::isfinite(macro.default_value) || macro.default_value < 0 ||
            macro.default_value > 1 ||
            !owner_exists(macro.owner_kind, macro.owner_id, timeline, mixer)) {
            error = "invalid performance macro";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (project.macros[j].id == macro.id ||
                project.macros[j].source_instance == macro.source_instance) {
                error = "duplicate performance macro identity";
                return false;
            }
        for (const auto &assignment : macro.assignments) {
            ModulatorInstanceId target{};
            ModulatorParameterId parameter{};
            if (!assignment.id || assignment.destination_id.empty() ||
                (!resolve_parameter_destination(assignment.destination_id) &&
                 !modulator_destination(assignment.destination_id, target,
                                        parameter)) ||
                !std::isfinite(assignment.minimum) ||
                !std::isfinite(assignment.maximum) ||
                assignment.minimum == assignment.maximum ||
                !std::isfinite(assignment.smoothing_seconds) ||
                assignment.smoothing_seconds < 0 ||
                assignment.smoothing_seconds > 10 || assignment.steps < 2) {
                error = "invalid performance macro assignment";
                return false;
            }
        }
    }
    if (assignments > MacroProject::default_assignment_capacity) {
        error = "performance macro assignment capacity exceeded";
        return false;
    }
    for (std::size_t i = 0; i < project.scenes.size(); ++i) {
        const auto &scene = project.scenes[i];
        if (!scene.id || scene.name.empty() || scene.name.size() > 128 ||
            !std::isfinite(scene.morph_duration) || scene.morph_duration < 0 ||
            scene.morph_duration > 3600 ||
            scene.curve > MacroMorphCurve::s_curve ||
            std::any_of(scene.macro_values.begin(), scene.macro_values.end(),
                        [](const auto &value) {
                            return !value.first || !std::isfinite(value.second) ||
                                   value.second < 0 || value.second > 1;
                        })) {
            error = "invalid performance scene";
            return false;
        }
        for (std::size_t other = 0; other < i; ++other)
            if (project.scenes[other].id == scene.id) {
                error = "duplicate performance scene identity";
                return false;
            }
    }
    project_ = std::move(project);
    morph_ = {};
    error.clear();
    return true;
}

bool PerformanceMacroService::create(std::string name, MacroOwnerKind owner,
                                     std::uint64_t owner_id, float value,
                                     PerformanceMacroId &id,
                                     std::string &error) {
    if (name.empty() || name.size() > 128 || !std::isfinite(value) || value < 0 ||
        value > 1) {
        error = "invalid performance macro name or value";
        return false;
    }
    if (project_.macros.size() == MacroProject::default_macro_capacity) {
        error = "performance macro capacity exhausted (32 macros)";
        return false;
    }
    id = 1;
    ModulatorInstanceId source = 0x40000001U;
    for (const auto &macro : project_.macros) {
        id = std::max(id, macro.id + 1);
        source = std::max(source, macro.source_instance + 1);
    }
    project_.macros.push_back(
        {id, source, std::move(name), {}, owner, owner_id, value, value, true, {}});
    error.clear();
    return true;
}

bool PerformanceMacroService::erase(PerformanceMacroId id, std::string &error) {
    const auto found = std::find_if(project_.macros.begin(), project_.macros.end(),
                                    [=](const auto &macro) {
                                        return macro.id == id;
                                    });
    if (found == project_.macros.end()) {
        error = "performance macro was not found";
        return false;
    }
    project_.macros.erase(found);
    for (auto &scene : project_.scenes)
        std::erase_if(scene.macro_values,
                      [=](const auto &value) { return value.first == id; });
    error.clear();
    return true;
}

bool PerformanceMacroService::rename(PerformanceMacroId id, std::string name,
                                     std::string description,
                                     std::string &error) {
    auto *macro = find(id);
    if (!macro || name.empty() || name.size() > 128 || description.size() > 512) {
        error = "invalid performance macro identity or text";
        return false;
    }
    macro->name = std::move(name);
    macro->description = std::move(description);
    error.clear();
    return true;
}

bool PerformanceMacroService::set_value(PerformanceMacroId id, float value,
                                        std::string &error) {
    auto *macro = find(id);
    if (!macro || !std::isfinite(value) || value < 0 || value > 1) {
        error = "macro value must be normalized";
        return false;
    }
    macro->value = value;
    error.clear();
    return true;
}

bool PerformanceMacroService::assign(PerformanceMacroId id,
                                     MacroAssignment assignment,
                                     MacroAssignmentId &assignment_id,
                                     std::string &error) {
    auto *macro = find(id);
    std::size_t total{};
    for (const auto &candidate : project_.macros)
        total += candidate.assignments.size();
    ModulatorInstanceId target{};
    ModulatorParameterId parameter{};
    if (!macro || total == MacroProject::default_assignment_capacity ||
        assignment.destination_id.empty() ||
        (!resolve_parameter_destination(assignment.destination_id) &&
         !modulator_destination(assignment.destination_id, target, parameter)) ||
        !std::isfinite(assignment.minimum) ||
        !std::isfinite(assignment.maximum) ||
        assignment.minimum == assignment.maximum ||
        !std::isfinite(assignment.smoothing_seconds) ||
        assignment.smoothing_seconds < 0 || assignment.smoothing_seconds > 10 ||
        assignment.steps < 2) {
        error = "invalid or unavailable macro assignment";
        return false;
    }
    assignment_id = 1;
    for (const auto &candidate : project_.macros)
        for (const auto &existing : candidate.assignments)
            assignment_id = std::max(assignment_id, existing.id + 1);
    assignment.id = assignment_id;
    macro->assignments.push_back(std::move(assignment));
    error.clear();
    return true;
}

bool PerformanceMacroService::remove_assignment(PerformanceMacroId macro_id,
                                                MacroAssignmentId id,
                                                std::string &error) {
    auto *macro = find(macro_id);
    if (!macro) {
        error = "performance macro was not found";
        return false;
    }
    const auto removed = std::erase_if(
        macro->assignments, [=](const auto &assignment) {
            return assignment.id == id;
        });
    if (!removed) {
        error = "macro assignment was not found";
        return false;
    }
    error.clear();
    return true;
}

bool PerformanceMacroService::update_assignment(
    PerformanceMacroId macro_id, MacroAssignmentId id,
    MacroAssignment assignment, std::string &error) {
    auto *macro = find(macro_id);
    if (!macro) {
        error = "performance macro was not found";
        return false;
    }
    const auto found = std::find_if(
        macro->assignments.begin(), macro->assignments.end(),
        [=](const auto &candidate) { return candidate.id == id; });
    ModulatorInstanceId target{};
    ModulatorParameterId parameter{};
    if (found == macro->assignments.end() || assignment.destination_id.empty() ||
        (!resolve_parameter_destination(assignment.destination_id) &&
         !modulator_destination(assignment.destination_id, target, parameter)) ||
        !std::isfinite(assignment.minimum) ||
        !std::isfinite(assignment.maximum) ||
        assignment.minimum == assignment.maximum ||
        !std::isfinite(assignment.smoothing_seconds) ||
        assignment.smoothing_seconds < 0 || assignment.smoothing_seconds > 10 ||
        assignment.steps < 2) {
        error = "invalid or unavailable macro assignment update";
        return false;
    }
    assignment.id = id;
    *found = std::move(assignment);
    error.clear();
    return true;
}

bool PerformanceMacroService::capture_scene(std::string name, float duration,
                                            bool tempo_sync,
                                            MacroMorphCurve curve,
                                            PerformanceSceneId &id,
                                            std::string &error) {
    if (name.empty() || name.size() > 128 || !std::isfinite(duration) ||
        duration < 0 || duration > 3600 || curve > MacroMorphCurve::s_curve ||
        project_.scenes.size() == MacroProject::default_scene_capacity) {
        error = "invalid performance scene or scene capacity exhausted";
        return false;
    }
    id = 1;
    for (const auto &scene : project_.scenes)
        id = std::max(id, scene.id + 1);
    PerformanceScene scene;
    scene.id = id;
    scene.name = std::move(name);
    scene.morph_duration = duration;
    scene.tempo_sync = tempo_sync;
    scene.curve = curve;
    for (const auto &macro : project_.macros)
        scene.macro_values.emplace_back(macro.id, macro.value);
    project_.scenes.push_back(std::move(scene));
    error.clear();
    return true;
}

bool PerformanceMacroService::erase_scene(PerformanceSceneId id,
                                          std::string &error) {
    const auto removed = std::erase_if(project_.scenes, [=](const auto &scene) {
        return scene.id == id;
    });
    if (!removed) {
        error = "performance scene was not found";
        return false;
    }
    if (morph_.scene == id)
        stop_morph();
    error.clear();
    return true;
}

bool PerformanceMacroService::rename_scene(PerformanceSceneId id,
                                           std::string name,
                                           std::string &error) {
    const auto found = std::find_if(project_.scenes.begin(), project_.scenes.end(),
                                    [=](const auto &scene) {
                                        return scene.id == id;
                                    });
    if (found == project_.scenes.end() || name.empty() || name.size() > 128) {
        error = "invalid performance scene identity or name";
        return false;
    }
    found->name = std::move(name);
    error.clear();
    return true;
}

bool PerformanceMacroService::update_scene(PerformanceSceneId id,
                                           std::string name, float duration,
                                           bool tempo_sync,
                                           MacroMorphCurve curve,
                                           std::string &error) {
    const auto found = std::find_if(project_.scenes.begin(), project_.scenes.end(),
                                    [=](const auto &scene) {
                                        return scene.id == id;
                                    });
    if (found == project_.scenes.end() || name.empty() || name.size() > 128 ||
        !std::isfinite(duration) || duration < 0 || duration > 3600 ||
        curve > MacroMorphCurve::s_curve) {
        error = "invalid performance scene update";
        return false;
    }
    found->name = std::move(name);
    found->morph_duration = duration;
    found->tempo_sync = tempo_sync;
    found->curve = curve;
    found->macro_values.clear();
    for (const auto &macro : project_.macros)
        found->macro_values.emplace_back(macro.id, macro.value);
    error.clear();
    return true;
}

bool PerformanceMacroService::duplicate_scene(PerformanceSceneId source,
                                              std::string name,
                                              PerformanceSceneId &id,
                                              std::string &error) {
    const auto found = std::find_if(project_.scenes.begin(), project_.scenes.end(),
                                    [=](const auto &scene) {
                                        return scene.id == source;
                                    });
    if (found == project_.scenes.end() || name.empty() || name.size() > 128 ||
        project_.scenes.size() == MacroProject::default_scene_capacity) {
        error = "invalid performance scene duplication";
        return false;
    }
    id = 1;
    for (const auto &scene : project_.scenes)
        id = std::max(id, scene.id + 1);
    auto copy = *found;
    copy.id = id;
    copy.name = std::move(name);
    project_.scenes.push_back(std::move(copy));
    error.clear();
    return true;
}

bool PerformanceMacroService::start_morph(PerformanceSceneId id, float duration,
                                          bool tempo_sync, MusicalTick tick,
                                          std::uint64_t frame,
                                          std::uint32_t sample_rate, double bpm,
                                          std::string &error) {
    const auto scene = std::find_if(project_.scenes.begin(), project_.scenes.end(),
                                    [=](const auto &candidate) {
                                        return candidate.id == id;
                                    });
    if (scene == project_.scenes.end() || !std::isfinite(duration) ||
        duration < 0 || duration > 3600 || !sample_rate || !std::isfinite(bpm) ||
        bpm <= 0) {
        error = "invalid performance scene morph";
        return false;
    }
    morph_start_.clear();
    morph_target_.clear();
    for (const auto &[macro_id, target] : scene->macro_values)
        if (const auto *macro = find(macro_id)) {
            morph_start_.emplace_back(macro_id, macro->value);
            morph_target_.emplace_back(macro_id, std::clamp(target, 0.0F, 1.0F));
        }
    morph_ = {true, tempo_sync, id, 0, duration, tick, 0, frame, 0, {}};
    if (tempo_sync)
        morph_.duration_ticks = static_cast<MusicalTick>(
            std::llround(duration * ticks_per_quarter));
    else
        morph_.duration_frames = static_cast<std::uint64_t>(
            std::llround(duration * sample_rate));
    if (duration == 0)
        morph_.duration_ticks = morph_.duration_frames = 0;
    error.clear();
    return true;
}

void PerformanceMacroService::stop_morph() noexcept { morph_.active = false; }

bool PerformanceMacroService::advance(
    MusicalTick tick, std::uint64_t frame,
    std::vector<std::pair<ModulatorInstanceId, float>> &updates) noexcept {
    if (!morph_.active)
        return false;
    const auto progress = morph_.tempo_sync
                              ? (morph_.duration_ticks == 0
                                     ? 1.0F
                                     : static_cast<float>(tick - morph_.start_tick) /
                                           morph_.duration_ticks)
                              : (morph_.duration_frames == 0
                                     ? 1.0F
                                     : static_cast<float>(frame - morph_.start_frame) /
                                           morph_.duration_frames);
    morph_.progress = std::clamp(progress, 0.0F, 1.0F);
    updates.clear();
    for (std::size_t i = 0; i < morph_target_.size(); ++i)
        if (auto *macro = find(morph_target_[i].first)) {
            macro->value = std::lerp(morph_start_[i].second,
                                     morph_target_[i].second, morph_.progress);
            updates.emplace_back(macro->source_instance, macro->value);
        }
    if (morph_.progress >= 1)
        morph_.active = false;
    return true;
}

bool PerformanceMacroService::prepare_scene_morph(
    PerformanceSceneId id, float duration, bool tempo_sync,
    MacroMorphCurve curve, std::uint64_t start_frame,
    std::span<const MacroMorphRuntime> runtime, std::uint64_t &next_morph_id,
    std::vector<MacroMorphCommand> &commands, std::string &error) {
    const auto scene = std::find_if(project_.scenes.begin(), project_.scenes.end(),
                                    [=](const auto &candidate) {
                                        return candidate.id == id;
                                    });
    if (scene == project_.scenes.end() || !std::isfinite(duration) ||
        duration < 0 || duration > 3600 ||
        curve > MacroMorphCurve::s_curve) {
        error = "invalid performance scene morph";
        return false;
    }
    commands.clear();
    for (const auto &[macro_id, target] : scene->macro_values) {
        auto *macro = find(macro_id);
        if (!macro)
            continue;
        auto start = macro->value;
        const auto active = std::find_if(
            runtime.begin(), runtime.end(), [&](const auto &candidate) {
                return candidate.instance == macro->source_instance &&
                       (candidate.active || candidate.scheduled ||
                        candidate.completed || candidate.interrupted);
            });
        if (active != runtime.end())
            start = active->effective_value;
        MacroMorphCommand command;
        command.type = MacroMorphCommandType::start;
        command.morph_id = next_morph_id++;
        command.instance = macro->source_instance;
        command.start_frame = start_frame;
        command.start_value = std::clamp(start, 0.0F, 1.0F);
        command.target_value = std::clamp(target, 0.0F, 1.0F);
        command.duration = duration;
        command.duration_unit = tempo_sync ? MacroMorphDurationUnit::beats
                                           : MacroMorphDurationUnit::seconds;
        command.curve = curve;
        commands.push_back(command);
        macro->value = command.target_value;
    }
    if (commands.empty()) {
        error = "performance scene has no available macro targets";
        return false;
    }
    error.clear();
    return true;
}

bool PerformanceMacroService::compile(
    const ModulationConfiguration &base, ModulationConfiguration &result,
    std::string &error) const {
    result = base;
    for (const auto &macro : project_.macros) {
        if (result.modulator_count == result.modulators.size()) {
            error = "modulator capacity exhausted by performance macros";
            return false;
        }
        if (std::any_of(result.modulators.begin(),
                        result.modulators.begin() + result.modulator_count,
                        [&](const auto &instance) {
                            return instance.id == macro.source_instance;
                        })) {
            error = "macro source instance conflicts with a modulator";
            return false;
        }
        auto &instance = result.modulators[result.modulator_count++];
        instance.id = macro.source_instance;
        instance.type = ModulatorType::performance_macro;
        instance.owner_kind = modulation_owner(macro.owner_kind);
        instance.owner_id = macro.owner_id;
        instance.enabled = macro.enabled;
        instance.shared = macro.owner_kind == MacroOwnerKind::project ||
                          macro.owner_kind == MacroOwnerKind::master;
        instance.settings.manual_value = macro.value;
        const auto count = std::min(macro.name.size(), instance.display_name.size() - 1);
        std::copy_n(macro.name.begin(), count, instance.display_name.begin());
        for (const auto &assignment : macro.assignments) {
            if (!assignment.enabled)
                continue;
            if (result.route_count == result.routes.size()) {
                error = "modulation route capacity exhausted by macro assignments";
                return false;
            }
            auto &route = result.routes[result.route_count++];
            route.id = static_cast<ModulationRouteId>(assignment.id);
            while (std::any_of(result.routes.begin(),
                               result.routes.begin() + result.route_count - 1,
                               [&](const auto &candidate) {
                                   return candidate.id == route.id;
                               }))
                ++route.id;
            route.source_instance = macro.source_instance;
            route.source_output = 1;
            route.scope = macro.owner_kind == MacroOwnerKind::track
                              ? ModulationScope::track
                              : ModulationScope::global;
            route.track_id = macro.owner_kind == MacroOwnerKind::track
                                 ? macro.owner_id
                                 : 0;
            route.minimum = std::min(assignment.minimum, assignment.maximum);
            route.maximum = std::max(assignment.minimum, assignment.maximum);
            route.base_value = assignment.minimum +
                               apply_mapping_curve(assignment.curve,
                                                   macro.default_value,
                                                   assignment.steps) *
                                                        (assignment.maximum -
                                                         assignment.minimum);
            route.enabled = macro.enabled;
            route.polarity = assignment.polarity;
            route.mapped_assignment = true;
            route.mapping_curve = assignment.curve;
            route.mapped_minimum = assignment.minimum;
            route.mapped_maximum = assignment.maximum;
            route.mapped_default = macro.default_value;
            route.mapped_steps = assignment.steps;
            ModulatorInstanceId target{};
            ModulatorParameterId parameter{};
            if (const auto destination =
                    resolve_parameter_destination(assignment.destination_id))
                route.destination = *destination;
            else if (modulator_destination(assignment.destination_id, target,
                                           parameter)) {
                route.destination_kind =
                    ModulationDestinationKind::modulator_parameter;
                route.destination_modulator = target;
                route.destination_modulator_parameter = parameter;
            } else {
                error = "macro assignment destination is unavailable: " +
                        assignment.destination_id;
                return false;
            }
        }
    }
    ModulationGraphDiagnostics diagnostics;
    if (!prepare_modulation_graph(result, diagnostics)) {
        error = diagnostics.message;
        return false;
    }
    error.clear();
    return true;
}

MacroDiscoverySnapshot PerformanceMacroService::snapshot() const {
    MacroDiscoverySnapshot result;
    result.macros = project_.macros;
    result.scenes = project_.scenes;
    result.morph = morph_;
    return result;
}
} // namespace neurogenic
