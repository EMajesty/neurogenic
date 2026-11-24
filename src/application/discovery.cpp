#include "neurogenic/application/discovery.hpp"
#include "neurogenic/dsp/effect_chain.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/project/preset.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace neurogenic {
namespace {
std::string escape(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(c) << std::dec;
            else
                out << static_cast<char>(c);
        }
    }
    return out.str();
}
const char *value_type(ValueType value) {
    switch (value) {
    case ValueType::none: return "none";
    case ValueType::boolean: return "boolean";
    case ValueType::integer: return "integer";
    case ValueType::real: return "real";
    case ValueType::text: return "text";
    case ValueType::xy: return "xy";
    }
    return "unknown";
}
const char *parameter_type(ParameterType value) {
    switch (value) {
    case ParameterType::boolean: return "boolean";
    case ParameterType::integer: return "integer";
    case ParameterType::real: return "real";
    case ParameterType::enumeration: return "enumeration";
    }
    return "unknown";
}
const char *binding_kind(DiscoveryValueKind value) {
    switch (value) {
    case DiscoveryValueKind::boolean: return "boolean";
    case DiscoveryValueKind::integer: return "integer";
    case DiscoveryValueKind::real: return "real";
    case DiscoveryValueKind::text: return "text";
    case DiscoveryValueKind::object: return "object";
    case DiscoveryValueKind::collection: return "collection";
    }
    return "unknown";
}
const char *update_rate(DiscoveryUpdateRate value) {
    switch (value) {
    case DiscoveryUpdateRate::static_value: return "static";
    case DiscoveryUpdateRate::state: return "state";
    case DiscoveryUpdateRate::telemetry: return "telemetry";
    }
    return "unknown";
}
void json_value(std::ostringstream &out, const ApplicationValue &value) {
    std::visit(
        [&](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>) out << "null";
            else if constexpr (std::is_same_v<T, bool>) out << (item ? "true" : "false");
            else if constexpr (std::is_same_v<T, std::string>) out << '"' << escape(item) << '"';
            else if constexpr (std::is_same_v<T, NormalizedXY>)
                out << "{\"x\":" << item.x << ",\"y\":" << item.y << '}';
            else out << item;
        }, value);
}
std::string object_id(std::string_view type, std::uint64_t id) {
    return std::string(type) + ":" + std::to_string(id);
}
std::string parameter_parent(std::string_view id) {
    if (id.rfind("transport.", 0) == 0) return "transport:main";
    if (id.rfind("mixer.master.", 0) == 0) return "master:main";
    if (id.rfind("recording.", 0) == 0) return "recording-session:main";
    return "project:current";
}
std::string action_target(std::string_view id) {
    if (id.rfind("track.", 0) == 0) return "track";
    if (id.rfind("pattern.", 0) == 0) return "pattern";
    if (id.rfind("arrangement.", 0) == 0) return "arrangement";
    if (id.rfind("transport.", 0) == 0) return "transport";
    if (id.rfind("effect.", 0) == 0) return "fx-instance";
    if (id.rfind("modulator.", 0) == 0) return "modulator";
    if (id.rfind("modulation.", 0) == 0) return "modulation-connection";
    if (id.rfind("macro.", 0) == 0) return "performance-macro";
    if (id.rfind("recording.", 0) == 0) return "recording-session";
    if (id.rfind("midi.", 0) == 0) return "midi-port";
    if (id.rfind("project.", 0) == 0) return "project";
    if (id.rfind("preset.", 0) == 0) return "preset";
    if (id.rfind("mixer.group", 0) == 0) return "mixer-group";
    if (id.rfind("mixer.aux", 0) == 0) return "aux-bus";
    if (id.rfind("mixer.", 0) == 0) return "master";
    if (id.rfind("sample.", 0) == 0 || id.rfind("slice.", 0) == 0 ||
        id.rfind("region.", 0) == 0 ||
        id.rfind("pad.", 0) == 0)
        return "sample-slot";
    if (id.rfind("sample-analysis.", 0) == 0)
        return "sample-analysis-session";
    if (id.rfind("sample-processing.", 0) == 0)
        return "sample-processing-job";
    return {};
}
} // namespace

ApplicationDiscovery discover_application(const ApplicationService &application) {
    const auto state = application.snapshot();
    ApplicationDiscovery result;
    result.capabilities = {
        {"semantic-actions", 1, true, {}}, {"typed-parameters", 1, true, {}},
        {"timeline-arrangement", 2, true, {}}, {"native-fx-chains", 1, true, {}},
        {"modulation-graph", 6, true, {}}, {"performance-macros", 2, true, {}},
        {"midi-routing", 2, true, {}}, {"audio-recording", 1, true, {}},
        {"sound-presets", 2, true, {}}, {"sample-regions", 1, true, {}},
        {"sample-analysis", 1, true, {}},
        {"sample-processing", 1, true, {}},
        {"audio-clips", 2, true, {}},
        {"live-looping", 3, true, {}},
        {"high-frequency-telemetry", 1, true, {}},
        {"typed-parameter-bindings", 1, true, {}}};
    result.object_types = {
        {"project", "Project", false, false, true},
        {"transport", "Transport", false, false, false},
        {"track", "Track", true, true, true},
        {"instrument", "Instrument", true, true, true},
        {"sample-slot", "Sample slot", false, false, true},
        {"sample-region", "Sample region", true, true, true},
        {"sample-analysis-session", "Sample analysis session", true, true, false},
        {"live-loop-session", "Live loop session", true, true, false},
        {"sample-analysis-algorithm", "Sample analysis algorithm", false, false, false},
        {"sample-processing-job", "Sample processing job", true, true, false},
        {"sample-processing-operation", "Sample processing operation", false, false, false},
        {"pattern", "Pattern", true, true, true},
        {"clip", "Clip", true, true, true},
        {"audio-clip", "Audio clip", true, true, true},
        {"arrangement", "Arrangement", false, false, true},
        {"mixer-group", "Mixer group", true, true, true},
        {"aux-bus", "Aux bus", false, false, true},
        {"master", "Master bus", false, false, true},
        {"fx-chain", "Effect chain", false, false, true},
        {"fx-instance", "Effect instance", true, true, true},
        {"modulator", "Modulator", true, true, true},
        {"modulator-type", "Modulator type", false, false, false},
        {"fx-processor-type", "Effect processor type", false, false, false},
        {"preset-type", "Preset type", false, false, false},
        {"preset", "Sound preset", true, true, true},
        {"preset-application-mode", "Preset application mode", false, false,
         false},
        {"modulation-connection", "Modulation connection", true, true, true},
        {"performance-macro", "Performance macro", true, true, true},
        {"scene", "Scene", true, true, true},
        {"recording-session", "Recording session", false, false, true},
        {"midi-port", "MIDI logical port", false, false, true}};
    result.objects = {{"project", "project:current", {}, state.project_name, true, true, {}},
                      {"transport", "transport:main", "project:current", "Transport", true, false, {}},
                      {"arrangement", "arrangement:main", "project:current", "Arrangement", true, false, state.arrangement_error},
                      {"master", "master:main", "project:current", "Master", true, false, {}},
                      {"recording-session", "recording-session:main", "project:current", "Recording", true, false, state.recording.error}};
    for (const auto type : {PresetType::synth_patch,
                            PresetType::sample_instrument,
                            PresetType::fx_chain,
                            PresetType::track_sound}) {
        const auto id = std::string(preset_type_name(type));
        result.objects.push_back(
            {"preset-type", "preset-type:" + id, {}, id, true, false, {}});
    }
    for (const auto mode : {PresetImportMode::replace_sound,
                            PresetImportMode::insert_fx_chain,
                            PresetImportMode::merge_modulation,
                            PresetImportMode::preview}) {
        const auto id = std::string(preset_import_mode_name(mode));
        result.objects.push_back({"preset-application-mode",
                                  "preset-application-mode:" + id, {}, id,
                                  true, false, {}});
    }
    for (const auto &type : modulator_type_registry()) {
        const auto parent = "modulator-type:" + std::string(type.id);
        result.objects.push_back({"modulator-type", parent, {},
                                  std::string(type.name), true, false, {}});
        for (const auto &parameter : type.parameters)
            result.parameters.push_back(
                {parent + "." + std::string(parameter.id), parent,
                 std::string(parameter.name), std::string(parameter.unit),
                 parameter.discrete ? ParameterType::integer
                                    : ParameterType::real,
                 static_cast<double>(parameter.minimum),
                 static_cast<double>(parameter.maximum),
                 static_cast<double>(parameter.default_value), {}, {}, 0.0,
                 true, false, !parameter.discrete, false,
                 parameter.modulatable, {}});
    }
    for (const auto type : {EffectProcessorType::gain,
                            EffectProcessorType::stereo_delay,
                            EffectProcessorType::low_pass_filter,
                            EffectProcessorType::saturation}) {
        const auto type_id = std::string(effect_processor_type_id(type));
        const auto parent = "fx-processor-type:" + type_id;
        result.objects.push_back({"fx-processor-type", parent, {}, type_id,
                                  true, false, {}});
        for (const auto &parameter : effect_parameter_descriptors(type))
            result.parameters.push_back(
                {parent + "." + std::string(parameter.stable_id), parent,
                 std::string(parameter.name), std::string(parameter.unit),
                 parameter.kind == EffectParameterKind::discrete
                     ? ParameterType::integer
                     : ParameterType::real,
                 static_cast<double>(parameter.minimum),
                 static_cast<double>(parameter.maximum),
                 static_cast<double>(parameter.default_value), {}, {}, 0.0,
                 true, false,
                 parameter.kind == EffectParameterKind::continuous,
                 parameter.automatable, parameter.modulatable, {}});
    }
    for (const auto &track : state.tracks) {
        result.objects.push_back({"track", object_id("track", track.id),
                                  "project:current", track.name, true,
                                  track.id == state.selected_track, {}});
        result.objects.push_back({"instrument", object_id("instrument", track.instrument_instance_id),
                                  object_id("track", track.id), track.instrument_id, true, false, {}});
    }
    for (std::size_t i = 0; i < state.sample_slots.size(); ++i)
        result.objects.push_back({"sample-slot", object_id("sample-slot", i),
                                  "project:current", "Sample " + std::to_string(i + 1),
                                  true, i == state.selected_sample_slot.value, {}});
    for (const auto &region : state.regions)
        result.objects.push_back(
            {"sample-region", object_id("sample-region", region.id),
             object_id("sample-slot", state.selected_sample_slot.value),
             region.name, true, region.id == state.selected_region, {}});
    for (const auto algorithm : {
             SampleAnalysisAlgorithm::transients,
             SampleAnalysisAlgorithm::quiet_regions,
             SampleAnalysisAlgorithm::equal_divisions,
             SampleAnalysisAlgorithm::tempo_grid}) {
        const auto name = std::string(sample_analysis_algorithm_name(algorithm));
        result.objects.push_back({"sample-analysis-algorithm",
                                  "sample-analysis-algorithm:" + name, {},
                                  name, true, false, {}});
    }
    for (const auto &session : state.sample_analysis_sessions)
        result.objects.push_back(
            {"sample-analysis-session",
             object_id("sample-analysis-session", session.id),
             object_id("sample-slot", session.result.source_slot.value),
             std::string(sample_analysis_algorithm_name(
                 session.result.settings.algorithm)),
             session.status != SampleAnalysisStatus::failed,
             session.id == state.selected_analysis_session,
             session.error});
    for (const auto operation : {
             SampleProcessingOperationType::trim,
             SampleProcessingOperationType::normalize_peak,
             SampleProcessingOperationType::normalize_rms,
             SampleProcessingOperationType::reverse,
             SampleProcessingOperationType::gain,
             SampleProcessingOperationType::fade,
             SampleProcessingOperationType::insert_silence,
             SampleProcessingOperationType::resample,
             SampleProcessingOperationType::channel_convert}) {
        const auto name = std::string(sample_processing_operation_name(operation));
        result.objects.push_back({"sample-processing-operation",
                                  "sample-processing-operation:" + name, {},
                                  name, true, false, {}});
    }
    for (const auto &job : state.sample_processing_jobs)
        result.objects.push_back({"sample-processing-job",
            object_id("sample-processing-job", job.id),
            object_id("sample-slot", job.source.slot.value),
            std::string(sample_processing_status_name(job.status)),
            job.status != SampleProcessingStatus::failed,
            job.id == state.selected_processing_job, job.error});
    for (const auto &pattern : state.patterns)
        result.objects.push_back({"pattern", object_id("pattern", pattern.id),
                                  "arrangement:main", pattern.name, true,
                                  pattern.id == state.selected_pattern, {}});
    for (const auto &clip : state.clips)
        result.objects.push_back({"clip", object_id("clip", clip.id),
                                  "arrangement:main", "Clip " + std::to_string(clip.id),
                                  true, false, {}});
    for (const auto &clip : state.audio_clips)
        result.objects.push_back(
            {"audio-clip", object_id("audio-clip", clip.clip.id),
             object_id("track", clip.clip.track),
             "Audio clip " + std::to_string(clip.clip.id),
             clip.source_available, false, clip.validation_error});
    for (const auto &slot : state.performance_launching.slots)
        result.objects.push_back(
            {"performance-launch-slot", object_id("performance-launch-slot", slot.id),
             object_id("track", slot.track),
             "Launch slot " + std::to_string(slot.id), true, false, {}});
    for (const auto &scene : state.performance_launching.scenes)
        result.objects.push_back(
            {"performance-launch-scene", object_id("performance-launch-scene", scene.id),
             "arrangement:main", scene.name, true, false, {}});
    for (const auto &session : state.live_loops)
        result.objects.push_back(
            {"live-loop-session", object_id("live-loop-session", session.id),
             object_id("track", session.configuration.destination_track),
             "Live loop " + std::to_string(session.id),
             session.state != LiveLoopState::error, false, session.error});
    for (const auto &crossfade : state.audio_crossfades)
        result.objects.push_back(
            {"audio-crossfade", object_id("audio-crossfade", crossfade.id),
             "arrangement:main",
             "Audio crossfade " + std::to_string(crossfade.id), true, false, {}});
    for (std::size_t i = 0; i < state.mixer.configuration.group_count; ++i) {
        const auto &group = state.mixer.configuration.groups[i];
        result.objects.push_back({"mixer-group", object_id("mixer-group", group.id),
                                  "master:main", std::string(group.name.data()), true,
                                  group.id == state.selected_group, {}});
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i)
        result.objects.push_back({"aux-bus", object_id("aux-bus", i), "master:main",
                                  "Aux " + std::to_string(i + 1), true, false, {}});
    const auto &rack = state.mixer.configuration.effects;
    for (std::size_t i = 0; i < rack.instance_count; ++i) {
        const auto &fx = rack.instances[i];
        const auto parent = object_id("fx-instance", fx.id);
        result.objects.push_back({"fx-instance", parent,
                                  "project:current", std::string(effect_processor_type_id(fx.type)),
                                  true, false, {}});
        for (const auto &descriptor : effect_parameter_descriptors(fx.type)) {
            auto base = descriptor.default_value;
            for (std::size_t p = 0; p < fx.parameter_count; ++p)
                if (fx.parameters[p].id == descriptor.id)
                    base = fx.parameters[p].value;
            result.parameters.push_back(
                {"fx." + std::to_string(fx.id) + "." +
                     std::string(descriptor.stable_id),
                 parent, std::string(descriptor.name),
                 std::string(descriptor.unit),
                 descriptor.kind == EffectParameterKind::discrete
                     ? ParameterType::integer
                     : ParameterType::real,
                 static_cast<double>(descriptor.minimum),
                 static_cast<double>(descriptor.maximum),
                 static_cast<double>(descriptor.default_value),
                 static_cast<double>(base), static_cast<double>(base), 0.0,
                 true, true,
                 descriptor.kind == EffectParameterKind::continuous,
                 descriptor.automatable, descriptor.modulatable, {}});
        }
    }
    for (std::size_t i = 0; i < state.modulation.modulator_count; ++i) {
        const auto &mod = state.modulation.modulators[i];
        const auto parent = object_id("modulator", mod.id);
        result.objects.push_back({"modulator", parent,
                                  "project:current", std::string(mod.display_name.data()), true,
                                  false, {}});
        if (const auto *type = find_modulator_type(mod.type))
            for (const auto &parameter : type->parameters) {
                const auto current = modulator_parameter_value(
                    mod.settings, mod.type, parameter.numeric_id);
                result.parameters.push_back(
                    {"modulator." + std::to_string(mod.id) + "." +
                         std::string(parameter.id),
                     parent, std::string(parameter.name),
                     std::string(parameter.unit),
                     parameter.discrete ? ParameterType::integer
                                        : ParameterType::real,
                     static_cast<double>(parameter.minimum),
                     static_cast<double>(parameter.maximum),
                     static_cast<double>(parameter.default_value),
                     static_cast<double>(current), static_cast<double>(current),
                     parameter.discrete ? 1.0 : 0.0, true, true,
                     !parameter.discrete, false, parameter.modulatable, {}});
            }
    }
    for (std::size_t i = 0; i < state.modulation.route_count; ++i)
        result.objects.push_back({"modulation-connection",
                                  object_id("modulation-connection", state.modulation.routes[i].id),
                                  "project:current", "Connection", true, false, {}});
    for (const auto &macro : state.performance_macros.macros)
        result.objects.push_back({"performance-macro", object_id("performance-macro", macro.id),
                                  "project:current", macro.name, true, false, {}});
    for (const auto &scene : state.performance_macros.scenes)
        result.objects.push_back({"scene", object_id("scene", scene.id),
                                  "project:current", scene.name, true, false, {}});
    for (std::size_t i = 0; i < maximum_midi_ports; ++i)
        result.objects.push_back({"midi-port", object_id("midi-port", i),
                                  "project:current", "MIDI " + std::to_string(i + 1),
                                  state.midi_input_connected[i] || state.midi_output_connected[i],
                                  false, (state.midi_input_connected[i] || state.midi_output_connected[i]) ? "" : "not connected"});
    for (const auto &parameter : state.parameters)
        result.parameters.push_back({parameter.id, parameter_parent(parameter.id),
                                     parameter.name, parameter.unit, parameter.type,
                                     parameter.minimum, parameter.maximum,
                                     parameter.default_value, parameter.current_value,
                                     parameter.effective_value, parameter.step, true, true,
                                     parameter.continuous, parameter.automatable,
                                     parameter.modulatable, parameter.choices});
    for (const auto &action : application.actions().actions())
        result.actions.push_back(
            {action, action_target(action.id), action.category});
    result.state_bindings = {
        {"project.name", "project:current", "Current project name", DiscoveryValueKind::text, DiscoveryUpdateRate::state},
        {"project.dirty", "project:current", "Unsaved changes", DiscoveryValueKind::boolean, DiscoveryUpdateRate::state},
        {"transport.playing", "transport:main", "Transport running", DiscoveryValueKind::boolean, DiscoveryUpdateRate::state},
        {"transport.frame", "transport:main", "Transport frame", DiscoveryValueKind::integer, DiscoveryUpdateRate::telemetry},
        {"transport.tempo", "transport:main", "Tempo BPM", DiscoveryValueKind::real, DiscoveryUpdateRate::state},
        {"selection.track", "project:current", "Selected track identity", DiscoveryValueKind::object, DiscoveryUpdateRate::state},
        {"tracks", "project:current", "Track collection", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"patterns", "arrangement:main", "Pattern collection", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"clips", "arrangement:main", "Clip collection", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"audio-clips", "arrangement:main", "Audio clip collection", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"recording.live-loops", "recording-session:main", "Live-loop sessions", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"sample.regions", "project:current", "Sample region collection", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"sample.region-assignments", "project:current", "Region pad assignments", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"sample.analysis-sessions", "project:current", "Transient sample analysis sessions", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"sample.analysis-progress", "project:current", "Sample analysis progress", DiscoveryValueKind::collection, DiscoveryUpdateRate::telemetry},
        {"sample.processing-jobs", "project:current", "Offline sample processing jobs", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"sample.processing-progress", "project:current", "Offline processing progress", DiscoveryValueKind::collection, DiscoveryUpdateRate::telemetry},
        {"mixer.meters", "master:main", "Mixer peak meters", DiscoveryValueKind::collection, DiscoveryUpdateRate::telemetry},
        {"modulation.outputs", "project:current", "Current modulator outputs", DiscoveryValueKind::collection, DiscoveryUpdateRate::telemetry},
        {"macros.effective-values", "project:current", "Effective macro values", DiscoveryValueKind::collection, DiscoveryUpdateRate::telemetry},
        {"recording.status", "recording-session:main", "Recording state", DiscoveryValueKind::object, DiscoveryUpdateRate::state},
        {"preset.application", "project:current", "Preset preparation and activation state", DiscoveryValueKind::object, DiscoveryUpdateRate::state},
        {"preset.missing-assets", "project:current", "Unresolved preset dependencies", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"arrangement.queued-pattern", "arrangement:main", "Queued launch", DiscoveryValueKind::object, DiscoveryUpdateRate::state},
        {"performance.capture", "arrangement:main", "Performance capture phase, counts, and diagnostics", DiscoveryValueKind::object, DiscoveryUpdateRate::state},
        {"performance.activation-history", "arrangement:main", "Acknowledged performance launches in the pending pass", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"performance.track-takeover", "arrangement:main", "Session-owned tracks and queued transitions", DiscoveryValueKind::collection, DiscoveryUpdateRate::state},
        {"validation.errors", "project:current", "Validation diagnostics", DiscoveryValueKind::collection, DiscoveryUpdateRate::state}};
    for (const auto &clip : state.audio_clips) {
        const auto parent = object_id("audio-clip", clip.clip.id);
        const auto prefix = parent + ".";
        result.state_bindings.push_back(
            {prefix + "start", parent, "Musical start",
             DiscoveryValueKind::integer, DiscoveryUpdateRate::state});
        result.state_bindings.push_back(
            {prefix + "duration", parent, "Musical duration",
             DiscoveryValueKind::integer, DiscoveryUpdateRate::state});
        result.state_bindings.push_back(
            {prefix + "source-range", parent, "Source frame range",
             DiscoveryValueKind::object, DiscoveryUpdateRate::state});
        result.state_bindings.push_back(
            {prefix + "available", parent, "Source availability",
             DiscoveryValueKind::boolean, DiscoveryUpdateRate::state});
        result.state_bindings.push_back(
            {prefix + "fade", parent, "Clip-time fade settings",
             DiscoveryValueKind::object, DiscoveryUpdateRate::state});
    }
    for (const auto &crossfade : state.audio_crossfades) {
        const auto parent = object_id("audio-crossfade", crossfade.id);
        result.state_bindings.push_back(
            {parent + ".state", parent, "Crossfade clips, duration and curve",
             DiscoveryValueKind::object, DiscoveryUpdateRate::state});
    }
    result.limits = {{"command-queue", 0, ApplicationService::default_queue_capacity},
                     {"mixer-tracks", state.mixer.configuration.track_count, maximum_mixer_tracks},
                     {"mixer-groups", state.mixer.configuration.group_count, maximum_mixer_groups},
                     {"effect-instances", rack.instance_count, maximum_effect_instances},
                     {"modulator-instances", state.modulation.modulator_count, state.modulation.modulators.size()},
                     {"modulation-connections", state.modulation.route_count, state.modulation.routes.size()},
                     {"performance-macros", state.performance_macros.macros.size(), state.performance_macros.macro_capacity},
                     {"performance-scenes", state.performance_macros.scenes.size(), state.performance_macros.scene_capacity}};
    result.limits.push_back({"sample-analysis-sessions",
                             state.sample_analysis_sessions.size(),
                             SampleAnalysisService::maximum_sessions});
    result.limits.push_back({"sample-processing-jobs",
                             state.sample_processing_jobs.size(),
                             SampleProcessingService::maximum_jobs});
    result.limits.push_back(
        {"audio-clips", state.audio_clips.size(), 1024});
    result.limits.push_back({"performance-launch-slots",
                             state.performance_launching.slots.size(),
                             maximum_launch_slots});
    result.limits.push_back({"performance-launch-scenes",
                             state.performance_launching.scenes.size(),
                             maximum_performance_scenes});
    result.limits.push_back(
        {"live-loop-sessions", state.live_loops.size(),
         LiveLoopService::maximum_sessions});
    if (!state.modulation_graph.valid && !state.modulation_graph.message.empty())
        result.validation_errors.push_back(state.modulation_graph.message);
    if (!state.preset_application.error.empty())
        result.validation_errors.push_back(state.preset_application.error);
    result.validation_errors.insert(result.validation_errors.end(),
                                    state.performance_macros.validation_errors.begin(),
                                    state.performance_macros.validation_errors.end());
    std::sort(result.objects.begin(), result.objects.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(result.parameters.begin(), result.parameters.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    for (auto &parameter : result.parameters) {
        parameter.pickup_recommended = parameter.continuous && parameter.writable;
        if (parameter.type == ParameterType::boolean) {
            parameter.format_style = "boolean";
            parameter.precision = 0;
            parameter.supports_relative = false;
            parameter.supports_high_resolution = false;
        } else if (parameter.type == ParameterType::enumeration) {
            parameter.format_style = "choice";
            parameter.precision = 0;
            parameter.supports_relative = false;
            parameter.supports_high_resolution = false;
        } else if (parameter.type == ParameterType::integer) {
            parameter.format_style = "integer";
            parameter.precision = 0;
        } else if (parameter.unit == "%") {
            parameter.format_style = "percent";
            parameter.precision = 1;
        }
    }
    std::sort(result.actions.begin(), result.actions.end(), [](const auto &a, const auto &b) { return a.descriptor.id < b.descriptor.id; });
    return result;
}

bool supports_discovery_schema(std::uint32_t minimum,
                               std::uint32_t maximum) noexcept {
    return minimum <= application_discovery_schema_version &&
           maximum >= application_discovery_schema_version;
}

bool validate_state_binding(const ApplicationDiscovery &value,
                            std::string_view path, std::string &error) {
    if (std::none_of(value.state_bindings.begin(), value.state_bindings.end(),
                     [&](const auto &binding) { return binding.path == path; })) {
        error = "unknown state binding: " + std::string(path);
        return false;
    }
    error.clear();
    return true;
}

bool validate_discovery(const ApplicationDiscovery &value, std::string &error) {
    if (value.schema_version != application_discovery_schema_version) {
        error = "unsupported discovery schema version";
        return false;
    }
    std::set<std::string> object_ids, object_type_ids, parameter_ids,
        action_ids, bindings;
    for (const auto &type : value.object_types)
        if (type.id.empty() || !object_type_ids.insert(type.id).second) {
            error = "duplicate or empty object type identity"; return false;
        }
    for (const auto &object : value.objects)
        if (object.id.empty() || !object_ids.insert(object.id).second) {
            error = "duplicate or empty object identity"; return false;
        }
    for (const auto &parameter : value.parameters)
        if (parameter.id.empty() || !parameter_ids.insert(parameter.id).second ||
            !object_ids.contains(parameter.parent)) {
            error = "invalid parameter identity or parent"; return false;
        }
    for (const auto &action : value.actions)
        if (action.descriptor.id.empty() ||
            !action_ids.insert(action.descriptor.id).second ||
            (!action.target_type.empty() &&
             !object_type_ids.contains(action.target_type))) {
            error = "duplicate or empty action identity"; return false;
        }
    for (const auto &binding : value.state_bindings)
        if (binding.path.empty() || !bindings.insert(binding.path).second ||
            !object_ids.contains(binding.parent)) {
            error = "invalid state binding or parent"; return false;
        }
    error.clear();
    return true;
}

std::string discovery_json(const ApplicationDiscovery &value) {
    std::ostringstream out;
    out << std::setprecision(9) << "{\"schema\":\"" << escape(value.schema)
        << "\",\"schema_version\":" << value.schema_version
        << ",\"minimum_client_version\":" << value.minimum_client_version;
    out << ",\"capabilities\":[";
    for (std::size_t i = 0; i < value.capabilities.size(); ++i) {
        if (i) out << ',';
        const auto &v = value.capabilities[i];
        out << "{\"id\":\"" << escape(v.id) << "\",\"version\":" << v.version
            << ",\"available\":" << (v.available ? "true" : "false")
            << ",\"reason\":\"" << escape(v.reason) << "\"}";
    }
    out << "],\"object_types\":[";
    for (std::size_t i = 0; i < value.object_types.size(); ++i) {
        if (i) out << ',';
        const auto &v = value.object_types[i];
        out << "{\"id\":\"" << escape(v.id) << "\",\"name\":\"" << escape(v.display_name)
            << "\",\"creatable\":" << (v.creatable ? "true" : "false")
            << ",\"deletable\":" << (v.deletable ? "true" : "false")
            << ",\"selectable\":" << (v.selectable ? "true" : "false") << '}';
    }
    out << "],\"objects\":[";
    for (std::size_t i = 0; i < value.objects.size(); ++i) {
        if (i) out << ',';
        const auto &v = value.objects[i];
        out << "{\"type\":\"" << escape(v.type) << "\",\"id\":\"" << escape(v.id)
            << "\",\"parent\":\"" << escape(v.parent) << "\",\"name\":\"" << escape(v.display_name)
            << "\",\"available\":" << (v.available ? "true" : "false")
            << ",\"selected\":" << (v.selected ? "true" : "false")
            << ",\"diagnostic\":\"" << escape(v.diagnostic) << "\"}";
    }
    out << "],\"parameters\":[";
    for (std::size_t i = 0; i < value.parameters.size(); ++i) {
        if (i) out << ',';
        const auto &v = value.parameters[i];
        out << "{\"id\":\"" << escape(v.id) << "\",\"parent\":\"" << escape(v.parent)
            << "\",\"name\":\"" << escape(v.display_name) << "\",\"type\":\"" << parameter_type(v.type)
            << "\",\"unit\":\"" << escape(v.unit) << "\",\"minimum\":"; json_value(out, v.minimum);
        out << ",\"maximum\":"; json_value(out, v.maximum); out << ",\"default\":"; json_value(out, v.default_value);
        out << ",\"base\":"; json_value(out, v.base_value); out << ",\"effective\":"; json_value(out, v.effective_value);
        out << ",\"step\":" << v.step << ",\"readable\":" << (v.readable ? "true" : "false")
            << ",\"writable\":" << (v.writable ? "true" : "false")
            << ",\"continuous\":" << (v.continuous ? "true" : "false")
            << ",\"automatable\":" << (v.automatable ? "true" : "false")
            << ",\"modulatable\":" << (v.modulatable ? "true" : "false")
            << ",\"format_hint\":{\"style\":\"" << escape(v.format_style)
            << "\",\"precision\":" << static_cast<unsigned>(v.precision)
            << ",\"absolute\":" << (v.supports_absolute ? "true" : "false")
            << ",\"relative\":" << (v.supports_relative ? "true" : "false")
            << ",\"pickup\":" << (v.pickup_recommended ? "true" : "false")
            << ",\"high_resolution\":" << (v.supports_high_resolution ? "true" : "false")
            << "},\"choices\":[";
        for (std::size_t c = 0; c < v.choices.size(); ++c) { if (c) out << ','; out << '"' << escape(v.choices[c]) << '"'; }
        out << "]}";
    }
    out << "],\"actions\":[";
    for (std::size_t i = 0; i < value.actions.size(); ++i) {
        if (i) out << ',';
        const auto &v = value.actions[i];
        const auto &a = v.descriptor;
        out << "{\"id\":\"" << escape(a.id) << "\",\"name\":\"" << escape(a.name)
            << "\",\"description\":\"" << escape(a.description) << "\",\"category\":\"" << escape(a.category)
            << "\",\"target_type\":\"" << escape(v.target_type)
            << "\",\"capability\":\"" << escape(v.capability)
            << "\",\"available\":" << (a.available ? "true" : "false") << ",\"context\":\"" << escape(a.context)
            << "\",\"unavailable_reason\":\"" << escape(a.unavailable_reason) << "\",\"arguments\":[";
        for (std::size_t j = 0; j < a.arguments.size(); ++j) { if (j) out << ','; const auto &arg = a.arguments[j];
            out << "{\"name\":\"" << escape(arg.name) << "\",\"type\":\"" << value_type(arg.type)
                << "\",\"required\":" << (arg.required ? "true" : "false") << '}'; }
        out << "]}";
    }
    out << "],\"state_bindings\":[";
    for (std::size_t i = 0; i < value.state_bindings.size(); ++i) { if (i) out << ','; const auto &v = value.state_bindings[i];
        out << "{\"path\":\"" << escape(v.path) << "\",\"parent\":\"" << escape(v.parent)
            << "\",\"description\":\"" << escape(v.description) << "\",\"type\":\"" << binding_kind(v.kind)
            << "\",\"update_rate\":\"" << update_rate(v.update_rate) << "\"}"; }
    out << "],\"limits\":[";
    for (std::size_t i = 0; i < value.limits.size(); ++i) { if (i) out << ','; const auto &v = value.limits[i];
        out << "{\"id\":\"" << escape(v.id) << "\",\"used\":" << v.used << ",\"maximum\":" << v.maximum << '}'; }
    out << "],\"validation_errors\":[";
    for (std::size_t i = 0; i < value.validation_errors.size(); ++i) { if (i) out << ','; out << '"' << escape(value.validation_errors[i]) << '"'; }
    out << "]}";
    return out.str();
}
} // namespace neurogenic
