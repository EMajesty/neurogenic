#include "neurogenic/application/control.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/dsp/voice_modulation.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

namespace neurogenic {
namespace {
ActionDescriptor action(std::string id, std::string name, std::string category,
                        std::vector<ArgumentSpec> arguments = {},
                        bool available = true, std::string reason = {}) {
    auto description = name + " application action";
    return {std::move(id),
            std::move(name),
            std::move(description),
            std::move(category),
            std::move(arguments),
            available,
            {},
            std::move(reason)};
}
template <class T>
const T *argument(const ApplicationCommand &command, std::string_view name) {
    const auto found = command.arguments.find(std::string(name));
    return found == command.arguments.end() ? nullptr
                                            : std::get_if<T>(&found->second);
}
bool is_numeric(const ApplicationValue &value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}
double number(const ApplicationValue &value) {
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    return std::get<double>(value);
}
bool publish_dsp_parameter(AudioEngine &engine, ParameterRegistry &parameters,
                           const std::string &id) {
    const auto descriptor = parameters.get(id);
    if (!descriptor ||
        !std::holds_alternative<double>(descriptor->current_value))
        return true;
    const auto value =
        static_cast<float>(std::get<double>(descriptor->current_value));
    if (id == "synth.waveform")
        return engine.set_synth_parameter(SynthParameter::waveform, value);
    if (id == "synth.attack")
        return engine.set_synth_parameter(SynthParameter::attack, value);
    if (id == "synth.decay")
        return engine.set_synth_parameter(SynthParameter::decay, value);
    if (id == "synth.sustain")
        return engine.set_synth_parameter(SynthParameter::sustain, value);
    if (id == "synth.release")
        return engine.set_synth_parameter(SynthParameter::release, value);
    if (id == "synth.cutoff")
        return engine.set_synth_parameter(SynthParameter::cutoff, value);
    if (id == "synth.resonance")
        return engine.set_synth_parameter(SynthParameter::resonance, value);
    if (id == "synth.pulse-width")
        return engine.set_synth_parameter(SynthParameter::pulse_width, value);
    if (id == "synth.gain")
        return engine.set_synth_parameter(SynthParameter::gain, value);
    if (id == "synth.pitch")
        return engine.set_synth_parameter(SynthParameter::pitch, value);
    if (id == "effect.delay.time")
        return engine.set_delay_parameter(DelayParameter::time, value);
    if (id == "effect.delay.feedback")
        return engine.set_delay_parameter(DelayParameter::feedback, value);
    if (id == "effect.delay.mix")
        return engine.set_delay_parameter(DelayParameter::mix, value);
    return true;
}
std::optional<TriggerCondition>
parse_trigger_condition(std::string_view value) {
    if (value == "always")
        return TriggerCondition::always;
    if (value == "first")
        return TriggerCondition::first_loop;
    if (value == "not-first")
        return TriggerCondition::not_first_loop;
    if (value == "every-n")
        return TriggerCondition::every_nth_loop;
    if (value == "n-of-m")
        return TriggerCondition::n_of_m;
    if (value == "previous-fired")
        return TriggerCondition::previous_fired;
    if (value == "previous-not-fired")
        return TriggerCondition::previous_not_fired;
    if (value == "fill")
        return TriggerCondition::fill_active;
    if (value == "not-fill")
        return TriggerCondition::fill_inactive;
    return std::nullopt;
}
std::string_view trigger_condition_name(TriggerCondition value) {
    static constexpr std::string_view names[]{
        "always",  "first",          "not-first",          "every-n",
        "n-of-m",  "previous-fired", "previous-not-fired", "fill",
        "not-fill"};
    return names[static_cast<std::size_t>(value)];
}
const MusicalPattern *edit_pattern(const Timeline &timeline,
                                   PatternId pattern_id, TrackId track_id) {
    if (pattern_id == 0) {
        const auto track = std::find_if(
            timeline.tracks.begin(), timeline.tracks.end(),
            [=](const auto &value) { return value.id == track_id; });
        return track == timeline.tracks.end() ? nullptr : &track->pattern;
    }
    const auto definition = std::find_if(
        timeline.arrangement.patterns.begin(),
        timeline.arrangement.patterns.end(),
        [=](const auto &value) { return value.id == pattern_id; });
    if (definition == timeline.arrangement.patterns.end()) return nullptr;
    const auto track = std::find_if(
        definition->tracks.begin(), definition->tracks.end(),
        [=](const auto &value) { return value.track == track_id; });
    return track == definition->tracks.end() ? nullptr : &track->pattern;
}
MusicalTick edit_pattern_length(const Timeline &timeline, PatternId pattern_id,
                                TrackId track_id) {
    if (const auto *pattern = edit_pattern(timeline, pattern_id, track_id))
        return pattern->length;
    if (pattern_id != 0) {
        const auto definition = std::find_if(
            timeline.arrangement.patterns.begin(),
            timeline.arrangement.patterns.end(),
            [=](const auto &value) { return value.id == pattern_id; });
        if (definition != timeline.arrangement.patterns.end())
            return definition->length;
    }
    return 0;
}
} // namespace

ActionRegistry::ActionRegistry() {
    std::string error;
    const auto add = [&](ActionDescriptor value) {
        if (!register_action(std::move(value), error))
            throw std::logic_error(error);
    };
    add(action("transport.play.v1", "Play", "Transport"));
    add(action("transport.stop.v1", "Stop", "Transport"));
    add(action("transport.restart.v1", "Restart", "Transport"));
    add(action("arrangement.seek.v1", "Seek arrangement", "Arrangement",
               {{"tick", ValueType::integer}}));
    add(action("track.select.v1", "Select track", "Tracks",
               {{"track_id", ValueType::integer}}));
    add(action("track.next.v1", "Next track", "Tracks"));
    add(action("track.previous.v1", "Previous track", "Tracks"));
    add(action("track.mute-toggle.v1", "Toggle track mute", "Tracks",
               {{"track_id", ValueType::integer, false}}));
    add(action("track.create.v1", "Create track", "Tracks",
               {{"track_id", ValueType::integer, false},
                {"name", ValueType::text, false},
                {"length", ValueType::integer},
                {"instrument_id", ValueType::text, false},
                {"instance_id", ValueType::integer, false}}));
    add(action("track.delete.v1", "Delete track", "Tracks",
               {{"track_id", ValueType::integer, false}}));
    add(action("track.rename.v1", "Rename track", "Tracks",
               {{"track_id", ValueType::integer, false},
                {"name", ValueType::text}}));
    add(action("track.duplicate.v1", "Duplicate track", "Tracks",
               {{"track_id", ValueType::integer, false},
                {"name", ValueType::text, false}}));
    add(action("track.pattern-length-set.v1", "Set track pattern length",
               "Tracks", {{"length", ValueType::integer}}));
    add(action("track.instrument-set.v1", "Set track instrument", "Tracks",
               {{"instrument_id", ValueType::text},
                {"instance_id", ValueType::integer, false}}));
    add(action(
        "track.midi-destination-set.v1", "Set external MIDI destination",
        "Tracks",
        {{"port", ValueType::integer}, {"channel", ValueType::integer}}));
    add(action("instrument.instance-parameter-set.v1",
               "Set instrument instance parameter", "Instruments",
               {{"instance_id", ValueType::integer},
                {"parameter_id", ValueType::text},
                {"value", ValueType::real}}));
    add(action("pattern.select.v1", "Select pattern", "Sequencer",
               {{"pattern_id", ValueType::integer}}));
    add(action("pattern.previous.v1", "Select previous pattern", "Sequencer"));
    add(action("pattern.next.v1", "Select next pattern", "Sequencer"));
    add(action("pattern.create.v1", "Create pattern", "Arrangement",
               {{"id", ValueType::integer, false},
                {"name", ValueType::text},
                {"length", ValueType::integer}}));
    add(action("pattern.duplicate.v1", "Duplicate pattern", "Arrangement",
               {{"source", ValueType::integer},
                {"id", ValueType::integer, false},
                {"name", ValueType::text}}));
    add(action("pattern.rename.v1", "Rename pattern", "Arrangement",
               {{"id", ValueType::integer}, {"name", ValueType::text}}));
    add(action("pattern.length-set.v1", "Set pattern length", "Arrangement",
               {{"id", ValueType::integer}, {"length", ValueType::integer}}));
    add(action("pattern.clear.v1", "Clear pattern track", "Arrangement",
               {{"id", ValueType::integer},
                {"track_id", ValueType::integer, false}}));
    add(action("pattern.delete.v1", "Delete pattern", "Arrangement",
               {{"id", ValueType::integer}}));
    add(action(
        "pattern.launch.v1", "Launch pattern", "Arrangement",
        {{"id", ValueType::integer}, {"queued", ValueType::boolean, false}}));
    add(action("performance.slot-assign.v1", "Assign performance launch slot",
               "Performance", {{"slot", ValueType::integer},
               {"track", ValueType::integer}, {"source_type", ValueType::text},
               {"source", ValueType::integer}, {"behavior", ValueType::text, false}}));
    add(action("performance.slot-clear.v1", "Clear performance launch slot",
               "Performance", {{"slot", ValueType::integer}}));
    add(action("performance.pad-assign.v1", "Assign logical performance pad",
               "Performance", {{"position", ValueType::integer},
               {"slot", ValueType::integer}}));
    add(action("performance.pad-clear.v1", "Clear logical performance pad",
               "Performance", {{"position", ValueType::integer}}));
    add(action("performance.slot-launch.v1", "Launch performance slot",
               "Performance", {{"slot", ValueType::integer}}));
    add(action("performance.slot-stop.v1", "Stop performance slot",
               "Performance", {{"slot", ValueType::integer}}));
    add(action("performance.slot-retrigger.v1", "Retrigger performance slot",
               "Performance", {{"slot", ValueType::integer}}));
    add(action("performance.slot-cancel.v1", "Cancel queued performance slot",
               "Performance", {{"slot", ValueType::integer}}));
    add(action("performance.slot-duplicate.v1", "Duplicate performance slot",
               "Performance", {{"source", ValueType::integer},
               {"slot", ValueType::integer}}));
    add(action("performance.scene-launch.v1", "Launch performance scene",
               "Performance", {{"scene", ValueType::integer}}));
    add(action("performance.return-arrangement.v1", "Return to arrangement",
               "Performance", {{"track", ValueType::integer, false},
               {"quantization", ValueType::text, false}}));
    add(action("performance.scene-create.v1", "Create performance scene",
               "Performance", {{"scene", ValueType::integer}, {"name", ValueType::text}}));
    add(action("performance.scene-delete.v1", "Delete performance scene",
               "Performance", {{"scene", ValueType::integer}}));
    add(action("performance.scene-rename.v1", "Rename performance scene",
               "Performance", {{"scene", ValueType::integer}, {"name", ValueType::text}}));
    add(action("performance.scene-duplicate.v1", "Duplicate performance scene",
               "Performance", {{"source", ValueType::integer}, {"scene", ValueType::integer},
               {"name", ValueType::text, false}}));
    add(action("performance.scene-slot-set.v1", "Assign slot to performance scene",
               "Performance", {{"scene", ValueType::integer}, {"track", ValueType::integer},
               {"slot", ValueType::integer}}));
    add(action("performance.scene-slot-remove.v1", "Remove track from performance scene",
               "Performance", {{"scene", ValueType::integer}, {"track", ValueType::integer}}));
    add(action("performance.slot-follow-set.v1", "Set launch-slot follow action",
               "Performance", {{"slot", ValueType::integer}, {"action", ValueType::text},
               {"target", ValueType::integer, false}, {"repetitions", ValueType::integer, false},
               {"seed", ValueType::integer, false}}));
    add(action("performance.capture-arm.v1", "Arm performance capture",
               "Performance", {{"armed", ValueType::boolean}}));
    add(action("performance.capture-start.v1", "Start performance capture",
               "Performance", {{"mode", ValueType::text},
               {"quantization", ValueType::text, false}}));
    add(action("performance.capture-stop.v1", "Stop performance capture", "Performance",
               {{"quantization", ValueType::text, false}}));
    add(action("performance.capture-commit.v1", "Commit performance capture", "Performance"));
    add(action("performance.capture-discard.v1", "Discard performance capture", "Performance"));
    add(action("performance.velocity-policy.v1", "Set performance velocity policy",
               "Performance", {{"policy", ValueType::text},
               {"velocity", ValueType::real, false}}));
    add(action("arrangement.mode-set.v1", "Set playback mode", "Arrangement",
               {{"mode", ValueType::text}}));
    add(action("arrangement.clip-create.v1", "Create clip", "Arrangement",
               {{"id", ValueType::integer, false},
                {"pattern", ValueType::integer},
                {"start", ValueType::integer},
                {"duration", ValueType::integer},
                {"loop", ValueType::boolean, false},
                {"transpose", ValueType::integer, false},
                {"velocity", ValueType::real, false}}));
    add(action("arrangement.clip-delete.v1", "Delete clip", "Arrangement",
               {{"id", ValueType::integer}}));
    add(action("arrangement.clip-duplicate.v1", "Duplicate clip",
               "Arrangement", {{"source", ValueType::integer},
                {"id", ValueType::integer, false},
                {"start", ValueType::integer, false}}));
    add(action("arrangement.clip-update.v1", "Move or resize clip",
               "Arrangement",
               {{"id", ValueType::integer},
                {"sample_slot", ValueType::integer, false},
                {"region_id", ValueType::integer, false},
                {"source_start", ValueType::integer, false},
                {"source_end", ValueType::integer, false},
                {"loop_start", ValueType::integer, false},
                {"loop_end", ValueType::integer, false},
                {"start", ValueType::integer, false},
                {"duration", ValueType::integer, false},
                {"transpose", ValueType::integer, false},
                {"velocity", ValueType::real, false},
                {"enabled", ValueType::boolean, false}}));
    add(action("arrangement.audio-clip-create.v1", "Create audio clip",
               "Arrangement",
               {{"id", ValueType::integer, false},
                {"track", ValueType::integer},
                {"sample_slot", ValueType::integer},
                {"region_id", ValueType::integer, false},
                {"start", ValueType::integer},
                {"duration", ValueType::integer},
                {"source_start", ValueType::integer, false},
                {"source_end", ValueType::integer, false},
                {"loop", ValueType::boolean, false},
                {"loop_start", ValueType::integer, false},
                {"loop_end", ValueType::integer, false},
                {"gain", ValueType::real, false},
                {"pan", ValueType::real, false},
                {"direction", ValueType::text, false},
                {"stretch", ValueType::text, false},
                {"source_bpm", ValueType::real, false},
                {"pitch", ValueType::real, false}}));
    add(action("arrangement.audio-clip-update.v1", "Edit audio clip",
               "Arrangement",
               {{"id", ValueType::integer},
                {"track", ValueType::integer, false},
                {"start", ValueType::integer, false},
                {"duration", ValueType::integer, false},
                {"gain", ValueType::real, false},
                {"pan", ValueType::real, false},
                {"loop", ValueType::boolean, false},
                {"enabled", ValueType::boolean, false},
                {"direction", ValueType::text, false},
                {"stretch", ValueType::text, false},
                {"source_bpm", ValueType::real, false},
                {"pitch", ValueType::real, false}}));
    add(action("arrangement.audio-clip-duplicate.v1", "Duplicate audio clip",
               "Arrangement",
               {{"source", ValueType::integer},
                {"id", ValueType::integer, false},
                {"start", ValueType::integer, false}}));
    add(action("arrangement.audio-clip-delete.v1", "Delete audio clip",
               "Arrangement", {{"id", ValueType::integer}}));
    add(action("arrangement.audio-clip-fade-set.v1", "Set audio clip fades",
               "Arrangement", {{"id", ValueType::integer},
                 {"fade_in", ValueType::integer},
                 {"fade_out", ValueType::integer},
                 {"curve", ValueType::text}}));
    add(action("arrangement.audio-crossfade-create.v1", "Create audio crossfade",
               "Arrangement", {{"id", ValueType::integer, false},
                 {"outgoing", ValueType::integer}, {"incoming", ValueType::integer},
                 {"duration", ValueType::integer}, {"curve", ValueType::text}}));
    add(action("arrangement.audio-crossfade-update.v1", "Edit audio crossfade",
               "Arrangement", {{"id", ValueType::integer},
                 {"duration", ValueType::integer, false},
                 {"curve", ValueType::text, false},
                 {"enabled", ValueType::boolean, false}}));
    add(action("arrangement.audio-crossfade-delete.v1", "Delete audio crossfade",
               "Arrangement", {{"id", ValueType::integer}}));
    add(action("arrangement.range-set.v1", "Set song range", "Arrangement",
               {{"start", ValueType::integer}, {"end", ValueType::integer}}));
    add(action("arrangement.loop-set.v1", "Set arrangement loop", "Arrangement",
               {{"enabled", ValueType::boolean},
                {"start", ValueType::integer},
                {"end", ValueType::integer}}));
    add(action("pattern.chain-entry-set.v1", "Set pattern chain entry",
               "Arrangement",
               {{"index", ValueType::integer},
                {"pattern", ValueType::integer},
                {"repeats", ValueType::integer}}));
    add(action("pattern.chain-play.v1", "Start pattern chain", "Arrangement",
               {{"entry", ValueType::integer, false},
                {"loop", ValueType::boolean, false}}));
    add(action("pattern.chain-stop.v1", "Stop pattern chain", "Arrangement"));
    add(action("pattern.chain-reset.v1", "Reset pattern chain", "Arrangement"));
    add(action("pattern.follow-set.v1", "Set pattern follow action",
               "Arrangement",
               {{"pattern", ValueType::integer},
                {"action", ValueType::text},
                {"repetitions", ValueType::integer},
                {"probability", ValueType::real, false},
                {"target", ValueType::integer, false}}));
    add(action("pattern.launch-quantization.v1", "Set launch quantization",
               "Arrangement", {{"quantization", ValueType::text}}));
    add(action("pattern.voice-transition.v1", "Set voice transition policy",
               "Arrangement", {{"policy", ValueType::text}}));
    add(action("arrangement.song-end.v1", "Set song-end behavior",
               "Arrangement",
               {{"behavior", ValueType::text},
                {"pattern", ValueType::integer, false}}));
    add(action("arrangement.return.v1", "Return to arrangement",
               "Arrangement"));
    add(action("arrangement.record-arm.v1", "Arm arrangement recording",
               "Arrangement", {{"armed", ValueType::boolean}}));
    add(action("arrangement.record-start.v1", "Start arrangement recording",
               "Arrangement",
               {{"mode", ValueType::text},
                {"count_in_bars", ValueType::integer, false},
                {"automation", ValueType::boolean, false}}));
    add(action("arrangement.record-stop.v1", "Stop arrangement recording",
               "Arrangement"));
    add(action("arrangement.record-commit.v1", "Commit arrangement recording",
               "Arrangement"));
    add(action("arrangement.record-discard.v1", "Discard arrangement recording",
               "Arrangement"));
    add(action("arrangement.automation-arm.v1", "Arm automation recording",
               "Arrangement", {{"armed", ValueType::boolean}}));
    add(action("arrangement.automation-thinning.v1", "Set automation thinning",
               "Arrangement", {{"ticks", ValueType::integer}}));
    add(action("arrangement.automation-update.v1", "Edit recorded automation",
               "Arrangement",
               {{"id", ValueType::integer},
                {"tick", ValueType::integer},
                {"value", ValueType::real}}));
    add(action("arrangement.automation-delete.v1", "Delete recorded automation",
               "Arrangement", {{"id", ValueType::integer}}));
    add(action("macro.create.v1", "Create performance macro", "Performance",
               {{"name", ValueType::text},
                {"owner", ValueType::text},
                {"owner_id", ValueType::integer, false},
                {"value", ValueType::real, false}}));
    add(action("macro.delete.v1", "Delete performance macro", "Performance",
               {{"id", ValueType::integer}}));
    add(action("macro.rename.v1", "Rename performance macro", "Performance",
               {{"id", ValueType::integer},
                {"name", ValueType::text},
                {"description", ValueType::text, false}}));
    add(action("macro.value-set.v1", "Set performance macro", "Performance",
               {{"id", ValueType::integer}, {"value", ValueType::real}}));
    add(action("macro.assignment-set.v1", "Assign macro destination",
               "Performance",
               {{"macro_id", ValueType::integer},
                {"destination", ValueType::text},
                {"minimum", ValueType::real},
                {"maximum", ValueType::real},
                {"curve", ValueType::text, false},
                {"polarity", ValueType::text, false},
                {"smoothing", ValueType::real, false},
                {"steps", ValueType::integer, false}}));
    add(action("macro.assignment-update.v1", "Update macro destination",
               "Performance",
               {{"macro_id", ValueType::integer},
                {"assignment_id", ValueType::integer},
                {"destination", ValueType::text},
                {"minimum", ValueType::real},
                {"maximum", ValueType::real},
                {"curve", ValueType::text, false},
                {"polarity", ValueType::text, false},
                {"smoothing", ValueType::real, false},
                {"steps", ValueType::integer, false}}));
    add(action("macro.assignment-delete.v1", "Remove macro destination",
               "Performance",
               {{"macro_id", ValueType::integer},
                {"assignment_id", ValueType::integer}}));
    add(action("macro.scene-capture.v1", "Capture performance scene",
               "Performance",
               {{"name", ValueType::text},
                {"duration", ValueType::real, false},
                {"tempo_sync", ValueType::boolean, false},
                {"curve", ValueType::text, false}}));
    add(action("macro.scene-delete.v1", "Delete performance scene",
               "Performance", {{"id", ValueType::integer}}));
    add(action("macro.scene-rename.v1", "Rename performance scene",
               "Performance", {{"id", ValueType::integer},
                                {"name", ValueType::text}}));
    add(action("macro.scene-update.v1", "Update performance scene",
               "Performance", {{"id", ValueType::integer},
                                {"name", ValueType::text},
                                {"duration", ValueType::real, false},
                                {"tempo_sync", ValueType::boolean, false},
                                {"curve", ValueType::text, false}}));
    add(action("macro.scene-duplicate.v1", "Duplicate performance scene",
               "Performance", {{"id", ValueType::integer},
                                {"name", ValueType::text}}));
    add(action("macro.scene-recall.v1", "Recall performance scene",
               "Performance",
               {{"id", ValueType::integer},
                {"duration", ValueType::real, false},
                {"tempo_sync", ValueType::boolean, false},
                {"curve", ValueType::text, false},
                {"start_frame", ValueType::integer, false}}));
    add(action("macro.morph-stop.v1", "Stop performance morph",
               "Performance"));
    add(action("macro.query.v1", "Query performance macros", "Performance"));
    add(action("step.select.v1", "Select step", "Sequencer",
               {{"step", ValueType::integer}}));
    add(action("step.toggle.v1", "Toggle step", "Sequencer",
               {{"step", ValueType::integer, false},
                {"velocity", ValueType::real, false},
                {"duration_ticks", ValueType::integer, false}}));
    add(action("step.erase.v1", "Erase step events", "Sequencer",
               {{"pattern_id", ValueType::integer},
                {"track_id", ValueType::integer},
                {"step", ValueType::integer}}));
    add(action("step.event-select.v1", "Select event at step", "Sequencer",
               {{"step", ValueType::integer, false},
                {"event_id", ValueType::integer, false}}));
    add(action("step.property-select.v1", "Select step property", "Sequencer",
               {{"property", ValueType::text}}));
    add(action("step.velocity-set.v1", "Set step velocity", "Sequencer",
               {{"velocity", ValueType::real}}));
    add(action("step.duration-set.v1", "Set step duration", "Sequencer",
               {{"duration_ticks", ValueType::integer}}));
    add(action("step.pitch-set.v1", "Set step pitch", "Sequencer",
               {{"pitch", ValueType::integer}}));
    add(action("step.timing-offset-set.v1", "Set step timing offset",
               "Sequencer", {{"offset_ticks", ValueType::integer}}));
    add(action("step.property-reset.v1", "Reset step property", "Sequencer",
               {{"property", ValueType::text}}));
    add(action("step.lock-select.v1", "Select parameter lock", "Sequencer",
               {{"parameter_id", ValueType::text}}));
    add(action(
        "step.lock-set.v1", "Set parameter lock", "Sequencer",
        {{"parameter_id", ValueType::text}, {"value", ValueType::real}}));
    add(action("step.lock-remove.v1", "Remove parameter lock", "Sequencer",
               {{"parameter_id", ValueType::text}}));
    add(action("step.locks-clear.v1", "Clear step locks", "Sequencer"));
    add(action("step.probability-set.v1", "Set trigger probability",
               "Sequencer", {{"probability", ValueType::real}}));
    add(action("step.condition-set.v1", "Set trigger condition", "Sequencer",
               {{"condition", ValueType::text},
                {"n", ValueType::integer, false},
                {"m", ValueType::integer, false}}));
    add(action("step.ratchet-set.v1", "Set ratchet count", "Sequencer",
               {{"count", ValueType::integer}}));
    add(action("step.trigger-reset.v1", "Reset trigger properties",
               "Sequencer"));
    add(action("performance.fill-set.v1", "Set momentary Fill", "Performance",
               {{"active", ValueType::boolean}}));
    add(action("performance.fill-latch.v1", "Toggle latched Fill",
               "Performance"));
    add(action("timeline.seed-set.v1", "Set deterministic seed", "Generation",
               {{"seed", ValueType::integer}}));
    add(action("step.page-next.v1", "Next step page", "Sequencer"));
    add(action("step.page-previous.v1", "Previous step page", "Sequencer"));
    add(action("step.page-select.v1", "Select step page", "Sequencer",
               {{"page", ValueType::integer},
                {"pattern_id", ValueType::integer, false},
                {"track_id", ValueType::integer, false}}));
    add(action("step.resolution-set.v1", "Set step resolution", "Sequencer",
               {{"resolution", ValueType::text}}));
    add(action("track.note-target-set.v1", "Set track note target", "Tracks",
               {{"note", ValueType::integer}}));
    add(action("sample.slot-select.v1", "Select sample slot", "Samples",
               {{"slot_id", ValueType::integer}}));
    add(action("sample.load.v1", "Load sample", "Samples",
               {{"slot_id", ValueType::integer}, {"path", ValueType::text}}));
    add(action("sample.replace.v1", "Replace sample", "Samples",
               {{"slot_id", ValueType::integer}, {"path", ValueType::text}}));
    add(action("sample.unload.v1", "Unload sample", "Samples",
               {{"slot_id", ValueType::integer}}));
    add(action("slice.select.v1", "Select slice", "Samples",
               {{"slice_id", ValueType::integer}}));
    add(action("slice.create.v1", "Create slice", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer},
                {"name", ValueType::text, false}}));
    add(action("slice.delete.v1", "Delete slice", "Samples"));
    add(action("slice.bounds-set.v1", "Set slice boundaries", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer}}));
    add(action("slice.rename.v1", "Rename slice", "Samples",
               {{"name", ValueType::text}}));
    add(action("slice.duplicate.v1", "Duplicate slice", "Samples"));
    add(action("slice.clear.v1", "Clear slices", "Samples"));
    add(action("slice.equal-divisions.v1", "Slice equally", "Samples",
               {{"divisions", ValueType::integer}}));
    add(action("slice.detect-transients.v1", "Detect transients", "Samples",
               {{"sensitivity", ValueType::real},
                {"minimum_frames", ValueType::integer}}));
    add(action("sample-analysis.start.v1", "Start sample analysis", "Samples",
               {{"algorithm", ValueType::text},
                {"sensitivity", ValueType::real, false},
                {"minimum_frames", ValueType::integer, false},
                {"divisions", ValueType::integer, false},
                {"source_bpm", ValueType::real, false},
                {"beats_per_slice", ValueType::real, false}}));
    add(action("sample-analysis.cancel.v1", "Cancel sample analysis", "Samples"));
    add(action("sample-analysis.discard.v1", "Discard sample analysis", "Samples"));
    add(action("sample-analysis.candidate-add.v1", "Add analysis boundary", "Samples",
               {{"frame", ValueType::integer}}));
    add(action("sample-analysis.candidate-move.v1", "Move analysis boundary", "Samples",
               {{"candidate_id", ValueType::integer}, {"frame", ValueType::integer}}));
    add(action("sample-analysis.candidate-remove.v1", "Remove analysis boundary", "Samples",
               {{"candidate_id", ValueType::integer}}));
    add(action("sample-analysis.apply.v1", "Apply sample analysis", "Samples",
               {{"target", ValueType::text}, {"mode", ValueType::text},
                {"assign_pads", ValueType::boolean, false},
                {"starting_pad", ValueType::integer, false}}));
    add(action("sample-processing.create.v1", "Create sample processing job", "Samples",
               {{"source", ValueType::text}, {"source_id", ValueType::integer, false},
                {"loop_frames", ValueType::integer, false}}));
    add(action("sample-processing.operation-add.v1", "Add processing operation", "Samples",
               {{"operation", ValueType::text}, {"value", ValueType::real, false},
                {"start_frame", ValueType::integer, false}, {"end_frame", ValueType::integer, false},
                {"frames", ValueType::integer, false}, {"sample_rate", ValueType::integer, false},
                {"channels", ValueType::integer, false}}));
    add(action("sample-processing.operation-remove.v1", "Remove processing operation", "Samples",
               {{"operation_id", ValueType::integer}}));
    add(action("sample-processing.operation-move.v1", "Reorder processing operation", "Samples",
               {{"operation_id", ValueType::integer}, {"position", ValueType::integer}}));
    add(action("sample-processing.validate.v1", "Validate sample processing", "Samples"));
    add(action("sample-processing.start.v1", "Start sample processing", "Samples"));
    add(action("sample-processing.cancel.v1", "Cancel sample processing", "Samples"));
    add(action("sample-processing.discard.v1", "Discard sample processing", "Samples"));
    add(action("sample-processing.save.v1", "Save processed sample", "Samples",
               {{"path", ValueType::text}, {"format", ValueType::text},
                {"overwrite", ValueType::boolean, false}}));
    add(action("sample-processing.publish.v1", "Load processed sample into slot", "Samples",
               {{"slot_id", ValueType::integer}}));
    add(action("slice.pad-assign.v1", "Assign slice to pad", "Samples",
               {{"pad", ValueType::integer}}));
    add(action("slice.pad-clear.v1", "Clear pad slice", "Samples",
               {{"pad", ValueType::integer}}));
    add(action("slice.preview.v1", "Preview slice", "Samples"));
    add(action("sample.preview.v1", "Preview sample", "Samples"));
    add(action("slice.preview-stop.v1", "Stop preview", "Samples"));
    add(action("region.select.v1", "Select sample region", "Samples",
               {{"region_id", ValueType::integer}}));
    add(action("region.create.v1", "Create sample region", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer},
                {"name", ValueType::text, false}}));
    add(action("region.from-slice.v1", "Create region from slice", "Samples"));
    add(action("region.delete.v1", "Delete sample region", "Samples"));
    add(action("region.duplicate.v1", "Duplicate sample region", "Samples"));
    add(action("region.rename.v1", "Rename sample region", "Samples",
               {{"name", ValueType::text}}));
    add(action("region.bounds-set.v1", "Set region boundaries", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer}}));
    add(action("region.fades-set.v1", "Set region fades", "Samples",
               {{"fade_in_frames", ValueType::integer},
                {"fade_out_frames", ValueType::integer},
                {"curve", ValueType::text}}));
    add(action("region.gain-set.v1", "Set region gain", "Samples",
               {{"gain", ValueType::real}}));
    add(action("region.direction-set.v1", "Set region direction", "Samples",
               {{"direction", ValueType::text}}));
    add(action("region.loop-set.v1", "Set region loop", "Samples",
               {{"enabled", ValueType::boolean},
                {"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer},
                {"crossfade_frames", ValueType::integer}}));
    add(action("region.pad-assign.v1", "Assign region to pad", "Samples",
               {{"pad", ValueType::integer}}));
    add(action("region.preview.v1", "Preview region", "Samples"));
    add(action("pad.trigger.v1", "Trigger performance pad", "Performance",
               {{"pad", ValueType::integer},
                {"velocity", ValueType::real, false},
                {"pitch_offset", ValueType::real, false}}));
    add(action("pad.release.v1", "Release performance pad", "Performance",
               {{"pad", ValueType::integer}}));
    add(action("pad.select.v1", "Select performance pad", "Performance",
               {{"pad", ValueType::integer}}));
    add(action("pad.property-select.v1", "Select pad property", "Samples",
               {{"property", ValueType::text}}));
    add(action("pad.playback-mode-set.v1", "Set pad playback mode", "Samples",
               {{"mode", ValueType::text}}));
    add(action("pad.choke-group-set.v1", "Set pad choke group", "Samples",
               {{"group", ValueType::integer}}));
    add(action("pad.choke-release-set.v1", "Set pad choke release", "Samples",
               {{"release", ValueType::boolean}}));
    add(action("pad.pitch-set.v1", "Set pad pitch", "Samples",
               {{"semitones", ValueType::real}}));
    add(action("pad.fine-set.v1", "Set pad fine tuning", "Samples",
               {{"cents", ValueType::real}}));
    add(action("pad.gain-set.v1", "Set pad gain", "Samples",
               {{"gain", ValueType::real}}));
    add(action("pad.reverse-set.v1", "Set pad reverse", "Samples",
               {{"reverse", ValueType::boolean}}));
    add(action("pad.envelope-set.v1", "Set pad amplitude envelope", "Samples",
               {{"attack", ValueType::real},
                {"decay", ValueType::real},
                {"sustain", ValueType::real},
                {"release", ValueType::real}}));
    add(action("pad.stretch-mode-set.v1", "Set pad stretch mode", "Samples",
               {{"mode", ValueType::text}}));
    add(action("pad.stretch-ratio-set.v1", "Set pad time ratio", "Samples",
               {{"ratio", ValueType::real}}));
    add(action("pad.source-beats-set.v1", "Set source beat length", "Samples",
               {{"beats", ValueType::real}}));
    add(action("pad.source-bpm-set.v1", "Set source tempo", "Samples",
               {{"bpm", ValueType::real}}));
    add(action("pad.stretch-quality-set.v1", "Set stretch quality", "Samples",
               {{"quality", ValueType::text}}));
    add(action("pad.stretch-reset.v1", "Reset stretch settings", "Samples"));
    add(action("project.new.v1", "New project", "Project",
               {{"name", ValueType::text, false},
                {"discard", ValueType::boolean, false}}));
    add(action(
        "project.open.v1", "Open project", "Project",
        {{"path", ValueType::text}, {"discard", ValueType::boolean, false}}));
    add(action("project.save.v1", "Save project", "Project"));
    add(action("project.save-as.v1", "Save project as", "Project",
               {{"path", ValueType::text}, {"name", ValueType::text, false}}));
    add(action("project.close.v1", "Close project", "Project",
               {{"discard", ValueType::boolean, false}}));
    add(action("project.rename.v1", "Rename project", "Project",
               {{"name", ValueType::text}}));
    add(action(
        "project.relink-sample.v1", "Relink missing sample", "Project",
        {{"missing", ValueType::text}, {"replacement", ValueType::text}}));
    add(action("project.relink-prefix.v1", "Relink missing sample directory",
               "Project",
               {{"missing_prefix", ValueType::text},
                {"replacement_prefix", ValueType::text}}));
    add(action("project.query.v1", "Query project status", "Project"));
    add(action("preset.validate.v1", "Validate preset", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"mode", ValueType::text},
                {"track_id", ValueType::integer, false}}));
    add(action("preset.prepare.v1", "Prepare preset application", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"mode", ValueType::text},
                {"track_id", ValueType::integer, false},
                {"release_voices", ValueType::boolean, false}}));
    add(action("preset.apply.v1", "Activate prepared preset", "Presets"));
    add(action("preset.cancel.v1", "Cancel prepared preset", "Presets"));
    add(action("preset.query.v1", "Query preset application", "Presets"));
    add(action("preset.capture.v1", "Save current configuration as preset",
               "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"name", ValueType::text}, {"type", ValueType::text},
                {"track_id", ValueType::integer, false},
                {"overwrite", ValueType::boolean, false}}));
    add(action("preset.rename.v1", "Rename preset", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"new_id", ValueType::text}, {"new_name", ValueType::text}}));
    add(action("preset.duplicate.v1", "Duplicate preset", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"new_id", ValueType::text}, {"new_name", ValueType::text}}));
    add(action("preset.delete.v1", "Delete preset", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text}}));
    add(action("preset.relink-asset.v1", "Relink preset asset", "Presets",
               {{"missing", ValueType::text},
                {"replacement", ValueType::text}}));
    add(action("preset.relink-prefix.v1", "Relink preset directory", "Presets",
               {{"missing_prefix", ValueType::text},
                {"replacement_prefix", ValueType::text}}));
    add(action("preset.repair-asset.v1", "Persist preset asset repair",
               "Presets",
               {{"library", ValueType::text},
                {"preset_id", ValueType::text},
                {"missing", ValueType::text},
                {"replacement", ValueType::text}}));
    add(action("preset.repair-prefix.v1", "Persist preset directory repair",
               "Presets",
               {{"library", ValueType::text},
                {"preset_id", ValueType::text},
                {"missing_prefix", ValueType::text},
                {"replacement_prefix", ValueType::text}}));
    add(action("preset.export-portable.v1", "Export portable preset", "Presets",
               {{"library", ValueType::text}, {"preset_id", ValueType::text},
                {"destination", ValueType::text}}));
    add(action("midi.clock-source-set.v1", "Set MIDI clock source", "MIDI",
               {{"source", ValueType::text}}));
    add(action(
        "midi.clock-output-set.v1", "Set MIDI clock output", "MIDI",
        {{"port", ValueType::integer}, {"enabled", ValueType::boolean}}));
    add(action("midi.clock-forward-set.v1", "Forward external MIDI clock",
               "MIDI", {{"enabled", ValueType::boolean}}));
    add(action("midi.route-set.v1", "Create or update MIDI route", "MIDI",
               {{"id", ValueType::integer},
                {"input_port", ValueType::integer},
                {"output_port", ValueType::integer},
                {"input_channel", ValueType::integer},
                {"output_channel", ValueType::integer},
                {"notes", ValueType::boolean},
                {"cc", ValueType::boolean},
                {"clock", ValueType::boolean},
                {"transport", ValueType::boolean},
                {"enabled", ValueType::boolean, false}}));
    add(action("midi.route-delete.v1", "Delete MIDI route", "MIDI",
               {{"id", ValueType::integer}}));
    add(action("midi.panic.v1", "MIDI panic", "MIDI"));
    add(action("midi.learn.v1", "MIDI learn", "MIDI", {}, false,
               "MIDI learn capture is not implemented"));
    add(action("transport.tempo-set.v1", "Set tempo", "Transport",
               {{"bpm", ValueType::real}}));
    add(action("recording.source-select.v1", "Select recording source",
               "Recording",
               {{"source", ValueType::text},
                {"source_id", ValueType::integer, false}}));
    add(action("recording.destination-select.v1",
               "Select recording destination", "Recording",
               {{"slot_id", ValueType::integer}}));
    add(action("recording.channels-set.v1", "Set recording channels",
               "Recording", {{"channels", ValueType::integer}}));
    add(action("recording.quantization-set.v1", "Set recording quantization",
               "Recording", {{"quantization", ValueType::text}}));
    add(action("recording.fixed-bars-set.v1", "Set fixed recording length",
               "Recording", {{"bars", ValueType::integer}}));
    add(action("recording.monitoring-set.v1", "Set input monitoring",
               "Recording", {{"mode", ValueType::text}}));
    add(action("recording.arm.v1", "Arm recording", "Recording"));
    add(action("recording.start.v1", "Start recording", "Recording"));
    add(action("recording.stop.v1", "Stop recording", "Recording"));
    add(action("recording.cancel.v1", "Cancel recording", "Recording"));
    add(action(
        "recording.save.v1", "Save recording", "Recording",
        {{"path", ValueType::text}, {"format", ValueType::text, false}}));
    add(action("recording.assign-pad.v1", "Assign recording to pad",
               "Recording", {{"pad", ValueType::integer}}));
    add(action("live-loop.create.v1", "Create live-loop session", "Recording",
               {{"track", ValueType::integer}, {"slot", ValueType::integer},
                {"source", ValueType::text}, {"source_id", ValueType::integer, false},
                {"channels", ValueType::integer, false},
                {"quantization", ValueType::text, false},
                {"subdivision_ticks", ValueType::integer, false},
                {"length_ticks", ValueType::integer},
                {"feedback", ValueType::real, false},
                {"monitoring", ValueType::text, false},
                {"latency_frames", ValueType::integer, false}}));
    add(action("live-loop.arm.v1", "Arm live-loop recording", "Recording",
               {{"id", ValueType::integer}, {"mode", ValueType::text, false}}));
    add(action("live-loop.start.v1", "Start live-loop recording", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.stop.v1", "Stop live-loop recording", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.overdub.v1", "Begin live-loop overdub", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.replace.v1", "Replace live-loop audio", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.cancel.v1", "Cancel live-loop operation", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.clear.v1", "Clear live-loop session", "Recording",
               {{"id", ValueType::integer}}));
    add(action("live-loop.save.v1", "Save live-loop audio", "Recording",
               {{"id", ValueType::integer}, {"path", ValueType::text},
                {"format", ValueType::text, false}}));
    add(action("live-loop.source-set.v1", "Set live-loop source", "Recording",
               {{"id", ValueType::integer}, {"source", ValueType::text},
                {"source_id", ValueType::integer, false}}));
    add(action("live-loop.quantization-set.v1", "Set live-loop quantization", "Recording",
               {{"id", ValueType::integer}, {"quantization", ValueType::text},
                {"subdivision_ticks", ValueType::integer, false}}));
    add(action("live-loop.feedback-set.v1", "Set live-loop overdub feedback", "Recording",
               {{"id", ValueType::integer}, {"feedback", ValueType::real}}));
    add(action("live-loop.monitoring-set.v1", "Set live-loop monitoring", "Recording",
               {{"id", ValueType::integer}, {"monitoring", ValueType::text}}));
    add(action("mixer.channel-select.v1", "Select mixer channel", "Mixer",
               {{"index", ValueType::integer}}));
    add(action("mixer.gain-set.v1", "Set mixer gain", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.pan-set.v1", "Set mixer pan", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.mute-toggle.v1", "Toggle mixer mute", "Mixer"));
    add(action("mixer.solo-toggle.v1", "Toggle mixer solo", "Mixer"));
    add(action("mixer.aux-send-set.v1", "Set auxiliary send", "Mixer",
               {{"aux", ValueType::integer},
                {"value", ValueType::real},
                {"pre_fader", ValueType::boolean, false}}));
    add(action("mixer.output-pair-set.v1", "Set channel output pair", "Mixer",
               {{"pair", ValueType::integer}}));
    add(action("mixer.master-gain-set.v1", "Set master gain", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.master-mute-toggle.v1", "Toggle master mute", "Mixer"));
    add(action("mixer.master-output-set.v1", "Set master output", "Mixer",
               {{"left", ValueType::integer}, {"right", ValueType::integer}}));
    add(action("mixer.input-set.v1", "Configure physical input", "Mixer",
               {{"index", ValueType::integer},
                {"physical_channel", ValueType::integer},
                {"gain", ValueType::real},
                {"pan", ValueType::real},
                {"mute", ValueType::boolean, false},
                {"route_to_master", ValueType::boolean, false},
                {"aux_1", ValueType::real, false},
                {"aux_2", ValueType::real, false}}));
    add(action("mixer.aux-set.v1", "Configure auxiliary bus", "Mixer",
               {{"aux", ValueType::integer},
                {"gain", ValueType::real},
                {"return_gain", ValueType::real},
                {"effect_enabled", ValueType::boolean},
                {"mute", ValueType::boolean, false},
                {"output_pair", ValueType::integer, false}}));
    add(action("mixer.group-create.v1", "Create group bus", "Mixer",
               {{"name", ValueType::text}}));
    add(action("mixer.group-delete.v1", "Delete group bus", "Mixer",
               {{"id", ValueType::integer}}));
    add(action("mixer.group-rename.v1", "Rename group bus", "Mixer",
               {{"id", ValueType::integer}, {"name", ValueType::text}}));
    add(action("mixer.group-select.v1", "Select group bus", "Mixer",
               {{"id", ValueType::integer}}));
    add(action(
        "mixer.track-destination.v1", "Set track destination", "Mixer",
        {{"track_id", ValueType::integer}, {"group_id", ValueType::integer}}));
    add(action("mixer.group-output.v1", "Set group destination", "Mixer",
               {{"id", ValueType::integer},
                {"group_id", ValueType::integer},
                {"output_pair", ValueType::integer, false}}));
    add(action("mixer.group-levels.v1", "Set group gain and pan", "Mixer",
               {{"id", ValueType::integer},
                {"gain", ValueType::real},
                {"pan", ValueType::real}}));
    add(action("mixer.group-mute-toggle.v1", "Toggle group mute", "Mixer",
               {{"id", ValueType::integer}}));
    add(action("mixer.group-solo-toggle.v1", "Toggle group solo", "Mixer",
               {{"id", ValueType::integer}}));
    add(action("mixer.group-aux-send.v1", "Set group auxiliary send", "Mixer",
               {{"id", ValueType::integer},
                {"aux", ValueType::integer},
                {"value", ValueType::real},
                {"pre_fader", ValueType::boolean, false}}));
    add(action("mixer.fx-loop-select.v1", "Select external FX loop", "Routing",
               {{"index", ValueType::integer}}));
    add(action("mixer.fx-loop-set.v1", "Configure external FX loop", "Routing",
               {{"index", ValueType::integer},
                {"source", ValueType::text},
                {"source_id", ValueType::integer},
                {"send_left", ValueType::integer},
                {"send_right", ValueType::integer},
                {"return_left", ValueType::integer},
                {"return_right", ValueType::integer},
                {"name", ValueType::text, false},
                {"enabled", ValueType::boolean, false},
                {"bypass", ValueType::boolean, false},
                {"mono_send", ValueType::boolean, false},
                {"mono_return", ValueType::boolean, false}}));
    add(action("mixer.fx-loop-levels.v1", "Set external FX loop levels",
               "Routing",
               {{"index", ValueType::integer},
                {"send_gain", ValueType::real},
                {"return_gain", ValueType::real},
                {"wet", ValueType::real},
                {"return_mute", ValueType::boolean, false}}));
    add(action(
        "mixer.fx-loop-latency.v1", "Set external FX loop latency", "Routing",
        {{"index", ValueType::integer}, {"frames", ValueType::integer}}));
    add(action("mixer.scene-save.v1", "Save routing scene", "Routing",
               {{"name", ValueType::text}}));
    add(action("mixer.scene-recall.v1", "Recall routing scene", "Routing",
               {{"id", ValueType::integer}}));
    add(action("mixer.scene-rename.v1", "Rename routing scene", "Routing",
               {{"id", ValueType::integer}, {"name", ValueType::text}}));
    add(action("mixer.scene-delete.v1", "Delete routing scene", "Routing",
               {{"id", ValueType::integer}}));
    add(action("effect.create.v1", "Create effect instance", "Effects",
               {{"id", ValueType::integer}, {"type", ValueType::text}}));
    add(action("effect.delete.v1", "Delete effect instance", "Effects",
               {{"id", ValueType::integer}}));
    add(action("effect.insert.v1", "Insert effect into chain", "Effects",
               {{"id", ValueType::integer},
                {"chain", ValueType::text},
                {"index", ValueType::integer},
                {"position", ValueType::integer}}));
    add(action("effect.remove.v1", "Remove effect from chain", "Effects",
               {{"chain", ValueType::text},
                {"index", ValueType::integer},
                {"position", ValueType::integer}}));
    add(action("effect.reorder.v1", "Reorder effect chain", "Effects",
               {{"chain", ValueType::text},
                {"index", ValueType::integer},
                {"from", ValueType::integer},
                {"to", ValueType::integer}}));
    add(action("effect.bypass.v1", "Bypass effect instance", "Effects",
               {{"id", ValueType::integer}, {"bypass", ValueType::boolean}}));
    add(action("effect.chain-bypass.v1", "Bypass effect chain", "Effects",
               {{"chain", ValueType::text},
                {"index", ValueType::integer},
                {"bypass", ValueType::boolean}}));
    add(action("effect.parameter-set.v1", "Set effect parameter", "Effects",
               {{"id", ValueType::integer},
                {"parameter", ValueType::integer},
                {"value", ValueType::real}}));
    add(action("effect.query.v1", "Query effect chains", "Effects"));
    add(action("edit.undo.v1", "Undo", "Editing"));
    add(action("edit.redo.v1", "Redo", "Editing"));
    add(action("edit.group-begin.v1", "Begin grouped edit", "Editing"));
    add(action("edit.group-commit.v1", "Commit grouped edit", "Editing"));
    add(action("edit.group-cancel.v1", "Cancel grouped edit", "Editing"));
    add(action("timeline.generate.v1", "Generate timeline", "Generation", {},
               false,
               "a generator choice and transaction payload are required"));
    add(action(
        "timeline.mutate-track.v1", "Mutate track", "Generation", {}, false,
        "mutation settings are not represented by this initial command"));
    add(action("pad.mode-select.v1", "Select pad mode", "Performance",
               {{"mode", ValueType::text}}));
    add(action("controller.context-select.v1", "Select controller context",
               "Controllers", {{"context", ValueType::text}}));
    add(action("navigation.focus-next.v1", "Focus next", "Navigation"));
    add(action("navigation.focus-previous.v1", "Focus previous", "Navigation"));
    add(action("navigation.adjust.v1", "Adjust focused item", "Navigation",
               {{"steps", ValueType::real}}));
    add(action("navigation.confirm.v1", "Confirm", "Navigation"));
    add(action("navigation.cancel.v1", "Cancel", "Navigation"));
    add(action("text.input.v1", "Text input", "Text",
               {{"text", ValueType::text}}));
    add(action("modulation.xy.v1", "X/Y control", "Modulation",
               {{"position", ValueType::xy}}));
    add(action("modulation.midi-cc.v1", "MIDI CC modulation", "Modulation",
               {{"value", ValueType::real}}));
    add(action("modulation.route-create.v1", "Create modulation route",
               "Modulation",
               {{"source", ValueType::integer},
                {"destination", ValueType::text},
                {"depth", ValueType::real},
                {"scope", ValueType::integer},
                {"track_id", ValueType::integer, false},
                {"instance_id", ValueType::integer, false}}));
    add(action("modulation.route-delete.v1", "Delete modulation route",
               "Modulation", {{"route_id", ValueType::integer}}));
    add(action("modulation.route-select.v1", "Select modulation route",
               "Modulation", {{"route_id", ValueType::integer}}));
    add(action("modulation.route-depth.v1", "Set modulation depth",
               "Modulation",
               {{"route_id", ValueType::integer}, {"depth", ValueType::real}}));
    add(action(
        "modulation.route-enable.v1", "Enable modulation route", "Modulation",
        {{"route_id", ValueType::integer}, {"enabled", ValueType::boolean}}));
    add(action("modulation.source-configure.v1", "Configure modulation source",
               "Modulation",
               {{"source", ValueType::integer},
                {"waveform", ValueType::integer},
                {"rate", ValueType::real},
                {"tempo_sync", ValueType::boolean},
                {"beats", ValueType::real},
                {"scope", ValueType::integer},
                {"retrigger", ValueType::boolean, false},
                {"attack", ValueType::real, false},
                {"decay", ValueType::real, false},
                {"sustain", ValueType::real, false},
                {"release", ValueType::real, false},
                {"seed", ValueType::integer, false}}));
    add(action("modulation.source-reset.v1", "Reset modulation source",
               "Modulation", {{"source", ValueType::integer}}));
    add(action("modulator.create.v1", "Create modulator", "Modulation",
               {{"type", ValueType::integer},
                {"owner", ValueType::integer},
                {"owner_id", ValueType::integer},
                {"name", ValueType::text, false}}));
    add(action("modulator.delete.v1", "Delete modulator", "Modulation",
               {{"id", ValueType::integer}}));
    add(action("modulator.duplicate.v1", "Duplicate modulator", "Modulation",
               {{"id", ValueType::integer}}));
    add(action("modulator.enable.v1", "Enable modulator", "Modulation",
               {{"id", ValueType::integer}, {"enabled", ValueType::boolean}}));
    add(action("modulator.rename.v1", "Rename modulator", "Modulation",
               {{"id", ValueType::integer}, {"name", ValueType::text}}));
    add(action("modulator.parameter-set.v1", "Set modulator parameter",
               "Modulation",
               {{"id", ValueType::integer},
                {"parameter", ValueType::text},
                {"value", ValueType::real}}));
    add(action("modulator.step-set.v1", "Set modulator step", "Modulation",
               {{"id", ValueType::integer},
                {"step", ValueType::integer},
                {"value", ValueType::real}}));
    add(action("modulator.query-types.v1", "Query modulator types",
               "Modulation"));
    add(action("modulator.query-instances.v1", "Query modulators",
               "Modulation"));
    add(action("modulator.query-capacity.v1", "Query modulation capacity",
               "Modulation"));
    add(action("modulator.route-create.v1", "Create instance route",
               "Modulation",
               {{"source_id", ValueType::integer},
                {"destination", ValueType::text},
                {"depth", ValueType::real},
                {"destination_instance", ValueType::integer, false},
                {"destination_owner", ValueType::integer, false},
                {"destination_owner_id", ValueType::integer, false}}));
    add(action("modulation.connection-create.v1", "Create graph connection",
               "Modulation",
               {{"source_id", ValueType::integer},
                {"destination_id", ValueType::integer},
                {"parameter", ValueType::integer},
                {"input", ValueType::integer, false},
                {"depth", ValueType::real}}));
    add(action("modulation.connection-delete.v1", "Delete graph connection",
               "Modulation", {{"connection_id", ValueType::integer}}));
    add(action(
        "modulation.connection-depth.v1", "Set connection depth", "Modulation",
        {{"connection_id", ValueType::integer}, {"depth", ValueType::real}}));
    add(action("modulation.connection-enable.v1", "Enable connection",
               "Modulation",
               {{"connection_id", ValueType::integer},
                {"enabled", ValueType::boolean}}));
    add(action("modulation.share.v1", "Share modulator", "Modulation",
               {{"id", ValueType::integer}, {"shared", ValueType::boolean}}));
    add(action("modulation.follower-tap.v1", "Select follower tap",
               "Modulation",
               {{"id", ValueType::integer},
                {"tap", ValueType::integer},
                {"tap_id", ValueType::integer}}));
    add(action("modulation.graph-query.v1", "Query modulation graph",
               "Modulation"));
    add(action("modulation.available-sources.v1", "Query available sources",
               "Modulation"));
    add(action("modulation.available-destinations.v1",
               "Query available destinations", "Modulation"));
    add(action("modulation.validation-query.v1", "Query graph validation",
               "Modulation"));
    add(action(
        "parameter.set-normalized.v1", "Set parameter", "Parameters",
        {{"parameter_id", ValueType::text}, {"value", ValueType::real}}));
    add(action(
        "parameter.adjust.v1", "Adjust parameter", "Parameters",
        {{"parameter_id", ValueType::text}, {"steps", ValueType::real}}));
    add(action("parameter.reset.v1", "Reset parameter", "Parameters",
               {{"parameter_id", ValueType::text}}));
    add(action("modifier.shift.v1", "Shift", "Modifiers",
               {{"active", ValueType::boolean},
                {"latched", ValueType::boolean, false}}));
    add(action("modifier.select.v1", "Select modifier", "Modifiers",
               {{"active", ValueType::boolean},
                {"latched", ValueType::boolean, false}}));
}
bool ActionRegistry::register_action(ActionDescriptor value,
                                     std::string &error) {
    if (value.id.empty() || value.name.empty()) {
        error = "action ID and name are required";
        return false;
    }
    if (find(value.id)) {
        error = "duplicate action ID: " + value.id;
        return false;
    }
    actions_.push_back(std::move(value));
    error.clear();
    return true;
}
const ActionDescriptor *
ActionRegistry::find(std::string_view id) const noexcept {
    for (const auto &value : actions_)
        if (value.id == id)
            return &value;
    return nullptr;
}
bool ActionRegistry::validate(
    std::string_view id, const std::map<std::string, ApplicationValue> &values,
    std::string &error) const {
    const auto *descriptor = find(id);
    if (!descriptor) {
        error = "unknown action: " + std::string(id);
        return false;
    }
    if (!descriptor->available) {
        error = descriptor->unavailable_reason.empty()
                    ? "action is unavailable"
                    : descriptor->unavailable_reason;
        return false;
    }
    for (const auto &spec : descriptor->arguments) {
        const auto found = values.find(spec.name);
        if (found == values.end()) {
            if (spec.required) {
                error = "missing argument: " + spec.name;
                return false;
            }
            continue;
        }
        const auto type = found->second.index();
        const auto expected = static_cast<std::size_t>(spec.type);
        if (type != expected) {
            error = "invalid type for argument: " + spec.name;
            return false;
        }
    }
    for (const auto &[name, value] : values) {
        (void)value;
        if (std::none_of(descriptor->arguments.begin(),
                         descriptor->arguments.end(),
                         [&](const auto &spec) { return spec.name == name; })) {
            error = "unexpected argument: " + name;
            return false;
        }
    }
    error.clear();
    return true;
}

bool ParameterRegistry::valid_value(const ParameterDescriptor &parameter,
                                    const ApplicationValue &value,
                                    std::string &error) const {
    switch (parameter.type) {
    case ParameterType::boolean:
        if (!std::holds_alternative<bool>(value)) {
            error = "parameter requires a boolean";
            return false;
        }
        break;
    case ParameterType::integer:
    case ParameterType::enumeration:
        if (!std::holds_alternative<std::int64_t>(value)) {
            error = "parameter requires an integer";
            return false;
        }
        break;
    case ParameterType::real:
        if (!is_numeric(value)) {
            error = "parameter requires a number";
            return false;
        }
        break;
    }
    if (parameter.type != ParameterType::boolean) {
        const auto current = number(value), minimum = number(parameter.minimum),
                   maximum = number(parameter.maximum);
        if (!std::isfinite(current) || current < minimum || current > maximum) {
            error = "parameter value is outside its range";
            return false;
        }
        if (parameter.type == ParameterType::enumeration &&
            static_cast<std::size_t>(std::get<std::int64_t>(value)) >=
                parameter.choices.size()) {
            error = "enumeration value has no choice";
            return false;
        }
    }
    error.clear();
    return true;
}
bool ParameterRegistry::register_parameter(ParameterDescriptor value,
                                           std::string &error) {
    std::lock_guard lock(mutex_);
    if (value.id.empty() || value.name.empty() || values_.contains(value.id)) {
        error = "parameter ID/name is empty or duplicated";
        return false;
    }
    if (value.step <= 0 || !std::isfinite(value.step)) {
        error = "parameter step must be positive";
        return false;
    }
    if (!valid_value(value, value.default_value, error) ||
        !valid_value(value, value.current_value, error))
        return false;
    values_.emplace(value.id, std::move(value));
    error.clear();
    return true;
}
bool ParameterRegistry::set(std::string_view id, const ApplicationValue &value,
                            std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    if (!valid_value(found->second, value, error))
        return false;
    found->second.current_value = value;
    return true;
}
bool ParameterRegistry::set_normalized(std::string_view id, double normalized,
                                       std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    if (!std::isfinite(normalized) || normalized < 0 || normalized > 1) {
        error = "normalized parameter value must be 0..1";
        return false;
    }
    auto &parameter = found->second;
    if (parameter.type == ParameterType::boolean)
        parameter.current_value = normalized >= 0.5;
    else {
        const auto scaled =
            number(parameter.minimum) +
            (number(parameter.maximum) - number(parameter.minimum)) *
                normalized;
        if (parameter.type == ParameterType::real)
            parameter.current_value = scaled;
        else
            parameter.current_value =
                static_cast<std::int64_t>(std::llround(scaled));
    }
    error.clear();
    return true;
}
bool ParameterRegistry::adjust(std::string_view id, double steps,
                               std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    auto &parameter = found->second;
    if (parameter.type == ParameterType::boolean) {
        if (steps != 0)
            parameter.current_value = !std::get<bool>(parameter.current_value);
        error.clear();
        return true;
    }
    if (!std::isfinite(steps)) {
        error = "relative adjustment must be finite";
        return false;
    }
    const auto minimum = number(parameter.minimum),
               maximum = number(parameter.maximum);
    auto updated =
        std::clamp(number(parameter.current_value) + steps * parameter.step,
                   minimum, maximum);
    if (parameter.type != ParameterType::real)
        parameter.current_value =
            static_cast<std::int64_t>(std::llround(updated));
    else
        parameter.current_value = updated;
    error.clear();
    return true;
}
bool ParameterRegistry::reset(std::string_view id, std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    found->second.current_value = found->second.default_value;
    error.clear();
    return true;
}
std::optional<ParameterDescriptor>
ParameterRegistry::get(std::string_view id) const {
    std::lock_guard lock(mutex_);
    const auto found = values_.find(id);
    return found == values_.end()
               ? std::nullopt
               : std::optional<ParameterDescriptor>(found->second);
}
std::vector<ParameterDescriptor> ParameterRegistry::parameters() const {
    std::lock_guard lock(mutex_);
    std::vector<ParameterDescriptor> result;
    result.reserve(values_.size());
    for (const auto &[id, value] : values_) {
        (void)id;
        result.push_back(value);
    }
    return result;
}

ApplicationService::ApplicationService(AudioEngine &engine,
                                       TimelineService &timeline,
                                       SampleManager &samples,
                                       std::size_t capacity)
    : engine_(engine), timeline_(timeline), samples_(samples),
      slices_(engine, samples), sample_analysis_(samples, slices_),
      sample_processing_(samples, slices_),
      recording_(engine.sample_rate()),
      live_loops_(engine, recording_, samples, timeline),
      capacity_(capacity) {
    if (capacity_ == 0)
        throw std::invalid_argument(
            "application command capacity must be positive");
    std::string error;
    parameters_.register_parameter({"transport.tempo",
                                    "Tempo",
                                    "BPM",
                                    ParameterType::real,
                                    20.0,
                                    300.0,
                                    120.0,
                                    120.0,
                                    0.1,
                                    true,
                                    false,
                                    false,
                                    {}},
                                   error);
    parameters_.register_parameter({"performance.xy.x",
                                    "X axis",
                                    "",
                                    ParameterType::real,
                                    0.0,
                                    1.0,
                                    0.5,
                                    0.5,
                                    0.01,
                                    true,
                                    false,
                                    true,
                                    {}},
                                   error);
    parameters_.register_parameter({"performance.xy.y",
                                    "Y axis",
                                    "",
                                    ParameterType::real,
                                    0.0,
                                    1.0,
                                    0.5,
                                    0.5,
                                    0.01,
                                    true,
                                    false,
                                    true,
                                    {}},
                                   error);
    const auto synth_parameter =
        [&](std::string id, std::string name, std::string unit, double minimum,
            double maximum, double initial, double step) {
            parameters_.register_parameter({std::move(id),
                                            std::move(name),
                                            std::move(unit),
                                            ParameterType::real,
                                            minimum,
                                            maximum,
                                            initial,
                                            initial,
                                            step,
                                            true,
                                            true,
                                            true,
                                            {}},
                                           error);
        };
    synth_parameter("synth.waveform", "Waveform", "", 0, 3, 2, 1);
    synth_parameter("synth.attack", "Attack", "s", 0.001, 5, 0.005, 0.01);
    synth_parameter("synth.decay", "Decay", "s", 0.001, 5, 0.12, 0.01);
    synth_parameter("synth.sustain", "Sustain", "", 0, 1, 0.75, 0.01);
    synth_parameter("synth.release", "Release", "s", 0.001, 10, 0.25, 0.01);
    synth_parameter("synth.cutoff", "Cutoff", "Hz", 20, 20000, 8000, 100);
    synth_parameter("synth.resonance", "Resonance", "", 0, 0.95, 0.15, 0.01);
    synth_parameter("synth.pulse-width", "Pulse width", "", 0.05, 0.95, 0.5,
                    0.01);
    synth_parameter("synth.gain", "Synth gain", "", 0, 1, 0.25, 0.01);
    synth_parameter("synth.pitch", "Synth pitch", "st", -24, 24, 0, 0.1);
    synth_parameter("effect.delay.time", "Delay time", "s", 0.001, 2, 0.25,
                    0.01);
    synth_parameter("effect.delay.feedback", "Delay feedback", "", 0, 0.95, 0.3,
                    0.01);
    synth_parameter("effect.delay.mix", "Delay mix", "", 0, 1, 0, 0.01);
    const auto mixer_parameter = [&](std::string id, std::string name,
                                     double minimum, double maximum,
                                     double initial) {
        parameters_.register_parameter({std::move(id),
                                        std::move(name),
                                        "",
                                        ParameterType::real,
                                        minimum,
                                        maximum,
                                        initial,
                                        initial,
                                        0.01,
                                        true,
                                        false,
                                        true,
                                        {}},
                                       error);
    };
    mixer_parameter("mixer.master.gain", "Master gain", 0, 2, 1);
    mixer_parameter("mixer.selected.gain", "Channel gain", 0, 2, 1);
    mixer_parameter("mixer.selected.pan", "Channel pan", -1, 1, 0);
    mixer_parameter("mixer.external.selected.wet", "External FX wet/dry", 0, 1,
                    0.5);
    mixer_parameter("sample.pitch", "Sample pitch", -24, 24, 0);
    mixer_parameter("sample.gain", "Sample gain", 0, 2, 1);
    mixer_parameter("sample.pan", "Sample pan", -1, 1, 0);
    mixer_parameter("mixer.track.gain", "Track gain", 0, 2, 1);
    mixer_parameter("mixer.track.pan", "Track pan", -1, 1, 0);
    mixer_parameter("mixer.track.aux-1", "Track aux 1", 0, 1, 0);
    mixer_parameter("mixer.track.aux-2", "Track aux 2", 0, 1, 0);
    mixer_parameter("mixer.aux.return", "Aux return", 0, 2, 1);
    mixer_parameter("mixer.group.gain", "Group gain", 0, 2, 1);
    mixer_parameter("mixer.group.pan", "Group pan", -1, 1, 0);
    mixer_parameter("mixer.group.aux-1", "Group aux 1", 0, 1, 0);
    mixer_parameter("mixer.group.aux-2", "Group aux 2", 0, 1, 0);
    mixer_parameter("external-fx.wet", "External FX wet", 0, 1, 0.5);
    mixer_parameter("external-fx.send-gain", "External FX send", 0, 2, 0.5);
    mixer_parameter("external-fx.return-gain", "External FX return", 0, 2, 1);
    state_.step_ticks = step_resolution_ticks(state_.step_resolution);
    state_.step_duration = state_.step_ticks;
    project_.timeline = timeline_.snapshot().timeline;
    state_.timeline_revision = timeline_.snapshot().revision;
    recording_configuration_.destination = SampleSlotId{0};
    mixer_configuration_.revision = 1;
    mixer_configuration_.track_count =
        std::min(project_.timeline.tracks.size(), maximum_mixer_tracks);
    for (std::size_t i = 0; i < mixer_configuration_.track_count; ++i)
        mixer_configuration_.tracks[i].track = project_.timeline.tracks[i].id;
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        auto &loop = mixer_configuration_.external_fx[i];
        loop.id = static_cast<ExternalFxLoopId>(i + 1);
        const auto name = "External FX " + std::to_string(i + 1);
        std::copy_n(name.begin(), std::min(name.size(), loop.name.size() - 1),
                    loop.name.begin());
        loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
        loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
        loop.return_left = static_cast<std::uint8_t>(i * 2);
        loop.return_right = static_cast<std::uint8_t>(1 + i * 2);
    }
    if (!engine_.publish_mixer(mixer_configuration_, error))
        throw std::runtime_error("cannot publish initial mixer: " + error);
    project_.modulation = modulation_configuration_;
    if (!engine_.publish_modulation(modulation_configuration_))
        throw std::runtime_error("cannot publish initial modulation state");
    engine_.set_recording_service(&recording_);
    engine_.set_midi_output_scheduler(&midi_output_);
    MidiRoutingConfiguration initial_midi;
    if (!engine_.publish_midi_routing(initial_midi))
        throw std::runtime_error("cannot publish initial MIDI routing");
}
ApplicationService::~ApplicationService() {
    engine_.set_midi_output_scheduler(nullptr);
    engine_.set_recording_service(nullptr);
}
bool ApplicationService::publish_modulation_with_macros(
    const ModulationConfiguration &base, std::string &error) {
    ModulationConfiguration compiled;
    if (!performance_macros_.compile(base, compiled, error))
        return false;
    if (!engine_.publish_modulation(compiled)) {
        error = "modulation publication queue is full";
        return false;
    }
    error.clear();
    return true;
}
bool ApplicationService::publish_performance_launches(
    const Timeline &source, std::span<const PreparedLaunch> session,
    std::span<const PreparedLaunch> changes,
    std::string &error) const {
    if (changes.empty()) {
        error = "performance launch batch is empty";
        return false;
    }
    auto audible = source;
    auto &a = audible.arrangement;
    if (a.playback_mode != TimelinePlaybackMode::song) {
        error = "hybrid playback requires an arrangement song";
        return false;
    }
    std::vector<TrackId> affected;
    for (const auto &change : changes)
        if (std::find(affected.begin(), affected.end(), change.track) ==
            affected.end())
            affected.push_back(change.track);
    std::vector<TrackId> owned;
    for (const auto &launch : session)
        if (launch.operation != PreparedLaunchOperation::stop)
            owned.push_back(launch.track);
    const auto is_owned = [&](TrackId track) {
        return std::find(owned.begin(), owned.end(), track) != owned.end();
    };
    for (auto &pattern : a.patterns)
        std::erase_if(pattern.tracks,
                      [&](const auto &track) { return is_owned(track.track); });
    std::erase_if(a.audio_clips,
                  [&](const auto &clip) { return is_owned(clip.track); });
    std::erase_if(a.audio_crossfades, [&](const auto &crossfade) {
        const auto present = [&](AudioClipId id) {
            return std::any_of(a.audio_clips.begin(), a.audio_clips.end(),
                               [&](const auto &clip) { return clip.id == id; });
        };
        return !present(crossfade.outgoing_clip) ||
               !present(crossfade.incoming_clip);
    });
    PatternId next_pattern = 1;
    ClipId next_clip = 1;
    AudioClipId next_audio = 1;
    for (const auto &pattern : a.patterns)
        next_pattern = std::max(next_pattern, pattern.id + 1);
    for (const auto &clip : a.clips)
        next_clip = std::max(next_clip, clip.id + 1);
    for (const auto &clip : a.audio_clips)
        next_audio = std::max(next_audio, clip.id + 1);
    const auto range_start = a.loop_enabled ? a.loop_start : a.song_start;
    const auto range_end = a.loop_enabled ? a.loop_end : a.song_end;
    const auto length = range_end - range_start;
    for (const auto &launch : session) {
        if (launch.operation == PreparedLaunchOperation::stop) continue;
        if (launch.source_type == LaunchSourceType::pattern) {
            const auto pattern = std::find_if(source.arrangement.patterns.begin(),
                source.arrangement.patterns.end(),
                [&](const auto &value) { return value.id == launch.source_id; });
            if (pattern == source.arrangement.patterns.end()) {
                error = "performance pattern source disappeared";
                return false;
            }
            PatternDefinition definition = *pattern;
            definition.id = next_pattern++;
            definition.name = "Session " + std::to_string(launch.slot);
            std::erase_if(definition.tracks, [&](const auto &track) {
                return track.track != launch.track;
            });
            if (definition.tracks.empty()) {
                error = "performance pattern has no content for destination track";
                return false;
            }
            const auto id = definition.id;
            a.patterns.push_back(std::move(definition));
            a.clips.push_back({next_clip++, id, range_start, length, true});
        } else {
            const auto source_clip = std::find_if(source.arrangement.audio_clips.begin(),
                source.arrangement.audio_clips.end(), [&](const auto &value) {
                    return value.id == launch.source_id;
                });
            if (source_clip == source.arrangement.audio_clips.end()) {
                error = "performance audio source disappeared";
                return false;
            }
            auto clip = *source_clip;
            clip.id = next_audio++;
            clip.start = range_start;
            clip.duration = length;
            clip.track = launch.track;
            clip.loop = launch.behavior == LaunchBehavior::loop ||
                        launch.behavior == LaunchBehavior::legato;
            a.audio_clips.push_back(std::move(clip));
        }
    }
    return engine_.queue_timeline_at(audible, changes.front().activation_frame,
                                     VoiceTransitionPolicy::release_outgoing,
                                     error, affected);
}
void ApplicationService::set_midi_output_connections(
    const std::array<bool, maximum_midi_ports> &connections) {
    std::lock_guard lock(mutex_);
    if (state_.midi_output_connected != connections) {
        state_.midi_output_connected = connections;
        changed_locked();
    }
}
void ApplicationService::set_midi_input_connections(
    const std::array<bool, maximum_midi_ports> &connections) {
    std::lock_guard lock(mutex_);
    if (state_.midi_input_connected != connections) {
        state_.midi_input_connected = connections;
        changed_locked();
    }
}
bool ApplicationService::submit(ApplicationCommand command) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= capacity_) {
            ++rejected_;
            return false;
        }
        queue_.emplace_back(next_sequence_++, std::move(command));
        return true;
    } catch (...) {
        return false;
    }
}
std::vector<CommandResult> ApplicationService::dispatch_pending() {
    std::vector<CommandResult> result;
    for (;;) {
        std::pair<std::uint64_t, ApplicationCommand> item;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty())
                break;
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        result.push_back(execute_with_history(item.second, item.first));
        {
            std::lock_guard lock(mutex_);
            published_snapshot_.reset();
        }
    }
    return result;
}
CommandResult ApplicationService::execute(const ApplicationCommand &command) {
    std::uint64_t sequence;
    {
        std::lock_guard lock(mutex_);
        sequence = next_sequence_++;
    }
    auto result = execute_with_history(command, sequence);
    {
        std::lock_guard lock(mutex_);
        // Some compatibility actions update presentation-visible state without
        // advancing the global revision. A command boundary is therefore also
        // an explicit cache invalidation boundary.
        published_snapshot_.reset();
    }
    return result;
}

std::unique_ptr<ApplicationService::AuthoringHistoryState>
ApplicationService::capture_authoring_state(bool include_samples) const {
    std::map<std::uint32_t, std::filesystem::path> sample_paths;
    if (include_samples)
        for (const auto &slot : samples_.states())
            if (slot.has_sample) sample_paths[slot.id.value] = slot.source_path;
    return std::make_unique<AuthoringHistoryState>(AuthoringHistoryState{
        mixer_configuration_, modulation_configuration_,
        performance_macros_.project(),
        include_samples
            ? std::optional<SliceProject>(
                  slices_.project_snapshot(std::move(sample_paths)))
            : std::nullopt,
        synth_instance_parameters_});
}

void ApplicationService::push_history(ApplicationHistoryEntry entry) const {
    if (history_undo_.size() == application_history_capacity)
        history_undo_.pop_front();
    history_undo_.push_back(std::move(entry));
    history_redo_.clear();
}

void ApplicationService::clear_history() {
    history_undo_.clear();
    history_redo_.clear();
    pending_history_restore_.reset();
}

CommandResult ApplicationService::execute_with_history(
    const ApplicationCommand &command, std::uint64_t sequence) {
    // Observe callback acknowledgements before deciding whether a new
    // authoring transaction conflicts with pending work.
    (void)snapshot();
    const auto timeline_before = timeline_.snapshot();
    const auto config_edit =
        command.action_id.rfind("modulation.route-", 0) == 0 ||
        command.action_id.rfind("modulation.source-", 0) == 0 ||
        command.action_id.rfind("modulator.", 0) == 0 ||
        command.action_id.rfind("instrument.instance-parameter-set", 0) == 0 ||
        (command.action_id.rfind("effect.", 0) == 0 &&
         command.action_id != "effect.query.v1") ||
        (command.action_id.rfind("macro.", 0) == 0 &&
         command.action_id != "macro.value-set.v1" &&
         command.action_id != "macro.scene-recall.v1");
    const auto timeline_edit =
        command.action_id.rfind("step.", 0) == 0 ||
        command.action_id.rfind("track.", 0) == 0 ||
        command.action_id.rfind("pattern.", 0) == 0 ||
        command.action_id.rfind("arrangement.", 0) == 0 ||
        command.action_id.rfind("timeline.", 0) == 0;
    if (pending_history_restore_ &&
        (config_edit || timeline_edit ||
         command.action_id.rfind("preset.", 0) == 0 ||
         command.action_id.rfind("project.", 0) == 0))
        return {false, sequence, snapshot().revision,
                "history restoration is awaiting audio activation"};
    if (pending_preset_ && pending_preset_->published &&
        (config_edit || timeline_edit ||
         command.action_id.rfind("preset.", 0) == 0 ||
         command.action_id.rfind("project.", 0) == 0) &&
        command.action_id != "preset.cancel.v1" &&
        command.action_id != "preset.query.v1")
        return {false, sequence, snapshot().revision,
                "preset activation is awaiting audio acknowledgement"};
    const auto authoring_before = config_edit
        ? std::optional<AuthoringHistoryState>(*capture_authoring_state())
        : std::nullopt;
    auto result = execute_locked(command, sequence);
    if (!result.accepted) return result;
    if (command.action_id == "edit.undo.v1" ||
        command.action_id == "edit.redo.v1" ||
        command.action_id == "preset.apply.v1")
        return result;
    if (command.action_id == "project.open.v1" ||
        command.action_id == "project.new.v1" ||
        command.action_id == "project.close.v1") {
        clear_history();
        return result;
    }
    const auto timeline_after = timeline_.snapshot();
    ApplicationHistoryEntry entry;
    if (const auto descriptor = actions_.find(command.action_id))
        entry.label = descriptor->name;
    else
        entry.label = command.action_id;
    if (timeline_after.revision != timeline_before.revision) {
        entry.timeline_before = timeline_before.timeline;
        entry.timeline_after = timeline_after.timeline;
    }
    if (authoring_before) {
        entry.authoring_before = *authoring_before;
        entry.authoring_after = *capture_authoring_state();
    }
    if (entry.timeline_before || entry.authoring_before)
        push_history(std::move(entry));
    return result;
}

bool ApplicationService::prepare_history_restore(
    const ApplicationHistoryEntry &entry, bool undo, std::string &error) {
    if (pending_history_restore_) {
        error = "a history restoration is awaiting audio activation";
        return false;
    }
    const auto &authoring = undo ? entry.authoring_before : entry.authoring_after;
    const auto &timeline = undo ? entry.timeline_before : entry.timeline_after;
    if (!authoring) {
        if (!timeline) {
            error = "history entry has no restorable state";
            return false;
        }
        auto transaction = timeline_.edit();
        transaction.timeline() = *timeline;
        transaction.set_generation_seed(timeline->generation_seed);
        return timeline_.commit(std::move(transaction),
                                TimelineCommitPolicy::next_audio_callback,
                                error);
    }
    PerformanceMacroService macros(authoring->macros);
    const auto target_timeline = timeline ? *timeline : timeline_.snapshot().timeline;
    if (!macros.replace(authoring->macros, target_timeline, authoring->mixer,
                        error))
        return false;
    ModulationConfiguration compiled;
    if (!macros.compile(authoring->modulation, compiled, error)) return false;
    PresetActivationRequest activation;
    activation.mixer = authoring->mixer;
    activation.mixer.revision = mixer_configuration_.revision + 1;
    activation.modulation = compiled;
    if (authoring->samples && authoring->samples->sample_paths.size() >
                                   activation.samples.size()) {
        error = "history sample capacity exceeded";
        return false;
    }
    std::array<std::filesystem::path, maximum_preset_samples> sample_paths{};
    if (authoring->samples) {
        for (const auto &[slot, path] : authoring->samples->sample_paths) {
            try {
                auto sample = std::make_shared<const Sample>(
                    load_wav(path, engine_.sample_rate()));
                const auto index = activation.sample_count++;
                activation.samples[index] = {SampleSlotId{slot}, sample};
                sample_paths[index] = path;
            } catch (const std::exception &exception) {
                error = "cannot restore history sample " + path.string() +
                        ": " + exception.what();
                return false;
            }
        }
    }
    activation.replace_pad_mappings = authoring->samples.has_value();
    if (authoring->samples) {
        for (std::size_t pad = 0; pad < activation.pads.size(); ++pad) {
        auto &mapping = activation.pads[pad];
        mapping.playback = authoring->samples->pad_settings[pad];
        if (authoring->samples->region_pads[pad]) {
            const auto found = std::find_if(
                authoring->samples->regions.begin(),
                authoring->samples->regions.end(), [&](const auto &region) {
                    return region.id == *authoring->samples->region_pads[pad];
                });
            if (found == authoring->samples->regions.end()) {
                error = "history pad references a missing region";
                return false;
            }
            const auto sample = std::find_if(
                activation.samples.begin(),
                activation.samples.begin() + activation.sample_count,
                [&](const auto &candidate) { return candidate.slot == found->slot; });
            if (sample == activation.samples.begin() + activation.sample_count ||
                found->start_frame >= found->end_frame ||
                found->end_frame > sample->sample->frames()) {
                error = "history region is outside its sample";
                return false;
            }
            mapping.slot = found->slot;
            mapping.start_frame = found->start_frame;
            mapping.end_frame = found->end_frame;
            mapping.gain = found->gain;
            mapping.playback.reverse =
                found->direction == RegionDirection::reverse;
            mapping.region = {found->fade_in_frames, found->fade_out_frames,
                              found->loop_start_frame, found->loop_end_frame,
                              found->loop_crossfade_frames, found->fade_curve,
                              found->loop_enabled};
            mapping.assigned = true;
            continue;
        }
        if (!authoring->samples->pads[pad]) continue;
        const auto found = std::find_if(
            authoring->samples->slices.begin(), authoring->samples->slices.end(),
            [&](const auto &slice) {
                return slice.id == *authoring->samples->pads[pad];
            });
        if (found == authoring->samples->slices.end()) {
            error = "history pad references a missing slice";
            return false;
        }
        const auto sample = std::find_if(
            activation.samples.begin(),
            activation.samples.begin() + activation.sample_count,
            [&](const auto &candidate) { return candidate.slot == found->slot; });
        if (sample == activation.samples.begin() + activation.sample_count ||
            found->start_frame >= found->end_frame ||
            found->end_frame > sample->sample->frames()) {
            error = "history slice is outside its sample";
            return false;
        }
        mapping.slot = found->slot;
        mapping.start_frame = found->start_frame;
        mapping.end_frame = found->end_frame;
        mapping.gain = found->gain;
        mapping.assigned = true;
        }
    }
    for (const auto &[instance, parameters] : authoring->synth_parameters)
        for (const auto &[id, value] : parameters) {
            const auto destination = resolve_parameter_destination(id);
            if (!destination ||
                destination->target != ParameterTarget::subtractive_synth ||
                activation.synth_parameter_count ==
                    activation.synth_parameters.size()) {
                error = "history contains an invalid synth parameter target";
                return false;
            }
            activation.synth_parameters[activation.synth_parameter_count++] = {
                instance, static_cast<SynthParameter>(destination->parameter),
                static_cast<float>(value)};
        }
    PendingHistoryRestore pending;
    pending.entry = entry;
    pending.target = *authoring;
    pending.target.mixer = activation.mixer;
    pending.activation = activation;
    pending.sample_publications.fill(0);
    pending.undo = undo;
    if (!engine_.publish_preset_activation(
            activation, pending.activation_sequence,
            pending.sample_publications, error))
        return false;
    for (std::size_t i = 0; i < activation.sample_count; ++i)
        if (pending.target.samples)
            pending.target.samples->sample_paths
                [activation.samples[i].slot.value] = sample_paths[i];
    pending_history_restore_ = std::move(pending);
    error.clear();
    return true;
}
CommandResult
ApplicationService::execute_locked(const ApplicationCommand &command,
                                   std::uint64_t sequence) {
    std::string error;
    if (!actions_.validate(command.action_id, command.arguments, error))
        return {false, sequence, snapshot().revision, std::move(error)};
    const auto before = snapshot();
    const auto publish_parameter = [&](const std::string &id) {
        if (id == "transport.tempo") {
            const auto descriptor = parameters_.get(id);
            if (!descriptor ||
                !std::holds_alternative<double>(descriptor->current_value))
                return false;
            auto transaction = timeline_.edit();
            std::string tempo_error;
            return transaction.set_tempo_bpm(
                       std::get<double>(descriptor->current_value),
                       tempo_error) &&
                   timeline_.commit(std::move(transaction),
                                    TimelineCommitPolicy::next_audio_callback,
                                    tempo_error);
        }
        if (id.rfind("mixer.", 0) != 0) {
            if (!publish_dsp_parameter(engine_, parameters_, id))
                return false;
            const auto destination = resolve_parameter_destination(id);
            const auto descriptor = parameters_.get(id);
            if (!destination || !descriptor ||
                !std::holds_alternative<double>(descriptor->current_value))
                return true;
            auto next = modulation_configuration_;
            bool changed{};
            for (std::size_t route = 0; route < next.route_count; ++route)
                if (next.routes[route].destination.target ==
                        destination->target &&
                    next.routes[route].destination.parameter ==
                        destination->parameter) {
                    next.routes[route].base_value = static_cast<float>(
                        std::get<double>(descriptor->current_value));
                    changed = true;
                }
            if (changed) {
                std::string modulation_error;
                if (!publish_modulation_with_macros(next, modulation_error))
                    return false;
                modulation_configuration_ = next;
                project_.modulation = next;
            }
            return true;
        }
        const auto descriptor = parameters_.get(id);
        if (!descriptor ||
            !std::holds_alternative<double>(descriptor->current_value))
            return false;
        const auto value =
            static_cast<float>(std::get<double>(descriptor->current_value));
        auto next = mixer_configuration_;
        if (id == "mixer.master.gain")
            next.master.gain = value;
        else if (state_.selected_mixer_channel < next.track_count &&
                 id == "mixer.selected.gain")
            next.tracks[state_.selected_mixer_channel].gain = value;
        else if (state_.selected_mixer_channel < next.track_count &&
                 id == "mixer.selected.pan")
            next.tracks[state_.selected_mixer_channel].pan = value;
        else if (id == "mixer.external.selected.wet" &&
                 state_.selected_external_fx_loop < mixer_external_fx_count)
            next.external_fx[state_.selected_external_fx_loop].wet = value;
        else
            return false;
        ++next.revision;
        std::string mixer_error;
        if (!engine_.publish_mixer(next, mixer_error))
            return false;
        mixer_configuration_ = next;
        project_.active_routing_scene = 0;
        state_.active_routing_scene = 0;
        state_.project_dirty = true;
        return true;
    };
    if (command.action_id.rfind("macro.", 0) == 0) {
        const auto integer = [&](std::string_view name, std::int64_t fallback = 0) {
            const auto *value = argument<std::int64_t>(command, name);
            return value ? *value : fallback;
        };
        const auto real = [&](std::string_view name, double fallback = 0.0) {
            const auto *value = argument<double>(command, name);
            return value ? *value : fallback;
        };
        const auto boolean = [&](std::string_view name, bool fallback = false) {
            const auto *value = argument<bool>(command, name);
            return value ? *value : fallback;
        };
        const auto text = [&](std::string_view name,
                              std::string fallback = {}) {
            const auto *value = argument<std::string>(command, name);
            return value ? *value : std::move(fallback);
        };
        const auto morph_curve = [&](std::string value,
                                     MacroMorphCurve fallback) {
            if (value.empty())
                return fallback;
            if (value == "exponential")
                return MacroMorphCurve::exponential;
            if (value == "logarithmic")
                return MacroMorphCurve::logarithmic;
            if (value == "s-curve")
                return MacroMorphCurve::s_curve;
            if (value != "linear")
                error = "unknown macro morph curve";
            return MacroMorphCurve::linear;
        };
        const auto previous = performance_macros_.project();
        bool structural{};
        if (command.action_id == "macro.create.v1") {
            const auto owner_text = text("owner", "project");
            auto owner = MacroOwnerKind::project;
            if (owner_text == "track")
                owner = MacroOwnerKind::track;
            else if (owner_text == "group")
                owner = MacroOwnerKind::group;
            else if (owner_text == "master")
                owner = MacroOwnerKind::master;
            else if (owner_text != "project")
                error = "unknown macro owner";
            PerformanceMacroId id{};
            if (error.empty() &&
                performance_macros_.create(
                    text("name"), owner,
                    static_cast<std::uint64_t>(integer("owner_id")),
                    static_cast<float>(real("value")), id, error))
                structural = true;
        } else if (command.action_id == "macro.delete.v1") {
            structural = performance_macros_.erase(
                static_cast<PerformanceMacroId>(integer("id")), error);
        } else if (command.action_id == "macro.rename.v1") {
            structural = performance_macros_.rename(
                static_cast<PerformanceMacroId>(integer("id")), text("name"),
                text("description"), error);
        } else if (command.action_id == "macro.value-set.v1") {
            const auto id = static_cast<PerformanceMacroId>(integer("id"));
            if (performance_macros_.set_value(id, static_cast<float>(real("value")),
                                              error)) {
                const auto &macros = performance_macros_.project().macros;
                const auto found = std::find_if(macros.begin(), macros.end(),
                                                [=](const auto &macro) {
                                                    return macro.id == id;
                                                });
                if (found == macros.end() ||
                    !engine_.set_modulator_value(found->source_instance,
                                                 found->value))
                    error = "DSP command queue is full";
                else if (arrangement_recording_.status().automation_armed)
                    arrangement_recording_.capture_automation(
                        "macro." + std::to_string(found->source_instance) +
                            ".value",
                        state_.transport_tick, found->value,
                        automation_thinning_ticks_);
                if (!error.empty()) {
                    std::string ignored;
                    (void)performance_macros_.replace(
                        previous, timeline_.snapshot().timeline,
                        mixer_configuration_, ignored);
                }
            }
        } else if (command.action_id == "macro.assignment-set.v1" ||
                   command.action_id == "macro.assignment-update.v1") {
            MacroAssignment assignment;
            assignment.destination_id = text("destination");
            assignment.minimum = static_cast<float>(real("minimum"));
            assignment.maximum = static_cast<float>(real("maximum"));
            assignment.smoothing_seconds =
                static_cast<float>(real("smoothing"));
            assignment.steps = static_cast<std::uint16_t>(integer("steps", 2));
            const auto curve = text("curve", "linear");
            if (curve == "exponential")
                assignment.curve = ModulationMappingCurve::exponential;
            else if (curve == "logarithmic")
                assignment.curve = ModulationMappingCurve::logarithmic;
            else if (curve == "s-curve")
                assignment.curve = ModulationMappingCurve::s_curve;
            else if (curve == "stepped")
                assignment.curve = ModulationMappingCurve::stepped;
            else if (curve != "linear")
                error = "unknown macro mapping curve";
            const auto polarity = text("polarity", "unipolar");
            if (polarity == "bipolar")
                assignment.polarity = ModulationPolarity::bipolar;
            else if (polarity != "unipolar")
                error = "unknown macro assignment polarity";
            const auto destination =
                resolve_parameter_destination(assignment.destination_id);
            std::optional<std::pair<float, float>> destination_range;
            if (const auto descriptor =
                    parameters_.get(assignment.destination_id);
                descriptor && descriptor->modulatable &&
                descriptor->type == ParameterType::real)
                destination_range = {
                    {static_cast<float>(number(descriptor->minimum)),
                     static_cast<float>(number(descriptor->maximum))}};
            else if (destination &&
                     destination->target != ParameterTarget::native_effect) {
                for (const auto &descriptor : parameters_.parameters())
                    if (const auto candidate =
                            resolve_parameter_destination(descriptor.id);
                        candidate && descriptor.modulatable &&
                        descriptor.type == ParameterType::real &&
                        candidate->target == destination->target &&
                        candidate->parameter == destination->parameter) {
                        destination_range = {{
                            static_cast<float>(number(descriptor.minimum)),
                            static_cast<float>(number(descriptor.maximum))}};
                        break;
                    }
            }
            else if (destination &&
                     destination->target == ParameterTarget::native_effect) {
                const auto effect = std::find_if(
                    mixer_configuration_.effects.instances.begin(),
                    mixer_configuration_.effects.instances.begin() +
                        mixer_configuration_.effects.instance_count,
                    [&](const auto &candidate) {
                        return candidate.id == destination->instance;
                    });
                if (effect != mixer_configuration_.effects.instances.begin() +
                                  mixer_configuration_.effects.instance_count)
                    for (const auto &descriptor :
                         effect_parameter_descriptors(effect->type))
                        if (descriptor.id == destination->parameter &&
                            descriptor.modulatable)
                            destination_range = {
                                {descriptor.minimum, descriptor.maximum}};
            }
            if (!destination_range)
                error = "macro destination is not a modulatable real parameter";
            else if (assignment.minimum < destination_range->first ||
                     assignment.minimum > destination_range->second ||
                     assignment.maximum < destination_range->first ||
                     assignment.maximum > destination_range->second)
                error = "macro assignment range exceeds destination range";
            MacroAssignmentId id{};
            if (error.empty()) {
                const auto macro_id = static_cast<PerformanceMacroId>(
                    integer("macro_id"));
                if (command.action_id == "macro.assignment-update.v1")
                    structural = performance_macros_.update_assignment(
                        macro_id,
                        static_cast<MacroAssignmentId>(integer("assignment_id")),
                        std::move(assignment), error);
                else if (performance_macros_.assign(
                             macro_id, std::move(assignment), id, error))
                    structural = true;
            }
        } else if (command.action_id == "macro.assignment-delete.v1") {
            structural = performance_macros_.remove_assignment(
                static_cast<PerformanceMacroId>(integer("macro_id")),
                static_cast<MacroAssignmentId>(integer("assignment_id")), error);
        } else if (command.action_id == "macro.scene-capture.v1") {
            PerformanceSceneId id{};
            const auto curve = morph_curve(text("curve"),
                                           MacroMorphCurve::linear);
            structural = performance_macros_.capture_scene(
                text("name"), static_cast<float>(real("duration")),
                boolean("tempo_sync"), curve, id, error);
        } else if (command.action_id == "macro.scene-delete.v1") {
            structural = performance_macros_.erase_scene(
                static_cast<PerformanceSceneId>(integer("id")), error);
        } else if (command.action_id == "macro.scene-rename.v1") {
            structural = performance_macros_.rename_scene(
                static_cast<PerformanceSceneId>(integer("id")), text("name"),
                error);
        } else if (command.action_id == "macro.scene-update.v1") {
            const auto scene_id =
                static_cast<PerformanceSceneId>(integer("id"));
            const auto scenes = performance_macros_.project().scenes;
            const auto found = std::find_if(
                scenes.begin(), scenes.end(), [=](const auto &scene) {
                    return scene.id == scene_id;
                });
            if (found == scenes.end())
                error = "performance scene was not found";
            else
                structural = performance_macros_.update_scene(
                    scene_id, text("name", found->name),
                    static_cast<float>(real("duration", found->morph_duration)),
                    boolean("tempo_sync", found->tempo_sync),
                    morph_curve(text("curve"), found->curve), error);
        } else if (command.action_id == "macro.scene-duplicate.v1") {
            PerformanceSceneId id{};
            structural = performance_macros_.duplicate_scene(
                static_cast<PerformanceSceneId>(integer("id")), text("name"),
                id, error);
        } else if (command.action_id == "macro.scene-recall.v1") {
            const auto scene_id =
                static_cast<PerformanceSceneId>(integer("id"));
            const auto &scenes = performance_macros_.project().scenes;
            const auto scene = std::find_if(
                scenes.begin(), scenes.end(), [=](const auto &candidate) {
                    return candidate.id == scene_id;
                });
            if (scene == scenes.end())
                error = "performance scene was not found";
            else {
                const auto duration_argument = argument<double>(command, "duration");
                const auto sync_argument = argument<bool>(command, "tempo_sync");
                const auto curve = morph_curve(text("curve"), scene->curve);
                const auto start_argument =
                    argument<std::int64_t>(command, "start_frame");
                if (start_argument && *start_argument < 0)
                    error = "morph start frame must be nonnegative";
                std::vector<MacroMorphCommand> commands;
                const auto runtime_morphs = engine_.macro_morphs();
                if (error.empty() && performance_macros_.prepare_scene_morph(
                                         scene_id,
                                         static_cast<float>(duration_argument
                                                                ? *duration_argument
                                                                : scene->morph_duration),
                                         sync_argument ? *sync_argument
                                                       : scene->tempo_sync,
                                         curve,
                                         start_argument
                                             ? static_cast<std::uint64_t>(
                                                   *start_argument)
                                             : engine_.frame_position(),
                                         runtime_morphs,
                                         next_macro_morph_id_, commands,
                                         error)) {
                    std::vector<ModulatorInstanceId> queued;
                    for (const auto &morph : commands)
                        if (!engine_.schedule_macro_morph(morph)) {
                            error = "macro morph command queue is full";
                            break;
                        } else
                            queued.push_back(morph.instance);
                    if (!error.empty()) {
                        for (const auto instance : queued)
                            (void)engine_.stop_macro_morph(instance);
                        std::string ignored;
                        (void)performance_macros_.replace(
                            previous, timeline_.snapshot().timeline,
                            mixer_configuration_, ignored);
                    }
                }
            }
        } else if (command.action_id == "macro.morph-stop.v1") {
            MacroMorphCommand stop;
            stop.type = MacroMorphCommandType::stop_all;
            if (!engine_.schedule_macro_morph(stop))
                error = "macro morph command queue is full";
        }
        if (error.empty() && structural) {
            if (!publish_modulation_with_macros(modulation_configuration_, error)) {
                const auto publication_error = error;
                std::string rollback_error;
                (void)performance_macros_.replace(
                    previous, timeline_.snapshot().timeline,
                    mixer_configuration_, rollback_error);
                error = publication_error;
            }
        }
        if (error.empty() && command.action_id != "macro.query.v1") {
            project_.performance_macros = performance_macros_.project();
            state_.project_dirty = true;
            changed_locked();
        }
    } else if (command.action_id.rfind("live-loop.", 0) == 0) {
        const auto parse_source = [&](const std::string &value,
                                      RecordingSource &source) {
            if (value == "external-mono") source = RecordingSource::external_mono;
            else if (value == "external-stereo") source = RecordingSource::external_stereo;
            else if (value == "master-pre") source = RecordingSource::master_pre_effects;
            else if (value == "master-post") source = RecordingSource::master_post_effects;
            else if (value == "track") source = RecordingSource::track_channel;
            else if (value == "aux") source = RecordingSource::auxiliary_bus;
            else if (value == "physical-input") source = RecordingSource::physical_input;
            else if (value == "external-fx-return") source = RecordingSource::external_fx_return;
            else if (value == "external-fx-mix") source = RecordingSource::external_fx_mix;
            else if (value == "group-pre") source = RecordingSource::group_pre_effects;
            else if (value == "group-post") source = RecordingSource::group_post_effects;
            else if (value == "group-fader") source = RecordingSource::group_post_fader;
            else return false;
            return true;
        };
        const auto parse_quantization = [&](const std::string &value,
                                            LiveLoopQuantization &quantization) {
            if (value == "immediate") quantization = LiveLoopQuantization::immediate;
            else if (value == "subdivision") quantization = LiveLoopQuantization::subdivision;
            else if (value == "beat") quantization = LiveLoopQuantization::beat;
            else if (value == "bar") quantization = LiveLoopQuantization::bar;
            else return false;
            return true;
        };
        const auto id_argument = [&] {
            return static_cast<LiveLoopSessionId>(
                std::max<std::int64_t>(0, *argument<std::int64_t>(command, "id")));
        };
        if (command.action_id == "live-loop.create.v1") {
            LiveLoopConfiguration configuration;
            const auto track = *argument<std::int64_t>(command, "track");
            const auto slot = *argument<std::int64_t>(command, "slot");
            if (track <= 0 || slot < 0 ||
                slot >= static_cast<std::int64_t>(
                            AudioEngine::default_note_bank_size))
                error = "invalid live-loop track or sample slot";
            else {
                configuration.destination_track = static_cast<TrackId>(track);
                configuration.destination_slot = SampleSlotId{static_cast<std::uint32_t>(slot)};
                configuration.length_ticks = *argument<std::int64_t>(command, "length_ticks");
                const auto source = *argument<std::string>(command, "source");
                if (!parse_source(source, configuration.source)) error = "unknown live-loop source";
                if (const auto value = argument<std::int64_t>(command, "source_id"))
                    configuration.source_id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "channels"))
                    configuration.channels = static_cast<std::uint16_t>(*value);
                if (const auto value = argument<std::string>(command, "quantization"))
                    if (!parse_quantization(*value, configuration.quantization))
                        error = "unknown live-loop quantization";
                if (const auto value = argument<std::int64_t>(command, "subdivision_ticks"))
                    configuration.subdivision_ticks = *value;
                if (const auto value = argument<double>(command, "feedback"))
                    configuration.overdub_feedback = static_cast<float>(*value);
                if (const auto value = argument<std::string>(command, "monitoring")) {
                    if (*value == "off") configuration.monitoring = RecordingMonitorMode::off;
                    else if (*value == "on") configuration.monitoring = RecordingMonitorMode::on;
                    else if (*value == "auto") configuration.monitoring = RecordingMonitorMode::automatic;
                    else error = "unknown live-loop monitoring mode";
                }
                if (const auto value = argument<std::int64_t>(command, "latency_frames"))
                    configuration.latency_compensation_frames = *value;
            }
            if (error.empty() && !live_loops_.create(configuration, error))
                if (error.empty()) error = "live-loop creation failed";
        } else if (command.action_id == "live-loop.arm.v1" ||
                   command.action_id == "live-loop.overdub.v1" ||
                   command.action_id == "live-loop.replace.v1") {
            auto mode = LiveLoopMode::capture;
            if (command.action_id == "live-loop.overdub.v1") mode = LiveLoopMode::overdub;
            else if (command.action_id == "live-loop.replace.v1") mode = LiveLoopMode::replace;
            else if (const auto value = argument<std::string>(command, "mode")) {
                if (*value == "overdub") mode = LiveLoopMode::overdub;
                else if (*value == "replace") mode = LiveLoopMode::replace;
                else if (*value != "capture") error = "unknown live-loop mode";
            }
            if (error.empty() && live_loops_.arm(id_argument(), mode, error) &&
                command.action_id != "live-loop.arm.v1")
                (void)live_loops_.start(id_argument(), error);
        } else if (command.action_id == "live-loop.start.v1") {
            (void)live_loops_.start(id_argument(), error);
        } else if (command.action_id == "live-loop.stop.v1") {
            (void)live_loops_.stop(id_argument(), error);
        } else if (command.action_id == "live-loop.cancel.v1") {
            (void)live_loops_.cancel(id_argument(), error);
        } else if (command.action_id == "live-loop.clear.v1") {
            (void)live_loops_.remove(id_argument(), error);
        } else if (command.action_id == "live-loop.save.v1") {
            auto format = RecordingWavFormat::pcm16;
            if (const auto value = argument<std::string>(command, "format")) {
                if (*value == "float32")
                    format = RecordingWavFormat::float32;
                else if (*value != "pcm16")
                    error = "live-loop WAV format must be pcm16 or float32";
            }
            if (error.empty() && live_loops_.save(
                    id_argument(), std::filesystem::path(
                        *argument<std::string>(command, "path")),
                    format, error)) {
                std::lock_guard lock(mutex_);
                state_.project_dirty = true;
            }
        } else {
            const auto id = id_argument();
            const auto *status = live_loops_.find(id);
            if (!status) error = "live-loop session does not exist";
            else {
                auto configuration = status->configuration;
                if (command.action_id == "live-loop.source-set.v1") {
                    if (!parse_source(*argument<std::string>(command, "source"),
                                      configuration.source))
                        error = "unknown live-loop source";
                    if (const auto value = argument<std::int64_t>(command, "source_id"))
                        configuration.source_id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                } else if (command.action_id == "live-loop.quantization-set.v1") {
                    if (!parse_quantization(*argument<std::string>(command, "quantization"),
                                            configuration.quantization))
                        error = "unknown live-loop quantization";
                    if (const auto value = argument<std::int64_t>(command, "subdivision_ticks"))
                        configuration.subdivision_ticks = *value;
                } else if (command.action_id == "live-loop.feedback-set.v1") {
                    configuration.overdub_feedback = static_cast<float>(
                        *argument<double>(command, "feedback"));
                } else if (command.action_id == "live-loop.monitoring-set.v1") {
                    const auto value = *argument<std::string>(command, "monitoring");
                    if (value == "off") configuration.monitoring = RecordingMonitorMode::off;
                    else if (value == "on") configuration.monitoring = RecordingMonitorMode::on;
                    else if (value == "auto") configuration.monitoring = RecordingMonitorMode::automatic;
                    else error = "unknown live-loop monitoring mode";
                }
                if (error.empty())
                    (void)live_loops_.configure(id, configuration, error);
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (error.empty() &&
                (command.action_id == "live-loop.clear.v1" ||
                 command.action_id == "live-loop.save.v1"))
                state_.project_dirty = true;
            changed_locked();
        }
    } else if (command.action_id.rfind("recording.", 0) == 0) {
        const auto previous_recording_configuration = recording_configuration_;
        const auto text = [&](std::string_view name) {
            return *argument<std::string>(command, name);
        };
        if (command.action_id == "recording.source-select.v1") {
            const auto value = text("source");
            if (value == "external-mono")
                recording_configuration_.source =
                    RecordingSource::external_mono;
            else if (value == "external-stereo")
                recording_configuration_.source =
                    RecordingSource::external_stereo;
            else if (value == "master-pre")
                recording_configuration_.source =
                    RecordingSource::master_pre_effects;
            else if (value == "master-post")
                recording_configuration_.source =
                    RecordingSource::master_post_effects;
            else if (value == "track")
                recording_configuration_.source =
                    RecordingSource::track_channel;
            else if (value == "aux")
                recording_configuration_.source =
                    RecordingSource::auxiliary_bus;
            else if (value == "physical-input")
                recording_configuration_.source =
                    RecordingSource::physical_input;
            else if (value == "external-fx-return")
                recording_configuration_.source =
                    RecordingSource::external_fx_return;
            else if (value == "external-fx-mix")
                recording_configuration_.source =
                    RecordingSource::external_fx_mix;
            else if (value == "group-pre")
                recording_configuration_.source =
                    RecordingSource::group_pre_effects;
            else if (value == "group-post-effects")
                recording_configuration_.source =
                    RecordingSource::group_post_effects;
            else if (value == "group-post-fader")
                recording_configuration_.source =
                    RecordingSource::group_post_fader;
            else
                error = "unknown recording source";
            if (const auto *source_id =
                    argument<std::int64_t>(command, "source_id")) {
                if (*source_id < 0)
                    error = "recording source ID must be non-negative";
                else
                    recording_configuration_.source_id =
                        static_cast<std::uint64_t>(*source_id);
            }
            if (recording_configuration_.source ==
                    RecordingSource::auxiliary_bus &&
                recording_configuration_.source_id >= mixer_aux_count)
                error = "auxiliary recording source is unavailable";
            if (recording_configuration_.source ==
                    RecordingSource::physical_input &&
                recording_configuration_.source_id >= maximum_mixer_inputs)
                error = "physical recording source is unavailable";
            if ((recording_configuration_.source ==
                     RecordingSource::external_fx_return ||
                 recording_configuration_.source ==
                     RecordingSource::external_fx_mix) &&
                recording_configuration_.source_id >= mixer_external_fx_count)
                error = "external FX recording source is unavailable";
            if ((recording_configuration_.source ==
                     RecordingSource::group_pre_effects ||
                 recording_configuration_.source ==
                     RecordingSource::group_post_effects ||
                 recording_configuration_.source ==
                     RecordingSource::group_post_fader) &&
                std::none_of(mixer_configuration_.groups.begin(),
                             mixer_configuration_.groups.begin() +
                                 mixer_configuration_.group_count,
                             [&](const auto &group) {
                                 return group.id ==
                                        recording_configuration_.source_id;
                             }))
                error = "group recording source is unavailable";
            if (recording_configuration_.source ==
                    RecordingSource::track_channel &&
                std::none_of(mixer_configuration_.tracks.begin(),
                             mixer_configuration_.tracks.begin() +
                                 mixer_configuration_.track_count,
                             [&](const auto &channel) {
                                 return channel.track ==
                                        recording_configuration_.source_id;
                             }))
                error = "track recording source is unavailable";
        } else if (command.action_id == "recording.destination-select.v1") {
            const auto value = *argument<std::int64_t>(command, "slot_id");
            if (value < 0 || value >= invalid_sample_slot.value)
                error = "invalid recording destination";
            else
                recording_configuration_.destination =
                    SampleSlotId{static_cast<std::uint32_t>(value)};
        } else if (command.action_id == "recording.channels-set.v1") {
            const auto value = *argument<std::int64_t>(command, "channels");
            if (value != 1 && value != 2)
                error = "recording channels must be 1 or 2";
            else
                recording_configuration_.channels =
                    static_cast<std::uint16_t>(value);
        } else if (command.action_id == "recording.quantization-set.v1") {
            const auto value = text("quantization");
            if (value == "immediate")
                recording_configuration_.quantization =
                    RecordingQuantization::immediate;
            else if (value == "beat")
                recording_configuration_.quantization =
                    RecordingQuantization::beat;
            else if (value == "bar")
                recording_configuration_.quantization =
                    RecordingQuantization::bar;
            else
                error = "unknown recording quantization";
        } else if (command.action_id == "recording.fixed-bars-set.v1") {
            const auto value = *argument<std::int64_t>(command, "bars");
            if (value < 0 || value > 1024)
                error = "fixed recording length must be 0..1024 bars";
            else
                recording_fixed_bars_ = static_cast<std::uint64_t>(value);
        } else if (command.action_id == "recording.monitoring-set.v1") {
            const auto value = text("mode");
            if (value == "off")
                recording_configuration_.monitoring = RecordingMonitorMode::off;
            else if (value == "on")
                recording_configuration_.monitoring = RecordingMonitorMode::on;
            else if (value == "auto")
                recording_configuration_.monitoring =
                    RecordingMonitorMode::automatic;
            else
                error = "unknown recording monitoring mode";
        } else if (command.action_id == "recording.arm.v1") {
            const auto timeline = timeline_.snapshot().timeline;
            const auto bar_ticks = static_cast<MusicalTick>(
                ticks_per_quarter * 4ULL *
                timeline.transport.time_signature_numerator /
                timeline.transport.time_signature_denominator);
            auto start_tick = before.transport_tick;
            if (recording_configuration_.quantization !=
                RecordingQuantization::immediate) {
                if (!before.playing)
                    error = "quantized recording requires a running transport";
                const auto quantum =
                    recording_configuration_.quantization ==
                            RecordingQuantization::beat
                        ? static_cast<MusicalTick>(ticks_per_quarter)
                        : bar_ticks;
                if (error.empty())
                    start_tick = ((start_tick / quantum) + 1) * quantum;
            }
            const auto frames_for_ticks = [&](MusicalTick ticks) {
                return static_cast<std::uint64_t>(std::ceil(
                    static_cast<long double>(std::max<MusicalTick>(0, ticks)) *
                    engine_.sample_rate() *
                    timeline.transport.microseconds_per_quarter /
                    (static_cast<long double>(ticks_per_quarter) *
                     1'000'000.0L)));
            };
            if (error.empty()) {
                recording_configuration_.start_frame =
                    engine_.callback_frame_position() +
                    frames_for_ticks(start_tick - before.transport_tick);
                recording_configuration_.fixed_frames =
                    recording_fixed_bars_
                        ? frames_for_ticks(bar_ticks * recording_fixed_bars_)
                        : 0;
                recording_.arm(recording_configuration_, error);
            }
        } else if (command.action_id == "recording.start.v1") {
            recording_.start(engine_.callback_frame_position(), error);
        } else if (command.action_id == "recording.stop.v1") {
            if (recording_configuration_.quantization ==
                RecordingQuantization::immediate)
                recording_.stop(error);
            else if (!before.playing)
                error = "quantized recording stop requires running transport";
            else {
                const auto timeline = timeline_.snapshot().timeline;
                const auto bar_ticks = static_cast<MusicalTick>(
                    ticks_per_quarter * 4ULL *
                    timeline.transport.time_signature_numerator /
                    timeline.transport.time_signature_denominator);
                const auto quantum =
                    recording_configuration_.quantization ==
                            RecordingQuantization::beat
                        ? static_cast<MusicalTick>(ticks_per_quarter)
                        : bar_ticks;
                const auto stop_tick =
                    ((before.transport_tick / quantum) + 1) * quantum;
                const auto delta_frames = static_cast<std::uint64_t>(
                    std::ceil(static_cast<long double>(stop_tick -
                                                       before.transport_tick) *
                              engine_.sample_rate() *
                              timeline.transport.microseconds_per_quarter /
                              (static_cast<long double>(ticks_per_quarter) *
                               1'000'000.0L)));
                recording_.stop_at(
                    engine_.callback_frame_position() + delta_frames, error);
            }
        } else if (command.action_id == "recording.cancel.v1") {
            recording_.cancel();
        } else if (command.action_id == "recording.save.v1") {
            const auto format_found = command.arguments.find("format");
            auto format = RecordingWavFormat::pcm16;
            if (format_found != command.arguments.end()) {
                const auto &value = std::get<std::string>(format_found->second);
                if (value == "float32")
                    format = RecordingWavFormat::float32;
                else if (value != "pcm16")
                    error = "recording WAV format must be pcm16 or float32";
            }
            const auto path = std::filesystem::path(text("path"));
            if (error.empty() && recording_.save_wav(path, format, error) &&
                !samples_.set_source_path(recording_configuration_.destination,
                                          path))
                error = "recorded sample is not published";
        } else if (command.action_id == "recording.assign-pad.v1") {
            const auto pad_value = *argument<std::int64_t>(command, "pad");
            if (pad_value < 0 || pad_value >= 16)
                error = "pad must be 0..15";
            else {
                const auto status =
                    samples_.status(recording_configuration_.destination);
                auto slice_state =
                    slices_.snapshot(recording_configuration_.destination);
                if (!status.has_sample || status.frames == 0)
                    error = "recorded sample is not audio-active";
                else {
                    SliceId id{};
                    const auto found = std::find_if(
                        slice_state.slices.begin(), slice_state.slices.end(),
                        [&](const auto &slice) {
                            return slice.start_frame == 0 &&
                                   slice.end_frame == status.frames;
                        });
                    if (found != slice_state.slices.end())
                        id = found->id;
                    else if (!slices_.create(
                                 recording_configuration_.destination,
                                 slice_state.revision, 0, status.frames,
                                 "Recording", id, error))
                        id = 0;
                    if (id) {
                        slice_state = slices_.snapshot(
                            recording_configuration_.destination);
                        slices_.assign_pad(static_cast<std::size_t>(pad_value),
                                           id, slice_state.revision, error);
                    }
                }
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (!error.empty())
                recording_configuration_ = previous_recording_configuration;
            if (error.empty() &&
                (command.action_id == "recording.save.v1" ||
                 command.action_id == "recording.assign-pad.v1"))
                state_.project_dirty = true;
            changed_locked();
        }
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("midi.", 0) == 0) {
        auto next = project_.midi;
        if (command.action_id == "midi.panic.v1") {
            engine_.midi_panic();
        } else if (command.action_id == "midi.clock-source-set.v1") {
            const auto &source = *argument<std::string>(command, "source");
            if (source == "internal")
                next.clock_source = MidiClockSource::internal;
            else if (source == "external")
                next.clock_source = MidiClockSource::external;
            else
                error = "MIDI clock source must be internal or external";
        } else if (command.action_id == "midi.clock-output-set.v1") {
            const auto port = *argument<std::int64_t>(command, "port");
            const auto enabled = *argument<bool>(command, "enabled");
            if (port < 1 ||
                port > static_cast<std::int64_t>(maximum_midi_ports))
                error = "MIDI output port is out of range";
            else {
                auto begin = next.clock_outputs.begin();
                auto end = begin +
                           static_cast<std::ptrdiff_t>(next.clock_output_count);
                auto found =
                    std::find(begin, end, static_cast<MidiPortId>(port));
                if (enabled && found == end &&
                    next.clock_output_count < maximum_midi_ports)
                    next.clock_outputs[next.clock_output_count++] =
                        static_cast<MidiPortId>(port);
                else if (!enabled && found != end) {
                    std::move(found + 1, end, found);
                    --next.clock_output_count;
                }
            }
        } else if (command.action_id == "midi.clock-forward-set.v1") {
            next.forward_external_clock = *argument<bool>(command, "enabled");
        } else if (command.action_id == "midi.route-delete.v1") {
            const auto id = *argument<std::int64_t>(command, "id");
            auto begin = next.routes.begin();
            auto end = begin + static_cast<std::ptrdiff_t>(next.route_count);
            const auto found = std::find_if(begin, end, [&](const auto &route) {
                return route.id == static_cast<std::uint32_t>(id);
            });
            if (found == end)
                error = "MIDI route was not found";
            else {
                std::move(found + 1, end, found);
                --next.route_count;
            }
        } else if (command.action_id == "midi.route-set.v1") {
            MidiRoute route;
            route.id = static_cast<std::uint32_t>(
                *argument<std::int64_t>(command, "id"));
            route.input_port = static_cast<MidiPortId>(
                *argument<std::int64_t>(command, "input_port"));
            route.output_port = static_cast<MidiPortId>(
                *argument<std::int64_t>(command, "output_port"));
            route.input_channel = static_cast<std::int8_t>(
                *argument<std::int64_t>(command, "input_channel"));
            route.output_channel = static_cast<std::int8_t>(
                *argument<std::int64_t>(command, "output_channel"));
            route.notes = *argument<bool>(command, "notes");
            route.control_change = *argument<bool>(command, "cc");
            route.clock = *argument<bool>(command, "clock");
            route.transport = *argument<bool>(command, "transport");
            if (const auto enabled = argument<bool>(command, "enabled"))
                route.enabled = *enabled;
            auto found =
                std::find_if(next.routes.begin(),
                             next.routes.begin() +
                                 static_cast<std::ptrdiff_t>(next.route_count),
                             [&](const auto &candidate) {
                                 return candidate.id == route.id;
                             });
            if (found != next.routes.begin() +
                             static_cast<std::ptrdiff_t>(next.route_count))
                *found = route;
            else if (next.route_count == maximum_midi_routes)
                error = "MIDI route capacity is exhausted";
            else
                next.routes[next.route_count++] = route;
        } else if (command.action_id == "midi.learn.v1") {
            error = "MIDI learn capture is not implemented";
        }
        if (error.empty()) {
            if (!midi_router_.configure({next.routes.data(), next.route_count},
                                        error)) {
            } else {
                MidiClockOutputConfiguration clock;
                clock.enabled =
                    next.clock_source == MidiClockSource::internal &&
                    next.clock_output_count;
                clock.forward_external =
                    next.clock_source == MidiClockSource::external &&
                    next.forward_external_clock;
                clock.port_count = next.clock_output_count;
                std::copy_n(next.clock_outputs.begin(), clock.port_count,
                            clock.ports.begin());
                if (!engine_.publish_midi_clock_output(clock))
                    error = "MIDI clock publication queue is full";
                MidiRoutingConfiguration routing;
                routing.route_count = next.route_count;
                routing.accept_external_clock =
                    next.clock_source == MidiClockSource::external;
                std::copy_n(next.routes.begin(), routing.route_count,
                            routing.routes.begin());
                if (error.empty() && !engine_.publish_midi_routing(routing))
                    error = "MIDI routing publication queue is full";
            }
        }
        if (error.empty()) {
            project_.midi = next;
            state_.midi = next;
            state_.project_dirty = true;
        }
        changed_locked();
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("effect.", 0) == 0) {
        if (command.action_id == "effect.query.v1")
            return {true, sequence, snapshot().revision, {}};
        auto next = mixer_configuration_;
        auto &rack = next.effects;
        const auto chain_for =
            [&](std::string_view kind,
                std::int64_t index) -> EffectChainConfiguration * {
            if (kind == "track" && index >= 0 &&
                static_cast<std::size_t>(index) < rack.tracks.size())
                return &rack.tracks[static_cast<std::size_t>(index)];
            if (kind == "aux" && index >= 0 &&
                static_cast<std::size_t>(index) < rack.aux.size())
                return &rack.aux[static_cast<std::size_t>(index)];
            if (kind == "group" && index > 0) {
                const auto found = std::find_if(
                    next.groups.begin(), next.groups.begin() + next.group_count,
                    [&](const auto &group) {
                        return group.id == static_cast<MixerBusId>(index);
                    });
                if (found != next.groups.begin() + next.group_count)
                    return &rack.groups[static_cast<std::size_t>(
                        found - next.groups.begin())];
            }
            if (kind == "master" && index == 0)
                return &rack.master;
            return nullptr;
        };
        const auto find_instance = [&](EffectInstanceId id) {
            return std::find_if(
                rack.instances.begin(),
                rack.instances.begin() + rack.instance_count,
                [&](const auto &value) { return value.id == id; });
        };
        if (command.action_id == "effect.create.v1") {
            const auto raw_id = *argument<std::int64_t>(command, "id");
            const auto &type_name = *argument<std::string>(command, "type");
            EffectProcessorType type{};
            if (type_name == "gain" ||
                type_name == effect_processor_type_id(
                                 EffectProcessorType::gain))
                type = EffectProcessorType::gain;
            else if (type_name == "delay" ||
                     type_name == effect_processor_type_id(
                                      EffectProcessorType::stereo_delay))
                type = EffectProcessorType::stereo_delay;
            else if (type_name == "filter" ||
                     type_name == effect_processor_type_id(
                                      EffectProcessorType::low_pass_filter))
                type = EffectProcessorType::low_pass_filter;
            else if (type_name == "saturation" ||
                     type_name == effect_processor_type_id(
                                      EffectProcessorType::saturation))
                type = EffectProcessorType::saturation;
            else
                error = "unknown effect processor type";
            if (error.empty() &&
                (raw_id <= 0 ||
                 raw_id > std::numeric_limits<std::uint32_t>::max()))
                error = "effect instance ID must be a positive 32-bit value";
            const auto id = static_cast<EffectInstanceId>(
                std::max<std::int64_t>(0, raw_id));
            if (error.empty() && find_instance(id) != rack.instances.begin() +
                                                          rack.instance_count)
                error = "effect instance ID already exists";
            else if (error.empty() &&
                     rack.instance_count == rack.instances.size())
                error = "effect instance capacity is full";
            else if (error.empty()) {
                auto &instance = rack.instances[rack.instance_count++];
                instance.id = id;
                instance.type = type;
                const auto descriptors = effect_parameter_descriptors(type);
                instance.parameter_count = descriptors.size();
                for (std::size_t i = 0; i < descriptors.size(); ++i)
                    instance.parameters[i] = {descriptors[i].id,
                                              descriptors[i].default_value};
            }
        } else if (command.action_id == "effect.delete.v1") {
            const auto id = static_cast<EffectInstanceId>(
                *argument<std::int64_t>(command, "id"));
            const auto found = find_instance(id);
            if (found == rack.instances.begin() + rack.instance_count)
                error = "unknown effect instance ID";
            else {
                const auto used = [&](const EffectChainConfiguration &chain) {
                    return std::find(chain.instances.begin(),
                                     chain.instances.begin() + chain.count,
                                     id) !=
                           chain.instances.begin() + chain.count;
                };
                bool referenced = used(rack.master);
                for (const auto &chain : rack.tracks)
                    referenced |= used(chain);
                for (const auto &chain : rack.aux)
                    referenced |= used(chain);
                for (const auto &chain : rack.groups)
                    referenced |= used(chain);
                if (referenced)
                    error = "effect instance is still assigned to a chain";
                else {
                    std::move(found + 1,
                              rack.instances.begin() + rack.instance_count,
                              found);
                    --rack.instance_count;
                }
            }
        } else if (command.action_id == "effect.bypass.v1" ||
                   command.action_id == "effect.parameter-set.v1") {
            const auto id = static_cast<EffectInstanceId>(
                *argument<std::int64_t>(command, "id"));
            const auto found = find_instance(id);
            if (found == rack.instances.begin() + rack.instance_count)
                error = "unknown effect instance ID";
            else if (command.action_id == "effect.bypass.v1")
                found->bypass = *argument<bool>(command, "bypass");
            else {
                const auto parameter = static_cast<std::uint32_t>(
                    *argument<std::int64_t>(command, "parameter"));
                const auto value =
                    static_cast<float>(*argument<double>(command, "value"));
                const auto descriptor = std::find_if(
                    effect_parameter_descriptors(found->type).begin(),
                    effect_parameter_descriptors(found->type).end(),
                    [&](const auto &item) { return item.id == parameter; });
                if (descriptor ==
                        effect_parameter_descriptors(found->type).end() ||
                    value < descriptor->minimum || value > descriptor->maximum)
                    error = "invalid effect parameter value";
                else {
                    const auto existing = std::find_if(
                        found->parameters.begin(),
                        found->parameters.begin() + found->parameter_count,
                        [&](const auto &item) { return item.id == parameter; });
                    if (existing !=
                        found->parameters.begin() + found->parameter_count)
                        existing->value = value;
                    else if (found->parameter_count < found->parameters.size())
                        found->parameters[found->parameter_count++] = {
                            parameter, value};
                }
            }
        } else {
            const auto &kind = *argument<std::string>(command, "chain");
            const auto index = *argument<std::int64_t>(command, "index");
            auto *chain = chain_for(kind, index);
            if (!chain)
                error = "unknown effect chain";
            else if (command.action_id == "effect.chain-bypass.v1")
                chain->bypass = *argument<bool>(command, "bypass");
            else if (command.action_id == "effect.insert.v1") {
                const auto id = static_cast<EffectInstanceId>(
                    *argument<std::int64_t>(command, "id"));
                const auto position =
                    *argument<std::int64_t>(command, "position");
                if (find_instance(id) ==
                    rack.instances.begin() + rack.instance_count)
                    error = "unknown effect instance ID";
                else if (position < 0 ||
                         static_cast<std::size_t>(position) > chain->count ||
                         chain->count == chain->instances.size())
                    error = "effect chain position or capacity is invalid";
                else if (std::find(chain->instances.begin(),
                                   chain->instances.begin() + chain->count,
                                   id) !=
                         chain->instances.begin() + chain->count)
                    error = "effect instance is already in this chain";
                else {
                    std::move_backward(chain->instances.begin() + position,
                                       chain->instances.begin() + chain->count,
                                       chain->instances.begin() + chain->count +
                                           1);
                    chain->instances[position] = id;
                    ++chain->count;
                }
            } else if (command.action_id == "effect.remove.v1") {
                const auto position =
                    *argument<std::int64_t>(command, "position");
                if (position < 0 ||
                    static_cast<std::size_t>(position) >= chain->count)
                    error = "effect chain position is invalid";
                else {
                    std::move(chain->instances.begin() + position + 1,
                              chain->instances.begin() + chain->count,
                              chain->instances.begin() + position);
                    --chain->count;
                }
            } else if (command.action_id == "effect.reorder.v1") {
                const auto from = *argument<std::int64_t>(command, "from");
                const auto to = *argument<std::int64_t>(command, "to");
                if (from < 0 || to < 0 ||
                    static_cast<std::size_t>(from) >= chain->count ||
                    static_cast<std::size_t>(to) >= chain->count)
                    error = "effect reorder position is invalid";
                else {
                    const auto id = chain->instances[from];
                    if (from < to)
                        std::move(chain->instances.begin() + from + 1,
                                  chain->instances.begin() + to + 1,
                                  chain->instances.begin() + from);
                    else
                        std::move_backward(chain->instances.begin() + to,
                                           chain->instances.begin() + from,
                                           chain->instances.begin() + from + 1);
                    chain->instances[to] = id;
                }
            }
        }
        if (error.empty()) {
            ++next.revision;
            if (!engine_.publish_mixer(next, error)) {
            } else {
                mixer_configuration_ = next;
                project_.mixer = next;
                state_.project_dirty = true;
                if (command.action_id == "effect.parameter-set.v1") {
                    const auto destination =
                        "fx." +
                        std::to_string(*argument<std::int64_t>(command, "id")) +
                        "." +
                        std::to_string(
                            *argument<std::int64_t>(command, "parameter"));
                    arrangement_recording_.capture_automation(
                        destination,
                        static_cast<float>(*argument<double>(command, "value")),
                        state_.transport_tick, automation_thinning_ticks_);
                }
            }
        }
        changed_locked();
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("mixer.", 0) == 0) {
        if (command.action_id.rfind("mixer.scene-", 0) == 0) {
            if (command.action_id == "mixer.scene-save.v1") {
                const auto &name = *argument<std::string>(command, "name");
                if (name.empty() || name.size() > 128)
                    error = "routing scene name must contain 1..128 characters";
                else if (project_.routing_scenes.size() >= 32)
                    error = "routing scene capacity is full";
                else {
                    std::uint64_t id = 1;
                    for (const auto &scene : project_.routing_scenes)
                        id = std::max(id, scene.id + 1);
                    project_.routing_scenes.push_back(
                        {id, name, mixer_configuration_});
                    project_.active_routing_scene = id;
                }
            } else {
                const auto requested = *argument<std::int64_t>(command, "id");
                const auto found = std::find_if(
                    project_.routing_scenes.begin(),
                    project_.routing_scenes.end(), [&](const auto &scene) {
                        return requested > 0 &&
                               scene.id ==
                                   static_cast<std::uint64_t>(requested);
                    });
                if (found == project_.routing_scenes.end())
                    error = "routing scene was not found";
                else if (command.action_id == "mixer.scene-recall.v1") {
                    auto next = found->mixer;
                    next.revision = mixer_configuration_.revision + 1;
                    if (engine_.publish_mixer(next, error)) {
                        mixer_configuration_ = next;
                        project_.active_routing_scene = found->id;
                    }
                } else if (command.action_id == "mixer.scene-rename.v1") {
                    const auto &name = *argument<std::string>(command, "name");
                    if (name.empty() || name.size() > 128)
                        error =
                            "routing scene name must contain 1..128 characters";
                    else
                        found->name = name;
                } else if (command.action_id == "mixer.scene-delete.v1") {
                    const auto id = found->id;
                    project_.routing_scenes.erase(found);
                    if (project_.active_routing_scene == id)
                        project_.active_routing_scene = 0;
                }
            }
            {
                std::lock_guard lock(mutex_);
                if (error.empty())
                    state_.project_dirty = true;
                state_.routing_scenes = project_.routing_scenes;
                state_.active_routing_scene = project_.active_routing_scene;
                changed_locked();
            }
            return {error.empty(), sequence, snapshot().revision,
                    std::move(error)};
        }
        const auto previous_configuration = mixer_configuration_;
        const auto previous_selected_group = state_.selected_group;
        auto changed = false;
        const auto find_group = [&](MixerBusId id) {
            return std::find_if(
                mixer_configuration_.groups.begin(),
                mixer_configuration_.groups.begin() +
                    mixer_configuration_.group_count,
                [&](const auto &group) { return group.id == id; });
        };
        if (command.action_id == "mixer.group-create.v1") {
            const auto &name = *argument<std::string>(command, "name");
            if (name.empty() || name.size() >= MixerGroupSettings{}.name.size())
                error = "group name must contain 1..31 characters";
            else if (mixer_configuration_.group_count == maximum_mixer_groups)
                error = "group bus capacity is exhausted (8 groups)";
            else {
                std::uint64_t next_id = 1;
                for (std::size_t i = 0; i < mixer_configuration_.group_count;
                     ++i)
                    next_id = std::max(next_id,
                                       static_cast<std::uint64_t>(
                                           mixer_configuration_.groups[i].id) +
                                           1);
                if (next_id > std::numeric_limits<MixerBusId>::max())
                    error = "group bus ID space is exhausted";
                else {
                    const auto id = static_cast<MixerBusId>(next_id);
                    auto &group =
                        mixer_configuration_
                            .groups[mixer_configuration_.group_count++];
                    group.id = id;
                    std::copy(name.begin(), name.end(), group.name.begin());
                    state_.selected_group = id;
                    changed = true;
                }
            }
        } else if (command.action_id == "mixer.group-select.v1") {
            const auto id =
                static_cast<MixerBusId>(*argument<std::int64_t>(command, "id"));
            if (find_group(id) == mixer_configuration_.groups.begin() +
                                      mixer_configuration_.group_count)
                error = "group bus does not exist";
            else
                state_.selected_group = id;
        } else if (command.action_id == "mixer.group-delete.v1") {
            const auto id =
                static_cast<MixerBusId>(*argument<std::int64_t>(command, "id"));
            const auto found = find_group(id);
            if (found == mixer_configuration_.groups.begin() +
                             mixer_configuration_.group_count)
                error = "group bus does not exist";
            else {
                const auto referenced_track =
                    std::any_of(mixer_configuration_.tracks.begin(),
                                mixer_configuration_.tracks.begin() +
                                    mixer_configuration_.track_count,
                                [&](const auto &track) {
                                    return track.destination_group == id;
                                });
                const auto referenced_group = std::any_of(
                    mixer_configuration_.groups.begin(),
                    mixer_configuration_.groups.begin() +
                        mixer_configuration_.group_count,
                    [&](const auto &group) {
                        return group.id != id && group.destination_group == id;
                    });
                const auto referenced_modulator =
                    std::any_of(modulation_configuration_.modulators.begin(),
                                modulation_configuration_.modulators.begin() +
                                    modulation_configuration_.modulator_count,
                                [&](const auto &modulator) {
                                    return modulator.owner_kind ==
                                               ModulatorOwnerKind::group &&
                                           modulator.owner_id == id;
                                });
                const auto referenced_route =
                    std::any_of(modulation_configuration_.routes.begin(),
                                modulation_configuration_.routes.begin() +
                                    modulation_configuration_.route_count,
                                [&](const auto &route) {
                                    return route.destination_owner_explicit &&
                                           route.destination_owner_kind ==
                                               ModulatorOwnerKind::group &&
                                           route.destination_owner_id == id;
                                });
                const auto referenced_external = std::any_of(
                    mixer_configuration_.external_fx.begin(),
                    mixer_configuration_.external_fx.end(),
                    [&](const auto &loop) {
                        return loop.enabled &&
                               loop.source == ExternalFxSource::group &&
                               loop.source_id == id;
                    });
                const auto recording_source_is_group =
                    recording_configuration_.source ==
                        RecordingSource::group_pre_effects ||
                    recording_configuration_.source ==
                        RecordingSource::group_post_effects ||
                    recording_configuration_.source ==
                        RecordingSource::group_post_fader;
                if (referenced_track || referenced_group ||
                    referenced_modulator || referenced_route ||
                    referenced_external ||
                    (recording_source_is_group &&
                     recording_configuration_.source_id == id))
                    error = "group bus is still referenced";
                else {
                    const auto slot = static_cast<std::size_t>(
                        found - mixer_configuration_.groups.begin());
                    std::move(found + 1,
                              mixer_configuration_.groups.begin() +
                                  mixer_configuration_.group_count,
                              found);
                    std::move(
                        mixer_configuration_.effects.groups.begin() + slot + 1,
                        mixer_configuration_.effects.groups.begin() +
                            mixer_configuration_.group_count,
                        mixer_configuration_.effects.groups.begin() + slot);
                    --mixer_configuration_.group_count;
                    mixer_configuration_
                        .groups[mixer_configuration_.group_count] = {};
                    mixer_configuration_.effects
                        .groups[mixer_configuration_.group_count] = {};
                    if (state_.selected_group == id)
                        state_.selected_group = 0;
                    changed = true;
                }
            }
        } else if (command.action_id == "mixer.group-rename.v1") {
            const auto id =
                static_cast<MixerBusId>(*argument<std::int64_t>(command, "id"));
            const auto &name = *argument<std::string>(command, "name");
            const auto found = find_group(id);
            if (found == mixer_configuration_.groups.begin() +
                             mixer_configuration_.group_count)
                error = "group bus does not exist";
            else if (name.empty() || name.size() >= found->name.size())
                error = "group name must contain 1..31 characters";
            else {
                found->name.fill(0);
                std::copy(name.begin(), name.end(), found->name.begin());
                changed = true;
            }
        } else if (command.action_id == "mixer.track-destination.v1") {
            const auto track_id = static_cast<TrackId>(
                *argument<std::int64_t>(command, "track_id"));
            const auto group_id = static_cast<MixerBusId>(
                *argument<std::int64_t>(command, "group_id"));
            const auto track = std::find_if(
                mixer_configuration_.tracks.begin(),
                mixer_configuration_.tracks.begin() +
                    mixer_configuration_.track_count,
                [&](const auto &item) { return item.track == track_id; });
            if (track == mixer_configuration_.tracks.begin() +
                             mixer_configuration_.track_count)
                error = "mixer track does not exist";
            else if (group_id && find_group(group_id) ==
                                     mixer_configuration_.groups.begin() +
                                         mixer_configuration_.group_count)
                error = "group bus does not exist";
            else {
                track->destination_group = group_id;
                if (group_id)
                    track->output_pair = -1;
                changed = true;
            }
        } else if (command.action_id == "mixer.group-output.v1" ||
                   command.action_id == "mixer.group-levels.v1" ||
                   command.action_id == "mixer.group-mute-toggle.v1" ||
                   command.action_id == "mixer.group-solo-toggle.v1" ||
                   command.action_id == "mixer.group-aux-send.v1") {
            const auto id =
                static_cast<MixerBusId>(*argument<std::int64_t>(command, "id"));
            const auto found = find_group(id);
            if (found == mixer_configuration_.groups.begin() +
                             mixer_configuration_.group_count)
                error = "group bus does not exist";
            else if (command.action_id == "mixer.group-output.v1") {
                const auto destination = static_cast<MixerBusId>(
                    *argument<std::int64_t>(command, "group_id"));
                auto output = std::int64_t{-1};
                if (const auto *value =
                        argument<std::int64_t>(command, "output_pair"))
                    output = *value;
                if ((destination && find_group(destination) ==
                                        mixer_configuration_.groups.begin() +
                                            mixer_configuration_.group_count) ||
                    (output != -1 && output != 0 && output != 2 && output != 4))
                    error = "invalid group destination";
                else {
                    found->destination_group = destination;
                    found->output_pair = static_cast<std::int8_t>(output);
                    changed = true;
                }
            } else if (command.action_id == "mixer.group-levels.v1") {
                const auto gain = number(command.arguments.at("gain"));
                const auto pan = number(command.arguments.at("pan"));
                if (!std::isfinite(gain) || gain < 0 || gain > 2 ||
                    !std::isfinite(pan) || pan < -1 || pan > 1)
                    error = "group gain/pan is out of range";
                else {
                    found->gain = static_cast<float>(gain);
                    found->pan = static_cast<float>(pan);
                    changed = true;
                }
            } else if (command.action_id == "mixer.group-mute-toggle.v1") {
                found->mute = !found->mute;
                changed = true;
            } else if (command.action_id == "mixer.group-solo-toggle.v1") {
                found->solo = !found->solo;
                changed = true;
            } else {
                const auto aux = *argument<std::int64_t>(command, "aux");
                const auto value = number(command.arguments.at("value"));
                if (aux < 0 || aux >= 2 || !std::isfinite(value) || value < 0 ||
                    value > 1)
                    error = "group aux send is out of range";
                else {
                    found->aux_send[static_cast<std::size_t>(aux)] =
                        static_cast<float>(value);
                    if (const auto *pre = argument<bool>(command, "pre_fader"))
                        found->aux_pre_fader[static_cast<std::size_t>(aux)] =
                            *pre;
                    changed = true;
                }
            }
        } else if (command.action_id == "mixer.fx-loop-select.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            if (index < 0 ||
                index >= static_cast<std::int64_t>(mixer_external_fx_count))
                error = "external FX loop index is unavailable";
            else
                state_.selected_external_fx_loop =
                    static_cast<std::size_t>(index);
            if (error.empty()) {
                std::string ignored;
                parameters_.set(
                    "mixer.external.selected.wet",
                    static_cast<double>(
                        mixer_configuration_
                            .external_fx[state_.selected_external_fx_loop]
                            .wet),
                    ignored);
            }
        } else if (command.action_id == "mixer.fx-loop-set.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto source_id =
                *argument<std::int64_t>(command, "source_id");
            const auto send_left =
                *argument<std::int64_t>(command, "send_left");
            const auto send_right =
                *argument<std::int64_t>(command, "send_right");
            const auto return_left =
                *argument<std::int64_t>(command, "return_left");
            const auto return_right =
                *argument<std::int64_t>(command, "return_right");
            if (index < 0 || index >= 2 || source_id < 0 || send_left < 0 ||
                send_left >= 6 || send_right < 0 || send_right >= 6 ||
                return_left < 0 || return_left >= 6 || return_right < 0 ||
                return_right >= 6)
                error = "invalid external FX mapping";
            else {
                auto &loop = mixer_configuration_
                                 .external_fx[static_cast<std::size_t>(index)];
                const auto source = *argument<std::string>(command, "source");
                if (source == "track")
                    loop.source = ExternalFxSource::track;
                else if (source == "aux")
                    loop.source = ExternalFxSource::auxiliary;
                else if (source == "master")
                    loop.source = ExternalFxSource::master;
                else if (source == "group")
                    loop.source = ExternalFxSource::group;
                else
                    error = "external FX source must be track, aux, group, or "
                            "master";
                loop.source_id = static_cast<std::uint64_t>(source_id);
                loop.send_left = static_cast<std::uint8_t>(send_left);
                loop.send_right = static_cast<std::uint8_t>(send_right);
                loop.return_left = static_cast<std::uint8_t>(return_left);
                loop.return_right = static_cast<std::uint8_t>(return_right);
                if (const auto *name = argument<std::string>(command, "name")) {
                    if (name->empty() || name->size() >= loop.name.size())
                        error =
                            "external FX name must contain 1..31 characters";
                    else {
                        loop.name.fill('\0');
                        std::copy(name->begin(), name->end(),
                                  loop.name.begin());
                    }
                }
                if (const auto *value = argument<bool>(command, "enabled"))
                    loop.enabled = *value;
                if (const auto *value = argument<bool>(command, "bypass"))
                    loop.bypass = *value;
                if (const auto *value = argument<bool>(command, "mono_send"))
                    loop.mono_send = *value;
                if (const auto *value = argument<bool>(command, "mono_return"))
                    loop.mono_return = *value;
                changed = error.empty();
            }
        } else if (command.action_id == "mixer.fx-loop-levels.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto send = number(command.arguments.at("send_gain"));
            const auto receive = number(command.arguments.at("return_gain"));
            const auto wet = number(command.arguments.at("wet"));
            if (index < 0 || index >= 2 || !std::isfinite(send) || send < 0 ||
                send > 2 || !std::isfinite(receive) || receive < 0 ||
                receive > 2 || !std::isfinite(wet) || wet < 0 || wet > 1)
                error = "invalid external FX levels";
            else {
                auto &loop = mixer_configuration_
                                 .external_fx[static_cast<std::size_t>(index)];
                loop.send_gain = static_cast<float>(send);
                loop.return_gain = static_cast<float>(receive);
                loop.wet = static_cast<float>(wet);
                if (const auto *mute = argument<bool>(command, "return_mute"))
                    loop.return_mute = *mute;
                changed = true;
            }
        } else if (command.action_id == "mixer.fx-loop-latency.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto frames = *argument<std::int64_t>(command, "frames");
            if (index < 0 || index >= 2 || frames < 0 ||
                frames > static_cast<std::int64_t>(
                             maximum_external_fx_latency_frames))
                error = "external FX latency is out of range";
            else {
                mixer_configuration_
                    .external_fx[static_cast<std::size_t>(index)]
                    .latency_frames = static_cast<std::uint32_t>(frames);
                changed = true;
            }
        } else if (command.action_id == "mixer.channel-select.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            if (index < 0 || static_cast<std::size_t>(index) >=
                                 mixer_configuration_.track_count)
                error = "mixer channel index is unavailable";
            else
                state_.selected_mixer_channel = static_cast<std::size_t>(index);
            if (error.empty()) {
                const auto &selected =
                    mixer_configuration_.tracks[state_.selected_mixer_channel];
                std::string ignored;
                parameters_.set("mixer.selected.gain",
                                static_cast<double>(selected.gain), ignored);
                parameters_.set("mixer.selected.pan",
                                static_cast<double>(selected.pan), ignored);
            }
        } else if (command.action_id == "mixer.master-gain-set.v1") {
            const auto value = number(command.arguments.at("value"));
            if (!std::isfinite(value) || value < 0 || value > 2)
                error = "master gain must be 0..2";
            else {
                mixer_configuration_.master.gain = static_cast<float>(value);
                changed = true;
            }
        } else if (command.action_id == "mixer.master-mute-toggle.v1") {
            mixer_configuration_.master.mute =
                !mixer_configuration_.master.mute;
            changed = true;
        } else if (command.action_id == "mixer.master-output-set.v1") {
            const auto left = *argument<std::int64_t>(command, "left");
            const auto right = *argument<std::int64_t>(command, "right");
            if (left < 0 || right < 0 || left >= 6 || right >= 6 ||
                left == right)
                error = "master outputs must be distinct channels 0..5";
            else {
                mixer_configuration_.master.output_left =
                    static_cast<std::uint8_t>(left);
                mixer_configuration_.master.output_right =
                    static_cast<std::uint8_t>(right);
                changed = true;
            }
        } else if (command.action_id == "mixer.input-set.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto physical =
                *argument<std::int64_t>(command, "physical_channel");
            const auto gain = number(command.arguments.at("gain"));
            const auto pan = number(command.arguments.at("pan"));
            if (index < 0 || index >= 6 || physical < 0 || physical >= 6 ||
                !std::isfinite(gain) || gain < 0 || gain > 2 ||
                !std::isfinite(pan) || pan < -1 || pan > 1)
                error = "invalid physical input configuration";
            else {
                auto &input = mixer_configuration_
                                  .inputs[static_cast<std::size_t>(index)];
                input.physical_channel = static_cast<std::uint8_t>(physical);
                input.gain = static_cast<float>(gain);
                input.pan = static_cast<float>(pan);
                if (const auto *mute = argument<bool>(command, "mute"))
                    input.mute = *mute;
                if (const auto *route =
                        argument<bool>(command, "route_to_master"))
                    input.route_to_master = *route;
                for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                    const auto id =
                        std::string("aux_") + std::to_string(aux + 1);
                    if (const auto found = command.arguments.find(id);
                        found != command.arguments.end()) {
                        const auto send = number(found->second);
                        if (!std::isfinite(send) || send < 0 || send > 1) {
                            error = "input aux sends must be 0..1";
                            break;
                        }
                        input.aux_send[aux] = static_cast<float>(send);
                    }
                }
                mixer_configuration_.input_count =
                    std::max(mixer_configuration_.input_count,
                             static_cast<std::size_t>(index + 1));
                changed = true;
            }
        } else if (command.action_id == "mixer.aux-set.v1") {
            const auto aux = *argument<std::int64_t>(command, "aux");
            const auto gain = number(command.arguments.at("gain"));
            const auto return_gain =
                number(command.arguments.at("return_gain"));
            if (aux < 0 || aux >= 2 || !std::isfinite(gain) || gain < 0 ||
                gain > 2 || !std::isfinite(return_gain) || return_gain < 0 ||
                return_gain > 2)
                error = "invalid auxiliary bus configuration";
            else {
                auto &bus =
                    mixer_configuration_.aux[static_cast<std::size_t>(aux)];
                bus.gain = static_cast<float>(gain);
                bus.return_gain = static_cast<float>(return_gain);
                bus.effect_enabled = *argument<bool>(command, "effect_enabled");
                if (const auto *mute = argument<bool>(command, "mute"))
                    bus.mute = *mute;
                if (const auto *output =
                        argument<std::int64_t>(command, "output_pair")) {
                    if (*output != -1 && *output != 0 && *output != 2 &&
                        *output != 4)
                        error =
                            "aux output pair must be master, 1/2, 3/4, or 5/6";
                    else
                        bus.output_pair = static_cast<std::int8_t>(*output);
                }
                changed = true;
            }
        } else if (state_.selected_mixer_channel >=
                   mixer_configuration_.track_count) {
            error = "no mixer channel is selected";
        } else {
            auto &channel =
                mixer_configuration_.tracks[state_.selected_mixer_channel];
            if (command.action_id == "mixer.gain-set.v1") {
                const auto value = number(command.arguments.at("value"));
                if (!std::isfinite(value) || value < 0 || value > 2)
                    error = "channel gain must be 0..2";
                else {
                    channel.gain = static_cast<float>(value);
                    changed = true;
                }
            } else if (command.action_id == "mixer.pan-set.v1") {
                const auto value = number(command.arguments.at("value"));
                if (!std::isfinite(value) || value < -1 || value > 1)
                    error = "channel pan must be -1..1";
                else {
                    channel.pan = static_cast<float>(value);
                    changed = true;
                }
            } else if (command.action_id == "mixer.mute-toggle.v1") {
                channel.mute = !channel.mute;
                changed = true;
            } else if (command.action_id == "mixer.solo-toggle.v1") {
                channel.solo = !channel.solo;
                changed = true;
            } else if (command.action_id == "mixer.aux-send-set.v1") {
                const auto aux = *argument<std::int64_t>(command, "aux");
                const auto value = number(command.arguments.at("value"));
                if (aux < 0 ||
                    aux >= static_cast<std::int64_t>(mixer_aux_count) ||
                    !std::isfinite(value) || value < 0 || value > 1)
                    error = "aux send requires bus 0..1 and value 0..1";
                else {
                    channel.aux_send[static_cast<std::size_t>(aux)] =
                        static_cast<float>(value);
                    if (const auto *pre = argument<bool>(command, "pre_fader"))
                        channel.aux_pre_fader[static_cast<std::size_t>(aux)] =
                            *pre;
                    changed = true;
                }
            } else if (command.action_id == "mixer.output-pair-set.v1") {
                const auto pair = *argument<std::int64_t>(command, "pair");
                if (pair != -1 && pair != 0 && pair != 2 && pair != 4)
                    error = "output pair must be master, 1/2, 3/4, or 5/6";
                else {
                    channel.output_pair = static_cast<std::int8_t>(pair);
                    changed = true;
                }
            }
        }
        if (!error.empty()) {
            mixer_configuration_ = previous_configuration;
            state_.selected_group = previous_selected_group;
        }
        if (changed && error.empty()) {
            ++mixer_configuration_.revision;
            if (!engine_.publish_mixer(mixer_configuration_, error)) {
                mixer_configuration_ = previous_configuration;
                state_.selected_group = previous_selected_group;
            } else {
                project_.active_routing_scene = 0;
                state_.active_routing_scene = 0;
                state_.project_dirty = true;
                std::string ignored;
                if (command.action_id == "mixer.master-gain-set.v1")
                    parameters_.set(
                        "mixer.master.gain",
                        static_cast<double>(mixer_configuration_.master.gain),
                        ignored);
                else if (state_.selected_mixer_channel <
                         mixer_configuration_.track_count) {
                    const auto &channel =
                        mixer_configuration_
                            .tracks[state_.selected_mixer_channel];
                    if (command.action_id == "mixer.gain-set.v1")
                        parameters_.set("mixer.selected.gain",
                                        static_cast<double>(channel.gain),
                                        ignored);
                    if (command.action_id == "mixer.pan-set.v1")
                        parameters_.set("mixer.selected.pan",
                                        static_cast<double>(channel.pan),
                                        ignored);
                }
            }
        }
        if (changed && error.empty()) {
            const auto capture = [&](std::string destination, float value) {
                arrangement_recording_.capture_automation(
                    std::move(destination), value, state_.transport_tick,
                    automation_thinning_ticks_);
            };
            if (command.action_id == "mixer.master-gain-set.v1")
                capture("mixer.master.gain", mixer_configuration_.master.gain);
            else if (command.action_id == "mixer.group-levels.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                const auto found = find_group(static_cast<MixerBusId>(id));
                capture("mixer.group." + std::to_string(id) + ".gain",
                        found->gain);
                capture("mixer.group." + std::to_string(id) + ".pan",
                        found->pan);
            } else if (command.action_id == "mixer.group-aux-send.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                const auto aux = *argument<std::int64_t>(command, "aux");
                const auto found = find_group(static_cast<MixerBusId>(id));
                capture("mixer.group." + std::to_string(id) + ".aux-" +
                            std::to_string(aux + 1),
                        found->aux_send[static_cast<std::size_t>(aux)]);
            } else if (command.action_id == "mixer.fx-loop-levels.v1") {
                const auto index = *argument<std::int64_t>(command, "index");
                const auto &loop =
                    mixer_configuration_
                        .external_fx[static_cast<std::size_t>(index)];
                const auto prefix =
                    "external-fx." + std::to_string(index + 1) + ".";
                capture(prefix + "send-gain", loop.send_gain);
                capture(prefix + "return-gain", loop.return_gain);
                capture(prefix + "wet", loop.wet);
            } else if (command.action_id == "mixer.aux-set.v1") {
                const auto aux = *argument<std::int64_t>(command, "aux");
                capture("mixer.aux." + std::to_string(aux + 1) + ".return",
                        mixer_configuration_.aux[static_cast<std::size_t>(aux)]
                            .return_gain);
            } else if (state_.selected_mixer_channel <
                       mixer_configuration_.track_count) {
                const auto &channel =
                    mixer_configuration_.tracks[state_.selected_mixer_channel];
                const auto prefix =
                    "mixer.track." + std::to_string(channel.track) + ".";
                if (command.action_id == "mixer.gain-set.v1")
                    capture(prefix + "gain", channel.gain);
                else if (command.action_id == "mixer.pan-set.v1")
                    capture(prefix + "pan", channel.pan);
                else if (command.action_id == "mixer.aux-send-set.v1") {
                    const auto aux = *argument<std::int64_t>(command, "aux");
                    capture(prefix + "aux-" + std::to_string(aux + 1),
                            channel.aux_send[static_cast<std::size_t>(aux)]);
                }
            }
        }
        changed_locked();
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("preset.", 0) == 0) {
        if (command.action_id == "preset.capture.v1") {
            const auto library_path =
                std::filesystem::path(*argument<std::string>(command, "library"));
            const auto preset_id = *argument<std::string>(command, "preset_id");
            const auto name = *argument<std::string>(command, "name");
            const auto type = parse_preset_type(
                *argument<std::string>(command, "type"));
            if (!type)
                return {false, sequence, before.revision,
                        "unknown preset capture type"};
            auto target = before.selected_track;
            if (const auto specified = argument<std::int64_t>(command, "track_id"))
                target = static_cast<TrackId>(*specified);
            const auto overwrite_value = argument<bool>(command, "overwrite");
            const bool overwrite = overwrite_value && *overwrite_value;
            const auto timeline = timeline_.snapshot().timeline;
            const auto track = std::find_if(timeline.tracks.begin(),
                timeline.tracks.end(), [&](const auto &item) {
                    return item.id == target;
                });
            if (track == timeline.tracks.end())
                return {false, sequence, before.revision,
                        "preset source track does not exist"};
            SoundPreset preset;
            preset.metadata.id = preset_id;
            preset.metadata.name = name;
            preset.metadata.type = *type;
            std::string capture_error;
            const bool capture_samples =
                *type == PresetType::sample_instrument ||
                (*type == PresetType::track_sound &&
                 track->instrument_id == sample_instrument_id);
            std::map<std::uint32_t, std::filesystem::path> sample_paths;
            if (capture_samples)
                for (const auto &slot : samples_.states())
                    if (slot.has_sample)
                        sample_paths[slot.id.value] = slot.source_path;
            {
                std::lock_guard lock(mutex_);
                const auto mixer_track = std::find_if(
                    mixer_configuration_.tracks.begin(),
                    mixer_configuration_.tracks.begin() +
                        mixer_configuration_.track_count,
                    [&](const auto &channel) { return channel.track == target; });
                if (mixer_track == mixer_configuration_.tracks.begin() +
                                       mixer_configuration_.track_count)
                    capture_error = "preset source mixer track does not exist";
                else {
                    const auto mixer_index = static_cast<std::size_t>(
                        mixer_track - mixer_configuration_.tracks.begin());
                    preset.track_gain = mixer_track->gain;
                    preset.track_pan = mixer_track->pan;
                    if (*type == PresetType::synth_patch ||
                        (*type == PresetType::track_sound &&
                         track->instrument_id == subtractive_instrument_id)) {
                        if (track->instrument_id != subtractive_instrument_id)
                            capture_error = "source track is not a subtractive synth";
                        else {
                            preset.metadata.required_capabilities.push_back(
                                "native-synth");
                            const auto found = synth_instance_parameters_.find(
                                track->instrument_instance_id);
                            for (const auto &parameter : parameters_.parameters()) {
                                const auto destination =
                                    resolve_parameter_destination(parameter.id);
                                if (!destination ||
                                    destination->target !=
                                        ParameterTarget::subtractive_synth ||
                                    !is_numeric(parameter.current_value))
                                    continue;
                                auto value = number(parameter.current_value);
                                if (found != synth_instance_parameters_.end())
                                    if (const auto instance_value =
                                            found->second.find(parameter.id);
                                        instance_value != found->second.end())
                                        value = instance_value->second;
                                preset.synth_parameters[parameter.id] = value;
                            }
                        }
                    }
                    if (capture_error.empty() &&
                        (*type == PresetType::sample_instrument ||
                         (*type == PresetType::track_sound &&
                          track->instrument_id == sample_instrument_id))) {
                        if (track->instrument_id != sample_instrument_id)
                            capture_error = "source track is not a sample instrument";
                        else {
                            preset.metadata.required_capabilities.push_back(
                                "sample-instrument");
                            for (const auto &[slot, path] :
                                 project_.samples.sample_paths)
                                if (!sample_paths.contains(slot))
                                    sample_paths[slot] = path;
                        }
                    }
                    if (capture_error.empty() &&
                        (*type == PresetType::fx_chain ||
                         *type == PresetType::track_sound)) {
                        PresetEffectChain captured;
                        captured.chain =
                            mixer_configuration_.effects.tracks[mixer_index];
                        for (std::size_t chain_index = 0;
                             chain_index < captured.chain.count; ++chain_index) {
                            const auto id = captured.chain.instances[chain_index];
                            const auto instance = std::find_if(
                                mixer_configuration_.effects.instances.begin(),
                                mixer_configuration_.effects.instances.begin() +
                                    mixer_configuration_.effects.instance_count,
                                [&](const auto &candidate) {
                                    return candidate.id == id;
                                });
                            if (instance == mixer_configuration_.effects.instances.begin() +
                                                mixer_configuration_.effects.instance_count) {
                                capture_error =
                                    "effect chain references a missing instance";
                                break;
                            }
                            captured.instances.push_back(*instance);
                            preset.metadata.processor_types.emplace_back(
                                effect_processor_type_id(instance->type));
                        }
                        if (capture_error.empty()) {
                            preset.effects = std::move(captured);
                            preset.metadata.required_capabilities.push_back(
                                "native-fx");
                        }
                    }
                    if (capture_error.empty()) {
                        std::array<ModulatorInstanceId,
                                   ModulationConfiguration::maximum_modulators>
                            local_ids{};
                        std::size_t local_count{};
                        for (std::size_t i = 0;
                             i < modulation_configuration_.modulator_count; ++i) {
                            const auto &source = modulation_configuration_.modulators[i];
                            if (source.owner_kind != ModulatorOwnerKind::track ||
                                source.owner_id != target)
                                continue;
                            preset.modulation.modulators
                                [preset.modulation.modulator_count++] = source;
                            local_ids[local_count++] = source.id;
                            if (const auto descriptor = find_modulator_type(source.type))
                                preset.metadata.modulator_types.emplace_back(
                                    descriptor->id);
                        }
                        const auto local = [&](ModulatorInstanceId id) {
                            return id && std::find(local_ids.begin(),
                                local_ids.begin() + local_count, id) !=
                                local_ids.begin() + local_count;
                        };
                        for (std::size_t i = 0;
                             i < modulation_configuration_.route_count; ++i) {
                            const auto &route = modulation_configuration_.routes[i];
                            const auto owned_scope =
                                (route.scope == ModulationScope::track ||
                                 route.scope == ModulationScope::voice) &&
                                route.track_id == target;
                            if (!owned_scope && !local(route.source_instance) &&
                                !local(route.destination_modulator))
                                continue;
                            if ((route.source_instance &&
                                 !local(route.source_instance)) ||
                                (route.destination_modulator &&
                                 !local(route.destination_modulator)))
                                continue;
                            if (preset.modulation.route_count ==
                                preset.modulation.routes.size()) {
                                capture_error = "local modulation capacity exceeded";
                                break;
                            }
                            preset.modulation.routes
                                [preset.modulation.route_count++] = route;
                        }
                        if (preset.modulation.modulator_count)
                            preset.metadata.required_capabilities.push_back(
                                "modulation-graph");
                    }
                }
            }
            if (!capture_error.empty())
                return {false, sequence, before.revision, capture_error};
            if (capture_samples)
                preset.samples =
                    slices_.project_snapshot(std::move(sample_paths));
            try {
                PresetLibrary library(library_path);
                if (library.contains(preset_id) && !overwrite)
                    return {false, sequence, before.revision,
                            "preset already exists; explicit overwrite required"};
                library.save(preset);
                return {true, sequence, before.revision, {}};
            } catch (const std::exception &exception) {
                return {false, sequence, before.revision, exception.what()};
            }
        }
        if (command.action_id == "preset.rename.v1" ||
            command.action_id == "preset.duplicate.v1" ||
            command.action_id == "preset.delete.v1") {
            try {
                PresetLibrary library(
                    *argument<std::string>(command, "library"));
                const auto source = *argument<std::string>(command, "preset_id");
                if (command.action_id == "preset.delete.v1")
                    library.erase(source);
                else {
                    const auto target = *argument<std::string>(command, "new_id");
                    if (library.contains(target))
                        return {false, sequence, before.revision,
                                "preset destination already exists"};
                    library.duplicate(source, target,
                        *argument<std::string>(command, "new_name"));
                    if (command.action_id == "preset.rename.v1")
                        library.erase(source);
                }
                return {true, sequence, before.revision, {}};
            } catch (const std::exception &exception) {
                return {false, sequence, before.revision, exception.what()};
            }
        }
        const auto prepare_plan = [&](SoundPreset preset,
                                      PresetImportMode mode, TrackId target,
                                      bool release_voices,
                                      PendingPresetApplication &pending,
                                      std::string &prepare_error) {
            const auto timeline = timeline_.snapshot().timeline;
            const auto track = std::find_if(
                timeline.tracks.begin(), timeline.tracks.end(),
                [&](const auto &candidate) { return candidate.id == target; });
            if (track == timeline.tracks.end()) {
                prepare_error = "preset target track does not exist";
                return false;
            }
            std::size_t mixer_track = mixer_configuration_.track_count;
            for (std::size_t i = 0; i < mixer_configuration_.track_count; ++i)
                if (mixer_configuration_.tracks[i].track == target) {
                    mixer_track = i;
                    break;
                }
            if (mixer_track == mixer_configuration_.track_count) {
                prepare_error = "preset target has no mixer channel";
                return false;
            }
            if (mode == PresetImportMode::insert_fx_chain && !preset.effects) {
                prepare_error = "preset has no effect chain to insert";
                return false;
            }
            if (mode == PresetImportMode::merge_modulation &&
                preset.modulation.modulator_count == 0 &&
                preset.modulation.route_count == 0) {
                prepare_error = "preset has no modulation to merge";
                return false;
            }
            for (std::size_t i = 0; i < preset.modulation.modulator_count; ++i) {
                auto &instance = preset.modulation.modulators[i];
                if (instance.owner_kind == ModulatorOwnerKind::track ||
                    instance.owner_kind ==
                        ModulatorOwnerKind::voice_definition)
                    instance.owner_id = target;
            }
            for (std::size_t i = 0; i < preset.modulation.route_count; ++i) {
                auto &route = preset.modulation.routes[i];
                if (route.scope == ModulationScope::track ||
                    route.scope == ModulationScope::voice)
                    route.track_id = target;
                if (route.destination_owner_explicit &&
                    (route.destination_owner_kind ==
                         ModulatorOwnerKind::track ||
                     route.destination_owner_kind ==
                         ModulatorOwnerKind::voice_definition))
                    route.destination_owner_id = target;
                if (route.destination.target ==
                    ParameterTarget::subtractive_synth)
                    route.destination.instance = track->instrument_instance_id;
                if (route.destination_kind !=
                    ModulationDestinationKind::parameter)
                    continue;
                bool compatible{};
                if (route.destination.target ==
                    ParameterTarget::native_effect) {
                    if (preset.effects)
                        for (const auto &effect : preset.effects->instances)
                            if (effect.id == route.destination.instance)
                                for (const auto &descriptor :
                                     effect_parameter_descriptors(effect.type))
                                    compatible |=
                                        descriptor.id ==
                                            route.destination.parameter &&
                                        descriptor.modulatable;
                } else
                    for (const auto &descriptor : parameters_.parameters()) {
                        const auto destination =
                            resolve_parameter_destination(descriptor.id);
                        compatible |= destination && descriptor.modulatable &&
                                      destination->target ==
                                          route.destination.target &&
                                      destination->parameter ==
                                          route.destination.parameter;
                    }
                if (!compatible) {
                    prepare_error =
                        "preset modulation destination is unavailable";
                    return false;
                }
            }
            auto base_effects = mixer_configuration_.effects;
            auto base_modulation = modulation_configuration_;
            if (mode == PresetImportMode::replace_sound) {
                std::array<EffectInstanceId, maximum_effects_per_chain>
                    replaced_effects{};
                const auto replaced_count =
                    base_effects.tracks[mixer_track].count;
                std::copy_n(base_effects.tracks[mixer_track].instances.begin(),
                            replaced_count, replaced_effects.begin());
                const auto used_outside_target = [&](EffectInstanceId id) {
                    const auto contains = [&](const auto &chain) {
                        return std::find(chain.instances.begin(),
                                         chain.instances.begin() + chain.count,
                                         id) !=
                               chain.instances.begin() + chain.count;
                    };
                    for (std::size_t i = 0; i < base_effects.tracks.size(); ++i)
                        if (i != mixer_track &&
                            contains(base_effects.tracks[i]))
                            return true;
                    for (const auto &chain : base_effects.aux)
                        if (contains(chain)) return true;
                    for (const auto &chain : base_effects.groups)
                        if (contains(chain)) return true;
                    return contains(base_effects.master);
                };
                std::size_t write{};
                for (std::size_t i = 0; i < base_effects.instance_count; ++i) {
                    const auto id = base_effects.instances[i].id;
                    const auto replaced =
                        std::find(replaced_effects.begin(),
                                  replaced_effects.begin() + replaced_count,
                                  id) != replaced_effects.begin() + replaced_count;
                    if (!replaced || used_outside_target(id))
                        base_effects.instances[write++] =
                            base_effects.instances[i];
                }
                base_effects.instance_count = write;
                base_effects.tracks[mixer_track] = {};

                std::array<ModulatorInstanceId,
                           ModulationConfiguration::maximum_modulators>
                    removed{};
                std::size_t removed_count{};
                write = 0;
                for (std::size_t i = 0;
                     i < base_modulation.modulator_count; ++i) {
                    const auto &instance = base_modulation.modulators[i];
                    const auto local =
                        (instance.owner_kind == ModulatorOwnerKind::track ||
                         instance.owner_kind ==
                             ModulatorOwnerKind::voice_definition) &&
                        instance.owner_id == target;
                    if (local)
                        removed[removed_count++] = instance.id;
                    else
                        base_modulation.modulators[write++] = instance;
                }
                base_modulation.modulator_count = write;
                write = 0;
                for (std::size_t i = 0; i < base_modulation.route_count; ++i) {
                    const auto &route = base_modulation.routes[i];
                    const auto removed_id = [&](ModulatorInstanceId id) {
                        return id &&
                               std::find(removed.begin(),
                                         removed.begin() + removed_count, id) !=
                                   removed.begin() + removed_count;
                    };
                    const auto local = removed_id(route.source_instance) ||
                                       removed_id(route.destination_modulator) ||
                                       ((route.scope == ModulationScope::track ||
                                         route.scope == ModulationScope::voice) &&
                                        route.track_id == target);
                    if (!local)
                        base_modulation.routes[write++] = route;
                }
                base_modulation.route_count = write;
            }
            PresetImportResult imported;
            if (!prepare_preset_import(preset, base_effects,
                                       base_modulation, imported,
                                       prepare_error))
                return false;
            auto next_mixer = mixer_configuration_;
            auto next_modulation = modulation_configuration_;
            if (mode != PresetImportMode::preview) {
                if (mode != PresetImportMode::merge_modulation &&
                    preset.effects) {
                    next_mixer.effects = imported.effects;
                    if (mode == PresetImportMode::insert_fx_chain) {
                        auto &chain = next_mixer.effects.tracks[mixer_track];
                        if (chain.count + imported.chain.count >
                            chain.instances.size()) {
                            prepare_error = "target effect chain capacity is exhausted";
                            return false;
                        }
                        for (std::size_t i = 0; i < imported.chain.count; ++i)
                            chain.instances[chain.count++] =
                                imported.chain.instances[i];
                    } else
                        next_mixer.effects.tracks[mixer_track] = imported.chain;
                }
                if (mode != PresetImportMode::insert_fx_chain)
                    next_modulation = imported.modulation;
                if (mode == PresetImportMode::replace_sound) {
                    next_mixer.tracks[mixer_track].gain = preset.track_gain;
                    next_mixer.tracks[mixer_track].pan = preset.track_pan;
                }
            }
            next_mixer.revision = mixer_configuration_.revision + 1;
            PendingPresetApplication built;
            built.preset = preset;
            built.mode = mode;
            built.target_track = target;
            built.target_instrument = track->instrument_instance_id;
            built.activation.mixer = next_mixer;
            built.activation.modulation = next_modulation;
            built.activation.release_existing_voices = release_voices;
            for (const auto &[id, value] : preset.synth_parameters) {
                const auto descriptor = parameters_.get(id);
                const auto destination = resolve_parameter_destination(id);
                if (!descriptor || !destination ||
                    destination->target !=
                        ParameterTarget::subtractive_synth ||
                    value < number(descriptor->minimum) ||
                    value > number(descriptor->maximum) ||
                    built.activation.synth_parameter_count ==
                        built.activation.synth_parameters.size()) {
                    prepare_error = "incompatible synth preset parameter: " + id;
                    return false;
                }
                built.activation.synth_parameters
                    [built.activation.synth_parameter_count++] = {
                        track->instrument_instance_id,
                        static_cast<SynthParameter>(destination->parameter),
                        static_cast<float>(value)};
                built.synth_parameters[id] = value;
            }
            if (preset.samples) {
                if (preset.samples->sample_paths.size() >
                    built.activation.samples.size()) {
                    prepare_error = "preset sample capacity exceeded";
                    return false;
                }
                for (const auto &[slot, path] : preset.samples->sample_paths) {
                    try {
                        auto sample = std::make_shared<const Sample>(
                            load_wav(path, engine_.sample_rate()));
                        const auto index = built.activation.sample_count++;
                        built.activation.samples[index] =
                            {SampleSlotId{slot}, sample};
                        built.sample_paths[index] = path;
                    } catch (const std::exception &exception) {
                        prepare_error = "cannot prepare sample " + path.string() +
                                        ": " + exception.what();
                        return false;
                    }
                }
                built.activation.replace_pad_mappings = true;
                for (std::size_t pad = 0; pad < 16; ++pad) {
                    auto &mapping = built.activation.pads[pad];
                    mapping.playback = preset.samples->pad_settings[pad];
                    if (preset.samples->region_pads[pad]) {
                        const auto found = std::find_if(
                            preset.samples->regions.begin(),
                            preset.samples->regions.end(),
                            [&](const auto &region) {
                                return region.id ==
                                       *preset.samples->region_pads[pad];
                            });
                        if (found == preset.samples->regions.end()) {
                            prepare_error =
                                "pad references a missing preset region";
                            return false;
                        }
                        const auto sample = std::find_if(
                            built.activation.samples.begin(),
                            built.activation.samples.begin() +
                                built.activation.sample_count,
                            [&](const auto &candidate) {
                                return candidate.slot == found->slot;
                            });
                        if (sample == built.activation.samples.begin() +
                                          built.activation.sample_count ||
                            found->start_frame >= found->end_frame ||
                            found->end_frame > sample->sample->frames()) {
                            prepare_error =
                                "preset region is outside its sample";
                            return false;
                        }
                        mapping.slot = found->slot;
                        mapping.start_frame = found->start_frame;
                        mapping.end_frame = found->end_frame;
                        mapping.gain = found->gain;
                        mapping.playback.reverse =
                            found->direction == RegionDirection::reverse;
                        mapping.region = {found->fade_in_frames,
                                          found->fade_out_frames,
                                          found->loop_start_frame,
                                          found->loop_end_frame,
                                          found->loop_crossfade_frames,
                                          found->fade_curve,
                                          found->loop_enabled};
                        mapping.assigned = true;
                        continue;
                    }
                    if (!preset.samples->pads[pad]) continue;
                    const auto found = std::find_if(
                        preset.samples->slices.begin(),
                        preset.samples->slices.end(), [&](const auto &slice) {
                            return slice.id == *preset.samples->pads[pad];
                        });
                    if (found == preset.samples->slices.end()) {
                        prepare_error = "pad references a missing preset slice";
                        return false;
                    }
                    const auto sample = std::find_if(
                        built.activation.samples.begin(),
                        built.activation.samples.begin() +
                            built.activation.sample_count,
                        [&](const auto &candidate) {
                            return candidate.slot == found->slot;
                        });
                    if (sample == built.activation.samples.begin() +
                                      built.activation.sample_count ||
                        found->start_frame >= found->end_frame ||
                        found->end_frame > sample->sample->frames()) {
                        prepare_error = "preset slice is outside its sample";
                        return false;
                    }
                    mapping.slot = found->slot;
                    mapping.start_frame = found->start_frame;
                    mapping.end_frame = found->end_frame;
                    mapping.gain = found->gain;
                    mapping.assigned = true;
                }
            }
            pending = std::move(built);
            prepare_error.clear();
            return true;
        };
        try {
            if (command.action_id == "preset.query.v1")
                return {true, sequence, before.revision, {}};
            if (command.action_id == "preset.cancel.v1") {
                std::lock_guard lock(mutex_);
                if (pending_preset_ && pending_preset_->published) {
                    if (!engine_.cancel_preset_activation(
                            pending_preset_->activation_sequence))
                        return {false, sequence, state_.revision,
                                "preset activation is already active"};
                    state_.preset_application.status = "cancelling";
                    ++state_.preset_application.revision;
                    changed_locked();
                    return {true, sequence, state_.revision, {}};
                }
                pending_preset_.reset();
                state_.preset_application.status = "cancelled";
                state_.preset_application.progress = 0;
                ++state_.preset_application.revision;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (command.action_id == "preset.export-portable.v1") {
                PresetLibrary library(
                    *argument<std::string>(command, "library"));
                const auto result = library.export_self_contained(
                    *argument<std::string>(command, "preset_id"),
                    *argument<std::string>(command, "destination"));
                if (!result.missing_assets.empty())
                    return {false, sequence, before.revision,
                            "portable export has unresolved assets"};
                return {true, sequence, before.revision, {}};
            }
            if (command.action_id == "preset.repair-asset.v1" ||
                command.action_id == "preset.repair-prefix.v1") {
                PresetLibrary library(
                    *argument<std::string>(command, "library"));
                const auto preset_id =
                    *argument<std::string>(command, "preset_id");
                PresetValidation repaired;
                if (command.action_id == "preset.repair-asset.v1") {
                    const PresetAssetRelink mapping{
                        std::filesystem::path(
                            *argument<std::string>(command, "missing")),
                        std::filesystem::path(
                            *argument<std::string>(command, "replacement"))};
                    repaired = library.repair_assets(
                        preset_id, {mapping}, {}, engine_.sample_rate());
                } else {
                    const PresetDirectoryRelink mapping{
                        std::filesystem::path(*argument<std::string>(
                            command, "missing_prefix")),
                        std::filesystem::path(*argument<std::string>(
                            command, "replacement_prefix"))};
                    repaired = library.repair_assets(
                        preset_id, {}, {mapping}, engine_.sample_rate());
                }
                return {repaired.valid, sequence, before.revision,
                        repaired.errors.empty() ? "" : repaired.errors.front()};
            }
            if (command.action_id == "preset.relink-asset.v1" ||
                command.action_id == "preset.relink-prefix.v1") {
                std::lock_guard lock(mutex_);
                if (!pending_preset_ || pending_preset_->published)
                    return {false, sequence, state_.revision,
                            "no editable prepared preset"};
                PresetValidation relinked;
                if (command.action_id == "preset.relink-asset.v1") {
                    const std::vector<PresetAssetRelink> mappings{{
                        std::filesystem::path(*argument<std::string>(
                            command, "missing")),
                        std::filesystem::path(*argument<std::string>(
                            command, "replacement"))}};
                    relinked = relink_preset_assets(
                        pending_preset_->preset, mappings, {},
                        engine_.sample_rate());
                } else {
                    const std::vector<PresetDirectoryRelink> mappings{{
                        std::filesystem::path(*argument<std::string>(
                            command, "missing_prefix")),
                        std::filesystem::path(*argument<std::string>(
                            command, "replacement_prefix"))}};
                    relinked = relink_preset_assets(
                        pending_preset_->preset, {}, mappings,
                        engine_.sample_rate());
                }
                if (relinked.valid) {
                    PendingPresetApplication rebuilt;
                    if (!prepare_plan(pending_preset_->preset,
                                      pending_preset_->mode,
                                      pending_preset_->target_track, false,
                                      rebuilt, error)) {
                        relinked.valid = false;
                        relinked.errors.push_back(error);
                    } else
                        *pending_preset_ = std::move(rebuilt);
                }
                state_.preset_application.missing_assets.clear();
                for (const auto &path : relinked.missing_assets)
                    state_.preset_application.missing_assets.emplace_back(path);
                state_.preset_application.error =
                    relinked.errors.empty() ? "" : relinked.errors.front();
                state_.preset_application.status =
                    relinked.valid ? "assets-resolved" : "awaiting-assets";
                state_.preset_application.progress = relinked.valid ? 0.35F : 0.2F;
                ++state_.preset_application.revision;
                changed_locked();
                return {relinked.valid, sequence, state_.revision,
                        state_.preset_application.error};
            }
            if (command.action_id == "preset.apply.v1") {
                std::lock_guard lock(mutex_);
                if (!pending_preset_ || pending_preset_->published)
                    return {false, sequence, state_.revision,
                            "no prepared preset application"};
                auto &pending = *pending_preset_;
                if (pending.mode == PresetImportMode::preview)
                    return {false, sequence, state_.revision,
                            "preview mode cannot be activated"};
                if (pending.activation.mixer.revision == 0) {
                    PendingPresetApplication rebuilt;
                    if (!prepare_plan(pending.preset, pending.mode,
                                      pending.target_track, false, rebuilt,
                                      error))
                        return {false, sequence, state_.revision, error};
                    pending = std::move(rebuilt);
                }
                if (!engine_.publish_preset_activation(
                        pending.activation, pending.activation_sequence,
                        pending.sample_publications, error)) {
                    state_.preset_application.status = "failed";
                    state_.preset_application.error = error;
                    ++state_.preset_application.revision;
                    changed_locked();
                    return {false, sequence, state_.revision, error};
                }
                pending.history_before = capture_authoring_state(true);
                pending.published = true;
                state_.preset_application.status = "activating";
                state_.preset_application.progress = 0.8F;
                state_.preset_application.activation_sequence =
                    pending.activation_sequence;
                ++state_.preset_application.revision;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            const auto library_path =
                *argument<std::string>(command, "library");
            const auto preset_id =
                *argument<std::string>(command, "preset_id");
            const auto mode = parse_preset_import_mode(
                *argument<std::string>(command, "mode"));
            if (!mode)
                return {false, sequence, before.revision,
                        "unknown preset application mode"};
            auto target = before.selected_track;
            if (const auto specified = argument<std::int64_t>(command, "track_id"))
                target = static_cast<TrackId>(*specified);
            const auto release_argument =
                argument<bool>(command, "release_voices");
            const auto release = release_argument && *release_argument;
            PresetLibrary library(library_path);
            auto preset = library.load(preset_id, false);
            const auto asset_validation = validate_preset(preset, true);
            PendingPresetApplication prepared;
            if (!asset_validation.missing_assets.empty()) {
                prepared.preset = std::move(preset);
                prepared.mode = *mode;
                prepared.target_track = target;
                if (command.action_id == "preset.validate.v1")
                    return {false, sequence, before.revision,
                            "preset has unresolved sample assets"};
                std::lock_guard lock(mutex_);
                pending_preset_ = std::move(prepared);
                state_.preset_application = {};
                state_.preset_application.status = "awaiting-assets";
                state_.preset_application.preset_id = preset_id;
                state_.preset_application.mode = preset_import_mode_name(*mode);
                state_.preset_application.target_track = target;
                for (const auto &path : asset_validation.missing_assets)
                    state_.preset_application.missing_assets.emplace_back(path);
                state_.preset_application.progress = 0.2F;
                ++state_.preset_application.revision;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (!prepare_plan(std::move(preset), *mode, target, release,
                              prepared, error))
                return {false, sequence, before.revision, error};
            if (command.action_id == "preset.validate.v1")
                return {true, sequence, before.revision, {}};
            std::lock_guard lock(mutex_);
            pending_preset_ = std::move(prepared);
            state_.preset_application = {};
            state_.preset_application.status = "prepared";
            state_.preset_application.preset_id = preset_id;
            state_.preset_application.mode = preset_import_mode_name(*mode);
            state_.preset_application.target_track = target;
            state_.preset_application.progress = 0.6F;
            ++state_.preset_application.revision;
            changed_locked();
            return {true, sequence, state_.revision, {}};
        } catch (const std::exception &exception) {
            std::lock_guard lock(mutex_);
            state_.preset_application.status = "failed";
            state_.preset_application.error = exception.what();
            ++state_.preset_application.revision;
            changed_locked();
            return {false, sequence, state_.revision,
                    state_.preset_application.error};
        }
    }
    if (command.action_id.rfind("project.", 0) == 0) {
        try {
            if (pending_project_recovery_ &&
                command.action_id != "project.query.v1")
                return {false, sequence, before.revision,
                        "project dependency recovery is awaiting audio activation"};
            if ((command.action_id == "project.new.v1" ||
                 command.action_id == "project.open.v1" ||
                 command.action_id == "project.close.v1") &&
                before.project_dirty) {
                const auto discard = argument<bool>(command, "discard");
                if (!discard || !*discard)
                    return {false, sequence, before.revision,
                            "project has unsaved changes; save or explicitly "
                            "discard"};
            }
            if (command.action_id == "project.query.v1")
                return {true, sequence, before.revision, {}};
            if (command.action_id == "project.rename.v1") {
                const auto &name = *argument<std::string>(command, "name");
                if (name.empty())
                    return {false, sequence, before.revision,
                            "project name must not be empty"};
                std::lock_guard lock(mutex_);
                project_.name = name;
                state_.project_name = name;
                state_.project_dirty = true;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (command.action_id == "project.save.v1" ||
                command.action_id == "project.save-as.v1") {
                std::filesystem::path path = before.project_path;
                if (command.action_id == "project.save-as.v1")
                    path = *argument<std::string>(command, "path");
                if (path.empty())
                    return {false, sequence, before.revision,
                            "project path is not set; use Save As"};
                Project saving;
                saving.name = before.project_name;
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    saving.name = std::get<std::string>(found->second);
                saving.timeline = timeline_.snapshot().timeline;
                saving.mixer = mixer_configuration_;
                saving.routing_scenes = project_.routing_scenes;
                saving.active_routing_scene = project_.active_routing_scene;
                saving.modulation = modulation_configuration_;
                saving.performance_macros = performance_macros_.project();
                saving.midi = project_.midi;
                for (const auto &slot : samples_.states())
                    if (slot.has_sample && slot.source_path.empty()) {
                        const auto recording = recording_.status();
                        if (recording.state != RecordingState::audio_active ||
                            recording.configuration.destination != slot.id)
                            throw std::runtime_error(
                                "project contains an unsaved in-memory sample");
                        const auto asset =
                            path / "recordings" /
                            ("slot-" + std::to_string(slot.id.value) + ".wav");
                        std::filesystem::create_directories(
                            asset.parent_path());
                        if (!recording_.save_wav(
                                asset, RecordingWavFormat::float32, error) ||
                            !samples_.set_source_path(slot.id, asset))
                            throw std::runtime_error(
                                error.empty() ? "cannot retain recorded asset"
                                              : error);
                    }
                std::map<std::uint32_t, std::filesystem::path> paths;
                for (const auto &slot : samples_.states())
                    if (slot.has_sample)
                        paths[slot.id.value] = slot.source_path;
                for (const auto &[slot, sample] : project_.samples.sample_paths)
                    if (!paths.contains(slot))
                        paths[slot] = sample;
                saving.samples = slices_.project_snapshot(std::move(paths));
                // SliceService intentionally exposes only audio-active samples.
                // Retain metadata that belongs to unresolved assets so a save
                // never erases the user's intended edits or pad assignments.
                std::set<SliceId> retained_missing_slices;
                for (const auto &slice : project_.samples.slices) {
                    const auto found =
                        project_.samples.sample_paths.find(slice.slot.value);
                    if (found != project_.samples.sample_paths.end() &&
                        std::find(project_.missing_assets.begin(),
                                  project_.missing_assets.end(),
                                  found->second) !=
                            project_.missing_assets.end()) {
                        saving.samples.slices.push_back(slice);
                        retained_missing_slices.insert(slice.id);
                    }
                }
                for (std::size_t pad = 0; pad < project_.samples.pads.size();
                     ++pad)
                    if (project_.samples.pads[pad] &&
                        retained_missing_slices.contains(
                            *project_.samples.pads[pad]))
                        saving.samples.pads[pad] = project_.samples.pads[pad];
                saving.missing_assets = project_.missing_assets;
                for (const auto &parameter : parameters_.parameters())
                    if (is_numeric(parameter.current_value) &&
                        (parameter.id.rfind("synth.", 0) == 0 ||
                         parameter.id.rfind("effect.", 0) == 0))
                        saving.parameters[parameter.id] =
                            number(parameter.current_value);
                for (const auto &[instance, values] :
                     synth_instance_parameters_)
                    for (const auto &[id, value] : values)
                        saving.parameters["instance." +
                                          std::to_string(instance) + "." + id] =
                            value;
                {
                    std::lock_guard lock(mutex_);
                    state_.project_saving = true;
                    changed_locked();
                }
                save_project(saving, path);
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(saving);
                    state_.project_name = project_.name;
                    state_.project_path = path;
                    state_.project_dirty = false;
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.project_saving = false;
                    state_.project_error.clear();
                    state_.missing_assets = project_.missing_assets;
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
            if (command.action_id == "project.open.v1") {
                live_loops_.clear();
                const std::filesystem::path path =
                    *argument<std::string>(command, "path");
                {
                    std::lock_guard lock(mutex_);
                    state_.project_loading = true;
                    changed_locked();
                }
                auto loaded = load_project(path);
                for (std::size_t route = 0;
                     route < loaded.modulation.route_count; ++route) {
                    const auto &candidate = loaded.modulation.routes[route];
                    bool supported{};
                    for (const auto &parameter : parameters_.parameters()) {
                        const auto destination =
                            resolve_parameter_destination(parameter.id);
                        supported |= destination && parameter.modulatable &&
                                     destination->target ==
                                         candidate.destination.target &&
                                     destination->parameter ==
                                         candidate.destination.parameter;
                    }
                    if (!supported)
                        throw std::runtime_error(
                            "project contains an unavailable modulation "
                            "destination");
                }
                for (const auto &[id, value] : loaded.parameters) {
                    if (id.rfind("instance.", 0) == 0)
                        continue;
                    const auto descriptor = parameters_.get(id);
                    if (!descriptor || value < number(descriptor->minimum) ||
                        value > number(descriptor->maximum))
                        throw std::runtime_error("invalid project parameter: " +
                                                 id);
                }
                recording_.cancel();
                engine_.stop();
                for (const auto &[slot, sample] : loaded.samples.sample_paths)
                    if (std::filesystem::exists(sample) &&
                        !samples_.load(SampleSlotId{slot}, sample))
                        throw std::runtime_error("sample load queue is full");
                samples_.wait_idle();
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline) {
                    bool pending{};
                    for (const auto &[slot, sample] :
                         loaded.samples.sample_paths)
                        if (std::filesystem::exists(sample)) {
                            const auto status =
                                samples_.status(SampleSlotId{slot});
                            pending |=
                                status.state != SampleSlotLoadState::loaded &&
                                status.state != SampleSlotLoadState::error;
                        }
                    if (!pending)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                for (const auto &[slot, sample] : loaded.samples.sample_paths)
                    if (std::filesystem::exists(sample)) {
                        const auto status = samples_.status(SampleSlotId{slot});
                        if (status.state == SampleSlotLoadState::error)
                            throw std::runtime_error(status.error);
                        if (status.state != SampleSlotLoadState::loaded)
                            throw std::runtime_error(
                                "sample activation timed out");
                    }
                auto edit = timeline_.edit();
                edit.timeline() = loaded.timeline;
                edit.set_generation_seed(loaded.timeline.generation_seed);
                if (!timeline_.commit(std::move(edit),
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))
                    throw std::runtime_error(error);
                parameters_.set(
                    "transport.tempo",
                    60'000'000.0 /
                        loaded.timeline.transport.microseconds_per_quarter,
                    error);
                if (!error.empty())
                    throw std::runtime_error(error);
                if (!slices_.restore_project(loaded.samples, error))
                    throw std::runtime_error(error);
                auto loaded_mixer = loaded.mixer;
                if (loaded_mixer.track_count == 0) {
                    loaded_mixer.track_count = std::min(
                        loaded.timeline.tracks.size(), maximum_mixer_tracks);
                    for (std::size_t i = 0; i < loaded_mixer.track_count; ++i)
                        loaded_mixer.tracks[i].track =
                            loaded.timeline.tracks[i].id;
                }
                for (std::size_t i = 0; i < mixer_external_fx_count; ++i)
                    if (!loaded_mixer.external_fx[i].id) {
                        auto &loop = loaded_mixer.external_fx[i];
                        loop.id = static_cast<ExternalFxLoopId>(i + 1);
                        const auto name =
                            "External FX " + std::to_string(i + 1);
                        std::copy_n(name.begin(),
                                    std::min(name.size(), loop.name.size() - 1),
                                    loop.name.begin());
                        loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
                        loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
                        loop.return_left = static_cast<std::uint8_t>(i * 2);
                        loop.return_right =
                            static_cast<std::uint8_t>(1 + i * 2);
                    }
                loaded_mixer.revision = mixer_configuration_.revision + 1;
                if (!engine_.publish_mixer(loaded_mixer, error))
                    throw std::runtime_error(error);
                mixer_configuration_ = loaded_mixer;
                if (!performance_macros_.replace(
                        loaded.performance_macros, loaded.timeline,
                        loaded_mixer, error) ||
                    !publish_modulation_with_macros(loaded.modulation, error))
                    throw std::runtime_error(error);
                modulation_configuration_ = loaded.modulation;
                synth_instance_parameters_.clear();
                next_modulation_route_id_ = 1;
                next_modulator_instance_id_ = 1;
                for (std::size_t route = 0;
                     route < modulation_configuration_.route_count; ++route)
                    next_modulation_route_id_ = std::max(
                        next_modulation_route_id_,
                        modulation_configuration_.routes[route].id + 1);
                for (std::size_t instance = 0;
                     instance < modulation_configuration_.modulator_count;
                     ++instance)
                    next_modulator_instance_id_ = std::max(
                        next_modulator_instance_id_,
                        modulation_configuration_.modulators[instance].id + 1);
                for (const auto &[id, value] : loaded.parameters) {
                    if (id.rfind("instance.", 0) == 0) {
                        const auto separator = id.find('.', 9);
                        if (separator == std::string::npos)
                            throw std::runtime_error(
                                "invalid instance parameter key: " + id);
                        const auto instance = static_cast<InstrumentInstanceId>(
                            std::stoul(id.substr(9, separator - 9)));
                        const auto parameter_id = id.substr(separator + 1);
                        const auto descriptor = parameters_.get(parameter_id);
                        const auto destination =
                            resolve_parameter_destination(parameter_id);
                        if (!descriptor || !destination ||
                            destination->target !=
                                ParameterTarget::subtractive_synth ||
                            instance == 0 ||
                            instance > maximum_instrument_instances ||
                            value < number(descriptor->minimum) ||
                            value > number(descriptor->maximum) ||
                            !engine_.set_synth_parameter(
                                static_cast<SynthParameter>(
                                    destination->parameter),
                                static_cast<float>(value), instance))
                            throw std::runtime_error(
                                "cannot restore instance parameter: " + id);
                        synth_instance_parameters_[instance][parameter_id] =
                            value;
                        continue;
                    }
                    const auto previous = parameters_.get(id);
                    if (!parameters_.set(id, value, error) ||
                        !publish_dsp_parameter(engine_, parameters_, id)) {
                        if (previous)
                            parameters_.set(id, previous->current_value, error);
                        throw std::runtime_error("cannot restore parameter: " +
                                                 id);
                    }
                }
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(loaded);
                    std::string midi_error;
                    if (!midi_router_.configure({project_.midi.routes.data(),
                                                 project_.midi.route_count},
                                                midi_error))
                        state_.project_error = std::move(midi_error);
                    MidiClockOutputConfiguration midi_clock;
                    midi_clock.enabled = project_.midi.clock_source ==
                                             MidiClockSource::internal &&
                                         project_.midi.clock_output_count != 0;
                    midi_clock.forward_external =
                        project_.midi.clock_source ==
                            MidiClockSource::external &&
                        project_.midi.forward_external_clock;
                    midi_clock.port_count = project_.midi.clock_output_count;
                    std::copy_n(project_.midi.clock_outputs.begin(),
                                midi_clock.port_count,
                                midi_clock.ports.begin());
                    if (!engine_.publish_midi_clock_output(midi_clock) &&
                        state_.project_error.empty())
                        state_.project_error =
                            "MIDI clock publication queue is full";
                    MidiRoutingConfiguration midi_routing;
                    midi_routing.route_count = project_.midi.route_count;
                    midi_routing.accept_external_clock =
                        project_.midi.clock_source == MidiClockSource::external;
                    std::copy_n(project_.midi.routes.begin(),
                                midi_routing.route_count,
                                midi_routing.routes.begin());
                    if (!engine_.publish_midi_routing(midi_routing) &&
                        state_.project_error.empty())
                        state_.project_error =
                            "MIDI routing publication queue is full";
                    state_.midi = project_.midi;
                    state_.project_name = project_.name;
                    state_.project_path = path;
                    state_.project_dirty = false;
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.routing_scenes = project_.routing_scenes;
                    state_.active_routing_scene = project_.active_routing_scene;
                    state_.project_loading = false;
                    state_.project_error.clear();
                    state_.missing_assets = project_.missing_assets;
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
            if (command.action_id == "project.relink-sample.v1") {
                const auto missing = std::filesystem::path(
                    *argument<std::string>(command, "missing"));
                const auto replacement = std::filesystem::path(
                    *argument<std::string>(command, "replacement"));
                auto relinked = project_;
                if (!relink_project_sample(relinked, missing, replacement,
                                           error))
                    return {false, sequence, before.revision, std::move(error)};
                const auto found = std::find_if(
                    relinked.samples.sample_paths.begin(),
                    relinked.samples.sample_paths.end(), [&](const auto &item) {
                        return item.second == replacement;
                    });
                if (found == relinked.samples.sample_paths.end() ||
                    !samples_.load(SampleSlotId{found->first}, replacement))
                    return {false, sequence, before.revision,
                            "sample load queue is full"};
                samples_.wait_idle();
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(5);
                for (;;) {
                    const auto status =
                        samples_.status(SampleSlotId{found->first});
                    if (status.state == SampleSlotLoadState::loaded)
                        break;
                    if (status.state == SampleSlotLoadState::error)
                        return {false, sequence, before.revision, status.error};
                    if (std::chrono::steady_clock::now() >= deadline)
                        return {false, sequence, before.revision,
                                "sample activation timed out"};
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!slices_.restore_project(relinked.samples, error))
                    return {false, sequence, before.revision, std::move(error)};
                std::lock_guard lock(mutex_);
                project_ = std::move(relinked);
                state_.missing_assets = project_.missing_assets;
                state_.project_dirty = true;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (command.action_id == "project.relink-prefix.v1") {
                if (pending_project_recovery_)
                    return {false, sequence, before.revision,
                            "a project recovery is already pending"};
                auto repaired = std::make_unique<Project>(project_);
                std::vector<std::filesystem::path> unresolved;
                const ProjectDirectoryRelink mapping{
                    std::filesystem::path(*argument<std::string>(
                        command, "missing_prefix")),
                    std::filesystem::path(*argument<std::string>(
                        command, "replacement_prefix"))};
                if (!relink_project_assets(
                        *repaired, {}, {mapping},
                        engine_.sample_rate(), unresolved, error))
                    return {false, sequence, before.revision, std::move(error)};
                auto pending = std::make_unique<PendingProjectRecovery>();
                pending->project = std::move(*repaired);
                pending->activation.mixer = mixer_configuration_;
                PerformanceMacroService macros(
                    pending->project.performance_macros);
                if (!macros.compile(pending->project.modulation,
                                    pending->activation.modulation, error))
                    return {false, sequence, before.revision, std::move(error)};
                if (pending->project.samples.sample_paths.size() >
                    pending->activation.samples.size())
                    return {false, sequence, before.revision,
                            "project sample capacity exceeded"};
                for (const auto &[slot, path] :
                     pending->project.samples.sample_paths) {
                    try {
                        auto sample = std::make_shared<const Sample>(
                            load_wav(path, engine_.sample_rate()));
                        const auto index = pending->activation.sample_count++;
                        pending->activation.samples[index] =
                            {SampleSlotId{slot}, sample};
                        pending->sample_paths[index] = path;
                    } catch (const std::exception &exception) {
                        return {false, sequence, before.revision,
                                "cannot prepare recovered sample " +
                                    path.string() + ": " + exception.what()};
                    }
                }
                const auto prepared_sample = [&](SampleSlotId slot)
                    -> std::shared_ptr<const Sample> {
                    const auto found = std::find_if(
                        pending->activation.samples.begin(),
                        pending->activation.samples.begin() +
                            pending->activation.sample_count,
                        [&](const auto &candidate) {
                            return candidate.slot == slot;
                        });
                    return found == pending->activation.samples.begin() +
                                        pending->activation.sample_count
                        ? nullptr
                        : found->sample;
                };
                for (const auto &slice : pending->project.samples.slices) {
                    const auto sample = prepared_sample(slice.slot);
                    if (!sample || !slice.id ||
                        slice.start_frame >= slice.end_frame ||
                        slice.end_frame > sample->frames() ||
                        !std::isfinite(slice.gain) || slice.gain < 0)
                        return {false, sequence, before.revision,
                                "recovered project contains an invalid slice"};
                }
                for (const auto &region : pending->project.samples.regions) {
                    const auto sample = prepared_sample(region.slot);
                    const auto length = region.end_frame - region.start_frame;
                    if (!sample || !region.id ||
                        region.start_frame >= region.end_frame ||
                        region.end_frame > sample->frames() ||
                        !std::isfinite(region.gain) || region.gain < 0 ||
                        region.fade_in_frames > length ||
                        region.fade_out_frames > length ||
                        (region.loop_enabled &&
                         (region.loop_start_frame < region.start_frame ||
                          region.loop_start_frame >= region.loop_end_frame ||
                          region.loop_end_frame > region.end_frame ||
                          region.loop_crossfade_frames >
                              region.loop_end_frame -
                                  region.loop_start_frame)))
                        return {false, sequence, before.revision,
                                "recovered project contains an invalid region"};
                }
                pending->activation.replace_pad_mappings = true;
                for (std::size_t pad = 0;
                     pad < pending->activation.pads.size();
                     ++pad) {
                    auto &mapping = pending->activation.pads[pad];
                    mapping.playback =
                        pending->project.samples.pad_settings[pad];
                    if (pending->project.samples.region_pads[pad]) {
                        const auto found = std::find_if(
                            pending->project.samples.regions.begin(),
                            pending->project.samples.regions.end(),
                            [&](const auto &region) {
                                return region.id ==
                                    *pending->project.samples.region_pads[pad];
                            });
                        if (found == pending->project.samples.regions.end())
                            return {false, sequence, before.revision,
                                    "recovered pad references a missing region"};
                        mapping.slot = found->slot;
                        mapping.start_frame = found->start_frame;
                        mapping.end_frame = found->end_frame;
                        mapping.gain = found->gain;
                        mapping.playback.reverse =
                            found->direction == RegionDirection::reverse;
                        mapping.region = {
                            found->fade_in_frames, found->fade_out_frames,
                            found->loop_start_frame, found->loop_end_frame,
                            found->loop_crossfade_frames, found->fade_curve,
                            found->loop_enabled};
                        mapping.assigned = true;
                    } else if (pending->project.samples.pads[pad]) {
                        const auto found = std::find_if(
                            pending->project.samples.slices.begin(),
                            pending->project.samples.slices.end(),
                            [&](const auto &slice) {
                                return slice.id ==
                                    *pending->project.samples.pads[pad];
                            });
                        if (found == pending->project.samples.slices.end())
                            return {false, sequence, before.revision,
                                    "recovered pad references a missing slice"};
                        mapping.slot = found->slot;
                        mapping.start_frame = found->start_frame;
                        mapping.end_frame = found->end_frame;
                        mapping.gain = found->gain;
                        mapping.assigned = true;
                    }
                }
                if (!engine_.publish_preset_activation(
                        pending->activation, pending->activation_sequence,
                        pending->sample_publications, error))
                    return {false, sequence, before.revision, std::move(error)};
                pending_project_recovery_ = std::move(*pending);
                state_.project_error.clear();
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (command.action_id == "project.new.v1" ||
                command.action_id == "project.close.v1") {
                live_loops_.clear();
                Project replacement;
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    replacement.name = std::get<std::string>(found->second);
                auto edit = timeline_.edit();
                edit.timeline() = replacement.timeline;
                if (!timeline_.commit(std::move(edit),
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))
                    return {false, sequence, before.revision, std::move(error)};
                parameters_.set(
                    "transport.tempo",
                    60'000'000.0 /
                        replacement.timeline.transport.microseconds_per_quarter,
                    error);
                if (!error.empty())
                    return {false, sequence, before.revision, std::move(error)};
                recording_.cancel();
                engine_.stop();
                if (!slices_.restore_project(replacement.samples, error))
                    return {false, sequence, before.revision, std::move(error)};
                replacement.mixer.revision = mixer_configuration_.revision + 1;
                for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
                    auto &loop = replacement.mixer.external_fx[i];
                    loop.id = static_cast<ExternalFxLoopId>(i + 1);
                    const auto name = "External FX " + std::to_string(i + 1);
                    std::copy_n(name.begin(),
                                std::min(name.size(), loop.name.size() - 1),
                                loop.name.begin());
                    loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
                    loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
                    loop.return_left = static_cast<std::uint8_t>(i * 2);
                    loop.return_right = static_cast<std::uint8_t>(1 + i * 2);
                }
                if (!engine_.publish_mixer(replacement.mixer, error))
                    return {false, sequence, before.revision, std::move(error)};
                mixer_configuration_ = replacement.mixer;
                if (!performance_macros_.replace(
                        replacement.performance_macros, replacement.timeline,
                        replacement.mixer, error) ||
                    !publish_modulation_with_macros(replacement.modulation,
                                                    error))
                    return {false, sequence, before.revision, std::move(error)};
                modulation_configuration_ = replacement.modulation;
                synth_instance_parameters_.clear();
                next_modulation_route_id_ = 1;
                next_modulator_instance_id_ = 1;
                for (const auto &slot : samples_.states())
                    if ((slot.has_sample ||
                         slot.state != SampleSlotLoadState::empty) &&
                        !samples_.unload(slot.id))
                        return {false, sequence, before.revision,
                                "sample unload queue is full"};
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(replacement);
                    state_.project_name = project_.name;
                    state_.project_path.clear();
                    state_.project_dirty =
                        command.action_id == "project.new.v1";
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.missing_assets.clear();
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
        } catch (const std::exception &exception) {
            std::lock_guard lock(mutex_);
            state_.project_loading = false;
            state_.project_saving = false;
            state_.project_error = exception.what();
            changed_locked();
            return {false, sequence, state_.revision, state_.project_error};
        }
    }
    if (command.action_id == "navigation.confirm.v1" &&
        before.focused_element == "steps") {
        ApplicationCommand select;
        select.action_id = "step.event-select.v1";
        auto result = execute_locked(select, sequence);
        if (!result.accepted) {
            ApplicationCommand toggle;
            toggle.action_id = "step.toggle.v1";
            result = execute_locked(toggle, sequence);
            if (result.accepted)
                result = execute_locked(select, sequence);
        }
        if (result.accepted) {
            std::lock_guard lock(mutex_);
            state_.focused_element = "step-properties";
            changed_locked();
            result.state_revision = state_.revision;
        }
        return result;
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "step-properties" && before.selected_event) {
        const auto delta = *argument<double>(command, "steps");
        ApplicationCommand edit;
        const auto &event = *before.selected_event;
        if (before.selected_step_property == "velocity") {
            edit.action_id = "step.velocity-set.v1";
            edit.arguments = {
                {"velocity",
                 std::clamp(static_cast<double>(event.velocity) + delta * 0.05,
                            0.0, 1.0)}};
        } else if (before.selected_step_property == "duration") {
            edit.action_id = "step.duration-set.v1";
            edit.arguments = {
                {"duration_ticks",
                 static_cast<std::int64_t>(std::max<MusicalTick>(
                     1, event.duration + static_cast<MusicalTick>(delta) *
                                             before.step_ticks / 4))}};
        } else if (before.selected_step_property == "pitch") {
            edit.action_id = "step.pitch-set.v1";
            edit.arguments = {
                {"pitch", static_cast<std::int64_t>(std::clamp<int>(
                              event.pitch + (delta < 0 ? -1 : 1), 0, 127))}};
        } else if (before.selected_step_property == "timing") {
            edit.action_id = "step.timing-offset-set.v1";
            edit.arguments = {
                {"offset_ticks",
                 static_cast<std::int64_t>(std::clamp<MusicalTick>(
                     event.timing_offset + static_cast<MusicalTick>(delta) * 10,
                     -before.step_ticks + 1, before.step_ticks - 1))}};
        } else if (before.selected_step_property == "probability") {
            edit.action_id = "step.probability-set.v1";
            edit.arguments = {
                {"probability",
                 std::clamp(static_cast<double>(event.trigger_probability) +
                                delta * 0.05,
                            0.0, 1.0)}};
        } else if (before.selected_step_property == "ratchet") {
            edit.action_id = "step.ratchet-set.v1";
            edit.arguments = {
                {"count",
                 static_cast<std::int64_t>(std::clamp<int>(
                     event.ratchet_count + (delta < 0 ? -1 : 1), 1, 8))}};
        } else {
            const auto current = static_cast<int>(event.trigger_condition);
            const auto next = (current + (delta < 0 ? 8 : 1)) % 9;
            edit.action_id = "step.condition-set.v1";
            edit.arguments = {
                {"condition", std::string(trigger_condition_name(
                                  static_cast<TriggerCondition>(next)))},
                {"n", static_cast<std::int64_t>(event.condition_n)},
                {"m", static_cast<std::int64_t>(event.condition_m)}};
        }
        return execute_locked(edit, sequence);
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "pad-playback") {
        const auto delta = *argument<double>(command, "steps");
        const auto &p = before.selected_pad_settings;
        ApplicationCommand edit;
        if (before.selected_pad_property == "mode") {
            const auto next = static_cast<SamplePlaybackMode>(
                (static_cast<int>(p.mode) + (delta < 0 ? 2 : 1)) % 3);
            edit.action_id = "pad.playback-mode-set.v1";
            edit.arguments = {
                {"mode", std::string(sample_playback_mode_name(next))}};
        } else if (before.selected_pad_property == "choke") {
            edit.action_id = "pad.choke-group-set.v1";
            edit.arguments = {
                {"group", static_cast<std::int64_t>(std::clamp<int>(
                              p.choke_group + (delta < 0 ? -1 : 1), 0, 16))}};
        } else if (before.selected_pad_property == "pitch") {
            edit.action_id = "pad.pitch-set.v1";
            edit.arguments = {
                {"semitones", static_cast<double>(p.semitones + delta)}};
        } else if (before.selected_pad_property == "fine") {
            edit.action_id = "pad.fine-set.v1";
            edit.arguments = {
                {"cents", static_cast<double>(p.fine_cents + delta)}};
        } else if (before.selected_pad_property == "gain") {
            edit.action_id = "pad.gain-set.v1";
            edit.arguments = {
                {"gain",
                 static_cast<double>(std::clamp(
                     p.gain + static_cast<float>(delta * 0.05), 0.0F, 4.0F))}};
        } else if (before.selected_pad_property == "reverse") {
            edit.action_id = "pad.reverse-set.v1";
            edit.arguments = {{"reverse", !p.reverse}};
        } else if (before.selected_pad_property == "stretch-mode") {
            edit.action_id = "pad.stretch-mode-set.v1";
            edit.arguments = {
                {"mode",
                 std::string(p.stretch_mode == StretchMode::off ? "realtime"
                                                                : "off")}};
        } else if (before.selected_pad_property == "stretch-ratio") {
            edit.action_id = "pad.stretch-ratio-set.v1";
            edit.arguments = {
                {"ratio", static_cast<double>(std::clamp(
                              p.stretch_ratio + delta * .05, .25, 4.0))}};
        } else if (before.selected_pad_property == "beats") {
            edit.action_id = "pad.source-beats-set.v1";
            edit.arguments = {
                {"beats", static_cast<double>(std::clamp(
                              p.source_beats + delta * .25, 0.0, 1024.0))}};
        } else if (before.selected_pad_property == "source-bpm") {
            edit.action_id = "pad.source-bpm-set.v1";
            edit.arguments = {{"bpm", static_cast<double>(std::clamp(
                                          p.source_bpm + delta, 0.0, 1000.0))}};
        } else if (before.selected_pad_property == "stretch-quality") {
            const auto current = static_cast<int>(p.stretch_quality);
            const auto next = (current + (delta < 0 ? 2 : 1)) % 3;
            edit.action_id = "pad.stretch-quality-set.v1";
            edit.arguments = {
                {"quality", std::string(stretch_quality_name(
                                static_cast<StretchQuality>(next)))}};
        } else {
            auto attack = p.attack_seconds;
            auto decay = p.decay_seconds;
            auto sustain = p.sustain;
            auto release = p.release_seconds;
            if (before.selected_pad_property == "attack")
                attack =
                    std::max(0.0F, attack + static_cast<float>(delta * .01));
            else if (before.selected_pad_property == "decay")
                decay = std::max(0.0F, decay + static_cast<float>(delta * .01));
            else if (before.selected_pad_property == "sustain")
                sustain = std::clamp(sustain + static_cast<float>(delta * .01),
                                     0.0F, 1.0F);
            else
                release =
                    std::max(0.0F, release + static_cast<float>(delta * .01));
            edit.action_id = "pad.envelope-set.v1";
            edit.arguments = {{"attack", static_cast<double>(attack)},
                              {"decay", static_cast<double>(decay)},
                              {"sustain", static_cast<double>(sustain)},
                              {"release", static_cast<double>(release)}};
        }
        return execute_locked(edit, sequence);
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "step-locks") {
        const auto descriptor = parameters_.get(before.selected_lock_parameter);
        if (!descriptor)
            return {false, sequence, before.revision,
                    "selected lock parameter is unavailable"};
        auto value = number(descriptor->current_value);
        const auto selected_lock = std::find_if(
            before.selected_step_locks.begin(),
            before.selected_step_locks.end(), [&](const auto &item) {
                return item.parameter_id == before.selected_lock_parameter;
            });
        if (selected_lock != before.selected_step_locks.end())
            value = selected_lock->value;
        value = std::clamp(
            value + *argument<double>(command, "steps") * descriptor->step,
            number(descriptor->minimum), number(descriptor->maximum));
        ApplicationCommand lock;
        lock.action_id = "step.lock-set.v1";
        lock.arguments = {{"parameter_id", before.selected_lock_parameter},
                          {"value", value}};
        return execute_locked(lock, sequence);
    }
    bool changed = false;
    if (command.action_id == "transport.play.v1") {
        engine_.play();
        changed = true;
    } else if (command.action_id == "transport.stop.v1") {
        if (arrangement_recording_.status().phase ==
            ArrangementRecordPhase::recording)
            arrangement_recording_.stop(state_.transport_tick, error);
        engine_.stop();
        changed = true;
    } else if (command.action_id == "transport.restart.v1") {
        if (arrangement_recording_.status().phase ==
            ArrangementRecordPhase::recording)
            error = "stop arrangement recording before restarting transport";
        else {
            engine_.restart();
            changed = true;
        }
    } else if (command.action_id == "arrangement.seek.v1") {
        const auto tick = *argument<std::int64_t>(command, "tick");
        const auto snapshot = timeline_.snapshot();
        if (arrangement_recording_.status().phase ==
            ArrangementRecordPhase::recording)
            error = "stop arrangement recording before seeking";
        else if (tick < 0)
            error = "arrangement seek tick must be nonnegative";
        else {
            const auto frame =
                (static_cast<long double>(tick) * engine_.sample_rate() *
                 snapshot.timeline.transport.microseconds_per_quarter) /
                (static_cast<long double>(ticks_per_quarter) * 1'000'000.0L);
            if (frame > std::numeric_limits<std::uint64_t>::max())
                error = "arrangement seek position is too large";
            else if (!engine_.seek_frame(static_cast<std::uint64_t>(frame)))
                error = "transport command queue is full";
            else
                changed = true;
        }
    } else if (command.action_id == "transport.tempo-set.v1") {
        auto transaction = timeline_.edit();
        if (!transaction.set_tempo_bpm(*argument<double>(command, "bpm"),
                                       error) ||
            !timeline_.commit(std::move(transaction),
                              TimelineCommitPolicy::next_audio_callback,
                              error)) {
        } else {
            std::string ignored;
            parameters_.set("transport.tempo",
                            *argument<double>(command, "bpm"), ignored);
            changed = true;
        }
    } else if (command.action_id == "sample.load.v1" ||
               command.action_id == "sample.replace.v1") {
        const auto slot = *argument<std::int64_t>(command, "slot_id");
        if (slot < 0 || slot > std::numeric_limits<std::uint32_t>::max())
            error = "sample slot is outside its range";
        else if (!samples_.load(SampleSlotId{static_cast<std::uint32_t>(slot)},
                                *argument<std::string>(command, "path")))
            error = "sample command queue rejected the request";
        else
            changed = true;
    } else if (command.action_id == "sample.unload.v1") {
        const auto slot = *argument<std::int64_t>(command, "slot_id");
        if (slot < 0 || slot > std::numeric_limits<std::uint32_t>::max())
            error = "sample slot is outside its range";
        else if (!samples_.unload(
                     SampleSlotId{static_cast<std::uint32_t>(slot)}))
            error = "sample command queue rejected the request";
        else
            changed = true;
    } else {
        std::lock_guard lock(mutex_);
        synchronize_locked();
        if (command.action_id == "track.select.v1") {
            const auto id = *argument<std::int64_t>(command, "track_id");
            const auto timeline = timeline_.snapshot();
            if (id <= 0 || std::none_of(timeline.timeline.tracks.begin(),
                                        timeline.timeline.tracks.end(),
                                        [&](const Track &track) {
                                            return track.id ==
                                                   static_cast<TrackId>(id);
                                        })) {
                error = "track does not exist";
            } else {
                state_.selected_track = static_cast<TrackId>(id);
                changed_locked();
            }
        } else if (command.action_id == "track.create.v1") {
            auto transaction = timeline_.edit();
            const auto requested = argument<std::int64_t>(command, "track_id");
            const auto id = requested ? static_cast<TrackId>(*requested)
                                      : transaction.next_track_id();
            const auto length = *argument<std::int64_t>(command, "length");
            const auto name = argument<std::string>(command, "name");
            const auto instrument =
                argument<std::string>(command, "instrument_id");
            const auto instance =
                argument<std::int64_t>(command, "instance_id");
            if ((requested && *requested <= 0) || length <= 0) {
                error = "track ID and length must be positive";
            } else if (!transaction.create_track(
                           id, name ? *name : "Track " + std::to_string(id),
                           static_cast<MusicalTick>(length), error)) {
            } else if (instrument &&
                       !transaction.set_track_instrument(
                           id, *instrument, error,
                           instance
                               ? static_cast<InstrumentInstanceId>(*instance)
                               : default_synth_instance_id)) {
            } else if (!timeline_.commit(
                           std::move(transaction),
                           TimelineCommitPolicy::next_audio_callback, error)) {
            } else {
                state_.selected_track = id;
                state_.selected_step = 0;
                state_.step_page = 0;
                changed_locked();
            }
        } else if (command.action_id == "track.delete.v1") {
            auto transaction = timeline_.edit();
            const auto requested = argument<std::int64_t>(command, "track_id");
            const auto id = requested ? static_cast<TrackId>(*requested)
                                      : state_.selected_track;
            if ((requested && *requested <= 0) || id == 0)
                error = "track ID must be positive";
            else if (std::any_of(modulation_configuration_.modulators.begin(),
                                 modulation_configuration_.modulators.begin() +
                                     modulation_configuration_.modulator_count,
                                 [&](const auto &instance) {
                                     return instance.owner_kind ==
                                                ModulatorOwnerKind::track &&
                                            instance.owner_id == id;
                                 }))
                error = "track owns modulators; delete or reassign them first";
            else if (!transaction.delete_track(id, error) ||
                     !timeline_.commit(
                         std::move(transaction),
                         TimelineCommitPolicy::next_audio_callback, error)) {
            } else {
                const auto remaining = timeline_.snapshot().timeline.tracks;
                state_.selected_track =
                    remaining.empty() ? 0 : remaining.front().id;
                selected_event_id_.reset();
                changed_locked();
            }
        } else if (command.action_id == "track.rename.v1" ||
                   command.action_id == "track.duplicate.v1" ||
                   command.action_id == "track.pattern-length-set.v1") {
            auto transaction = timeline_.edit();
            std::optional<TrackId> duplicated_track;
            const auto requested = argument<std::int64_t>(command, "track_id");
            const auto id = requested ? static_cast<TrackId>(*requested)
                                      : state_.selected_track;
            if ((requested && *requested <= 0) || id == 0) {
                error = "track ID must be positive";
            } else if (command.action_id == "track.rename.v1") {
                transaction.set_track_name(
                    id, *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "track.pattern-length-set.v1") {
                transaction.set_track_length(
                    id, *argument<std::int64_t>(command, "length"), error);
            } else {
                const auto *source = transaction.find_track(id);
                if (!source) {
                    error = "track does not exist";
                } else {
                    const auto source_copy = *source;
                    const auto destination = transaction.next_track_id();
                    const auto name = argument<std::string>(command, "name");
                    if (transaction.create_track(
                            destination,
                            name ? *name : source_copy.name + " Copy",
                            source_copy.pattern.length, error)) {
                        auto *copy = transaction.find_track(destination);
                        copy->pattern = source_copy.pattern;
                        copy->muted = source_copy.muted;
                        copy->instrument_id = source_copy.instrument_id;
                        copy->instrument_instance_id =
                            source_copy.instrument_instance_id;
                        copy->midi_output_port = source_copy.midi_output_port;
                        copy->midi_channel = source_copy.midi_channel;
                        duplicated_track = destination;
                    }
                }
            }
            if (error.empty() && timeline_.commit(
                    std::move(transaction),
                    TimelineCommitPolicy::next_audio_callback, error)) {
                if (duplicated_track)
                    state_.selected_track = *duplicated_track;
                state_.selected_step = 0;
                state_.step_page = 0;
                selected_event_id_.reset();
                state_.project_dirty = true;
                changed_locked();
            }
        } else if (command.action_id == "track.next.v1" ||
                   command.action_id == "track.previous.v1") {
            const auto timeline = timeline_.snapshot();
            if (timeline.timeline.tracks.empty())
                error = "no tracks are available";
            else {
                auto found = std::find_if(
                    timeline.timeline.tracks.begin(),
                    timeline.timeline.tracks.end(), [&](const Track &track) {
                        return track.id == state_.selected_track;
                    });
                auto index =
                    found == timeline.timeline.tracks.end()
                        ? 0
                        : static_cast<std::size_t>(
                              found - timeline.timeline.tracks.begin());
                if (command.action_id == "track.next.v1")
                    index = (index + 1) % timeline.timeline.tracks.size();
                else
                    index = (index + timeline.timeline.tracks.size() - 1) %
                            timeline.timeline.tracks.size();
                state_.selected_track = timeline.timeline.tracks[index].id;
                changed_locked();
            }
        } else if (command.action_id == "track.mute-toggle.v1") {
            auto transaction = timeline_.edit();
            const auto requested = argument<std::int64_t>(command, "track_id");
            const auto track_id = requested
                ? static_cast<TrackId>(*requested)
                : state_.selected_track;
            auto *track = transaction.find_track(track_id);
            if (!track)
                error = "selected track does not exist";
            else if (!transaction.set_track_muted(track->id, !track->muted,
                                                  error) ||
                     !timeline_.commit(
                         std::move(transaction),
                         TimelineCommitPolicy::next_audio_callback, error)) {
            } else {
                changed_locked();
            }
        } else if (command.action_id == "track.instrument-set.v1") {
            auto transaction = timeline_.edit();
            const auto instance =
                argument<std::int64_t>(command, "instance_id");
            if (!transaction.set_track_instrument(
                    state_.selected_track,
                    *argument<std::string>(command, "instrument_id"), error,
                    instance ? static_cast<InstrumentInstanceId>(*instance)
                             : default_synth_instance_id) ||
                !timeline_.commit(std::move(transaction),
                                  TimelineCommitPolicy::next_audio_callback,
                                  error)) {
            } else
                changed_locked();
        } else if (command.action_id == "track.midi-destination-set.v1") {
            const auto port = *argument<std::int64_t>(command, "port");
            const auto channel = *argument<std::int64_t>(command, "channel");
            auto transaction = timeline_.edit();
            if (port < 1 ||
                port > static_cast<std::int64_t>(maximum_midi_ports) ||
                channel < 1 || channel > 16)
                error = "MIDI destination requires port 1..8 and channel 1..16";
            else if (!transaction.set_track_midi_destination(
                         state_.selected_track,
                         static_cast<std::uint32_t>(port),
                         static_cast<std::uint8_t>(channel - 1), error) ||
                     !timeline_.commit(
                         std::move(transaction),
                         TimelineCommitPolicy::next_audio_callback, error)) {
            } else
                changed_locked();
        } else if (command.action_id == "performance.scene-create.v1" ||
                   command.action_id == "performance.scene-delete.v1" ||
                   command.action_id == "performance.scene-rename.v1" ||
                   command.action_id == "performance.scene-duplicate.v1" ||
                   command.action_id == "performance.scene-slot-set.v1" ||
                   command.action_id == "performance.scene-slot-remove.v1" ||
                   command.action_id == "performance.slot-follow-set.v1") {
            auto transaction = timeline_.edit();
            auto &arrangement = transaction.timeline().arrangement;
            if (command.action_id == "performance.slot-follow-set.v1") {
                const auto id = static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot"));
                const auto found = std::find_if(arrangement.launch_slots.begin(), arrangement.launch_slots.end(),
                    [=](const auto &slot) { return slot.id == id; });
                if (found == arrangement.launch_slots.end()) error = "launch slot does not exist";
                else {
                    const auto &name = *argument<std::string>(command, "action");
                    if (name == "none") found->follow = SlotFollowAction::none;
                    else if (name == "stop") found->follow = SlotFollowAction::stop;
                    else if (name == "repeat") found->follow = SlotFollowAction::repeat;
                    else if (name == "next") found->follow = SlotFollowAction::next;
                    else if (name == "previous") found->follow = SlotFollowAction::previous;
                    else if (name == "specified") found->follow = SlotFollowAction::specified;
                    else if (name == "random") found->follow = SlotFollowAction::random_eligible;
                    else error = "unknown slot follow action";
                    if (const auto v = argument<std::int64_t>(command, "target")) found->follow_target = *v;
                    if (const auto v = argument<std::int64_t>(command, "repetitions"))
                        found->follow_repetitions = static_cast<std::uint16_t>(std::max<std::int64_t>(1, *v));
                    if (const auto v = argument<std::int64_t>(command, "seed")) found->random_seed = *v;
                }
            } else {
                const auto id = static_cast<PerformanceSceneId>(*argument<std::int64_t>(command, "scene"));
                auto found = std::find_if(arrangement.performance_scenes.begin(), arrangement.performance_scenes.end(),
                    [=](const auto &scene) { return scene.id == id; });
                if (command.action_id == "performance.scene-create.v1") {
                    if (!id || found != arrangement.performance_scenes.end()) error = "performance scene already exists";
                    else { PerformanceLaunchScene scene; scene.id = id; scene.name = *argument<std::string>(command, "name"); arrangement.performance_scenes.push_back(std::move(scene)); }
                } else if (command.action_id == "performance.scene-delete.v1") {
                    if (found == arrangement.performance_scenes.end()) error = "performance scene does not exist";
                    else arrangement.performance_scenes.erase(found);
                } else if (command.action_id == "performance.scene-duplicate.v1") {
                    const auto source_id = static_cast<PerformanceSceneId>(*argument<std::int64_t>(command, "source"));
                    const auto source = std::find_if(arrangement.performance_scenes.begin(), arrangement.performance_scenes.end(),
                        [=](const auto &scene) { return scene.id == source_id; });
                    if (source == arrangement.performance_scenes.end()) error = "source performance scene does not exist";
                    else if (!id || found != arrangement.performance_scenes.end()) error = "performance scene already exists";
                    else { auto copy = *source; copy.id = id; if (const auto n = argument<std::string>(command, "name")) copy.name = *n; arrangement.performance_scenes.push_back(std::move(copy)); }
                } else if (found == arrangement.performance_scenes.end()) error = "performance scene does not exist";
                else if (command.action_id == "performance.scene-rename.v1") found->name = *argument<std::string>(command, "name");
                else {
                    const auto track = static_cast<TrackId>(*argument<std::int64_t>(command, "track"));
                    std::erase_if(found->entries, [=](const auto &entry) { return entry.track == track; });
                    if (command.action_id == "performance.scene-slot-set.v1")
                        found->entries.push_back({track, static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot"))});
                }
            }
            if (error.empty()) {
                PerformanceLauncher validation(engine_.sample_rate());
                if (validation.configure(transaction.timeline(), error) &&
                    timeline_.commit(std::move(transaction), TimelineCommitPolicy::next_audio_callback, error)) {
                    state_.project_dirty = true; changed_locked();
                }
            }
        } else if (command.action_id == "performance.slot-assign.v1" ||
                   command.action_id == "performance.slot-clear.v1" ||
                   command.action_id == "performance.slot-duplicate.v1" ||
                   command.action_id == "performance.pad-assign.v1" ||
                   command.action_id == "performance.pad-clear.v1" ||
                   command.action_id == "performance.velocity-policy.v1") {
            auto transaction = timeline_.edit();
            auto &arrangement = transaction.timeline().arrangement;
            if (command.action_id == "performance.pad-assign.v1" ||
                command.action_id == "performance.pad-clear.v1") {
                const auto position = *argument<std::int64_t>(command, "position");
                if (position < 0 || position > 65535)
                    error = "logical pad position is out of range";
                else {
                    for (auto &slot : arrangement.launch_slots)
                        if (slot.logical_position == position)
                            slot.logical_position = -1;
                    if (command.action_id == "performance.pad-assign.v1") {
                        const auto id = static_cast<LaunchSlotId>(
                            *argument<std::int64_t>(command, "slot"));
                        const auto found = std::find_if(
                            arrangement.launch_slots.begin(),
                            arrangement.launch_slots.end(),
                            [=](const auto &slot) { return slot.id == id; });
                        if (found == arrangement.launch_slots.end())
                            error = "launch slot does not exist";
                        else
                            found->logical_position =
                                static_cast<std::int32_t>(position);
                    }
                }
            } else if (command.action_id == "performance.slot-clear.v1") {
                const auto id = static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot"));
                const auto before = arrangement.launch_slots.size();
                std::erase_if(arrangement.launch_slots, [=](const auto &slot) { return slot.id == id; });
                for (const auto &scene : arrangement.performance_scenes)
                    if (std::any_of(scene.entries.begin(), scene.entries.end(),
                                    [=](const auto &entry) { return entry.slot == id; })) {
                        error = "launch slot is referenced by a scene";
                        break;
                    }
                if (error.empty() && before == arrangement.launch_slots.size())
                    error = "launch slot does not exist";
            } else if (command.action_id == "performance.slot-duplicate.v1") {
                const auto source_id = static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "source"));
                const auto new_id = static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot"));
                const auto source = std::find_if(arrangement.launch_slots.begin(), arrangement.launch_slots.end(),
                    [&](const auto &slot) { return slot.id == source_id; });
                if (source == arrangement.launch_slots.end()) error = "source launch slot does not exist";
                else if (!new_id || std::any_of(arrangement.launch_slots.begin(), arrangement.launch_slots.end(),
                    [&](const auto &slot) { return slot.id == new_id; })) error = "destination launch slot already exists";
                else { auto copy = *source; copy.id = new_id;
                    copy.logical_position = -1;
                    arrangement.launch_slots.push_back(copy); }
            } else if (command.action_id == "performance.velocity-policy.v1") {
                const auto &policy = *argument<std::string>(command, "policy");
                if (policy == "fixed") arrangement.velocity_policy = VelocityPolicy::fixed;
                else if (policy == "native") arrangement.velocity_policy = VelocityPolicy::native;
                else if (policy == "full-level") arrangement.velocity_policy = VelocityPolicy::full_level;
                else error = "unknown velocity policy";
                if (const auto value = argument<double>(command, "velocity"))
                    arrangement.fixed_velocity = static_cast<float>(*value);
            } else {
                LaunchSlot value;
                value.id = static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot"));
                value.track = static_cast<TrackId>(*argument<std::int64_t>(command, "track"));
                value.source_id = static_cast<std::uint64_t>(*argument<std::int64_t>(command, "source"));
                const auto &source = *argument<std::string>(command, "source_type");
                if (source == "pattern") value.source_type = LaunchSourceType::pattern;
                else if (source == "audio") value.source_type = LaunchSourceType::audio_clip;
                else if (source == "live-loop") value.source_type = LaunchSourceType::live_loop;
                else error = "unknown launch source type";
                if (const auto behavior = argument<std::string>(command, "behavior")) {
                    if (*behavior == "one-shot") value.behavior = LaunchBehavior::one_shot;
                    else if (*behavior == "toggle") value.behavior = LaunchBehavior::toggle;
                    else if (*behavior == "gate") value.behavior = LaunchBehavior::gate;
                    else if (*behavior == "loop") value.behavior = LaunchBehavior::loop;
                    else if (*behavior == "retrigger") value.behavior = LaunchBehavior::retrigger;
                    else if (*behavior == "legato") value.behavior = LaunchBehavior::legato;
                    else error = "unknown launch behavior";
                }
                if (error.empty()) {
                    const auto found = std::find_if(arrangement.launch_slots.begin(), arrangement.launch_slots.end(),
                        [&](const auto &slot) { return slot.id == value.id; });
                    if (found == arrangement.launch_slots.end()) arrangement.launch_slots.push_back(value);
                    else { value.logical_position = found->logical_position; *found = value; }
                }
            }
            if (error.empty()) {
                PerformanceLauncher validation(engine_.sample_rate());
                if (validation.configure(transaction.timeline(), error) &&
                    timeline_.commit(std::move(transaction), TimelineCommitPolicy::next_audio_callback, error)) {
                    state_.project_dirty = true;
                    changed_locked();
                }
            }
        } else if (command.action_id == "performance.slot-cancel.v1") {
            if (!performance_launcher_.cancel(static_cast<LaunchSlotId>(
                    *argument<std::int64_t>(command, "slot"))))
                error = "queued launch does not exist";
            else changed_locked();
        } else if (command.action_id == "performance.slot-launch.v1" ||
                   command.action_id == "performance.slot-stop.v1" ||
                   command.action_id == "performance.slot-retrigger.v1" ||
                   command.action_id == "performance.scene-launch.v1") {
            const auto snapshot = timeline_.snapshot();
            if (performance_launcher_.configure(snapshot.timeline, error, true)) {
                std::vector<PreparedLaunch> launches;
                if (command.action_id == "performance.scene-launch.v1") {
                    performance_launcher_.prepare_scene(
                        static_cast<PerformanceSceneId>(*argument<std::int64_t>(command, "scene")),
                        state_.transport_tick, engine_.frame_position(), launches, error);
                } else {
                    PreparedLaunch launch;
                    performance_launcher_.prepare_slot(
                        static_cast<LaunchSlotId>(*argument<std::int64_t>(command, "slot")),
                        command.action_id == "performance.slot-stop.v1"
                            ? PreparedLaunchOperation::stop
                            : command.action_id == "performance.slot-retrigger.v1"
                                ? PreparedLaunchOperation::retrigger
                                : PreparedLaunchOperation::start,
                        state_.transport_tick, engine_.frame_position(), launch, error);
                    if (error.empty()) launches.push_back(launch);
                }
                if (error.empty() && !launches.empty()) {
                    const auto session = performance_launcher_.compose_session(launches);
                    if (!publish_performance_launches(snapshot.timeline, session,
                                                      launches, error))
                        ++state_.performance_launching.diagnostics.rejected;
                    else changed_locked();
                }
            }
        } else if (command.action_id == "performance.return-arrangement.v1") {
            const auto snapshot = timeline_.snapshot();
            if (performance_launcher_.configure(snapshot.timeline, error, true)) {
                const auto track_argument = argument<std::int64_t>(command, "track");
                if (track_argument && *track_argument < 0)
                    error = "track ID must be nonnegative";
                const auto track = track_argument && *track_argument >= 0
                    ? static_cast<TrackId>(*track_argument) : TrackId{};
                auto quantization = LaunchQuantization::bar;
                if (const auto value = argument<std::string>(command, "quantization")) {
                    if (*value == "immediate") quantization = LaunchQuantization::immediate;
                    else if (*value == "quarter") quantization = LaunchQuantization::quarter;
                    else if (*value != "bar") error = "unknown return quantization";
                }
                std::vector<PreparedLaunch> returns;
                if (error.empty() && performance_launcher_.prepare_return(
                        track, state_.transport_tick, engine_.frame_position(),
                        quantization, returns, error)) {
                    const auto session = performance_launcher_.compose_session(returns);
                    if (!publish_performance_launches(snapshot.timeline, session,
                                                      returns, error))
                        ++state_.performance_launching.diagnostics.rejected;
                    else changed_locked();
                }
            }
        } else if (command.action_id == "pattern.select.v1") {
            const auto id = *argument<std::int64_t>(command, "pattern_id");
            if (id < 0)
                error = "pattern ID must be nonnegative";
            else if (id != 0) {
                const auto snapshot = timeline_.snapshot();
                const auto found = std::find_if(
                    snapshot.timeline.arrangement.patterns.begin(),
                    snapshot.timeline.arrangement.patterns.end(),
                    [=](const auto &pattern) {
                        return pattern.id == static_cast<PatternId>(id);
                    });
                if (found == snapshot.timeline.arrangement.patterns.end())
                    error = "pattern does not exist";
            }
            if (error.empty()) {
                state_.selected_pattern = static_cast<std::uint64_t>(id);
                state_.selected_step = 0;
                state_.step_page = 0;
                selected_event_id_.reset();
                changed_locked();
            }
        } else if (command.action_id == "pattern.previous.v1" ||
                   command.action_id == "pattern.next.v1") {
            const auto snapshot = timeline_.snapshot();
            const auto &patterns = snapshot.timeline.arrangement.patterns;
            if (patterns.empty())
                error = "no reusable patterns are available";
            else {
                const auto found = std::find_if(
                    patterns.begin(), patterns.end(), [&](const auto &pattern) {
                        return pattern.id == state_.selected_pattern;
                    });
                auto index = found == patterns.end()
                    ? std::size_t{}
                    : static_cast<std::size_t>(found - patterns.begin());
                if (command.action_id == "pattern.next.v1")
                    index = (index + 1) % patterns.size();
                else
                    index = (index + patterns.size() - 1) % patterns.size();
                state_.selected_pattern = patterns[index].id;
                state_.selected_step = 0;
                state_.step_page = 0;
                selected_event_id_.reset();
                changed_locked();
            }
        } else if (command.action_id == "arrangement.record-arm.v1" ||
                   command.action_id == "performance.capture-arm.v1") {
            if (arrangement_recording_.arm(*argument<bool>(command, "armed"),
                                           error))
                changed_locked();
        } else if (command.action_id == "arrangement.automation-arm.v1") {
            if (arrangement_recording_.arm_automation(
                    *argument<bool>(command, "armed"), error))
                changed_locked();
        } else if (command.action_id == "arrangement.automation-thinning.v1") {
            const auto ticks = *argument<std::int64_t>(command, "ticks");
            if (ticks < 0)
                error = "automation thinning must be nonnegative";
            else {
                automation_thinning_ticks_ = ticks;
                changed_locked();
            }
        } else if (command.action_id == "arrangement.record-start.v1" ||
                   command.action_id == "performance.capture-start.v1") {
            const auto &mode = *argument<std::string>(command, "mode");
            const auto bars = argument<std::int64_t>(command, "count_in_bars");
            const auto automation = argument<bool>(command, "automation");
            if (mode != "replace" && mode != "overdub")
                error = "recording mode must be replace or overdub";
            else if (bars && *bars < 0)
                error = "count-in bars must be nonnegative";
            else {
                const auto timeline = timeline_.snapshot();
                const auto bar =
                    static_cast<MusicalTick>(ticks_per_quarter) *
                    timeline.timeline.transport.time_signature_numerator * 4 /
                    timeline.timeline.transport.time_signature_denominator;
                const auto prior = arrangement_recording_.status();
                auto capture_tick = state_.transport_tick;
                if (command.action_id == "performance.capture-start.v1") {
                    if (const auto q = argument<std::string>(command, "quantization")) {
                        const auto quantum = *q == "beat" ? MusicalTick(ticks_per_quarter)
                            : *q == "bar" ? bar : MusicalTick(0);
                        if (*q != "immediate" && !quantum)
                            error = "capture quantization must be immediate, beat, or bar";
                        else if (quantum)
                            capture_tick = ((capture_tick + quantum - 1) / quantum) * quantum;
                    }
                }
                if (error.empty() && arrangement_recording_.start(
                        capture_tick,
                        mode == "replace" ? ArrangementRecordMode::replace
                                          : ArrangementRecordMode::overdub,
                        (bars ? *bars : 0) * bar,
                        automation ? *automation : prior.automation_armed,
                        error)) {
                    const auto &arrangement = timeline.timeline.arrangement;
                    if (arrangement.playback_mode ==
                            TimelinePlaybackMode::live_pattern &&
                        arrangement.active_pattern)
                        arrangement_recording_.capture_pattern(
                            arrangement.active_pattern,
                            capture_tick + (bars ? *bars : 0) * bar);
                    if (arrangement.chain_running)
                        arrangement_recording_.capture_chain(
                            timeline.timeline,
                            capture_tick + (bars ? *bars : 0) * bar);
                    changed_locked();
                }
            }
        } else if (command.action_id == "arrangement.record-stop.v1" ||
                   command.action_id == "performance.capture-stop.v1") {
            auto stop_tick = state_.transport_tick;
            if (command.action_id == "performance.capture-stop.v1") {
                if (const auto q = argument<std::string>(command, "quantization")) {
                    const auto snapshot = timeline_.snapshot();
                    const auto bar = static_cast<MusicalTick>(ticks_per_quarter) *
                        snapshot.timeline.transport.time_signature_numerator * 4 /
                        snapshot.timeline.transport.time_signature_denominator;
                    const auto quantum = *q == "beat" ? MusicalTick(ticks_per_quarter)
                        : *q == "bar" ? bar : MusicalTick(0);
                    if (*q != "immediate" && !quantum)
                        error = "capture quantization must be immediate, beat, or bar";
                    else if (quantum)
                        stop_tick = ((stop_tick + quantum - 1) / quantum) * quantum;
                }
            }
            if (error.empty() && arrangement_recording_.stop(stop_tick, error))
                changed_locked();
        } else if (command.action_id == "arrangement.record-commit.v1" ||
                   command.action_id == "performance.capture-commit.v1") {
            if (arrangement_recording_.commit(timeline_, error)) {
                state_.project_dirty = true;
                changed_locked();
            }
        } else if (command.action_id == "arrangement.record-discard.v1" ||
                   command.action_id == "performance.capture-discard.v1") {
            arrangement_recording_.discard();
            changed_locked();
        } else if (command.action_id == "pattern.create.v1" ||
                   command.action_id == "pattern.duplicate.v1" ||
                   command.action_id == "pattern.rename.v1" ||
                   command.action_id == "pattern.length-set.v1" ||
                   command.action_id == "pattern.clear.v1" ||
                   command.action_id == "pattern.delete.v1" ||
                   command.action_id == "pattern.launch.v1" ||
                   command.action_id == "pattern.chain-entry-set.v1" ||
                   command.action_id == "pattern.chain-play.v1" ||
                   command.action_id == "pattern.chain-stop.v1" ||
                   command.action_id == "pattern.chain-reset.v1" ||
                   command.action_id == "pattern.follow-set.v1" ||
                   command.action_id == "pattern.launch-quantization.v1" ||
                   command.action_id == "pattern.voice-transition.v1" ||
                   command.action_id == "arrangement.mode-set.v1" ||
                   command.action_id == "arrangement.song-end.v1" ||
                   command.action_id == "arrangement.return.v1" ||
                   command.action_id == "arrangement.clip-create.v1" ||
                   command.action_id == "arrangement.clip-update.v1" ||
                   command.action_id == "arrangement.clip-duplicate.v1" ||
                   command.action_id == "arrangement.clip-delete.v1" ||
                   command.action_id == "arrangement.audio-clip-create.v1" ||
                   command.action_id == "arrangement.audio-clip-update.v1" ||
                   command.action_id == "arrangement.audio-clip-duplicate.v1" ||
                   command.action_id == "arrangement.audio-clip-delete.v1" ||
                   command.action_id == "arrangement.audio-clip-fade-set.v1" ||
                   command.action_id == "arrangement.audio-crossfade-create.v1" ||
                   command.action_id == "arrangement.audio-crossfade-update.v1" ||
                   command.action_id == "arrangement.audio-crossfade-delete.v1" ||
                   command.action_id == "arrangement.range-set.v1" ||
                   command.action_id == "arrangement.loop-set.v1" ||
                   command.action_id == "arrangement.automation-update.v1" ||
                   command.action_id == "arrangement.automation-delete.v1") {
            auto transaction = timeline_.edit();
            TimelineService::Publish scheduled_publisher;
            if (command.action_id == "pattern.create.v1") {
                const auto requested = argument<std::int64_t>(command, "id");
                if (requested && *requested <= 0)
                    error = "pattern ID must be positive";
                const auto id = requested ? static_cast<PatternId>(*requested)
                                          : transaction.next_pattern_id();
                if (error.empty())
                    transaction.create_pattern(
                        id, *argument<std::string>(command, "name"),
                        *argument<std::int64_t>(command, "length"), error);
            } else if (command.action_id == "pattern.duplicate.v1") {
                const auto requested = argument<std::int64_t>(command, "id");
                const auto source = *argument<std::int64_t>(command, "source");
                if (source <= 0 || (requested && *requested <= 0))
                    error = "pattern IDs must be positive";
                if (error.empty())
                    transaction.duplicate_pattern(
                        static_cast<PatternId>(source),
                        requested ? static_cast<PatternId>(*requested)
                                  : transaction.next_pattern_id(),
                        *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "pattern.rename.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "pattern ID must be positive";
                else
                    transaction.rename_pattern(
                        static_cast<PatternId>(id),
                        *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "pattern.length-set.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                const auto length = *argument<std::int64_t>(command, "length");
                if (id <= 0)
                    error = "pattern ID must be positive";
                else
                    transaction.set_pattern_length(
                        static_cast<PatternId>(id), length, error);
            } else if (command.action_id == "pattern.clear.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                const auto requested_track =
                    argument<std::int64_t>(command, "track_id");
                const auto track = requested_track
                    ? static_cast<TrackId>(*requested_track)
                    : state_.selected_track;
                if (id <= 0 || (requested_track && *requested_track <= 0) ||
                    track == 0)
                    error = "pattern and track IDs must be positive";
                else if (auto *pattern = transaction.find_edit_pattern(
                             static_cast<PatternId>(id), track))
                    pattern->events.clear();
                else
                    error = "pattern track does not exist";
            } else if (command.action_id == "pattern.delete.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "pattern ID must be positive";
                else
                    transaction.delete_pattern(static_cast<PatternId>(id),
                                               error);
            } else if (command.action_id == "pattern.launch.v1") {
                const auto raw_id = *argument<std::int64_t>(command, "id");
                const auto id = static_cast<PatternId>(raw_id);
                const auto queued = argument<bool>(command, "queued");
                const bool use_queue = queued ? *queued : true;
                if (raw_id <= 0)
                    error = "pattern ID must be positive";
                else
                    transaction.launch_pattern(id, use_queue, error);
                if (error.empty() && use_queue) {
                    const auto &arrangement =
                        transaction.timeline().arrangement;
                    const auto found =
                        std::find_if(arrangement.patterns.begin(),
                                     arrangement.patterns.end(),
                                     [=](const auto &p) { return p.id == id; });
                    queued_launch_tick_ = launch_boundary(
                        state_.transport_tick, arrangement.launch_quantization,
                        found->length, transaction.timeline().transport);
                    const auto frame =
                        (static_cast<long double>(*queued_launch_tick_) *
                         engine_.sample_rate() *
                         transaction.timeline()
                             .transport.microseconds_per_quarter) /
                        (static_cast<long double>(ticks_per_quarter) *
                         1'000'000.0L);
                    queued_launch_frame_ =
                        static_cast<std::uint64_t>(std::llround(frame));
                    const auto policy = arrangement.voice_transition;
                    scheduled_publisher = [this, id, policy,
                                           activation = *queued_launch_frame_](
                                              const Timeline &queued_timeline,
                                              std::string &publish_error) {
                        auto audible = queued_timeline;
                        audible.arrangement.active_pattern = id;
                        audible.arrangement.queued_pattern = 0;
                        audible.arrangement.playback_mode =
                            TimelinePlaybackMode::live_pattern;
                        return engine_.queue_timeline_at(audible, activation,
                                                         policy, publish_error);
                    };
                } else if (error.empty()) {
                    queued_launch_tick_.reset();
                    queued_launch_frame_.reset();
                }
            } else if (command.action_id == "arrangement.mode-set.v1") {
                const auto &mode = *argument<std::string>(command, "mode");
                if (mode == "legacy")
                    transaction.set_playback_mode(TimelinePlaybackMode::legacy,
                                                  error);
                else if (mode == "live")
                    transaction.set_playback_mode(
                        TimelinePlaybackMode::live_pattern, error);
                else if (mode == "song")
                    transaction.set_playback_mode(TimelinePlaybackMode::song,
                                                  error);
                else
                    error = "unknown arrangement playback mode";
            } else if (command.action_id == "pattern.chain-entry-set.v1") {
                const auto index = *argument<std::int64_t>(command, "index");
                const auto pattern =
                    *argument<std::int64_t>(command, "pattern");
                const auto repeats =
                    *argument<std::int64_t>(command, "repeats");
                auto entries = transaction.timeline().arrangement.chain_entries;
                if (index < 0 ||
                    static_cast<std::size_t>(index) > entries.size() ||
                    pattern <= 0 || repeats <= 0 || repeats > 65535)
                    error = "invalid pattern chain entry";
                else {
                    const PatternChainEntry entry{
                        static_cast<PatternId>(pattern),
                        static_cast<std::uint16_t>(repeats)};
                    if (static_cast<std::size_t>(index) == entries.size())
                        entries.push_back(entry);
                    else
                        entries[static_cast<std::size_t>(index)] = entry;
                    transaction.set_pattern_chain(
                        transaction.timeline().arrangement.pattern_chain_id,
                        std::move(entries),
                        transaction.timeline().arrangement.chain_loop, error);
                }
            } else if (command.action_id == "pattern.chain-play.v1") {
                const auto entry = argument<std::int64_t>(command, "entry");
                const auto loop = argument<bool>(command, "loop");
                if (loop) {
                    auto entries =
                        transaction.timeline().arrangement.chain_entries;
                    transaction.set_pattern_chain(
                        transaction.timeline().arrangement.pattern_chain_id,
                        std::move(entries), *loop, error);
                }
                if (error.empty())
                    transaction.set_chain_running(
                        true, entry ? static_cast<std::size_t>(*entry) : 0,
                        error);
            } else if (command.action_id == "pattern.chain-stop.v1") {
                transaction.set_chain_running(
                    false,
                    transaction.timeline().arrangement.pattern_chain_position,
                    error);
            } else if (command.action_id == "pattern.chain-reset.v1") {
                transaction.set_chain_running(false, 0, error);
            } else if (command.action_id == "pattern.follow-set.v1") {
                PatternFollowAction follow;
                const auto &action = *argument<std::string>(command, "action");
                if (action == "continue")
                    follow.type = FollowActionType::continue_current;
                else if (action == "next")
                    follow.type = FollowActionType::next;
                else if (action == "previous")
                    follow.type = FollowActionType::previous;
                else if (action == "specific")
                    follow.type = FollowActionType::specific;
                else if (action == "random")
                    follow.type = FollowActionType::random_eligible;
                else if (action == "stop")
                    follow.type = FollowActionType::stop;
                else
                    error = "unknown pattern follow action";
                const auto repetitions =
                    *argument<std::int64_t>(command, "repetitions");
                const auto probability =
                    argument<double>(command, "probability");
                const auto target = argument<std::int64_t>(command, "target");
                if (repetitions <= 0 || repetitions > 65535)
                    error = "follow repetitions must be 1..65535";
                else {
                    follow.after_repetitions =
                        static_cast<std::uint16_t>(repetitions);
                    follow.probability =
                        probability ? static_cast<float>(*probability) : 1.0F;
                    follow.target =
                        target ? static_cast<PatternId>(*target) : 0;
                }
                if (error.empty())
                    transaction.set_follow_action(
                        static_cast<PatternId>(
                            *argument<std::int64_t>(command, "pattern")),
                        follow, error);
            } else if (command.action_id == "pattern.launch-quantization.v1") {
                const auto &value =
                    *argument<std::string>(command, "quantization");
                if (value == "immediate")
                    transaction.set_launch_quantization(
                        LaunchQuantization::immediate, error);
                else if (value == "quarter")
                    transaction.set_launch_quantization(
                        LaunchQuantization::quarter, error);
                else if (value == "bar")
                    transaction.set_launch_quantization(LaunchQuantization::bar,
                                                        error);
                else if (value == "two-bars")
                    transaction.set_launch_quantization(
                        LaunchQuantization::two_bars, error);
                else if (value == "pattern-end")
                    transaction.set_launch_quantization(
                        LaunchQuantization::pattern_end, error);
                else
                    error = "unknown launch quantization";
            } else if (command.action_id == "pattern.voice-transition.v1") {
                const auto &value = *argument<std::string>(command, "policy");
                if (value == "preserve")
                    transaction.set_voice_transition(
                        VoiceTransitionPolicy::preserve, error);
                else if (value == "release")
                    transaction.set_voice_transition(
                        VoiceTransitionPolicy::release_outgoing, error);
                else if (value == "stop")
                    transaction.set_voice_transition(
                        VoiceTransitionPolicy::stop_outgoing, error);
                else
                    error = "unknown voice transition policy";
            } else if (command.action_id == "arrangement.song-end.v1") {
                const auto &value = *argument<std::string>(command, "behavior");
                const auto pattern = argument<std::int64_t>(command, "pattern");
                auto behavior = SongEndBehavior::loop;
                if (value == "stop")
                    behavior = SongEndBehavior::stop;
                else if (value == "return-live")
                    behavior = SongEndBehavior::return_to_live;
                else if (value != "loop")
                    error = "unknown song-end behavior";
                if (error.empty())
                    transaction.set_song_end_behavior(
                        behavior,
                        pattern ? static_cast<PatternId>(*pattern) : 0, error);
            } else if (command.action_id == "arrangement.return.v1") {
                transaction.set_playback_mode(TimelinePlaybackMode::song,
                                              error);
            } else if (command.action_id == "arrangement.clip-create.v1") {
                ArrangementClip clip;
                const auto requested = argument<std::int64_t>(command, "id");
                const auto pattern =
                    *argument<std::int64_t>(command, "pattern");
                if ((requested && *requested <= 0) || pattern <= 0)
                    error = "clip and pattern IDs must be positive";
                clip.id = requested ? static_cast<ClipId>(*requested)
                                    : transaction.next_clip_id();
                clip.pattern = static_cast<PatternId>(pattern);
                clip.start = *argument<std::int64_t>(command, "start");
                clip.duration = *argument<std::int64_t>(command, "duration");
                if (const auto value = argument<bool>(command, "loop"))
                    clip.loop = *value;
                if (const auto value =
                        argument<std::int64_t>(command, "transpose")) {
                    if (*value < -127 || *value > 127)
                        error = "clip transpose must be -127..127";
                    else
                        clip.transpose = static_cast<std::int8_t>(*value);
                }
                if (const auto value = argument<double>(command, "velocity"))
                    clip.velocity_scale = static_cast<float>(*value);
                if (error.empty())
                    transaction.create_clip(clip, error);
            } else if (command.action_id == "arrangement.clip-update.v1") {
                const auto raw_id = *argument<std::int64_t>(command, "id");
                const auto id = static_cast<ClipId>(raw_id);
                const auto found = std::find_if(
                    transaction.timeline().arrangement.clips.begin(),
                    transaction.timeline().arrangement.clips.end(),
                    [=](const auto &clip) { return clip.id == id; });
                if (raw_id <= 0)
                    error = "clip ID must be positive";
                else if (found ==
                         transaction.timeline().arrangement.clips.end())
                    error = "clip does not exist";
                else {
                    auto clip = *found;
                    if (const auto value =
                            argument<std::int64_t>(command, "start"))
                        clip.start = *value;
                    if (const auto value =
                            argument<std::int64_t>(command, "duration"))
                        clip.duration = *value;
                    if (const auto value =
                            argument<std::int64_t>(command, "transpose")) {
                        if (*value < -127 || *value > 127)
                            error = "clip transpose must be -127..127";
                        else
                            clip.transpose = static_cast<std::int8_t>(*value);
                    }
                    if (const auto value =
                            argument<double>(command, "velocity"))
                        clip.velocity_scale = static_cast<float>(*value);
                    if (const auto value = argument<bool>(command, "enabled"))
                        clip.enabled = *value;
                    if (error.empty())
                        transaction.update_clip(clip, error);
                }
            } else if (command.action_id == "arrangement.clip-delete.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "clip ID must be positive";
                else
                    transaction.delete_clip(static_cast<ClipId>(id), error);
            } else if (command.action_id ==
                       "arrangement.clip-duplicate.v1") {
                const auto source = *argument<std::int64_t>(command, "source");
                const auto requested = argument<std::int64_t>(command, "id");
                const auto found = std::find_if(
                    transaction.timeline().arrangement.clips.begin(),
                    transaction.timeline().arrangement.clips.end(),
                    [&](const auto &clip) {
                        return clip.id == static_cast<ClipId>(source);
                    });
                if (source <= 0 || (requested && *requested <= 0) ||
                    found == transaction.timeline().arrangement.clips.end())
                    error = "clip duplication source or ID is invalid";
                else {
                    auto clip = *found;
                    clip.id = requested ? static_cast<ClipId>(*requested)
                                        : transaction.next_clip_id();
                    if (const auto start =
                            argument<std::int64_t>(command, "start"))
                        clip.start = *start;
                    else
                        clip.start += clip.duration;
                    transaction.create_clip(std::move(clip), error);
                }
            } else if (command.action_id ==
                       "arrangement.audio-clip-create.v1") {
                AudioClip clip;
                const auto requested = argument<std::int64_t>(command, "id");
                const auto track = *argument<std::int64_t>(command, "track");
                const auto slot =
                    *argument<std::int64_t>(command, "sample_slot");
                if ((requested && *requested <= 0) || track <= 0 || slot < 0 ||
                    slot >= static_cast<std::int64_t>(engine_.sample_slot_count()))
                    error = "audio clip IDs, track, or sample slot are invalid";
                const auto sample_status =
                    error.empty()
                        ? samples_.status(SampleSlotId{
                              static_cast<std::uint32_t>(slot)})
                        : SampleSlotStatus{};
                if (error.empty() && !sample_status.has_sample)
                    error = "audio clip source sample is not active";
                clip.id = requested ? static_cast<AudioClipId>(*requested)
                                    : transaction.next_audio_clip_id();
                clip.track = static_cast<TrackId>(track);
                clip.sample_slot = static_cast<std::uint32_t>(slot);
                clip.sample_publication = sample_status.active_publication;
                clip.asset_reference = sample_status.source_path.string();
                clip.start = *argument<std::int64_t>(command, "start");
                clip.duration = *argument<std::int64_t>(command, "duration");
                clip.source_start_frame = 0;
                clip.source_end_frame = sample_status.frames;
                if (const auto value = argument<std::int64_t>(command,
                                                              "source_start"))
                    clip.source_start_frame = static_cast<std::uint64_t>(*value);
                if (const auto value = argument<std::int64_t>(command,
                                                              "source_end"))
                    clip.source_end_frame = static_cast<std::uint64_t>(*value);
                if (const auto value = argument<std::int64_t>(command,
                                                              "region_id")) {
                    const auto snapshot = slices_.snapshot(
                        SampleSlotId{clip.sample_slot});
                    const auto region = std::find_if(
                        snapshot.regions.begin(), snapshot.regions.end(),
                        [&](const auto &candidate) {
                            return candidate.id ==
                                   static_cast<RegionId>(*value);
                        });
                    if (region == snapshot.regions.end())
                        error = "audio clip region does not exist in source slot";
                    else {
                        clip.region_id = region->id;
                        clip.source_start_frame = region->start_frame;
                        clip.source_end_frame = region->end_frame;
                        clip.loop_start_frame = region->loop_start_frame;
                        clip.loop_end_frame = region->loop_end_frame;
                        clip.fade_in_frames = region->fade_in_frames;
                        clip.fade_out_frames = region->fade_out_frames;
                        clip.loop_crossfade_frames =
                            region->loop_crossfade_frames;
                        clip.fade_curve =
                            region->fade_curve == RegionFadeCurve::equal_power
                                ? AudioClipFadeCurve::equal_power
                                : AudioClipFadeCurve::linear;
                        clip.loop = region->loop_enabled;
                        clip.gain = region->gain;
                        clip.direction =
                            region->direction == RegionDirection::reverse
                                ? AudioClipDirection::reverse
                                : AudioClipDirection::forward;
                    }
                }
                if (const auto value = argument<bool>(command, "loop"))
                    clip.loop = *value;
                if (const auto value = argument<std::int64_t>(command,
                                                              "loop_start"))
                    clip.loop_start_frame = static_cast<std::uint64_t>(*value);
                if (const auto value = argument<std::int64_t>(command,
                                                              "loop_end"))
                    clip.loop_end_frame = static_cast<std::uint64_t>(*value);
                if (clip.loop && clip.loop_end_frame == 0) {
                    clip.loop_start_frame = clip.source_start_frame;
                    clip.loop_end_frame = clip.source_end_frame;
                }
                if (const auto value = argument<double>(command, "gain"))
                    clip.gain = static_cast<float>(*value);
                if (const auto value = argument<double>(command, "pan"))
                    clip.pan = static_cast<float>(*value);
                if (const auto value = argument<double>(command, "source_bpm"))
                    clip.source_bpm = static_cast<float>(*value);
                if (const auto value = argument<double>(command, "pitch"))
                    clip.pitch_semitones = static_cast<float>(*value);
                if (const auto value = argument<std::string>(command,
                                                             "direction")) {
                    if (*value == "reverse")
                        clip.direction = AudioClipDirection::reverse;
                    else if (*value != "forward")
                        error = "audio clip direction must be forward or reverse";
                }
                if (const auto value = argument<std::string>(command,
                                                             "stretch")) {
                    if (*value == "tempo-sync")
                        clip.stretch_mode = AudioClipStretchMode::tempo_sync;
                    else if (*value != "native")
                        error = "audio clip stretch must be native or tempo-sync";
                }
                if (error.empty() &&
                    (clip.source_start_frame >= clip.source_end_frame ||
                     clip.source_end_frame > sample_status.frames))
                    error = "audio clip source range exceeds the active sample";
                if (error.empty())
                    transaction.create_audio_clip(std::move(clip), error);
            } else if (command.action_id ==
                       "arrangement.audio-clip-update.v1") {
                const auto raw_id = *argument<std::int64_t>(command, "id");
                auto found = std::find_if(
                    transaction.timeline().arrangement.audio_clips.begin(),
                    transaction.timeline().arrangement.audio_clips.end(),
                    [&](const auto &clip) {
                        return clip.id == static_cast<AudioClipId>(raw_id);
                    });
                if (raw_id <= 0 || found ==
                                       transaction.timeline()
                                           .arrangement.audio_clips.end())
                    error = "audio clip does not exist";
                else {
                    auto clip = *found;
                    if (const auto value =
                            argument<std::int64_t>(command, "track"))
                        clip.track = static_cast<TrackId>(*value);
                    if (const auto value =
                            argument<std::int64_t>(command, "sample_slot")) {
                        if (*value < 0 ||
                            *value >= static_cast<std::int64_t>(
                                          engine_.sample_slot_count()))
                            error = "audio clip sample slot is invalid";
                        else {
                            const auto status = samples_.status(SampleSlotId{
                                static_cast<std::uint32_t>(*value)});
                            if (!status.has_sample)
                                error = "audio clip source sample is not active";
                            else {
                                clip.sample_slot =
                                    static_cast<std::uint32_t>(*value);
                                clip.sample_publication = status.active_publication;
                                clip.asset_reference = status.source_path.string();
                                clip.region_id = 0;
                                clip.source_start_frame = 0;
                                clip.source_end_frame = status.frames;
                            }
                        }
                    }
                    if (const auto value =
                            argument<std::int64_t>(command, "region_id")) {
                        const auto snapshot =
                            slices_.snapshot(SampleSlotId{clip.sample_slot});
                        const auto region = std::find_if(
                            snapshot.regions.begin(), snapshot.regions.end(),
                            [&](const auto &candidate) {
                                return candidate.id ==
                                       static_cast<RegionId>(*value);
                            });
                        if (*value <= 0 || region == snapshot.regions.end())
                            error = "audio clip region does not exist in source slot";
                        else {
                            clip.region_id = region->id;
                            clip.source_start_frame = region->start_frame;
                            clip.source_end_frame = region->end_frame;
                            clip.fade_in_frames = region->fade_in_frames;
                            clip.fade_out_frames = region->fade_out_frames;
                            clip.loop_start_frame = region->loop_start_frame;
                            clip.loop_end_frame = region->loop_end_frame;
                            clip.loop_crossfade_frames =
                                region->loop_crossfade_frames;
                            clip.loop = region->loop_enabled;
                            clip.gain = region->gain;
                            clip.direction =
                                region->direction == RegionDirection::reverse
                                    ? AudioClipDirection::reverse
                                    : AudioClipDirection::forward;
                            clip.fade_curve =
                                region->fade_curve == RegionFadeCurve::equal_power
                                    ? AudioClipFadeCurve::equal_power
                                    : AudioClipFadeCurve::linear;
                        }
                    }
                    if (const auto value = argument<std::int64_t>(
                            command, "source_start"))
                        clip.source_start_frame =
                            static_cast<std::uint64_t>(*value);
                    if (const auto value = argument<std::int64_t>(
                            command, "source_end"))
                        clip.source_end_frame =
                            static_cast<std::uint64_t>(*value);
                    if (const auto value = argument<std::int64_t>(
                            command, "loop_start"))
                        clip.loop_start_frame =
                            static_cast<std::uint64_t>(*value);
                    if (const auto value = argument<std::int64_t>(
                            command, "loop_end"))
                        clip.loop_end_frame =
                            static_cast<std::uint64_t>(*value);
                    if (const auto value = argument<std::int64_t>(command, "start"))
                        clip.start = *value;
                    if (const auto value = argument<std::int64_t>(command, "duration"))
                        clip.duration = *value;
                    if (const auto value = argument<double>(command, "gain"))
                        clip.gain = static_cast<float>(*value);
                    if (const auto value = argument<double>(command, "pan"))
                        clip.pan = static_cast<float>(*value);
                    if (const auto value = argument<double>(command, "source_bpm"))
                        clip.source_bpm = static_cast<float>(*value);
                    if (const auto value = argument<double>(command, "pitch"))
                        clip.pitch_semitones = static_cast<float>(*value);
                    if (const auto value = argument<bool>(command, "loop"))
                        clip.loop = *value;
                    if (const auto value = argument<bool>(command, "enabled"))
                        clip.enabled = *value;
                    if (const auto value = argument<std::string>(command, "direction")) {
                        if (*value == "forward") clip.direction = AudioClipDirection::forward;
                        else if (*value == "reverse") clip.direction = AudioClipDirection::reverse;
                        else error = "audio clip direction must be forward or reverse";
                    }
                    if (const auto value = argument<std::string>(command, "stretch")) {
                        if (*value == "native") clip.stretch_mode = AudioClipStretchMode::native;
                        else if (*value == "tempo-sync") clip.stretch_mode = AudioClipStretchMode::tempo_sync;
                        else error = "audio clip stretch must be native or tempo-sync";
                    }
                    const auto source =
                        samples_.status(SampleSlotId{clip.sample_slot});
                    if (error.empty() &&
                        (!source.has_sample ||
                         clip.source_start_frame >= clip.source_end_frame ||
                         clip.source_end_frame > source.frames))
                        error = "audio clip source range exceeds the active sample";
                    if (error.empty())
                        transaction.update_audio_clip(std::move(clip), error);
                }
            } else if (command.action_id ==
                       "arrangement.audio-clip-duplicate.v1") {
                const auto source =
                    *argument<std::int64_t>(command, "source");
                const auto requested = argument<std::int64_t>(command, "id");
                const auto found = std::find_if(
                    transaction.timeline().arrangement.audio_clips.begin(),
                    transaction.timeline().arrangement.audio_clips.end(),
                    [&](const auto &clip) {
                        return clip.id == static_cast<AudioClipId>(source);
                    });
                if (source <= 0 || (requested && *requested <= 0) ||
                    found == transaction.timeline().arrangement.audio_clips.end())
                    error = "audio clip duplication source or ID is invalid";
                else {
                    auto clip = *found;
                    clip.id = requested ? static_cast<AudioClipId>(*requested)
                                        : transaction.next_audio_clip_id();
                    if (const auto start =
                            argument<std::int64_t>(command, "start"))
                        clip.start = *start;
                    else
                        clip.start += clip.duration;
                    transaction.create_audio_clip(std::move(clip), error);
                }
            } else if (command.action_id ==
                       "arrangement.audio-clip-delete.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "audio clip ID must be positive";
                else
                    transaction.delete_audio_clip(
                        static_cast<AudioClipId>(id), error);
            } else if (command.action_id == "arrangement.audio-clip-fade-set.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                auto found = std::find_if(transaction.timeline().arrangement.audio_clips.begin(),
                    transaction.timeline().arrangement.audio_clips.end(),
                    [=](const auto &clip) { return clip.id == static_cast<AudioClipId>(id); });
                if (id <= 0 || found == transaction.timeline().arrangement.audio_clips.end())
                    error = "audio clip does not exist";
                else {
                    auto clip = *found;
                    clip.clip_fade_in = *argument<std::int64_t>(command, "fade_in");
                    clip.clip_fade_out = *argument<std::int64_t>(command, "fade_out");
                    const auto curve = *argument<std::string>(command, "curve");
                    if (curve == "linear") clip.clip_fade_curve = AudioClipFadeCurve::linear;
                    else if (curve == "equal-power") clip.clip_fade_curve = AudioClipFadeCurve::equal_power;
                    else error = "fade curve must be linear or equal-power";
                    if (error.empty()) transaction.update_audio_clip(clip, error);
                }
            } else if (command.action_id == "arrangement.audio-crossfade-create.v1") {
                AudioClipCrossfade crossfade;
                const auto requested = argument<std::int64_t>(command, "id");
                crossfade.id = requested ? static_cast<AudioCrossfadeId>(*requested)
                                         : transaction.next_audio_crossfade_id();
                crossfade.outgoing_clip = static_cast<AudioClipId>(*argument<std::int64_t>(command, "outgoing"));
                crossfade.incoming_clip = static_cast<AudioClipId>(*argument<std::int64_t>(command, "incoming"));
                crossfade.duration = *argument<std::int64_t>(command, "duration");
                const auto curve = *argument<std::string>(command, "curve");
                if (curve == "linear") crossfade.curve = AudioClipFadeCurve::linear;
                else if (curve == "equal-power") crossfade.curve = AudioClipFadeCurve::equal_power;
                else error = "crossfade curve must be linear or equal-power";
                if (error.empty()) transaction.create_audio_crossfade(crossfade, error);
            } else if (command.action_id == "arrangement.audio-crossfade-update.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                auto found = std::find_if(transaction.timeline().arrangement.audio_crossfades.begin(),
                    transaction.timeline().arrangement.audio_crossfades.end(),
                    [=](const auto &value) { return value.id == static_cast<AudioCrossfadeId>(id); });
                if (id <= 0 || found == transaction.timeline().arrangement.audio_crossfades.end())
                    error = "audio crossfade does not exist";
                else {
                    auto crossfade = *found;
                    if (const auto value = argument<std::int64_t>(command, "duration")) crossfade.duration = *value;
                    if (const auto value = argument<bool>(command, "enabled")) crossfade.enabled = *value;
                    if (const auto value = argument<std::string>(command, "curve")) {
                        if (*value == "linear") crossfade.curve = AudioClipFadeCurve::linear;
                        else if (*value == "equal-power") crossfade.curve = AudioClipFadeCurve::equal_power;
                        else error = "crossfade curve must be linear or equal-power";
                    }
                    if (error.empty()) transaction.update_audio_crossfade(crossfade, error);
                }
            } else if (command.action_id == "arrangement.audio-crossfade-delete.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0) error = "audio crossfade ID must be positive";
                else transaction.delete_audio_crossfade(static_cast<AudioCrossfadeId>(id), error);
            } else if (command.action_id == "arrangement.range-set.v1") {
                transaction.set_song_range(
                    *argument<std::int64_t>(command, "start"),
                    *argument<std::int64_t>(command, "end"), error);
            } else if (command.action_id == "arrangement.loop-set.v1") {
                transaction.set_loop_region(
                    *argument<bool>(command, "enabled"),
                    *argument<std::int64_t>(command, "start"),
                    *argument<std::int64_t>(command, "end"), error);
            } else if (command.action_id ==
                       "arrangement.automation-update.v1") {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "automation event ID must be positive";
                else
                    transaction.update_arrangement_automation(
                        static_cast<EventId>(id),
                        *argument<std::int64_t>(command, "tick"),
                        static_cast<float>(*argument<double>(command, "value")),
                        error);
            } else {
                const auto id = *argument<std::int64_t>(command, "id");
                if (id <= 0)
                    error = "automation event ID must be positive";
                else
                    transaction.delete_arrangement_automation(
                        static_cast<EventId>(id), error);
            }
            const auto committed =
                error.empty() &&
                (scheduled_publisher
                     ? timeline_.commit_with(
                           std::move(transaction),
                           TimelineCommitPolicy::next_audio_callback,
                           std::move(scheduled_publisher), error)
                     : timeline_.commit(
                           std::move(transaction),
                           TimelineCommitPolicy::next_audio_callback, error));
            if (committed) {
                if (command.action_id == "pattern.launch.v1") {
                    pending_record_pattern_ = static_cast<PatternId>(
                        *argument<std::int64_t>(command, "id"));
                    pending_record_frame_ =
                        queued_launch_frame_.value_or(engine_.frame_position());
                }
                if (command.action_id == "pattern.chain-play.v1") {
                    pending_record_chain_ = true;
                    pending_record_frame_ =
                        queued_launch_frame_.value_or(engine_.frame_position());
                }
                state_.project_dirty = true;
                changed_locked();
            }
        } else if (command.action_id == "step.select.v1") {
            const auto step = *argument<std::int64_t>(command, "step");
            if (step < 0)
                error = "step must be nonnegative";
            else {
                state_.selected_step = step;
                state_.step_page = static_cast<std::uint64_t>(step) / 16;
                selected_event_id_.reset();
                changed_locked();
            }
        } else if (command.action_id == "step.event-select.v1") {
            const auto step_arg = argument<std::int64_t>(command, "step");
            const auto event_arg = argument<std::int64_t>(command, "event_id");
            const auto step = step_arg ? *step_arg : state_.selected_step;
            const auto snapshot = timeline_.snapshot();
            const auto track = std::find_if(
                snapshot.timeline.tracks.begin(),
                snapshot.timeline.tracks.end(), [&](const Track &value) {
                    return value.id == state_.selected_track;
                });
            const auto *pattern = edit_pattern(
                snapshot.timeline, state_.selected_pattern,
                state_.selected_track);
            const auto pattern_length = edit_pattern_length(
                snapshot.timeline, state_.selected_pattern,
                state_.selected_track);
            if (step < 0 || track == snapshot.timeline.tracks.end())
                error = step < 0 ? "step must be nonnegative"
                                 : "selected track does not exist";
            else {
                const auto begin = step * state_.step_ticks;
                const auto end = std::min<MusicalTick>(
                    begin + state_.step_ticks, pattern_length);
                const MusicalEvent *selected = nullptr;
                if (pattern)
                for (const auto &event : pattern->events)
                    if (event.type == MusicalEventType::note &&
                        event.tick >= begin && event.tick < end &&
                        (!event_arg ||
                         (*event_arg > 0 &&
                          event.id == static_cast<EventId>(*event_arg)))) {
                        if (!selected || event.id < selected->id)
                            selected = &event;
                    }
                if (!selected)
                    error = "no selectable note event at step";
                else {
                    state_.selected_step = step;
                    state_.step_page = static_cast<std::uint64_t>(step) / 16;
                    selected_event_id_ = selected->id;
                    changed_locked();
                }
            }
        } else if (command.action_id == "performance.fill-set.v1" ||
                   command.action_id == "performance.fill-latch.v1") {
            const auto previous_latched = state_.fill_latched;
            const auto previous_momentary = fill_momentary_;
            if (command.action_id == "performance.fill-latch.v1") {
                state_.fill_latched = !state_.fill_latched;
            } else {
                fill_momentary_ = *argument<bool>(command, "active");
            }
            state_.fill_active = fill_momentary_ || state_.fill_latched;
            if (!engine_.set_fill(state_.fill_active)) {
                state_.fill_latched = previous_latched;
                fill_momentary_ = previous_momentary;
                state_.fill_active = fill_momentary_ || state_.fill_latched;
                error = "transport command queue is full";
            } else
                changed_locked();
        } else if (command.action_id == "timeline.seed-set.v1") {
            const auto value = *argument<std::int64_t>(command, "seed");
            if (value < 0)
                error = "generation seed must be nonnegative";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                transaction.set_generation_seed(
                    static_cast<std::uint64_t>(value));
                if (edit_group_)
                    edit_group_ = std::move(transaction);
                else if (!timeline_.commit(
                             std::move(transaction),
                             TimelineCommitPolicy::next_audio_callback,
                             error)) {
                }
                if (error.empty())
                    changed_locked();
            }
        } else if (command.action_id == "step.probability-set.v1" ||
                   command.action_id == "step.condition-set.v1" ||
                   command.action_id == "step.ratchet-set.v1" ||
                   command.action_id == "step.trigger-reset.v1") {
            if (!selected_event_id_)
                error = "no note event is selected";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                auto *track = transaction.find_track(state_.selected_track);
                auto *pattern = transaction.find_edit_pattern(
                    state_.selected_pattern, state_.selected_track);
                MusicalEvent *event = nullptr;
                if (track && pattern)
                    for (auto &candidate : pattern->events)
                        if (candidate.id == *selected_event_id_ &&
                            candidate.type == MusicalEventType::note) {
                            event = &candidate;
                            break;
                        }
                if (!event)
                    error = "selected note event no longer exists";
                else if (command.action_id == "step.probability-set.v1") {
                    const auto value =
                        *argument<double>(command, "probability");
                    if (!std::isfinite(value) || value < 0 || value > 1)
                        error = "trigger probability must be 0..1";
                    else
                        event->trigger_probability = static_cast<float>(value);
                } else if (command.action_id == "step.ratchet-set.v1") {
                    const auto value =
                        *argument<std::int64_t>(command, "count");
                    if (value < 1 || value > 8)
                        error = "ratchet count must be 1..8";
                    else
                        event->ratchet_count = static_cast<std::uint8_t>(value);
                } else if (command.action_id == "step.condition-set.v1") {
                    const auto condition = parse_trigger_condition(
                        *argument<std::string>(command, "condition"));
                    const auto n_arg = argument<std::int64_t>(command, "n");
                    const auto m_arg = argument<std::int64_t>(command, "m");
                    const auto n = n_arg ? *n_arg : 1;
                    const auto m = m_arg ? *m_arg : n;
                    if (!condition)
                        error = "unknown trigger condition";
                    else if (n < 1 || m < 1 || n > m || m > 65535)
                        error = "condition cycles require 1 <= n <= m <= 65535";
                    else {
                        event->trigger_condition = *condition;
                        event->condition_n = static_cast<std::uint16_t>(n);
                        event->condition_m = static_cast<std::uint16_t>(m);
                    }
                } else {
                    event->trigger_probability = 1.0F;
                    event->trigger_condition = TriggerCondition::always;
                    event->condition_n = event->condition_m = 1;
                    event->ratchet_count = 1;
                }
                if (error.empty()) {
                    if (edit_group_)
                        edit_group_ = std::move(transaction);
                    else
                        timeline_.commit(
                            std::move(transaction),
                            TimelineCommitPolicy::next_audio_callback, error);
                    if (error.empty())
                        changed_locked();
                }
            }
        } else if (command.action_id == "step.property-select.v1") {
            const auto &property = *argument<std::string>(command, "property");
            if (property != "velocity" && property != "duration" &&
                property != "pitch" && property != "timing" &&
                property != "probability" && property != "condition" &&
                property != "ratchet")
                error = "unknown step property";
            else {
                state_.selected_step_property = property;
                state_.focused_element = "step-properties";
                changed_locked();
            }
        } else if (command.action_id == "step.lock-select.v1") {
            const auto &id = *argument<std::string>(command, "parameter_id");
            if (!resolve_parameter_destination(id) || !parameters_.get(id))
                error = "unknown parameter lock destination";
            else {
                state_.selected_lock_parameter = id;
                state_.focused_element = "step-locks";
                changed_locked();
            }
        } else if (command.action_id == "step.lock-set.v1" ||
                   command.action_id == "step.lock-remove.v1" ||
                   command.action_id == "step.locks-clear.v1") {
            auto transaction = edit_group_ ? *edit_group_ : timeline_.edit();
            auto *track = transaction.find_track(state_.selected_track);
            auto *pattern = transaction.find_edit_pattern(
                state_.selected_pattern, state_.selected_track);
            const auto pattern_length = pattern
                ? pattern->length
                : edit_pattern_length(transaction.timeline(),
                                      state_.selected_pattern,
                                      state_.selected_track);
            const auto tick = state_.selected_step * state_.step_ticks;
            if (!track || tick < 0 || tick >= pattern_length)
                error = "selected step is outside the track";
            else {
                std::vector<EventId> remove;
                std::string id;
                if (command.action_id != "step.locks-clear.v1")
                    id = *argument<std::string>(command, "parameter_id");
                if (pattern)
                for (const auto &event : pattern->events)
                    if (event.type == MusicalEventType::parameter &&
                        event.tick == tick &&
                        (id.empty() || event.parameter_destination == id))
                        remove.push_back(event.id);
                bool edited = true;
                if (command.action_id == "step.lock-set.v1") {
                    const auto destination = resolve_parameter_destination(id);
                    const auto descriptor = parameters_.get(id);
                    const auto value = *argument<double>(command, "value");
                    if (!destination || !descriptor)
                        error = "unknown parameter lock destination";
                    else if (!std::isfinite(value) ||
                             value < number(descriptor->minimum) ||
                             value > number(descriptor->maximum))
                        error = "parameter lock value is outside its range";
                    else if (destination->target ==
                                 ParameterTarget::subtractive_synth &&
                             track->instrument_id != subtractive_instrument_id)
                        error = "synth parameter lock requires a synth track";
                    else if (!remove.empty())
                        edited = transaction.modify_parameter_destination(
                            state_.selected_pattern, track->id, remove.front(), tick, id,
                            static_cast<float>(value), error);
                    else
                        edited = transaction.add_parameter_destination(
                            state_.selected_pattern, track->id,
                            transaction.next_event_id(), tick, id,
                            static_cast<float>(value), error);
                } else {
                    if (command.action_id == "step.lock-remove.v1" &&
                        remove.empty())
                        error = "parameter lock does not exist";
                    for (const auto event_id : remove)
                        if (!transaction.remove_event(state_.selected_pattern,
                                                      track->id, event_id,
                                                      error)) {
                            edited = false;
                            break;
                        }
                }
                if (error.empty() && edited &&
                    (edit_group_ ||
                     timeline_.commit(transaction,
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))) {
                    if (edit_group_)
                        edit_group_ = std::move(transaction);
                    if (!id.empty())
                        state_.selected_lock_parameter = id;
                    changed_locked();
                }
            }
        } else if (command.action_id == "edit.undo.v1" ||
                   command.action_id == "edit.redo.v1") {
            if (edit_group_)
                error = "finish or cancel the grouped edit first";
            else if (pending_history_restore_)
                error = "a history restoration is awaiting audio activation";
            else {
                const auto undo = command.action_id == "edit.undo.v1";
                auto &source = undo ? history_undo_ : history_redo_;
                auto &destination = undo ? history_redo_ : history_undo_;
                bool ok{};
                if (!source.empty()) {
                    auto entry = std::make_unique<ApplicationHistoryEntry>(
                        source.back());
                    ok = prepare_history_restore(*entry, undo, error);
                    if (ok && !pending_history_restore_) {
                        source.pop_back();
                        if (destination.size() == application_history_capacity)
                            destination.pop_front();
                        destination.push_back(std::move(*entry));
                    }
                } else
                    error = undo ? "nothing to undo" : "nothing to redo";
                if (ok) {
                    selected_event_id_.reset();
                    changed_locked();
                }
            }
        } else if (command.action_id == "edit.group-begin.v1") {
            if (edit_group_)
                error = "a grouped edit is already active";
            else {
                edit_group_ = timeline_.edit();
                state_.edit_group_active = true;
                changed_locked();
            }
        } else if (command.action_id == "edit.group-cancel.v1") {
            if (!edit_group_)
                error = "no grouped edit is active";
            else {
                edit_group_.reset();
                state_.edit_group_active = false;
                changed_locked();
            }
        } else if (command.action_id == "edit.group-commit.v1") {
            if (!edit_group_)
                error = "no grouped edit is active";
            else if (timeline_.commit(*edit_group_,
                                      TimelineCommitPolicy::next_audio_callback,
                                      error)) {
                edit_group_.reset();
                state_.edit_group_active = false;
                changed_locked();
            }
        } else if (command.action_id == "step.velocity-set.v1" ||
                   command.action_id == "step.duration-set.v1" ||
                   command.action_id == "step.pitch-set.v1" ||
                   command.action_id == "step.timing-offset-set.v1" ||
                   command.action_id == "step.property-reset.v1") {
            if (!selected_event_id_)
                error = "no note event is selected";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                auto *track = transaction.find_track(state_.selected_track);
                auto *pattern = transaction.find_edit_pattern(
                    state_.selected_pattern, state_.selected_track);
                MusicalEvent *event = nullptr;
                if (track && pattern)
                    for (auto &candidate : pattern->events)
                        if (candidate.id == *selected_event_id_ &&
                            candidate.type == MusicalEventType::note) {
                            event = &candidate;
                            break;
                        }
                if (!event)
                    error = "selected note event no longer exists";
                else {
                    auto tick = event->tick;
                    auto pitch = event->pitch;
                    auto velocity = event->velocity;
                    auto duration = event->duration;
                    const auto base = state_.selected_step * state_.step_ticks;
                    if (command.action_id == "step.velocity-set.v1") {
                        const auto value =
                            *argument<double>(command, "velocity");
                        if (!std::isfinite(value) || value < 0 || value > 1)
                            error = "step velocity must be 0..1";
                        else
                            velocity = static_cast<float>(value);
                    } else if (command.action_id == "step.duration-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "duration_ticks");
                        if (value <= 0)
                            error = "step duration must be positive";
                        else
                            duration = value;
                    } else if (command.action_id == "step.pitch-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "pitch");
                        if (value < 0 || value > 127)
                            error = "step pitch must be 0..127";
                        else
                            pitch = static_cast<std::uint8_t>(value);
                    } else if (command.action_id ==
                               "step.timing-offset-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "offset_ticks");
                        if (value <= -state_.step_ticks ||
                            value >= state_.step_ticks || base + value < 0 ||
                            base + value >= pattern->length)
                            error = "timing offset must remain within one step "
                                    "and the pattern";
                        else
                            tick = base + value;
                    } else {
                        const auto &property =
                            *argument<std::string>(command, "property");
                        if (property == "velocity")
                            velocity = state_.step_velocity;
                        else if (property == "duration")
                            duration = state_.step_duration;
                        else if (property == "pitch")
                            pitch = track_targets_.contains(track->id)
                                        ? track_targets_[track->id]
                                        : 36;
                        else if (property == "timing")
                            tick = base;
                        else
                            error = "unknown step property";
                    }
                    if (error.empty() && transaction.modify_note(
                                             state_.selected_pattern,
                                             track->id, event->id, tick, pitch,
                                             velocity, duration, error)) {
                        if (edit_group_) {
                            edit_group_ = std::move(transaction);
                            changed_locked();
                        } else if (timeline_.commit(std::move(transaction),
                                                    TimelineCommitPolicy::
                                                        next_audio_callback,
                                                    error))
                            changed_locked();
                    }
                }
            }
        } else if (command.action_id == "step.page-next.v1" ||
                   command.action_id == "step.page-previous.v1" ||
                   command.action_id == "step.page-select.v1") {
            const auto snapshot = timeline_.snapshot();
            const auto requested_pattern =
                argument<std::int64_t>(command, "pattern_id");
            const auto requested_track =
                argument<std::int64_t>(command, "track_id");
            if ((requested_pattern &&
                 static_cast<PatternId>(*requested_pattern) !=
                     state_.selected_pattern) ||
                (requested_track && static_cast<TrackId>(*requested_track) !=
                                        state_.selected_track)) {
                error = "stable step-page target is no longer selected";
                return {false, sequence, before.revision, std::move(error)};
            }
            const auto found = std::find_if(
                snapshot.timeline.tracks.begin(),
                snapshot.timeline.tracks.end(), [&](const Track &track) {
                    return track.id == state_.selected_track;
                });
            const auto length = found == snapshot.timeline.tracks.end()
                ? MusicalTick{}
                : edit_pattern_length(snapshot.timeline,
                                      state_.selected_pattern,
                                      state_.selected_track);
            const auto steps = static_cast<std::uint64_t>(
                (length + state_.step_ticks - 1) / state_.step_ticks);
            if (steps == 0)
                error = "selected track has no editable steps";
            else {
                const auto pages = (steps + 15) / 16;
                if (command.action_id == "step.page-select.v1") {
                    const auto page = *argument<std::int64_t>(command, "page");
                    if (page < 0 || static_cast<std::uint64_t>(page) >= pages)
                        error = "step page is outside the selected track";
                    else
                        state_.step_page = static_cast<std::uint64_t>(page);
                } else if (command.action_id == "step.page-next.v1")
                    state_.step_page = (state_.step_page + 1) % pages;
                else
                    state_.step_page = (state_.step_page + pages - 1) % pages;
                if (error.empty()) {
                    state_.selected_step = static_cast<std::int64_t>(
                        std::min<std::uint64_t>(state_.step_page * 16, steps - 1));
                    selected_event_id_.reset();
                    changed_locked();
                }
            }
        } else if (command.action_id == "step.resolution-set.v1") {
            const auto resolution = parse_step_resolution(
                *argument<std::string>(command, "resolution"));
            if (!resolution)
                error = "unknown step resolution";
            else {
                state_.step_resolution = *resolution;
                state_.step_ticks = step_resolution_ticks(*resolution);
                state_.step_duration = state_.step_ticks;
                state_.step_page = 0;
                state_.selected_step = 0;
                changed_locked();
            }
        } else if (command.action_id == "track.note-target-set.v1") {
            const auto note = *argument<std::int64_t>(command, "note");
            const auto snapshot = timeline_.snapshot();
            if (note < 0 || note > 127)
                error = "track note target must be 0..127";
            else if (std::none_of(snapshot.timeline.tracks.begin(),
                                  snapshot.timeline.tracks.end(),
                                  [&](const Track &track) {
                                      return track.id == state_.selected_track;
                                  }))
                error = "selected track does not exist";
            else {
                track_targets_[state_.selected_track] =
                    static_cast<std::uint8_t>(note);
                changed_locked();
            }
        } else if (command.action_id == "step.erase.v1") {
            const auto pattern_id = static_cast<PatternId>(
                *argument<std::int64_t>(command, "pattern_id"));
            const auto track_id = static_cast<TrackId>(
                *argument<std::int64_t>(command, "track_id"));
            const auto step = *argument<std::int64_t>(command, "step");
            auto transaction = timeline_.edit();
            auto *pattern = transaction.find_edit_pattern(pattern_id, track_id);
            if (!pattern)
                error = "stable pattern-track erase target is unavailable";
            else if (step < 0 ||
                     step > std::numeric_limits<MusicalTick>::max() /
                                state_.step_ticks)
                error = "step is outside its range";
            else {
                const auto tick = step * state_.step_ticks;
                std::vector<EventId> matching;
                for (const auto &event : pattern->events)
                    if (event.tick == tick) matching.push_back(event.id);
                if (matching.empty())
                    error = "step contains no erasable events";
                else {
                    for (const auto id : matching)
                        if (!transaction.remove_event(pattern_id, track_id, id,
                                                      error))
                            break;
                    if (error.empty() && timeline_.commit(
                            std::move(transaction),
                            TimelineCommitPolicy::next_audio_callback, error)) {
                        if (state_.selected_pattern == pattern_id &&
                            state_.selected_track == track_id) {
                            state_.selected_step = step;
                            state_.step_page =
                                static_cast<std::uint64_t>(step) / 16;
                            selected_event_id_.reset();
                        }
                        changed_locked();
                    }
                }
            }
        } else if (command.action_id == "step.toggle.v1") {
            auto transaction = edit_group_ ? *edit_group_ : timeline_.edit();
            auto *track = transaction.find_track(state_.selected_track);
            const auto requested = argument<std::int64_t>(command, "step");
            const auto step = requested ? *requested : state_.selected_step;
            const auto velocity = argument<double>(command, "velocity");
            const auto duration =
                argument<std::int64_t>(command, "duration_ticks");
            if (!track)
                error = "selected track does not exist";
            else if (step < 0 ||
                     step > std::numeric_limits<MusicalTick>::max() /
                                state_.step_ticks)
                error = "step is outside its range";
            else {
                if (!transaction.ensure_pattern_track(
                        state_.selected_pattern, state_.selected_track,
                        error)) {
                    // The stable target disappeared after selection.
                }
                auto *pattern = error.empty()
                    ? transaction.find_edit_pattern(state_.selected_pattern,
                                                    state_.selected_track)
                    : nullptr;
                const auto tick = step * state_.step_ticks;
                const auto count =
                    pattern ? (pattern->length + state_.step_ticks - 1) /
                                  state_.step_ticks
                            : 0;
                if (!error.empty()) {
                } else if (step >= count || tick >= pattern->length)
                    error = "step is outside the selected track";
                else if (velocity && (!std::isfinite(*velocity) ||
                                      *velocity < 0 || *velocity > 1))
                    error = "step velocity must be 0..1";
                else if (duration && *duration <= 0)
                    error = "step duration must be positive";
                else {
                    const auto track_index = static_cast<std::size_t>(
                        track - transaction.timeline().tracks.data());
                    const auto target =
                        track_targets_.contains(track->id)
                            ? track_targets_[track->id]
                            : static_cast<std::uint8_t>(
                                  std::min<std::size_t>(127, 36 + track_index));
                    std::vector<EventId> matching;
                    for (const auto &event : pattern->events)
                        if (event.type == MusicalEventType::note &&
                            event.tick == tick && event.pitch == target)
                            matching.push_back(event.id);
                    bool edited = false;
                    if (!matching.empty()) {
                        edited = true;
                        for (const auto id : matching)
                            if (!transaction.remove_event(
                                    state_.selected_pattern, track->id, id,
                                    error)) {
                                edited = false;
                                break;
                            }
                    } else
                        edited = transaction.add_note(
                            state_.selected_pattern, track->id,
                            transaction.next_event_id(), tick,
                            target,
                            static_cast<float>(velocity ? *velocity
                                                        : state_.step_velocity),
                            duration ? *duration : state_.step_duration, error);
                    if (edited &&
                        (edit_group_ ||
                         timeline_.commit(
                             transaction,
                             TimelineCommitPolicy::next_audio_callback,
                             error))) {
                        if (edit_group_)
                            edit_group_ = std::move(transaction);
                        state_.selected_step = step;
                        state_.step_page =
                            static_cast<std::uint64_t>(step) / 16;
                        selected_event_id_.reset();
                        changed_locked();
                    }
                }
            }
        } else if (command.action_id.rfind("sample-processing.", 0) == 0) {
            const auto job_id = state_.selected_processing_job;
            if (command.action_id == "sample-processing.create.v1") {
                SampleProcessingSource source;
                source.slot = state_.selected_sample_slot;
                const auto kind = *argument<std::string>(command, "source");
                if (kind == "sample") source.kind = SampleProcessingSourceKind::sample;
                else if (kind == "region") {
                    source.kind = SampleProcessingSourceKind::region;
                    source.region = argument<std::int64_t>(command, "source_id")
                        ? static_cast<RegionId>(*argument<std::int64_t>(command, "source_id"))
                        : state_.selected_region;
                } else if (kind == "slice") {
                    source.kind = SampleProcessingSourceKind::slice;
                    source.slice = argument<std::int64_t>(command, "source_id")
                        ? static_cast<SliceId>(*argument<std::int64_t>(command, "source_id"))
                        : state_.selected_slice;
                } else error = "processing source must be sample, region or slice";
                if (const auto frames = argument<std::int64_t>(command, "loop_frames"))
                    source.loop_output_frames = static_cast<std::uint64_t>(
                        std::max<std::int64_t>(0, *frames));
                SampleProcessingJobId created{};
                if (error.empty() && sample_processing_.create(source, created, error))
                    state_.selected_processing_job = created;
            } else if (command.action_id == "sample-processing.operation-add.v1") {
                SampleProcessingOperation operation;
                const auto type = *argument<std::string>(command, "operation");
                const std::array names{
                    std::pair{"trim", SampleProcessingOperationType::trim},
                    std::pair{"normalize-peak", SampleProcessingOperationType::normalize_peak},
                    std::pair{"normalize-rms", SampleProcessingOperationType::normalize_rms},
                    std::pair{"reverse", SampleProcessingOperationType::reverse},
                    std::pair{"gain", SampleProcessingOperationType::gain},
                    std::pair{"fade", SampleProcessingOperationType::fade},
                    std::pair{"insert-silence", SampleProcessingOperationType::insert_silence},
                    std::pair{"resample", SampleProcessingOperationType::resample},
                    std::pair{"channel-convert", SampleProcessingOperationType::channel_convert}};
                const auto matched = std::find_if(names.begin(), names.end(),
                    [&](const auto &v){ return v.first == type; });
                if (matched == names.end()) error = "unknown sample processing operation";
                else operation.type = matched->second;
                if (const auto value = argument<double>(command, "value")) operation.value = *value;
                if (const auto value = argument<std::int64_t>(command, "start_frame")) operation.start_frame = static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "end_frame")) operation.end_frame = static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "frames")) operation.frames = static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "sample_rate")) operation.sample_rate = static_cast<std::uint32_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "channels")) operation.channels = static_cast<std::uint16_t>(std::max<std::int64_t>(0, *value));
                SampleProcessingOperationId created{};
                if (error.empty()) (void)sample_processing_.add_operation(job_id, operation, created, error);
            } else if (command.action_id == "sample-processing.operation-remove.v1") {
                (void)sample_processing_.remove_operation(job_id,
                    static_cast<SampleProcessingOperationId>(*argument<std::int64_t>(command, "operation_id")), error);
            } else if (command.action_id == "sample-processing.operation-move.v1") {
                const auto position = *argument<std::int64_t>(command, "position");
                if (position < 0) error = "operation position must be nonnegative";
                else (void)sample_processing_.move_operation(job_id,
                    static_cast<SampleProcessingOperationId>(*argument<std::int64_t>(command, "operation_id")),
                    static_cast<std::size_t>(position), error);
            } else if (command.action_id == "sample-processing.validate.v1") {
                (void)sample_processing_.validate(job_id, error);
            } else if (command.action_id == "sample-processing.start.v1") {
                (void)sample_processing_.start(job_id, error);
            } else if (command.action_id == "sample-processing.cancel.v1") {
                (void)sample_processing_.cancel(job_id, error);
            } else if (command.action_id == "sample-processing.discard.v1") {
                if (sample_processing_.discard(job_id, error)) state_.selected_processing_job = 0;
            } else if (command.action_id == "sample-processing.save.v1") {
                SampleWavFormat format{};
                const auto value = *argument<std::string>(command, "format");
                if (value == "pcm16") format = SampleWavFormat::pcm16;
                else if (value == "pcm24") format = SampleWavFormat::pcm24;
                else if (value == "float32") format = SampleWavFormat::float32;
                else error = "processing WAV format must be pcm16, pcm24 or float32";
                const auto overwrite_value = argument<bool>(command, "overwrite");
                const auto overwrite = overwrite_value && *overwrite_value;
                if (error.empty()) (void)sample_processing_.save(job_id,
                    *argument<std::string>(command, "path"), format, overwrite, error);
            } else if (command.action_id == "sample-processing.publish.v1") {
                const auto slot = *argument<std::int64_t>(command, "slot_id");
                if (slot < 0) error = "destination slot must be nonnegative";
                else (void)sample_processing_.publish_to_slot(job_id,
                    SampleSlotId{static_cast<std::uint32_t>(slot)}, error);
            }
            state_.sample_processing_jobs = sample_processing_.snapshots();
            if (error.empty()) changed_locked();
        } else if (command.action_id.rfind("sample-analysis.", 0) == 0) {
            if (command.action_id == "sample-analysis.start.v1") {
                SampleAnalysisSettings settings;
                const auto algorithm = *argument<std::string>(command, "algorithm");
                if (algorithm == "transients")
                    settings.algorithm = SampleAnalysisAlgorithm::transients;
                else if (algorithm == "quiet-regions")
                    settings.algorithm = SampleAnalysisAlgorithm::quiet_regions;
                else if (algorithm == "equal-divisions")
                    settings.algorithm = SampleAnalysisAlgorithm::equal_divisions;
                else if (algorithm == "tempo-grid")
                    settings.algorithm = SampleAnalysisAlgorithm::tempo_grid;
                else
                    error = "unknown sample analysis algorithm";
                if (const auto value = argument<double>(command, "sensitivity"))
                    settings.sensitivity = static_cast<float>(*value);
                if (const auto value = argument<std::int64_t>(command, "minimum_frames"))
                    settings.minimum_separation_frames =
                        static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
                if (const auto value = argument<std::int64_t>(command, "divisions"))
                    settings.divisions = static_cast<std::uint32_t>(
                        std::max<std::int64_t>(0, *value));
                if (const auto value = argument<double>(command, "source_bpm"))
                    settings.source_bpm = *value;
                if (const auto value = argument<double>(command, "beats_per_slice"))
                    settings.beats_per_slice = *value;
                SampleAnalysisSessionId created{};
                if (error.empty() && sample_analysis_.start(
                                         state_.selected_sample_slot, settings,
                                         created, error))
                    state_.selected_analysis_session = created;
            } else if (command.action_id == "sample-analysis.cancel.v1") {
                (void)sample_analysis_.cancel(state_.selected_analysis_session,
                                              error);
            } else if (command.action_id == "sample-analysis.discard.v1") {
                if (sample_analysis_.discard(state_.selected_analysis_session,
                                             error))
                    state_.selected_analysis_session = 0;
            } else if (command.action_id == "sample-analysis.candidate-add.v1") {
                const auto frame = *argument<std::int64_t>(command, "frame");
                SampleAnalysisCandidateId created{};
                if (frame < 0)
                    error = "candidate frame must be nonnegative";
                else
                    (void)sample_analysis_.add_candidate(
                        state_.selected_analysis_session,
                        static_cast<std::uint64_t>(frame), created, error);
            } else if (command.action_id == "sample-analysis.candidate-move.v1") {
                const auto candidate = *argument<std::int64_t>(command, "candidate_id");
                const auto frame = *argument<std::int64_t>(command, "frame");
                if (candidate <= 0 || frame < 0)
                    error = "candidate identity/frame is invalid";
                else
                    (void)sample_analysis_.move_candidate(
                        state_.selected_analysis_session,
                        static_cast<SampleAnalysisCandidateId>(candidate),
                        static_cast<std::uint64_t>(frame), error);
            } else if (command.action_id == "sample-analysis.candidate-remove.v1") {
                const auto candidate = *argument<std::int64_t>(command, "candidate_id");
                if (candidate <= 0)
                    error = "candidate identity is invalid";
                else
                    (void)sample_analysis_.remove_candidate(
                        state_.selected_analysis_session,
                        static_cast<SampleAnalysisCandidateId>(candidate), error);
            } else if (command.action_id == "sample-analysis.apply.v1") {
                SampleAnalysisApplyOptions options;
                const auto target = *argument<std::string>(command, "target");
                const auto mode = *argument<std::string>(command, "mode");
                if (target == "slices") options.target = SampleAnalysisApplyTarget::slices;
                else if (target == "regions") options.target = SampleAnalysisApplyTarget::regions;
                else error = "analysis target must be slices or regions";
                if (mode == "replace") options.mode = SampleAnalysisApplyMode::replace;
                else if (mode == "append") options.mode = SampleAnalysisApplyMode::append;
                else error = "analysis apply mode must be replace or append";
                if (const auto value = argument<bool>(command, "assign_pads"))
                    options.assign_pads = *value;
                if (const auto value = argument<std::int64_t>(command, "starting_pad"))
                    options.starting_pad = static_cast<std::size_t>(
                        std::max<std::int64_t>(0, *value));
                if (error.empty())
                    (void)sample_analysis_.apply(
                        state_.selected_analysis_session, options, error);
            }
            state_.sample_analysis_sessions = sample_analysis_.snapshots();
            if (error.empty()) changed_locked();
        } else if (command.action_id.rfind("slice.", 0) == 0 ||
                   command.action_id.rfind("region.", 0) == 0 ||
                   command.action_id == "sample.preview.v1" ||
                   (command.action_id.rfind("pad.", 0) == 0 &&
                    command.action_id != "pad.mode-select.v1")) {
            auto refresh_slices = [&] {
                const auto value =
                    slices_.snapshot(state_.selected_sample_slot);
                state_.slice_revision = value.revision;
                state_.sample_frames = value.sample_frames;
                state_.sample_rate = value.sample_rate;
                state_.sample_channels = value.channels;
                state_.slice_pad_assignments = value.pad_assignments;
                state_.region_pad_assignments = value.region_pad_assignments;
                state_.pad_settings = value.pad_settings;
                state_.selected_pad_settings =
                    value.pad_settings[state_.selected_pad];
                state_.stretch_latency_frames =
                    RealtimeTimeStretcher::latency_frames(
                        {state_.selected_pad_settings.stretch_mode,
                         state_.selected_pad_settings.stretch_quality,
                         state_.selected_pad_settings.stretch_ratio, 1.0F,
                         engine_.sample_rate()});
                state_.waveform_peaks = value.waveform;
                state_.slices.clear();
                for (const auto &slice : value.slices)
                    state_.slices.push_back({slice.id, slice.name,
                                             slice.start_frame, slice.end_frame,
                                             slice.gain, slice.root_note});
                state_.regions.clear();
                for (const auto &region : value.regions)
                    state_.regions.push_back(
                        {region.id,
                         region.name,
                         region.start_frame,
                         region.end_frame,
                         region.direction,
                         region.fade_curve,
                         region.fade_in_frames,
                         region.fade_out_frames,
                         region.gain,
                         region.loop_enabled,
                         region.loop_start_frame,
                         region.loop_end_frame,
                         region.loop_crossfade_frames,
                         slices_.region_usage(region.id)});
                if (state_.selected_slice != 0 &&
                    std::none_of(state_.slices.begin(), state_.slices.end(),
                                 [&](const auto &slice) {
                                     return slice.id == state_.selected_slice;
                                 }))
                    state_.selected_slice = 0;
                if (state_.selected_region != 0 &&
                    std::none_of(state_.regions.begin(), state_.regions.end(),
                                 [&](const auto &region) {
                                     return region.id == state_.selected_region;
                                 }))
                    state_.selected_region = 0;
            };
            if (command.action_id == "slice.select.v1") {
                const auto id = *argument<std::int64_t>(command, "slice_id");
                if (id <= 0 ||
                    std::none_of(state_.slices.begin(), state_.slices.end(),
                                 [&](const auto &slice) {
                                     return slice.id ==
                                            static_cast<SliceId>(id);
                                 }))
                    error = "slice does not exist";
                else
                    state_.selected_slice = static_cast<SliceId>(id);
            } else if (command.action_id == "slice.create.v1") {
                const auto start =
                    *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                std::string name = "Slice";
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    name = std::get<std::string>(found->second);
                SliceId created{};
                if (start < 0 || end < 0 ||
                    !slices_.create(state_.selected_sample_slot,
                                    state_.slice_revision,
                                    static_cast<std::uint64_t>(start),
                                    static_cast<std::uint64_t>(end),
                                    std::move(name), created, error)) {
                    if (error.empty())
                        error = "slice boundaries are invalid";
                } else
                    state_.selected_slice = created;
            } else if (command.action_id == "slice.delete.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (slices_.erase(state_.selected_slice,
                                       state_.slice_revision, error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.bounds-set.v1") {
                const auto start =
                    *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (start < 0 || end < 0)
                    error = "slice boundaries cannot be negative";
                else
                    (void)slices_.set_bounds(
                        state_.selected_slice, state_.slice_revision,
                        static_cast<std::uint64_t>(start),
                        static_cast<std::uint64_t>(end), error);
            } else if (command.action_id == "slice.rename.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else
                    (void)slices_.rename(
                        state_.selected_slice, state_.slice_revision,
                        *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "slice.duplicate.v1") {
                SliceId created{};
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (slices_.duplicate(state_.selected_slice,
                                           state_.slice_revision, created,
                                           error))
                    state_.selected_slice = created;
            } else if (command.action_id == "slice.clear.v1") {
                if (slices_.clear(state_.selected_sample_slot,
                                  state_.slice_revision, error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.equal-divisions.v1") {
                const auto divisions =
                    *argument<std::int64_t>(command, "divisions");
                if (divisions <= 0)
                    error = "division count must be positive";
                else if (slices_.equal_divisions(
                             state_.selected_sample_slot, state_.slice_revision,
                             static_cast<std::size_t>(divisions), error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.detect-transients.v1") {
                const auto separation =
                    *argument<std::int64_t>(command, "minimum_frames");
                if (separation <= 0)
                    error = "minimum separation must be positive";
                else if (slices_.detect_transients(
                             state_.selected_sample_slot, state_.slice_revision,
                             static_cast<float>(
                                 *argument<double>(command, "sensitivity")),
                             static_cast<std::uint64_t>(separation), error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.pad-assign.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (pad < 0 ||
                         !slices_.assign_pad(static_cast<std::size_t>(pad),
                                             state_.selected_slice,
                                             state_.slice_revision, error)) {
                    if (error.empty())
                        error = "pad index is invalid";
                }
            } else if (command.action_id == "slice.pad-clear.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 ||
                    !slices_.clear_pad(static_cast<std::size_t>(pad),
                                       state_.slice_revision, error)) {
                    if (error.empty())
                        error = "pad index is invalid";
                }
            } else if (command.action_id == "slice.preview.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else
                    (void)slices_.preview(state_.selected_slice, 1.0F, error);
            } else if (command.action_id == "sample.preview.v1") {
                (void)slices_.preview_full(state_.selected_sample_slot, 1.0F,
                                           error);
            } else if (command.action_id == "slice.preview-stop.v1") {
                slices_.stop_preview();
            } else if (command.action_id == "region.select.v1") {
                const auto id = *argument<std::int64_t>(command, "region_id");
                if (id <= 0 ||
                    std::none_of(state_.regions.begin(), state_.regions.end(),
                                 [&](const auto &region) {
                                     return region.id ==
                                            static_cast<RegionId>(id);
                                 }))
                    error = "region does not exist";
                else
                    state_.selected_region = static_cast<RegionId>(id);
            } else if (command.action_id == "region.create.v1") {
                const auto start = *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                std::string name = "Region";
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    name = std::get<std::string>(found->second);
                RegionId created{};
                if (start < 0 || end < 0 ||
                    !slices_.create_region(
                        state_.selected_sample_slot, state_.slice_revision,
                        static_cast<std::uint64_t>(start),
                        static_cast<std::uint64_t>(end), std::move(name),
                        created, error)) {
                    if (error.empty()) error = "region boundaries are invalid";
                } else
                    state_.selected_region = created;
            } else if (command.action_id == "region.from-slice.v1") {
                RegionId created{};
                if (!state_.selected_slice)
                    error = "no slice is selected";
                else if (slices_.create_region_from_slice(
                             state_.selected_slice, state_.slice_revision,
                             created, error))
                    state_.selected_region = created;
            } else if (command.action_id == "region.delete.v1") {
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (slices_.erase_region(state_.selected_region,
                                               state_.slice_revision, error))
                    state_.selected_region = 0;
            } else if (command.action_id == "region.duplicate.v1") {
                RegionId created{};
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (slices_.duplicate_region(
                             state_.selected_region, state_.slice_revision,
                             created, error))
                    state_.selected_region = created;
            } else if (command.action_id == "region.rename.v1") {
                if (!state_.selected_region)
                    error = "no region is selected";
                else
                    (void)slices_.rename_region(
                        state_.selected_region, state_.slice_revision,
                        *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "region.bounds-set.v1") {
                const auto start = *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (start < 0 || end < 0)
                    error = "region boundaries cannot be negative";
                else
                    (void)slices_.set_region_bounds(
                        state_.selected_region, state_.slice_revision,
                        static_cast<std::uint64_t>(start),
                        static_cast<std::uint64_t>(end), error);
            } else if (command.action_id == "region.fades-set.v1") {
                const auto fade_in =
                    *argument<std::int64_t>(command, "fade_in_frames");
                const auto fade_out =
                    *argument<std::int64_t>(command, "fade_out_frames");
                const auto &curve = *argument<std::string>(command, "curve");
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (fade_in < 0 || fade_out < 0 ||
                         (curve != "linear" && curve != "equal-power"))
                    error = "invalid region fade";
                else
                    (void)slices_.set_region_fades(
                        state_.selected_region, state_.slice_revision,
                        static_cast<std::uint64_t>(fade_in),
                        static_cast<std::uint64_t>(fade_out),
                        curve == "equal-power"
                            ? RegionFadeCurve::equal_power
                            : RegionFadeCurve::linear,
                        error);
            } else if (command.action_id == "region.gain-set.v1") {
                if (!state_.selected_region)
                    error = "no region is selected";
                else
                    (void)slices_.set_region_gain(
                        state_.selected_region, state_.slice_revision,
                        static_cast<float>(*argument<double>(command, "gain")),
                        error);
            } else if (command.action_id == "region.direction-set.v1") {
                const auto &direction =
                    *argument<std::string>(command, "direction");
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (direction != "forward" && direction != "reverse")
                    error = "region direction must be forward or reverse";
                else
                    (void)slices_.set_region_direction(
                        state_.selected_region, state_.slice_revision,
                        direction == "reverse" ? RegionDirection::reverse
                                               : RegionDirection::forward,
                        error);
            } else if (command.action_id == "region.loop-set.v1") {
                const auto start = *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                const auto crossfade =
                    *argument<std::int64_t>(command, "crossfade_frames");
                if (!state_.selected_region || start < 0 || end < 0 ||
                    crossfade < 0)
                    error = "invalid region loop";
                else
                    (void)slices_.set_region_loop(
                        state_.selected_region, state_.slice_revision,
                        *argument<bool>(command, "enabled"),
                        static_cast<std::uint64_t>(start),
                        static_cast<std::uint64_t>(end),
                        static_cast<std::uint32_t>(crossfade), error);
            } else if (command.action_id == "region.pad-assign.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (!state_.selected_region)
                    error = "no region is selected";
                else if (pad < 0 ||
                         !slices_.assign_region_pad(
                             static_cast<std::size_t>(pad),
                             state_.selected_region, state_.slice_revision,
                             error)) {
                    if (error.empty()) error = "pad index is invalid";
                }
            } else if (command.action_id == "region.preview.v1") {
                if (!state_.selected_region)
                    error = "no region is selected";
                else
                    (void)slices_.preview_region(state_.selected_region, 1.0F,
                                                 error);
            } else if (command.action_id == "pad.select.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 || pad >= 16)
                    error = "pad index must be 0..15";
                else
                    state_.selected_pad = static_cast<std::size_t>(pad);
            } else if (command.action_id == "pad.property-select.v1") {
                static constexpr std::string_view properties[]{
                    "mode",    "choke",      "pitch",          "fine",
                    "gain",    "reverse",    "attack",         "decay",
                    "sustain", "release",    "stretch-mode",   "stretch-ratio",
                    "beats",   "source-bpm", "stretch-quality"};
                const auto &property =
                    *argument<std::string>(command, "property");
                if (std::find(properties, properties + 15, property) ==
                    properties + 15)
                    error = "unknown pad playback property";
                else {
                    state_.selected_pad_property = property;
                    state_.focused_element = "pad-playback";
                }
            } else if (command.action_id == "pad.trigger.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                double velocity = 1.0;
                double pitch_offset = 0.0;
                if (const auto found = command.arguments.find("velocity");
                    found != command.arguments.end())
                    velocity = std::get<double>(found->second);
                if (const auto found = command.arguments.find("pitch_offset");
                    found != command.arguments.end())
                    pitch_offset = std::get<double>(found->second);
                if (pad < 0 || pad >= 16 || velocity < 0 || velocity > 1 ||
                    pitch_offset < -96 || pitch_offset > 96)
                    error = "pad or velocity is outside its range";
                else if (!engine_.trigger_pad(
                             static_cast<std::uint8_t>(36 + pad),
                             static_cast<float>(velocity), true,
                             static_cast<float>(pitch_offset)))
                    error = "pad trigger queue is full";
            } else if (command.action_id == "pad.release.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 || pad >= 16)
                    error = "pad index must be 0..15";
                else if (!engine_.trigger_pad(
                             static_cast<std::uint8_t>(36 + pad), 0, false))
                    error = "pad release queue is full";
            } else {
                auto settings = state_.pad_settings[state_.selected_pad];
                if (command.action_id == "pad.playback-mode-set.v1") {
                    const auto mode = parse_sample_playback_mode(
                        *argument<std::string>(command, "mode"));
                    if (!mode)
                        error = "unknown sample playback mode";
                    else
                        settings.mode = *mode;
                } else if (command.action_id == "pad.choke-group-set.v1") {
                    const auto group =
                        *argument<std::int64_t>(command, "group");
                    if (group < 0 || group > 16)
                        error = "choke group must be 0..16";
                    else
                        settings.choke_group = static_cast<std::uint8_t>(group);
                } else if (command.action_id == "pad.choke-release-set.v1")
                    settings.choke_release =
                        *argument<bool>(command, "release");
                else if (command.action_id == "pad.pitch-set.v1")
                    settings.semitones = static_cast<float>(
                        *argument<double>(command, "semitones"));
                else if (command.action_id == "pad.fine-set.v1")
                    settings.fine_cents =
                        static_cast<float>(*argument<double>(command, "cents"));
                else if (command.action_id == "pad.gain-set.v1")
                    settings.gain =
                        static_cast<float>(*argument<double>(command, "gain"));
                else if (command.action_id == "pad.reverse-set.v1")
                    settings.reverse = *argument<bool>(command, "reverse");
                else if (command.action_id == "pad.envelope-set.v1") {
                    settings.attack_seconds = static_cast<float>(
                        *argument<double>(command, "attack"));
                    settings.decay_seconds =
                        static_cast<float>(*argument<double>(command, "decay"));
                    settings.sustain = static_cast<float>(
                        *argument<double>(command, "sustain"));
                    settings.release_seconds = static_cast<float>(
                        *argument<double>(command, "release"));
                } else if (command.action_id == "pad.stretch-mode-set.v1") {
                    const auto mode = parse_stretch_mode(
                        *argument<std::string>(command, "mode"));
                    if (!mode)
                        error = "unknown stretch mode";
                    else
                        settings.stretch_mode = *mode;
                } else if (command.action_id == "pad.stretch-ratio-set.v1")
                    settings.stretch_ratio =
                        static_cast<float>(*argument<double>(command, "ratio"));
                else if (command.action_id == "pad.source-beats-set.v1")
                    settings.source_beats =
                        static_cast<float>(*argument<double>(command, "beats"));
                else if (command.action_id == "pad.source-bpm-set.v1")
                    settings.source_bpm =
                        static_cast<float>(*argument<double>(command, "bpm"));
                else if (command.action_id == "pad.stretch-quality-set.v1") {
                    const auto quality = parse_stretch_quality(
                        *argument<std::string>(command, "quality"));
                    if (!quality)
                        error = "unknown stretch quality";
                    else
                        settings.stretch_quality = *quality;
                } else if (command.action_id == "pad.stretch-reset.v1") {
                    settings.stretch_mode = StretchMode::off;
                    settings.stretch_quality = StretchQuality::balanced;
                    settings.stretch_ratio = 1;
                    settings.source_beats = 0;
                    settings.source_bpm = 0;
                } else
                    error = "unsupported pad action";
                if (error.empty())
                    (void)slices_.set_pad_settings(
                        state_.selected_sample_slot, state_.selected_pad,
                        state_.slice_revision, settings, error);
            }
            if (error.empty()) {
                if (command.action_id.rfind("region.", 0) == 0 &&
                    command.action_id != "region.select.v1" &&
                    command.action_id != "region.preview.v1")
                    state_.project_dirty = true;
                refresh_slices();
                changed_locked();
            }
        } else if (command.action_id == "sample.slot-select.v1") {
            const auto id = *argument<std::int64_t>(command, "slot_id");
            if (id < 0 || id > std::numeric_limits<std::uint32_t>::max())
                error = "sample slot is outside its range";
            else {
                state_.selected_sample_slot =
                    SampleSlotId{static_cast<std::uint32_t>(id)};
                state_.selected_slice = 0;
                changed_locked();
            }
        } else if (command.action_id == "controller.context-select.v1") {
            const auto context = argument<std::string>(command, "context");
            static constexpr std::string_view contexts[]{
                "sequencer",   "samples",    "parameters", "xy",
                "performance", "settings",   "recording",  "mixer",
                "routing",     "modulation", "midi"};
            if (!context || std::find(std::begin(contexts), std::end(contexts),
                                      *context) == std::end(contexts))
                error = context ? "unknown controller context" : error;
            else {
                state_.controller_context = *context;
                state_.parameter_page = *context;
                changed = true;
            }
        } else if (command.action_id == "pad.mode-select.v1") {
            const auto &mode = *argument<std::string>(command, "mode");
            if (mode == "step")
                state_.pad_mode = PadMode::step;
            else if (mode == "pad")
                state_.pad_mode = PadMode::pad;
            else if (mode == "levels")
                state_.pad_mode = PadMode::levels;
            else if (mode == "notes")
                state_.pad_mode = PadMode::notes;
            else
                error = "unknown pad mode";
            if (error.empty())
                changed_locked();
        } else if (command.action_id == "navigation.focus-next.v1" ||
                   command.action_id == "navigation.focus-previous.v1") {
            static constexpr std::string_view focus[]{
                "transport", "tracks", "samples", "pad-playback", "parameters"};
            auto found = std::find(focus, focus + 5, state_.focused_element);
            std::size_t index = found == focus + 5
                                    ? 0
                                    : static_cast<std::size_t>(found - focus);
            index = command.action_id == "navigation.focus-next.v1"
                        ? (index + 1) % 5
                        : (index + 4) % 5;
            state_.focused_element = focus[index];
            changed_locked();
        } else if (command.action_id == "navigation.confirm.v1") {
            if (state_.focused_element == "transport")
                state_.focused_element = "tracks";
            else if (state_.focused_element == "tracks")
                state_.focused_element = "steps";
            else if (state_.focused_element == "step-properties") {
                static constexpr std::string_view properties[]{
                    "velocity",    "duration",  "pitch",  "timing",
                    "probability", "condition", "ratchet"};
                const auto found = std::find(properties, properties + 7,
                                             state_.selected_step_property);
                const auto index =
                    found == properties + 7
                        ? 0
                        : (static_cast<std::size_t>(found - properties) + 1) %
                              7;
                if (state_.selected_step_property == "ratchet")
                    state_.focused_element = "step-locks";
                else
                    state_.selected_step_property = properties[index];
            } else if (state_.focused_element == "step-locks") {
                std::vector<std::string> destinations;
                for (const auto &parameter : state_.parameters)
                    if (resolve_parameter_destination(parameter.id))
                        destinations.push_back(parameter.id);
                const auto found =
                    std::find(destinations.begin(), destinations.end(),
                              state_.selected_lock_parameter);
                if (!destinations.empty())
                    state_.selected_lock_parameter =
                        destinations[(found == destinations.end()
                                          ? 0
                                          : static_cast<std::size_t>(
                                                found - destinations.begin() +
                                                1)) %
                                     destinations.size()];
            } else if (state_.focused_element == "pad-playback") {
                static constexpr std::string_view properties[]{
                    "mode",    "choke",      "pitch",          "fine",
                    "gain",    "reverse",    "attack",         "decay",
                    "sustain", "release",    "stretch-mode",   "stretch-ratio",
                    "beats",   "source-bpm", "stretch-quality"};
                const auto found = std::find(properties, properties + 15,
                                             state_.selected_pad_property);
                const auto index =
                    found == properties + 15
                        ? 0
                        : (static_cast<std::size_t>(found - properties) + 1) %
                              15;
                state_.selected_pad_property = properties[index];
            } else
                state_.focused_element += ".confirmed";
            changed_locked();
        } else if (command.action_id == "navigation.cancel.v1") {
            state_.focused_element =
                state_.focused_element == "pad-playback" ? "samples"
                : state_.focused_element == "step-locks" ? "step-properties"
                : state_.focused_element == "step-properties" ? "steps"
                : state_.focused_element == "steps"           ? "tracks"
                                                              : "transport";
            changed_locked();
        } else if (command.action_id == "navigation.adjust.v1") {
            const auto delta = *argument<double>(command, "steps");
            const auto direction = delta < 0 ? -1 : delta > 0 ? 1 : 0;
            if (!std::isfinite(delta))
                error = "navigation adjustment must be finite";
            else if (direction == 0) {
            } else if (state_.focused_element == "tracks") {
                const auto timeline = timeline_.snapshot();
                if (timeline.timeline.tracks.empty())
                    error = "no tracks are available";
                else {
                    auto found = std::find_if(timeline.timeline.tracks.begin(),
                                              timeline.timeline.tracks.end(),
                                              [&](const Track &track) {
                                                  return track.id ==
                                                         state_.selected_track;
                                              });
                    auto index =
                        found == timeline.timeline.tracks.end()
                            ? 0
                            : static_cast<std::size_t>(
                                  found - timeline.timeline.tracks.begin());
                    index =
                        direction > 0
                            ? (index + 1) % timeline.timeline.tracks.size()
                            : (index + timeline.timeline.tracks.size() - 1) %
                                  timeline.timeline.tracks.size();
                    state_.selected_track = timeline.timeline.tracks[index].id;
                    state_.selected_step = 0;
                    state_.step_page = 0;
                    changed_locked();
                }
            } else if (state_.focused_element == "steps") {
                const auto track =
                    std::find_if(state_.tracks.begin(), state_.tracks.end(),
                                 [&](const TrackSummary &value) {
                                     return value.id == state_.selected_track;
                                 });
                if (track == state_.tracks.end())
                    error = "selected track does not exist";
                else {
                    const auto steps =
                        (track->pattern_length + state_.step_ticks - 1) /
                        state_.step_ticks;
                    auto step = state_.selected_step + direction;
                    if (step < 0)
                        step = steps - 1;
                    if (step >= steps)
                        step = 0;
                    state_.selected_step = step;
                    state_.step_page = static_cast<std::uint64_t>(step) / 16;
                    changed_locked();
                }
            } else if (state_.focused_element == "transport") {
                const auto previous = parameters_.get("transport.tempo");
                if (parameters_.adjust("transport.tempo", delta, error)) {
                    if (!publish_parameter("transport.tempo")) {
                        std::string ignored;
                        if (previous)
                            parameters_.set("transport.tempo",
                                            previous->current_value, ignored);
                        error = "tempo publication failed";
                    } else
                        changed_locked();
                }
            }
        } else if (command.action_id == "text.input.v1") {
            state_.focused_element =
                "text:" + *argument<std::string>(command, "text");
            changed_locked();
        } else if (command.action_id ==
                   "instrument.instance-parameter-set.v1") {
            const auto instance =
                *argument<std::int64_t>(command, "instance_id");
            const auto &id = *argument<std::string>(command, "parameter_id");
            const auto value = *argument<double>(command, "value");
            const auto descriptor = parameters_.get(id);
            const auto destination = resolve_parameter_destination(id);
            if (instance <= 0 ||
                instance >
                    static_cast<std::int64_t>(maximum_instrument_instances) ||
                !descriptor || !destination ||
                destination->target != ParameterTarget::subtractive_synth ||
                !std::isfinite(value) || value < number(descriptor->minimum) ||
                value > number(descriptor->maximum))
                error = "invalid instrument instance parameter";
            else if (!engine_.set_synth_parameter(
                         static_cast<SynthParameter>(destination->parameter),
                         static_cast<float>(value),
                         static_cast<InstrumentInstanceId>(instance)))
                error = "DSP parameter queue is full";
            else {
                auto next = modulation_configuration_;
                bool modulation_changed{};
                for (std::size_t route = 0; route < next.route_count; ++route)
                    if (next.routes[route].destination.target ==
                            ParameterTarget::subtractive_synth &&
                        next.routes[route].destination.parameter ==
                            destination->parameter &&
                        next.routes[route].destination.instance == instance) {
                        next.routes[route].base_value =
                            static_cast<float>(value);
                        modulation_changed = true;
                    }
                if (modulation_changed &&
                    !publish_modulation_with_macros(next, error))
                    error = "modulation publication queue is full";
                if (!error.empty())
                    return {false, sequence, before.revision, std::move(error)};
                modulation_configuration_ = next;
                project_.modulation = next;
                synth_instance_parameters_[static_cast<InstrumentInstanceId>(
                    instance)][id] = value;
                changed_locked();
            }
        } else if (command.action_id == "parameter.set-normalized.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.set_normalized(
                    id, *argument<double>(command, "value"), error)) {
                if (!publish_parameter(id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
        } else if (command.action_id == "parameter.adjust.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.adjust(id, *argument<double>(command, "steps"),
                                   error)) {
                if (!publish_parameter(id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
        } else if (command.action_id == "parameter.reset.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.reset(id, error)) {
                if (!publish_parameter(id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
        } else if (command.action_id == "modifier.shift.v1") {
            state_.modifiers.shift = *argument<bool>(command, "active");
            if (const auto *latched = argument<bool>(command, "latched"))
                state_.modifiers.shift_latched = *latched;
            changed_locked();
        } else if (command.action_id == "modifier.select.v1") {
            state_.modifiers.select = *argument<bool>(command, "active");
            if (const auto *latched = argument<bool>(command, "latched"))
                state_.modifiers.select_latched = *latched;
            changed_locked();
        } else if (command.action_id == "modulation.graph-query.v1" ||
                   command.action_id == "modulation.available-sources.v1" ||
                   command.action_id ==
                       "modulation.available-destinations.v1" ||
                   command.action_id == "modulation.validation-query.v1") {
            changed_locked();
        } else if (command.action_id.rfind("modulation.connection-", 0) == 0 ||
                   command.action_id == "modulation.share.v1" ||
                   command.action_id == "modulation.follower-tap.v1") {
            auto next = modulation_configuration_;
            const auto find_instance = [&](ModulatorInstanceId id) {
                return std::find_if(
                    next.modulators.begin(),
                    next.modulators.begin() + next.modulator_count,
                    [&](const auto &value) { return value.id == id; });
            };
            if (command.action_id == "modulation.connection-create.v1") {
                const auto source_id = static_cast<ModulatorInstanceId>(
                    *argument<std::int64_t>(command, "source_id"));
                const auto destination_id = static_cast<ModulatorInstanceId>(
                    *argument<std::int64_t>(command, "destination_id"));
                const auto parameter = static_cast<ModulatorParameterId>(
                    *argument<std::int64_t>(command, "parameter"));
                const auto depth = *argument<double>(command, "depth");
                const auto source = find_instance(source_id);
                const auto destination = find_instance(destination_id);
                if (source == next.modulators.begin() + next.modulator_count ||
                    destination ==
                        next.modulators.begin() + next.modulator_count)
                    error = "graph connection references a missing modulator";
                else if (!std::isfinite(depth) || depth < -1 || depth > 1)
                    error = "connection depth must be -1..1";
                else if (next.route_count == next.routes.size())
                    error = "modulation connection capacity is exhausted";
                else {
                    auto &route = next.routes[next.route_count++];
                    route.id = next_modulation_route_id_++;
                    route.source_instance = source_id;
                    route.source_output = 1;
                    if (const auto *input =
                            argument<std::int64_t>(command, "input")) {
                        if (*input <= 0 ||
                            *input > std::numeric_limits<std::uint32_t>::max())
                            error = "invalid destination input port";
                        else {
                            route.destination_kind =
                                ModulationDestinationKind::modulator_input;
                            route.destination_input =
                                static_cast<std::uint32_t>(*input);
                        }
                    } else
                        route.destination_kind =
                            ModulationDestinationKind::modulator_parameter;
                    route.destination_modulator = destination_id;
                    route.destination_modulator_parameter = parameter;
                    route.depth = static_cast<float>(depth);
                    route.minimum = -1;
                    route.maximum = 1;
                    route.scope =
                        source->owner_kind == ModulatorOwnerKind::track
                            ? ModulationScope::track
                        : source->owner_kind ==
                                ModulatorOwnerKind::voice_definition
                            ? ModulationScope::voice
                            : ModulationScope::global;
                    route.track_id = route.scope != ModulationScope::global
                                         ? source->owner_id
                                         : 0;
                    route.destination_owner_explicit = true;
                    route.destination_owner_kind = destination->owner_kind;
                    route.destination_owner_id = destination->owner_id;
                }
            } else if (command.action_id == "modulation.share.v1" ||
                       command.action_id == "modulation.follower-tap.v1") {
                const auto id = static_cast<ModulatorInstanceId>(
                    *argument<std::int64_t>(command, "id"));
                const auto instance = find_instance(id);
                if (instance == next.modulators.begin() + next.modulator_count)
                    error = "modulator instance does not exist";
                else if (command.action_id == "modulation.share.v1")
                    instance->shared = *argument<bool>(command, "shared");
                else {
                    const auto tap = *argument<std::int64_t>(command, "tap");
                    const auto tap_id =
                        *argument<std::int64_t>(command, "tap_id");
                    if (instance->type != ModulatorType::envelope_follower ||
                        tap < 0 ||
                        tap >
                            static_cast<std::int64_t>(FollowerTapKind::group) ||
                        tap_id < 0)
                        error = "invalid envelope follower tap";
                    else if (static_cast<FollowerTapKind>(tap) ==
                                 FollowerTapKind::group &&
                             std::none_of(mixer_configuration_.groups.begin(),
                                          mixer_configuration_.groups.begin() +
                                              mixer_configuration_.group_count,
                                          [&](const auto &group) {
                                              return group.id ==
                                                     static_cast<MixerBusId>(
                                                         tap_id);
                                          }))
                        error = "envelope follower group tap does not exist";
                    else {
                        instance->settings.follower_tap =
                            static_cast<FollowerTapKind>(tap);
                        instance->settings.follower_tap_id =
                            static_cast<std::uint64_t>(tap_id);
                    }
                }
            } else {
                const auto id = static_cast<ModulationRouteId>(
                    *argument<std::int64_t>(command, "connection_id"));
                const auto route = std::find_if(
                    next.routes.begin(), next.routes.begin() + next.route_count,
                    [&](const auto &value) { return value.id == id; });
                if (route == next.routes.begin() + next.route_count)
                    error = "modulation connection does not exist";
                else if (command.action_id ==
                         "modulation.connection-delete.v1") {
                    std::move(route + 1, next.routes.begin() + next.route_count,
                              route);
                    --next.route_count;
                } else if (command.action_id ==
                           "modulation.connection-depth.v1") {
                    const auto depth = *argument<double>(command, "depth");
                    if (!std::isfinite(depth) || depth < -1 || depth > 1)
                        error = "connection depth must be -1..1";
                    else
                        route->depth = static_cast<float>(depth);
                } else
                    route->enabled = *argument<bool>(command, "enabled");
            }
            ModulationGraphDiagnostics diagnostics;
            if (error.empty() && !validate_modulation_graph(next, diagnostics))
                error = "connection " + std::to_string(diagnostics.connection) +
                        ": " + diagnostics.message;
            if (error.empty() &&
                !publish_modulation_with_macros(next, error))
                error = "modulation publication queue is full";
            if (error.empty()) {
                modulation_configuration_ = next;
                project_.modulation = next;
                state_.modulation_graph = std::move(diagnostics);
                changed_locked();
            }
        } else if (command.action_id.rfind("modulator.", 0) == 0) {
            if (command.action_id == "modulator.query-types.v1" ||
                command.action_id == "modulator.query-instances.v1" ||
                command.action_id == "modulator.query-capacity.v1")
                return {true, sequence, snapshot().revision, {}};
            auto next = modulation_configuration_;
            const auto find_instance = [&](ModulatorInstanceId id) {
                return std::find_if(
                    next.modulators.begin(),
                    next.modulators.begin() + next.modulator_count,
                    [&](const auto &value) { return value.id == id; });
            };
            if (command.action_id == "modulator.create.v1") {
                const auto type_value =
                    *argument<std::int64_t>(command, "type");
                const auto owner_value =
                    *argument<std::int64_t>(command, "owner");
                const auto owner_id =
                    *argument<std::int64_t>(command, "owner_id");
                if (type_value <
                        static_cast<std::int64_t>(ModulatorType::lfo) ||
                    type_value >
                        static_cast<std::int64_t>(ModulatorType::gate) ||
                    owner_value <
                        static_cast<std::int64_t>(ModulatorOwnerKind::track) ||
                    owner_value > static_cast<std::int64_t>(
                                      ModulatorOwnerKind::voice_definition) ||
                    owner_id < 0)
                    error = "invalid modulator type or owner";
                else if (next.modulator_count == next.modulators.size())
                    error = "modulator capacity is exhausted (64 instances)";
                else {
                    const auto owner =
                        static_cast<ModulatorOwnerKind>(owner_value);
                    if (owner == ModulatorOwnerKind::track ||
                        owner == ModulatorOwnerKind::voice_definition) {
                        const auto timeline = timeline_.snapshot().timeline;
                        if (std::none_of(
                                timeline.tracks.begin(), timeline.tracks.end(),
                                [&](const auto &track) {
                                    return track.id ==
                                           static_cast<TrackId>(owner_id);
                                }))
                            error = "modulator owner track does not exist";
                        else if (owner ==
                                     ModulatorOwnerKind::voice_definition &&
                                 !voice_modulator_supported(
                                     static_cast<ModulatorType>(type_value)))
                            error = "modulator type does not support voice "
                                    "ownership";
                    } else if (owner == ModulatorOwnerKind::auxiliary &&
                               owner_id >= 2)
                        error = "modulator auxiliary owner does not exist";
                    else if (owner == ModulatorOwnerKind::master &&
                             owner_id != 0)
                        error = "master owner ID must be zero";
                    else if (owner == ModulatorOwnerKind::group &&
                             std::none_of(mixer_configuration_.groups.begin(),
                                          mixer_configuration_.groups.begin() +
                                              mixer_configuration_.group_count,
                                          [&](const auto &group) {
                                              return group.id ==
                                                     static_cast<MixerBusId>(
                                                         owner_id);
                                          }))
                        error = "modulator group owner does not exist";
                    if (error.empty()) {
                        auto &instance =
                            next.modulators[next.modulator_count++];
                        instance.id = next_modulator_instance_id_++;
                        instance.type = static_cast<ModulatorType>(type_value);
                        instance.owner_kind = owner;
                        instance.owner_id =
                            static_cast<std::uint64_t>(owner_id);
                        if (const auto *name =
                                argument<std::string>(command, "name")) {
                            if (name->size() >= instance.display_name.size())
                                error = "modulator name is too long";
                            else
                                std::copy(name->begin(), name->end(),
                                          instance.display_name.begin());
                        }
                    }
                }
            } else if (command.action_id == "modulator.route-create.v1") {
                const auto source_id = static_cast<ModulatorInstanceId>(
                    *argument<std::int64_t>(command, "source_id"));
                const auto source = find_instance(source_id);
                const auto destination_id =
                    *argument<std::string>(command, "destination");
                const auto destination =
                    resolve_parameter_destination(destination_id);
                auto descriptor = parameters_.get(destination_id);
                // Effect instances are dynamic discovery objects, so their
                // descriptors are synthesized from the authoritative rack
                // instead of being retained in the static parameter registry.
                if (!descriptor && destination_id.rfind("fx.", 0) == 0) {
                    const auto separator = destination_id.find('.', 3);
                    if (separator != std::string::npos) {
                        EffectInstanceId effect_id{};
                        const auto text_id = destination_id.substr(3, separator - 3);
                        const auto [end, ec] = std::from_chars(
                            text_id.data(), text_id.data() + text_id.size(), effect_id);
                        const auto effect = std::find_if(
                            mixer_configuration_.effects.instances.begin(),
                            mixer_configuration_.effects.instances.begin() +
                                mixer_configuration_.effects.instance_count,
                            [&](const auto &value) { return value.id == effect_id; });
                        if (ec == std::errc{} && end == text_id.data() + text_id.size() &&
                            effect != mixer_configuration_.effects.instances.begin() +
                                          mixer_configuration_.effects.instance_count) {
                            const auto stable = std::string_view(destination_id).substr(separator + 1);
                            const auto item = std::find_if(
                                effect_parameter_descriptors(effect->type).begin(),
                                effect_parameter_descriptors(effect->type).end(),
                                [&](const auto &value) {
                                    if (value.stable_id == stable) return true;
                                    std::uint32_t numeric{};
                                    const auto parsed = std::from_chars(
                                        stable.data(), stable.data() + stable.size(), numeric);
                                    return parsed.ec == std::errc{} &&
                                        parsed.ptr == stable.end() && numeric == value.id;
                                });
                            if (item != effect_parameter_descriptors(effect->type).end()) {
                                auto current = item->default_value;
                                for (std::size_t i = 0; i < effect->parameter_count; ++i)
                                    if (effect->parameters[i].id == item->id)
                                        current = effect->parameters[i].value;
                                descriptor = ParameterDescriptor{
                                    destination_id, std::string(item->name),
                                    std::string(item->unit), ParameterType::real,
                                    static_cast<double>(item->minimum),
                                    static_cast<double>(item->maximum),
                                    static_cast<double>(item->default_value),
                                    static_cast<double>(current), 0.01, true,
                                    item->automatable, item->modulatable, {}};
                            }
                        }
                    }
                }
                const auto depth = *argument<double>(command, "depth");
                if (source == next.modulators.begin() + next.modulator_count)
                    error = "modulator source instance does not exist";
                else if (!destination || !descriptor ||
                         !descriptor->modulatable ||
                         descriptor->type != ParameterType::real)
                    error = "parameter is not a modulation destination";
                else if (!std::isfinite(depth) || depth < -1 || depth > 1)
                    error = "modulation depth must be -1..1";
                else if (next.route_count == next.routes.size())
                    error = "modulation route capacity is exhausted";
                else {
                    auto &route = next.routes[next.route_count++];
                    route.id = next_modulation_route_id_++;
                    route.source_instance = source_id;
                    route.destination = *destination;
                    route.minimum =
                        static_cast<float>(number(descriptor->minimum));
                    route.maximum =
                        static_cast<float>(number(descriptor->maximum));
                    route.base_value =
                        static_cast<float>(number(descriptor->current_value));
                    route.depth = static_cast<float>(depth);
                    route.scope =
                        source->owner_kind == ModulatorOwnerKind::track
                            ? ModulationScope::track
                        : source->owner_kind ==
                                ModulatorOwnerKind::voice_definition
                            ? ModulationScope::voice
                            : ModulationScope::global;
                    route.track_id = route.scope != ModulationScope::global
                                         ? source->owner_id
                                         : 0;
                    if (route.scope == ModulationScope::track ||
                        route.scope == ModulationScope::voice)
                        route.destination.instance =
                            static_cast<std::uint32_t>(source->owner_id);
                    if (source->owner_kind == ModulatorOwnerKind::auxiliary)
                        route.destination.instance =
                            static_cast<std::uint32_t>(source->owner_id + 1);
                    if (source->owner_kind == ModulatorOwnerKind::group) {
                        route.destination.instance =
                            static_cast<std::uint32_t>(source->owner_id);
                        route.destination_owner_explicit = true;
                        route.destination_owner_kind =
                            ModulatorOwnerKind::group;
                        route.destination_owner_id = source->owner_id;
                    }
                    if (const auto *instance = argument<std::int64_t>(
                            command, "destination_instance")) {
                        if (*instance < 0 ||
                            *instance >
                                std::numeric_limits<std::uint32_t>::max())
                            error = "destination instance is out of range";
                        else
                            route.destination.instance =
                                static_cast<std::uint32_t>(*instance);
                    }
                    const auto owner =
                        argument<std::int64_t>(command, "destination_owner");
                    const auto owner_id =
                        argument<std::int64_t>(command, "destination_owner_id");
                    if ((owner && !owner_id) || (!owner && owner_id))
                        error =
                            "destination owner kind and ID are both required";
                    else if (owner &&
                             (*owner < static_cast<std::int64_t>(
                                           ModulatorOwnerKind::track) ||
                              *owner >
                                  static_cast<std::int64_t>(
                                      ModulatorOwnerKind::voice_definition) ||
                              *owner_id < 0))
                        error = "invalid destination owner";
                    else if (owner) {
                        route.destination_owner_explicit = true;
                        route.destination_owner_kind =
                            static_cast<ModulatorOwnerKind>(*owner);
                        route.destination_owner_id =
                            static_cast<std::uint64_t>(*owner_id);
                    }
                }
            } else {
                const auto raw = *argument<std::int64_t>(command, "id");
                const auto found =
                    find_instance(static_cast<ModulatorInstanceId>(raw));
                if (raw <= 0 ||
                    found == next.modulators.begin() + next.modulator_count)
                    error = "modulator instance does not exist";
                else if (command.action_id == "modulator.delete.v1") {
                    const auto referenced = std::any_of(
                        next.routes.begin(),
                        next.routes.begin() + next.route_count,
                        [&](const auto &route) {
                            return route.source_instance == found->id;
                        });
                    if (referenced)
                        error = "modulator is still referenced by a route";
                    else {
                        std::move(found + 1,
                                  next.modulators.begin() +
                                      next.modulator_count,
                                  found);
                        --next.modulator_count;
                    }
                } else if (command.action_id == "modulator.duplicate.v1") {
                    if (next.modulator_count == next.modulators.size())
                        error =
                            "modulator capacity is exhausted (64 instances)";
                    else {
                        auto copy = *found;
                        copy.id = next_modulator_instance_id_++;
                        next.modulators[next.modulator_count++] = copy;
                    }
                } else if (command.action_id == "modulator.enable.v1")
                    found->enabled = *argument<bool>(command, "enabled");
                else if (command.action_id == "modulator.rename.v1") {
                    const auto &name = *argument<std::string>(command, "name");
                    if (name.size() >= found->display_name.size())
                        error = "modulator name is too long";
                    else {
                        found->display_name.fill(0);
                        std::copy(name.begin(), name.end(),
                                  found->display_name.begin());
                    }
                } else if (command.action_id == "modulator.parameter-set.v1") {
                    const auto &name =
                        *argument<std::string>(command, "parameter");
                    const auto value = *argument<double>(command, "value");
                    const auto *type = find_modulator_type(found->type);
                    const auto descriptor =
                        type ? std::find_if(type->parameters.begin(),
                                            type->parameters.end(),
                                            [&](const auto &candidate) {
                                                return candidate.id == name;
                                            })
                             : std::span<const ModulatorParameterDescriptor>::
                                   iterator{};
                    if (!std::isfinite(value))
                        error = "modulator parameter must be finite";
                    else if (!type || descriptor == type->parameters.end() ||
                             value < descriptor->minimum ||
                             value > descriptor->maximum)
                        error =
                            "unknown modulator parameter or value out of range";
                    else {
                        auto &settings = found->settings;
                        const auto numeric = static_cast<float>(value);
                        switch (descriptor->numeric_id) {
                        case ModulatorParameterId::rate:
                            settings.rate_hz = numeric;
                            break;
                        case ModulatorParameterId::beats:
                            settings.beats_per_cycle = numeric;
                            break;
                        case ModulatorParameterId::attack:
                            if (found->type == ModulatorType::envelope_follower)
                                settings.follower_attack_seconds = numeric;
                            else
                                settings.attack_seconds = numeric;
                            break;
                        case ModulatorParameterId::decay:
                            settings.decay_seconds = numeric;
                            break;
                        case ModulatorParameterId::sustain:
                            settings.sustain = numeric;
                            break;
                        case ModulatorParameterId::release:
                            if (found->type == ModulatorType::envelope_follower)
                                settings.follower_release_seconds = numeric;
                            else
                                settings.release_seconds = numeric;
                            break;
                        case ModulatorParameterId::follower_gain:
                            settings.follower_gain = numeric;
                            break;
                        case ModulatorParameterId::follower_threshold:
                            settings.follower_threshold = numeric;
                            break;
                        case ModulatorParameterId::step_duration:
                            settings.step_duration_beats = numeric;
                            break;
                        case ModulatorParameterId::swing:
                            settings.swing = numeric;
                            break;
                        case ModulatorParameterId::gate_length:
                            settings.gate_length = numeric;
                            break;
                        case ModulatorParameterId::step_count:
                            settings.step_count =
                                static_cast<std::uint8_t>(value);
                            settings.pulses =
                                std::min(settings.pulses, settings.step_count);
                            settings.rotation %= settings.step_count;
                            break;
                        case ModulatorParameterId::pulses:
                            settings.pulses = static_cast<std::uint8_t>(value);
                            break;
                        case ModulatorParameterId::rotation:
                            settings.rotation =
                                static_cast<std::uint8_t>(value);
                            break;
                        case ModulatorParameterId::order:
                            settings.order = static_cast<std::uint8_t>(value);
                            break;
                        case ModulatorParameterId::distribution:
                            settings.distribution =
                                static_cast<std::uint8_t>(value);
                            break;
                        case ModulatorParameterId::correlation:
                            settings.correlation = numeric;
                            break;
                        case ModulatorParameterId::quantization:
                            settings.quantization = numeric;
                            break;
                        case ModulatorParameterId::slew:
                            settings.slew_seconds = numeric;
                            break;
                        case ModulatorParameterId::rise:
                            settings.rise_seconds = numeric;
                            break;
                        case ModulatorParameterId::fall:
                            settings.fall_seconds = numeric;
                            break;
                        case ModulatorParameterId::curve:
                            settings.curve = numeric;
                            break;
                        case ModulatorParameterId::scale:
                            settings.scale = numeric;
                            break;
                        case ModulatorParameterId::offset:
                            settings.offset = numeric;
                            break;
                        case ModulatorParameterId::chaos:
                            settings.chaos = numeric;
                            break;
                        default:
                            error = "parameter is not editable";
                            break;
                        }
                    }
                } else if (command.action_id == "modulator.step-set.v1") {
                    const auto step = *argument<std::int64_t>(command, "step");
                    const auto value = *argument<double>(command, "value");
                    if (found->type != ModulatorType::step_sequencer ||
                        step < 0 || step >= found->settings.step_count ||
                        !std::isfinite(value) || value < -1 || value > 1)
                        error = "invalid step modulator value";
                    else
                        found->settings
                            .step_values[static_cast<std::size_t>(step)] =
                            static_cast<float>(value);
                }
            }
            if (error.empty()) {
                ModulationEngine validation;
                if (!validation.configure(next))
                    error = "invalid modulator ownership or route";
            }
            if (error.empty() &&
                !publish_modulation_with_macros(next, error))
                error = "modulation publication queue is full";
            if (error.empty()) {
                modulation_configuration_ = next;
                project_.modulation = next;
                if (command.action_id == "modulator.route-create.v1" &&
                    next.route_count)
                    state_.selected_modulation_route =
                        next.routes[next.route_count - 1].id;
                changed_locked();
            }
        } else if (command.action_id == "modulation.route-create.v1") {
            const auto source_value =
                *argument<std::int64_t>(command, "source");
            const auto scope_value = *argument<std::int64_t>(command, "scope");
            const auto depth = *argument<double>(command, "depth");
            const auto destination_id =
                *argument<std::string>(command, "destination");
            if (source_value < 0 ||
                source_value >=
                    static_cast<std::int64_t>(ModulationSourceId::count))
                error = "invalid modulation source";
            else if (scope_value < 0 ||
                     scope_value >
                         static_cast<std::int64_t>(ModulationScope::voice))
                error = "invalid modulation scope";
            else if (!std::isfinite(depth) || depth < -1 || depth > 1)
                error = "modulation depth must be -1..1";
            else if (modulation_configuration_.route_count ==
                     modulation_configuration_.routes.size())
                error = "modulation route capacity is exhausted";
            else {
                const auto destination =
                    resolve_parameter_destination(destination_id);
                const auto descriptor = parameters_.get(destination_id);
                if (!destination || !descriptor || !descriptor->modulatable ||
                    descriptor->type != ParameterType::real)
                    error =
                        "parameter is not a real-time modulation destination";
                else {
                    const auto scope =
                        static_cast<ModulationScope>(scope_value);
                    const auto source =
                        static_cast<ModulationSourceId>(source_value);
                    const auto source_scope =
                        modulation_configuration_
                            .sources[static_cast<std::size_t>(source)]
                            .scope;
                    const auto track =
                        argument<std::int64_t>(command, "track_id");
                    const auto instance =
                        argument<std::int64_t>(command, "instance_id");
                    if (scope != source_scope)
                        error = "route scope must match the source scope";
                    else if (instance && (*instance < 0 || *instance > 32))
                        error = "destination instance is out of range";
                    else if (scope == ModulationScope::track &&
                             (!track || *track <= 0))
                        error = "track modulation requires a positive track ID";
                    else if ((scope == ModulationScope::track ||
                              scope == ModulationScope::voice) &&
                             destination->target !=
                                 ParameterTarget::sample_voice &&
                             (!instance || *instance <= 0 || *instance > 32))
                        error = "local modulation requires a valid destination "
                                "instance";
                    else if (scope == ModulationScope::track &&
                             destination->target ==
                                 ParameterTarget::subtractive_synth) {
                        const auto timeline = timeline_.snapshot().timeline;
                        const auto routed = std::find_if(
                            timeline.tracks.begin(), timeline.tracks.end(),
                            [&](const Track &candidate) {
                                return candidate.id ==
                                           static_cast<TrackId>(*track) &&
                                       candidate.instrument_id ==
                                           subtractive_instrument_id &&
                                       candidate.instrument_instance_id ==
                                           static_cast<InstrumentInstanceId>(
                                               *instance);
                            });
                        if (routed == timeline.tracks.end())
                            error = "track is not routed to the requested "
                                    "synth instance";
                        else {
                            auto next = modulation_configuration_;
                            auto &route = next.routes[next.route_count++];
                            route.id = next_modulation_route_id_++;
                            route.source = source;
                            route.destination = *destination;
                            route.destination.instance =
                                static_cast<std::uint32_t>(*instance);
                            route.minimum =
                                static_cast<float>(number(descriptor->minimum));
                            route.maximum =
                                static_cast<float>(number(descriptor->maximum));
                            route.base_value = static_cast<float>(
                                number(descriptor->current_value));
                            route.depth = static_cast<float>(depth);
                            route.scope = scope;
                            route.track_id = static_cast<std::uint64_t>(*track);
                            if (!publish_modulation_with_macros(next, error))
                                error = "modulation publication queue is full";
                            else {
                                modulation_configuration_ = next;
                                project_.modulation = next;
                                state_.selected_modulation_route = route.id;
                                changed_locked();
                            }
                        }
                    } else if (scope == ModulationScope::voice &&
                               destination->target !=
                                   ParameterTarget::subtractive_synth)
                        error =
                            "per-voice sample modulation is not yet available";
                    else {
                        auto next = modulation_configuration_;
                        auto &route = next.routes[next.route_count++];
                        route.id = next_modulation_route_id_++;
                        route.source = source;
                        route.destination = *destination;
                        route.destination.instance =
                            destination->target ==
                                        ParameterTarget::sample_voice &&
                                    scope == ModulationScope::track
                                ? static_cast<std::uint32_t>(*track)
                            : instance ? static_cast<std::uint32_t>(*instance)
                                       : 0;
                        route.minimum =
                            static_cast<float>(number(descriptor->minimum));
                        route.maximum =
                            static_cast<float>(number(descriptor->maximum));
                        route.base_value = static_cast<float>(
                            number(descriptor->current_value));
                        route.depth = static_cast<float>(depth);
                        route.scope = scope;
                        route.track_id =
                            track ? static_cast<std::uint64_t>(*track) : 0;
                        if (!publish_modulation_with_macros(next, error))
                            error = "modulation publication queue is full";
                        else {
                            modulation_configuration_ = next;
                            project_.modulation = next;
                            state_.selected_modulation_route = route.id;
                            changed_locked();
                        }
                    }
                }
            }
        } else if (command.action_id == "modulation.route-delete.v1" ||
                   command.action_id == "modulation.route-select.v1" ||
                   command.action_id == "modulation.route-depth.v1" ||
                   command.action_id == "modulation.route-enable.v1") {
            const auto id = static_cast<ModulationRouteId>(
                *argument<std::int64_t>(command, "route_id"));
            auto next = modulation_configuration_;
            std::size_t index = next.route_count;
            for (std::size_t i = 0; i < next.route_count; ++i)
                if (next.routes[i].id == id)
                    index = i;
            if (index == next.route_count)
                error = "modulation route does not exist";
            else if (command.action_id == "modulation.route-select.v1") {
                state_.selected_modulation_route = id;
                changed_locked();
            } else {
                if (command.action_id == "modulation.route-delete.v1") {
                    for (std::size_t i = index + 1; i < next.route_count; ++i)
                        next.routes[i - 1] = next.routes[i];
                    --next.route_count;
                } else if (command.action_id == "modulation.route-depth.v1") {
                    const auto depth = *argument<double>(command, "depth");
                    if (!std::isfinite(depth) || depth < -1 || depth > 1)
                        error = "modulation depth must be -1..1";
                    else
                        next.routes[index].depth = static_cast<float>(depth);
                } else
                    next.routes[index].enabled =
                        *argument<bool>(command, "enabled");
                if (error.empty()) {
                    if (!publish_modulation_with_macros(next, error))
                        error = "modulation publication queue is full";
                    else {
                        modulation_configuration_ = next;
                        project_.modulation = next;
                        if (command.action_id == "modulation.route-delete.v1" &&
                            state_.selected_modulation_route == id)
                            state_.selected_modulation_route = 0;
                        changed_locked();
                    }
                }
            }
        } else if (command.action_id == "modulation.source-configure.v1") {
            const auto source = *argument<std::int64_t>(command, "source");
            const auto waveform = *argument<std::int64_t>(command, "waveform");
            const auto scope = *argument<std::int64_t>(command, "scope");
            const auto rate = *argument<double>(command, "rate");
            const auto beats = *argument<double>(command, "beats");
            if (source < 0 ||
                source >=
                    static_cast<std::int64_t>(ModulationSourceId::count) ||
                waveform < 0 ||
                waveform > static_cast<std::int64_t>(LfoWaveform::random) ||
                scope < 0 ||
                scope > static_cast<std::int64_t>(ModulationScope::voice) ||
                !std::isfinite(rate) || rate < 0 || rate > 100 ||
                !std::isfinite(beats) || beats <= 0 || beats > 64)
                error = "invalid modulation source settings";
            else {
                auto next = modulation_configuration_;
                auto &settings = next.sources[static_cast<std::size_t>(source)];
                settings.waveform = static_cast<LfoWaveform>(waveform);
                settings.rate_hz = static_cast<float>(rate);
                settings.tempo_sync = *argument<bool>(command, "tempo_sync");
                settings.beats_per_cycle = static_cast<float>(beats);
                settings.scope = static_cast<ModulationScope>(scope);
                if (settings.scope == ModulationScope::voice && source >= 4 &&
                    source != static_cast<std::int64_t>(
                                  ModulationSourceId::envelope) &&
                    source !=
                        static_cast<std::int64_t>(ModulationSourceId::velocity))
                    error = "source does not support per-voice scope";
                if (const auto *retrigger =
                        argument<bool>(command, "retrigger"))
                    settings.retrigger = *retrigger;
                const auto assign_envelope = [&](const char *name, float &field,
                                                 double minimum,
                                                 double maximum) {
                    if (const auto found = command.arguments.find(name);
                        found != command.arguments.end()) {
                        const auto value = number(found->second);
                        if (!std::isfinite(value) || value < minimum ||
                            value > maximum)
                            return false;
                        field = static_cast<float>(value);
                    }
                    return true;
                };
                if (!assign_envelope("attack", settings.attack_seconds, 0,
                                     60) ||
                    !assign_envelope("decay", settings.decay_seconds, 0, 60) ||
                    !assign_envelope("sustain", settings.sustain, 0, 1) ||
                    !assign_envelope("release", settings.release_seconds, 0,
                                     60))
                    error = "invalid modulation envelope settings";
                if (const auto *seed =
                        argument<std::int64_t>(command, "seed")) {
                    if (*seed < 0 ||
                        *seed > std::numeric_limits<std::uint32_t>::max())
                        error = "modulation seed is out of range";
                    else
                        settings.seed = static_cast<std::uint32_t>(*seed);
                }
                bool incompatible{};
                for (std::size_t i = 0; i < next.route_count; ++i)
                    incompatible |=
                        next.routes[i].source ==
                            static_cast<ModulationSourceId>(source) &&
                        next.routes[i].scope != settings.scope;
                if (error.empty() && incompatible)
                    error = "source scope conflicts with an existing route";
                else if (error.empty() &&
                         !publish_modulation_with_macros(next, error))
                    error = "modulation publication queue is full";
                else if (error.empty()) {
                    modulation_configuration_ = next;
                    project_.modulation = next;
                    changed_locked();
                }
            }
        } else if (command.action_id == "modulation.source-reset.v1") {
            const auto source = *argument<std::int64_t>(command, "source");
            if (source < 0 ||
                source >= static_cast<std::int64_t>(ModulationSourceId::count))
                error = "invalid modulation source";
            else {
                auto next = modulation_configuration_;
                next.sources[static_cast<std::size_t>(source)] = {};
                bool incompatible{};
                for (std::size_t i = 0; i < next.route_count; ++i)
                    incompatible |=
                        next.routes[i].source ==
                            static_cast<ModulationSourceId>(source) &&
                        next.routes[i].scope != ModulationScope::global;
                if (incompatible)
                    error = "default source scope conflicts with an existing route";
                else if (!publish_modulation_with_macros(next, error))
                    error = "modulation publication queue is full";
                else {
                    modulation_configuration_ = next;
                    project_.modulation = next;
                    state_.project_dirty = true;
                    changed_locked();
                }
            }
        } else if (command.action_id == "modulation.xy.v1") {
            const auto position = *argument<NormalizedXY>(command, "position");
            if (position.x < 0 || position.x > 1 || position.y < 0 ||
                position.y > 1)
                error = "X/Y position must be normalized";
            else {
                parameters_.set("performance.xy.x", position.x, error);
                if (error.empty())
                    parameters_.set("performance.xy.y", position.y, error);
                if (error.empty() &&
                    (!engine_.set_modulation_input(
                         ModulationSourceId::xy_x,
                         static_cast<float>(position.x * 2.0 - 1.0)) ||
                     !engine_.set_modulation_input(
                         ModulationSourceId::xy_y,
                         static_cast<float>(position.y * 2.0 - 1.0))))
                    error = "modulation input queue is full";
                if (error.empty())
                    changed_locked();
            }
        } else if (command.action_id == "modulation.midi-cc.v1") {
            const auto value = *argument<double>(command, "value");
            if (!std::isfinite(value) || value < 0 || value > 1)
                error = "MIDI CC modulation must be normalized";
            else if (!engine_.set_modulation_input(
                         ModulationSourceId::midi_cc,
                         static_cast<float>(value * 2.0 - 1.0)))
                error = "modulation input queue is full";
            else
                changed_locked();
        }
    }
    if (changed && (command.action_id.rfind("sample.", 0) == 0 ||
                    command.action_id.rfind("transport.", 0) == 0)) {
        std::lock_guard lock(mutex_);
        changed_locked();
    }
    if (error.empty()) {
        const auto &id = command.action_id;
        std::string automation_id;
        std::optional<float> automation_value;
        if (id == "parameter.set-normalized.v1" ||
            id == "parameter.adjust.v1" || id == "parameter.reset.v1") {
            automation_id = *argument<std::string>(command, "parameter_id");
            if (const auto descriptor = parameters_.get(automation_id);
                descriptor && descriptor->automatable &&
                resolve_parameter_destination(automation_id) &&
                is_numeric(descriptor->current_value))
                automation_value =
                    static_cast<float>(number(descriptor->current_value));
        } else if (id == "instrument.instance-parameter-set.v1") {
            const auto &parameter =
                *argument<std::string>(command, "parameter_id");
            automation_id = "synth.instance." +
                            std::to_string(*argument<std::int64_t>(
                                command, "instance_id")) +
                            "." + parameter.substr(parameter.find('.') + 1);
            automation_value =
                static_cast<float>(*argument<double>(command, "value"));
        }
        if (automation_value) {
            std::lock_guard lock(mutex_);
            arrangement_recording_.capture_automation(
                std::move(automation_id), *automation_value,
                state_.transport_tick, automation_thinning_ticks_);
        }
        const auto musical_edit =
            id.rfind("step.", 0) == 0 || id.rfind("slice.", 0) == 0 ||
            id.rfind("sample.load", 0) == 0 ||
            id.rfind("sample.replace", 0) == 0 ||
            id.rfind("sample.unload", 0) == 0 ||
            id.rfind("parameter.", 0) == 0 ||
            id.rfind("track.create", 0) == 0 ||
            id.rfind("track.delete", 0) == 0 ||
            id.rfind("track.mute", 0) == 0 ||
            id.rfind("track.instrument", 0) == 0 ||
            id.rfind("track.midi-destination", 0) == 0 ||
            id.rfind("pattern.", 0) == 0 || id.rfind("arrangement.", 0) == 0 ||
            id.rfind("transport.tempo", 0) == 0 ||
            id.rfind("modulation.route", 0) == 0 ||
            id.rfind("modulation.source", 0) == 0 ||
            id.rfind("modulator.", 0) == 0 ||
            id.rfind("instrument.instance-parameter", 0) == 0 ||
            id.rfind("timeline.", 0) == 0 || id.rfind("edit.undo", 0) == 0 ||
            id.rfind("edit.redo", 0) == 0 ||
            (id.rfind("pad.", 0) == 0 && id != "pad.trigger.v1" &&
             id != "pad.release.v1" && id != "pad.select.v1" &&
             id != "pad.property-select.v1" && id != "pad.mode-select.v1");
        if (musical_edit) {
            std::lock_guard lock(mutex_);
            state_.project_dirty = true;
            changed_locked();
        }
    }
    return {error.empty(), sequence, snapshot().revision, std::move(error)};
}
void ApplicationService::synchronize_locked() const {
    const auto previous_live_loops = state_.live_loops;
    live_loops_.update();
    state_.live_loops = live_loops_.sessions();
    const auto completed_live_loop_pass = std::any_of(
        state_.live_loops.begin(), state_.live_loops.end(),
        [&](const auto &session) {
            if (session.state != LiveLoopState::playing)
                return false;
            const auto previous = std::find_if(
                previous_live_loops.begin(), previous_live_loops.end(),
                [&](const auto &candidate) { return candidate.id == session.id; });
            return previous == previous_live_loops.end() ||
                   previous->state != LiveLoopState::playing ||
                   previous->actual_stop_frame != session.actual_stop_frame;
        });
    if (completed_live_loop_pass) {
        state_.project_dirty = true;
        ++state_.revision;
    }
    if (pending_project_recovery_ &&
        engine_.preset_activation_cancelled(
            pending_project_recovery_->activation_sequence)) {
        engine_.reclaim_preset_activations();
        pending_project_recovery_.reset();
        state_.project_error = "project dependency recovery was cancelled";
        ++state_.revision;
    }
    if (pending_project_recovery_ &&
        engine_.preset_activation_applied(
            pending_project_recovery_->activation_sequence)) {
        auto pending = std::make_unique<PendingProjectRecovery>(
            std::move(*pending_project_recovery_));
        for (std::size_t i = 0; i < pending->activation.sample_count; ++i)
            samples_.adopt_preset_activation(
                pending->activation.samples[i].slot,
                pending->activation.samples[i].sample,
                pending->sample_paths[i], pending->sample_publications[i]);
        std::string recovery_error;
        if (slices_.adopt_activated_project(pending->project.samples,
                                            recovery_error)) {
            project_ = std::move(pending->project);
            state_.missing_assets = project_.missing_assets;
            state_.project_dirty = true;
            state_.project_error.clear();
        } else {
            state_.project_error =
                "project dependency recovery adoption failed: " +
                recovery_error;
        }
        (void)engine_.finalize_preset_activation(
            pending->activation_sequence);
        pending_project_recovery_.reset();
        ++state_.revision;
    }
    if (pending_preset_ && pending_preset_->published &&
        engine_.preset_activation_cancelled(
            pending_preset_->activation_sequence)) {
        state_.preset_application.status = "cancelled";
        state_.preset_application.progress = 0;
        ++state_.preset_application.revision;
        engine_.reclaim_preset_activations();
        pending_preset_.reset();
    }
    if (pending_preset_ && pending_preset_->published &&
        engine_.preset_activation_applied(
            pending_preset_->activation_sequence)) {
        auto &pending = *pending_preset_;
        for (std::size_t i = 0; i < pending.activation.sample_count; ++i)
            samples_.adopt_preset_activation(
                pending.activation.samples[i].slot,
                pending.activation.samples[i].sample, pending.sample_paths[i],
                pending.sample_publications[i]);
        std::string adoption_error;
        if (pending.preset.samples &&
            !slices_.adopt_activated_project(*pending.preset.samples,
                                             adoption_error))
            state_.preset_application.error = adoption_error;
        mixer_configuration_ = pending.activation.mixer;
        modulation_configuration_ = pending.activation.modulation;
        for (const auto &[id, value] : pending.synth_parameters)
            synth_instance_parameters_[pending.target_instrument][id] = value;
        if (pending.history_before) {
            auto after = capture_authoring_state(true);
            const auto same_sample_slots =
                pending.history_before->samples && after->samples &&
                pending.history_before->samples->sample_paths.size() ==
                    after->samples->sample_paths.size() &&
                std::equal(
                    pending.history_before->samples->sample_paths.begin(),
                    pending.history_before->samples->sample_paths.end(),
                    after->samples->sample_paths.begin(),
                    [](const auto &left, const auto &right) {
                        return left.first == right.first;
                    });
            if (same_sample_slots) {
                auto entry = std::make_unique<ApplicationHistoryEntry>();
                entry->label = "Apply preset";
                entry->authoring_before = *pending.history_before;
                entry->authoring_after = std::move(*after);
                push_history(std::move(*entry));
            } else {
                state_.preset_application.error =
                    "preset applied; Undo is unavailable because the sample "
                    "slot set changed";
            }
        }
        state_.project_dirty = true;
        state_.preset_application.status = "applied";
        state_.preset_application.progress = 1;
        ++state_.preset_application.revision;
        (void)engine_.finalize_preset_activation(
            pending.activation_sequence);
        pending_preset_.reset();
    }
    if (pending_history_restore_ &&
        engine_.preset_activation_cancelled(
            pending_history_restore_->activation_sequence)) {
        engine_.reclaim_preset_activations();
        pending_history_restore_.reset();
        state_.project_error = "history restoration was cancelled";
        ++state_.revision;
    }
    if (pending_history_restore_ &&
        engine_.preset_activation_applied(
            pending_history_restore_->activation_sequence)) {
        auto pending = std::make_unique<PendingHistoryRestore>(
            std::move(*pending_history_restore_));
        const auto &timeline_target = pending->undo
            ? pending->entry.timeline_before : pending->entry.timeline_after;
        std::string restore_error;
        bool valid = performance_macros_.replace(
            pending->target.macros,
            timeline_target ? *timeline_target : timeline_.snapshot().timeline,
            pending->target.mixer, restore_error);
        if (valid && timeline_target) {
            auto transaction = timeline_.edit();
            transaction.timeline() = *timeline_target;
            transaction.set_generation_seed(timeline_target->generation_seed);
            valid = timeline_.commit(
                std::move(transaction),
                TimelineCommitPolicy::next_audio_callback, restore_error);
        }
        if (valid) {
            for (std::size_t i = 0;
                 pending->target.samples &&
                 i < pending->activation.sample_count; ++i) {
                const auto &sample = pending->activation.samples[i];
                const auto path = pending->target.samples->sample_paths.find(
                    sample.slot.value);
                samples_.adopt_preset_activation(
                    sample.slot, sample.sample,
                    path == pending->target.samples->sample_paths.end()
                        ? std::filesystem::path{}
                        : path->second,
                    pending->sample_publications[i]);
            }
            if (pending->target.samples)
                valid = slices_.adopt_activated_project(
                    *pending->target.samples, restore_error);
        }
        if (valid) {
            mixer_configuration_ = pending->target.mixer;
            modulation_configuration_ = pending->target.modulation;
            synth_instance_parameters_ = pending->target.synth_parameters;
            project_.mixer = mixer_configuration_;
            project_.modulation = modulation_configuration_;
            project_.performance_macros = pending->target.macros;
            if (pending->target.samples)
                project_.samples = *pending->target.samples;
            auto &source = pending->undo ? history_undo_ : history_redo_;
            auto &destination = pending->undo ? history_redo_ : history_undo_;
            if (!source.empty()) source.pop_back();
            if (destination.size() == application_history_capacity)
                destination.pop_front();
            destination.push_back(std::move(pending->entry));
            state_.project_dirty = true;
            state_.project_error.clear();
        } else {
            state_.project_error = "history restoration failed: " + restore_error;
        }
        (void)engine_.finalize_preset_activation(
            pending->activation_sequence);
        pending_history_restore_.reset();
        ++state_.revision;
    }
    state_.midi = project_.midi;
    state_.midi_dropped =
        midi_output_.dropped() + engine_.dropped_midi_inputs();
    state_.midi_sent = midi_output_.sent();
    state_.external_midi_clock = engine_.external_clock_state();
    if (state_.external_midi_clock.available &&
        state_.external_midi_clock.last_timestamp_ns) {
        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        state_.external_midi_clock.dropout =
            now > state_.external_midi_clock.last_timestamp_ns + 500'000'000ULL;
    }
    auto recording = recording_.status();
    if (recording.state != RecordingState::armed &&
        recording.state != RecordingState::recording &&
        recording.state != RecordingState::completed)
        recording.configuration = recording_configuration_;
    if (recording.state == RecordingState::completed) {
        std::string recording_error;
        if (!recording_.finalize_to_slot(samples_, recording_error))
            state_.project_error = std::move(recording_error);
        else
            state_.project_dirty = true;
        recording = recording_.status();
    }
    const auto playing = engine_.playing();
    const auto frame = engine_.frame_position();
    auto timeline = timeline_.snapshot();
    AudioEngine::TimelineActivationEvent activation;
    while (engine_.pop_timeline_activation(activation)) {
        ++observed_timeline_activation_count_;
        std::vector<PreparedLaunch> activated_launches;
        performance_launcher_.acknowledge(activation.frame, &activated_launches);
        for (const auto &launch : activated_launches) {
            arrangement_recording_.capture_performance(
                launch, activation.frame, launch.scene,
                launch.follow_transition,
                timeline.timeline.arrangement.fixed_velocity);
            PreparedLaunch follow;
            std::string follow_error;
            if (launch.operation != PreparedLaunchOperation::stop &&
                performance_launcher_.prepare_follow(launch, follow, follow_error)) {
                const auto session = performance_launcher_.compose_session(
                    std::span(&follow, 1));
                if (!publish_performance_launches(
                        timeline.timeline, session, std::span(&follow, 1),
                        follow_error))
                    state_.arrangement_error = follow_error;
            } else if (!follow_error.empty())
                state_.arrangement_error = follow_error;
        }
        if (pending_record_frame_ && activation.frame < *pending_record_frame_)
            continue;
        const auto activation_tick = static_cast<MusicalTick>(
            (static_cast<long double>(activation.frame) * ticks_per_quarter *
             1'000'000.0L) /
            (static_cast<long double>(engine_.sample_rate()) *
             timeline.timeline.transport.microseconds_per_quarter));
        if (pending_record_pattern_)
            arrangement_recording_.capture_pattern(pending_record_pattern_,
                                                   activation_tick);
        if (pending_record_chain_)
            arrangement_recording_.capture_chain(timeline.timeline,
                                                 activation_tick);
        pending_record_pattern_ = 0;
        pending_record_chain_ = false;
        pending_record_frame_.reset();
    }
    const auto activation_drops = engine_.dropped_timeline_activations();
    if (activation_drops != observed_timeline_activation_drops_) {
        arrangement_recording_.report_overflow(
            activation_drops - observed_timeline_activation_drops_);
        observed_timeline_activation_drops_ = activation_drops;
    }
    const auto song_end_count = engine_.song_end_count();
    if (song_end_count != observed_song_end_count_) {
        observed_song_end_count_ = song_end_count;
        const auto &arrangement = timeline.timeline.arrangement;
        if (arrangement.song_end_behavior == SongEndBehavior::return_to_live &&
            arrangement.return_live_pattern) {
            auto transaction = timeline_.edit();
            std::string return_error;
            if (transaction.launch_pattern(arrangement.return_live_pattern,
                                           false, return_error) &&
                timeline_.commit(std::move(transaction),
                                 TimelineCommitPolicy::next_audio_callback,
                                 return_error)) {
                timeline = timeline_.snapshot();
                state_.project_dirty = true;
            } else
                state_.arrangement_error = return_error.empty()
                                               ? "return-to-live failed"
                                               : std::move(return_error);
        }
    }
    if (queued_launch_frame_ &&
        engine_.last_timeline_activation_frame() >= *queued_launch_frame_) {
        auto transaction = timeline_.edit();
        std::string activation_error;
        const auto id = transaction.timeline().arrangement.queued_pattern;
        if (id && transaction.launch_pattern(id, false, activation_error) &&
            timeline_.commit_with(
                std::move(transaction),
                TimelineCommitPolicy::next_audio_callback,
                [](const Timeline &, std::string &) { return true; },
                activation_error)) {
            queued_launch_tick_.reset();
            queued_launch_frame_.reset();
            timeline = timeline_.snapshot();
            state_.project_dirty = true;
        } else if (!activation_error.empty()) {
            state_.arrangement_error = std::move(activation_error);
            queued_launch_tick_.reset();
            queued_launch_frame_.reset();
        }
    }
    const auto history = TimelineHistoryStatus{
        !history_undo_.empty() && !pending_history_restore_,
        !history_redo_.empty() && !pending_history_restore_,
        history_undo_.size(), history_redo_.size(),
        application_history_capacity};
    auto slots = samples_.states();
    const auto desired_tracks =
        std::min(timeline.timeline.tracks.size(), maximum_mixer_tracks);
    auto mixer_matches = mixer_configuration_.track_count == desired_tracks;
    for (std::size_t i = 0; mixer_matches && i < desired_tracks; ++i)
        mixer_matches = mixer_configuration_.tracks[i].track ==
                        timeline.timeline.tracks[i].id;
    if (!mixer_matches) {
        auto updated = mixer_configuration_;
        updated.track_count = desired_tracks;
        for (std::size_t i = 0; i < desired_tracks; ++i) {
            const auto id = timeline.timeline.tracks[i].id;
            const auto found = std::find_if(
                mixer_configuration_.tracks.begin(),
                mixer_configuration_.tracks.begin() +
                    mixer_configuration_.track_count,
                [&](const auto &channel) { return channel.track == id; });
            updated.tracks[i] = found == mixer_configuration_.tracks.begin() +
                                             mixer_configuration_.track_count
                                    ? MixerChannelSettings{}
                                    : *found;
            updated.tracks[i].track = id;
        }
        ++updated.revision;
        std::string ignored;
        if (engine_.publish_mixer(updated, ignored))
            mixer_configuration_ = updated;
    }
    const auto mixer = engine_.mixer_state();
    if (recording.state == RecordingState::publication_pending) {
        const auto active =
            std::find_if(slots.begin(), slots.end(), [&](const auto &slot) {
                return slot.id == recording.configuration.destination &&
                       slot.state == SampleSlotLoadState::loaded;
            });
        if (active != slots.end()) {
            recording_.acknowledge_audio_active();
            recording = recording_.status();
        }
    }
    auto slice_state = slices_.snapshot(state_.selected_sample_slot);
    auto analysis_sessions = sample_analysis_.snapshots();
    auto processing_jobs = sample_processing_.snapshots();
    auto parameters = parameters_.parameters();
    const auto modulation_runtime = engine_.modulation_snapshot();
    InstrumentInstanceId selected_instance = default_synth_instance_id;
    for (const auto &track : timeline.timeline.tracks)
        if (track.id == state_.selected_track)
            selected_instance = track.instrument_instance_id;
    for (auto &parameter : parameters) {
        if (const auto instance =
                synth_instance_parameters_.find(selected_instance);
            instance != synth_instance_parameters_.end())
            if (const auto value = instance->second.find(parameter.id);
                value != instance->second.end())
                parameter.current_value = value->second;
        parameter.effective_value = parameter.current_value;
        const auto destination = resolve_parameter_destination(parameter.id);
        if (!destination)
            continue;
        for (std::size_t route = 0;
             route < modulation_configuration_.route_count; ++route)
            if (modulation_configuration_.routes[route].destination.target ==
                    destination->target &&
                modulation_configuration_.routes[route].destination.parameter ==
                    destination->parameter &&
                (modulation_configuration_.routes[route].destination.instance ==
                     0 ||
                 modulation_configuration_.routes[route].destination.instance ==
                     selected_instance))
                parameter.effective_value = static_cast<double>(
                    modulation_runtime.effective_values[route]);
    }
    if (state_.timeline_revision != timeline.revision &&
        !state_.project_loading)
        state_.project_dirty = true;
    bool external = state_.playing != playing ||
                    state_.transport_frame != frame ||
                    state_.timeline_revision != timeline.revision ||
                    state_.can_undo != history.can_undo ||
                    state_.can_redo != history.can_redo ||
                    state_.undo_depth != history.undo_depth ||
                    state_.redo_depth != history.redo_depth ||
                    state_.sample_slots.size() != slots.size() ||
                    state_.slice_revision != slice_state.revision ||
                    state_.sample_analysis_sessions.size() !=
                        analysis_sessions.size() ||
                    state_.sample_processing_jobs.size() !=
                        processing_jobs.size() ||
                    state_.parameters.size() != parameters.size();
    external = external || state_.modulation_runtime.generation !=
                               modulation_runtime.generation;
    external = external || state_.mixer.revision != mixer.revision ||
               state_.mixer.master.left != mixer.master.left ||
               state_.mixer.master.right != mixer.master.right;
    if (!external)
        for (std::size_t i = 0; i < maximum_mixer_tracks; ++i)
            if (state_.mixer.tracks[i].left != mixer.tracks[i].left ||
                state_.mixer.tracks[i].right != mixer.tracks[i].right ||
                state_.mixer.tracks[i].clip != mixer.tracks[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < mixer_aux_count; ++i)
            if (state_.mixer.aux[i].left != mixer.aux[i].left ||
                state_.mixer.aux[i].right != mixer.aux[i].right ||
                state_.mixer.aux[i].clip != mixer.aux[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < maximum_mixer_groups; ++i)
            if (state_.mixer.groups[i].left != mixer.groups[i].left ||
                state_.mixer.groups[i].right != mixer.groups[i].right ||
                state_.mixer.groups[i].clip != mixer.groups[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < maximum_mixer_inputs; ++i)
            if (state_.mixer.inputs[i].left != mixer.inputs[i].left ||
                state_.mixer.inputs[i].right != mixer.inputs[i].right ||
                state_.mixer.inputs[i].clip != mixer.inputs[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i)
            if (state_.mixer.external_returns[i].left !=
                    mixer.external_returns[i].left ||
                state_.mixer.external_returns[i].right !=
                    mixer.external_returns[i].right ||
                state_.mixer.external_send_available[i] !=
                    mixer.external_send_available[i] ||
                state_.mixer.external_return_available[i] !=
                    mixer.external_return_available[i]) {
                external = true;
                break;
            }
    const auto &old_recording = state_.recording;
    const auto &old_configuration = old_recording.configuration;
    const auto &new_configuration = recording.configuration;
    external =
        external || old_recording.state != recording.state ||
        old_configuration.source != new_configuration.source ||
        old_configuration.source_id != new_configuration.source_id ||
        old_configuration.destination != new_configuration.destination ||
        old_configuration.channels != new_configuration.channels ||
        old_configuration.monitoring != new_configuration.monitoring ||
        old_configuration.quantization != new_configuration.quantization ||
        old_configuration.start_frame != new_configuration.start_frame ||
        old_configuration.fixed_frames != new_configuration.fixed_frames ||
        old_recording.captured_frames != recording.captured_frames ||
        old_recording.dropped_frames != recording.dropped_frames ||
        old_recording.overflow_count != recording.overflow_count ||
        old_recording.finalization_microseconds !=
            recording.finalization_microseconds ||
        old_recording.peak_left != recording.peak_left ||
        old_recording.peak_right != recording.peak_right ||
        old_recording.error != recording.error;
    if (!external)
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (state_.sample_slots[i].revision != slots[i].revision ||
                state_.sample_slots[i].state != slots[i].state) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < parameters.size(); ++i)
            if (state_.parameters[i].id != parameters[i].id ||
                state_.parameters[i].current_value !=
                    parameters[i].current_value) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < analysis_sessions.size(); ++i)
            if (state_.sample_analysis_sessions[i].id != analysis_sessions[i].id ||
                state_.sample_analysis_sessions[i].status !=
                    analysis_sessions[i].status ||
                state_.sample_analysis_sessions[i].progress !=
                    analysis_sessions[i].progress ||
                state_.sample_analysis_sessions[i].source_stale !=
                    analysis_sessions[i].source_stale ||
                state_.sample_analysis_sessions[i].error !=
                    analysis_sessions[i].error) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < processing_jobs.size(); ++i)
            if (state_.sample_processing_jobs[i].id != processing_jobs[i].id ||
                state_.sample_processing_jobs[i].status !=
                    processing_jobs[i].status ||
                state_.sample_processing_jobs[i].progress !=
                    processing_jobs[i].progress ||
                state_.sample_processing_jobs[i].error !=
                    processing_jobs[i].error ||
                state_.sample_processing_jobs[i].result.saved_path !=
                    processing_jobs[i].result.saved_path ||
                state_.sample_processing_jobs[i].result.destination !=
                    processing_jobs[i].result.destination) {
                external = true;
                break;
            }
    state_.playing = playing;
    state_.transport_frame = frame;
    const auto transport_tick =
        (static_cast<long double>(frame) * ticks_per_quarter * 1'000'000.0L) /
        (static_cast<long double>(engine_.sample_rate()) *
         timeline.timeline.transport.microseconds_per_quarter);
    state_.transport_tick =
        transport_tick >= std::numeric_limits<MusicalTick>::max()
            ? std::numeric_limits<MusicalTick>::max()
            : static_cast<MusicalTick>(transport_tick);
    state_.recording = std::move(recording);
    state_.mixer = mixer;
    state_.timeline_revision = timeline.revision;
    state_.can_undo = history.can_undo;
    state_.can_redo = history.can_redo;
    state_.history_pending = pending_history_restore_.has_value();
    state_.undo_label = history_undo_.empty() ? "" : history_undo_.back().label;
    state_.redo_label = history_redo_.empty() ? "" : history_redo_.back().label;
    state_.undo_depth = history.undo_depth;
    state_.redo_depth = history.redo_depth;
    state_.history_capacity = history.capacity;
    state_.slice_revision = slice_state.revision;
    state_.sample_frames = slice_state.sample_frames;
    state_.sample_rate = slice_state.sample_rate;
    state_.sample_channels = slice_state.channels;
    state_.slice_pad_assignments = slice_state.pad_assignments;
    state_.region_pad_assignments = slice_state.region_pad_assignments;
    state_.pad_settings = slice_state.pad_settings;
    state_.selected_pad_settings =
        slice_state.pad_settings[state_.selected_pad];
    state_.effective_stretch_ratio = state_.selected_pad_settings.stretch_ratio;
    const auto current_tempo =
        60'000'000.0 / timeline.timeline.transport.microseconds_per_quarter;
    auto stretch_source_frames = slice_state.sample_frames;
    if (const auto assigned = slice_state.pad_assignments[state_.selected_pad])
        if (const auto found = std::find_if(
                slice_state.slices.begin(), slice_state.slices.end(),
                [&](const auto &slice) { return slice.id == *assigned; });
            found != slice_state.slices.end())
            stretch_source_frames = found->end_frame - found->start_frame;
    if (const auto assigned =
            slice_state.region_pad_assignments[state_.selected_pad])
        if (const auto found = std::find_if(
                slice_state.regions.begin(), slice_state.regions.end(),
                [&](const auto &region) { return region.id == *assigned; });
            found != slice_state.regions.end())
            stretch_source_frames = found->end_frame - found->start_frame;
    const auto source_seconds =
        slice_state.sample_rate > 0
            ? static_cast<double>(stretch_source_frames) /
                  slice_state.sample_rate
            : 0.0;
    if (state_.selected_pad_settings.source_beats > 0 && source_seconds > 0)
        state_.effective_stretch_ratio =
            static_cast<float>(state_.selected_pad_settings.source_beats *
                               60.0 / current_tempo / source_seconds);
    else if (state_.selected_pad_settings.source_bpm > 0)
        state_.effective_stretch_ratio =
            state_.selected_pad_settings.source_bpm / current_tempo;
    state_.effective_stretch_ratio =
        std::clamp(state_.effective_stretch_ratio, 0.25F, 4.0F);
    state_.stretch_latency_frames = RealtimeTimeStretcher::latency_frames(
        {state_.selected_pad_settings.stretch_mode,
         state_.selected_pad_settings.stretch_quality,
         state_.effective_stretch_ratio, 1.0F, engine_.sample_rate()});
    state_.waveform_peaks = std::move(slice_state.waveform);
    state_.sample_analysis_sessions = std::move(analysis_sessions);
    state_.sample_processing_jobs = std::move(processing_jobs);
    state_.slices.clear();
    state_.slices.reserve(slice_state.slices.size());
    for (const auto &slice : slice_state.slices)
        state_.slices.push_back({slice.id, slice.name, slice.start_frame,
                                 slice.end_frame, slice.gain, slice.root_note});
    state_.regions.clear();
    state_.regions.reserve(slice_state.regions.size());
    for (const auto &region : slice_state.regions)
        state_.regions.push_back(
            {region.id,
             region.name,
             region.start_frame,
             region.end_frame,
             region.direction,
             region.fade_curve,
             region.fade_in_frames,
             region.fade_out_frames,
             region.gain,
             region.loop_enabled,
             region.loop_start_frame,
             region.loop_end_frame,
             region.loop_crossfade_frames,
             slices_.region_usage(region.id)});
    if (state_.selected_slice != 0 &&
        std::none_of(state_.slices.begin(), state_.slices.end(),
                     [&](const auto &slice) {
                         return slice.id == state_.selected_slice;
                     }))
        state_.selected_slice = 0;
    if (state_.selected_region != 0 &&
        std::none_of(state_.regions.begin(), state_.regions.end(),
                     [&](const auto &region) {
                         return region.id == state_.selected_region;
                     }))
        state_.selected_region = 0;
    state_.tempo_bpm =
        60'000'000.0 / timeline.timeline.transport.microseconds_per_quarter;
    state_.time_signature_numerator =
        timeline.timeline.transport.time_signature_numerator;
    state_.time_signature_denominator =
        timeline.timeline.transport.time_signature_denominator;
    const auto &arrangement = timeline.timeline.arrangement;
    state_.playback_mode = arrangement.playback_mode;
    state_.active_pattern = arrangement.active_pattern;
    state_.queued_pattern = arrangement.queued_pattern;
    state_.next_pattern = arrangement.queued_pattern;
    if (!state_.next_pattern && arrangement.chain_running &&
        !arrangement.chain_entries.empty()) {
        auto next = arrangement.pattern_chain_position + 1;
        if (next >= arrangement.chain_entries.size())
            next =
                arrangement.chain_loop ? 0 : arrangement.chain_entries.size();
        if (next < arrangement.chain_entries.size())
            state_.next_pattern = arrangement.chain_entries[next].pattern;
    }
    state_.launch_quantization = arrangement.launch_quantization;
    if (performance_launcher_revision_ != timeline.revision) {
        std::string ignored;
        if (performance_launcher_.configure(timeline.timeline, ignored, true))
            performance_launcher_revision_ = timeline.revision;
    }
    state_.performance_launching.slots = arrangement.launch_slots;
    state_.performance_launching.scenes = arrangement.performance_scenes;
    state_.performance_launching.mode = arrangement.performance_mode;
    state_.performance_launching.velocity_policy = arrangement.velocity_policy;
    state_.performance_launching.fixed_velocity = arrangement.fixed_velocity;
    state_.performance_launching.pending.assign(performance_launcher_.pending().begin(),
                                                 performance_launcher_.pending().end());
    state_.performance_launching.ownership = performance_launcher_.ownership();
    state_.performance_launching.diagnostics = performance_launcher_.diagnostics();
    state_.song_start = arrangement.song_start;
    state_.song_end = arrangement.song_end;
    state_.arrangement_loop_enabled = arrangement.loop_enabled;
    state_.arrangement_loop_start = arrangement.loop_start;
    state_.arrangement_loop_end = arrangement.loop_end;
    state_.next_launch_frame = queued_launch_frame_.value_or(0);
    state_.last_launch_frame = engine_.last_timeline_activation_frame();
    state_.launch_lateness_frames = engine_.last_timeline_lateness_frames();
    state_.arrangement_recording = arrangement_recording_.status();
    state_.captured_pattern_launches = arrangement_recording_.patterns();
    state_.captured_automation_changes = arrangement_recording_.automation();
    state_.captured_performance_launches =
        arrangement_recording_.performance_launches();
    state_.arrangement_automation = arrangement.automation;
    state_.patterns.clear();
    state_.patterns.reserve(arrangement.patterns.size());
    for (const auto &pattern : arrangement.patterns)
        state_.patterns.push_back({pattern.id, pattern.name, pattern.length,
                                   pattern.tracks.size(), pattern.follow});
    if (state_.selected_pattern != 0 &&
        std::none_of(state_.patterns.begin(), state_.patterns.end(),
                     [&](const auto &pattern) {
                         return pattern.id == state_.selected_pattern;
                     }))
        state_.selected_pattern = state_.patterns.empty()
            ? PatternId{} : state_.patterns.front().id;
    state_.clips.clear();
    state_.clips.reserve(arrangement.clips.size());
    for (const auto &clip : arrangement.clips)
        state_.clips.push_back({clip.id, clip.pattern, clip.start,
                                clip.duration, clip.loop, clip.enabled,
                                clip.transpose, clip.velocity_scale});
    state_.audio_clips.clear();
    state_.audio_clips.reserve(arrangement.audio_clips.size());
    for (const auto &clip : arrangement.audio_clips) {
        const auto source = samples_.status(SampleSlotId{clip.sample_slot});
        const auto available =
            source.has_sample &&
            (clip.sample_publication == 0 ||
             clip.sample_publication == source.active_publication) &&
            (clip.asset_reference.empty() ||
             source.source_path.string() == clip.asset_reference) &&
            clip.source_start_frame < clip.source_end_frame &&
            clip.source_end_frame <= source.frames;
        state_.audio_clips.push_back(
            {clip, available,
             available ? std::string{}
                       : "source asset is missing or not audio-active"});
    }
    state_.audio_crossfades = arrangement.audio_crossfades;
    state_.pattern_chain_id = arrangement.pattern_chain_id;
    state_.pattern_chain_position = arrangement.pattern_chain_position;
    state_.pattern_chain_loop = arrangement.chain_loop;
    state_.pattern_chain_running = arrangement.chain_running;
    state_.voice_transition = arrangement.voice_transition;
    state_.song_end_behavior = arrangement.song_end_behavior;
    state_.pattern_chain_entries.clear();
    state_.pattern_chain_entries.reserve(arrangement.chain_entries.size());
    for (const auto &entry : arrangement.chain_entries)
        state_.pattern_chain_entries.push_back({entry.pattern, entry.repeats});
    if (state_.selected_track == 0 && !timeline.timeline.tracks.empty())
        state_.selected_track = timeline.timeline.tracks.front().id;
    state_.tracks.clear();
    state_.generation_seed = timeline.generation_seed;
    std::size_t index{};
    for (const auto &track : timeline.timeline.tracks) {
        const auto fallback =
            static_cast<std::uint8_t>(std::min<std::size_t>(127, 36 + index++));
        const auto target = track_targets_.contains(track.id)
                                ? track_targets_.at(track.id)
                                : fallback;
        const auto *edited = edit_pattern(timeline.timeline,
                                          state_.selected_pattern, track.id);
        const auto edited_length = edit_pattern_length(
            timeline.timeline, state_.selected_pattern, track.id);
        state_.tracks.push_back(
            {track.id, track.name, track.muted,
             edited_length > 0 ? edited_length : track.pattern.length, target,
             track.instrument_id,
             edited ? edited->events : std::vector<MusicalEvent>{},
             track.instrument_instance_id});
    }
    state_.selected_event.reset();
    state_.selected_step_locks.clear();
    const auto selected_track = std::find_if(
        timeline.timeline.tracks.begin(), timeline.timeline.tracks.end(),
        [&](const Track &value) { return value.id == state_.selected_track; });
    const auto *selected_pattern = edit_pattern(
        timeline.timeline, state_.selected_pattern, state_.selected_track);
    if (selected_track != timeline.timeline.tracks.end()) {
        const auto lock_tick = state_.selected_step * state_.step_ticks;
        if (selected_pattern)
        for (const auto &event : selected_pattern->events)
            if (event.type == MusicalEventType::parameter &&
                event.tick == lock_tick && !event.parameter_destination.empty())
                state_.selected_step_locks.push_back(
                    {event.id, event.parameter_destination, event.value});
        std::sort(state_.selected_step_locks.begin(),
                  state_.selected_step_locks.end(),
                  [](const auto &a, const auto &b) {
                      return a.parameter_id < b.parameter_id;
                  });
    }
    if (selected_event_id_) {
        const auto track = selected_track;
        if (track != timeline.timeline.tracks.end() && selected_pattern) {
            const auto selected = std::find_if(
                selected_pattern->events.begin(), selected_pattern->events.end(),
                [&](const MusicalEvent &event) {
                    return event.id == *selected_event_id_ &&
                           event.type == MusicalEventType::note;
                });
            if (selected != selected_pattern->events.end()) {
                const auto base = state_.selected_step * state_.step_ticks;
                std::size_t index_at_step{}, events_at_step{};
                for (const auto &event : selected_pattern->events)
                    if (event.type == MusicalEventType::note &&
                        event.tick >= base &&
                        event.tick <
                            std::min<MusicalTick>(base + state_.step_ticks,
                                                  selected_pattern->length)) {
                        if (event.id == selected->id)
                            index_at_step = events_at_step;
                        ++events_at_step;
                    }
                state_.selected_event =
                    SelectedEventState{track->id,
                                       selected->id,
                                       state_.selected_step,
                                       selected->tick,
                                       selected->tick - base,
                                       selected->pitch,
                                       selected->velocity,
                                       selected->duration,
                                       index_at_step,
                                       events_at_step,
                                       selected->trigger_probability,
                                       selected->trigger_condition,
                                       selected->condition_n,
                                       selected->condition_m,
                                       selected->ratchet_count};
            } else
                selected_event_id_.reset();
        }
    }
    state_.sample_slots = std::move(slots);
    state_.parameters = std::move(parameters);
    std::string macro_graph_error;
    if (!performance_macros_.compile(modulation_configuration_,
                                     state_.modulation, macro_graph_error)) {
        state_.modulation = modulation_configuration_;
        state_.project_error = macro_graph_error;
    }
    state_.modulation_runtime = modulation_runtime;
    ModulationGraphDiagnostics graph;
    (void)validate_modulation_graph(state_.modulation, graph);
    state_.modulation_graph = std::move(graph);
    state_.performance_macros = performance_macros_.snapshot();
    const auto runtime_morphs = engine_.macro_morphs();
    for (const auto &macro : state_.performance_macros.macros) {
        auto effective = macro.value;
        const auto runtime = std::find_if(
            runtime_morphs.begin(), runtime_morphs.end(),
            [&](const auto &candidate) {
                return candidate.instance == macro.source_instance &&
                       (candidate.active || candidate.scheduled ||
                        candidate.completed || candidate.interrupted);
            });
        if (runtime != runtime_morphs.end()) {
            effective = runtime->effective_value;
            state_.performance_macros.active_morphs.push_back(*runtime);
            if ((runtime->active || runtime->scheduled) &&
                !state_.performance_macros.morph.active) {
                state_.performance_macros.morph.active = true;
                state_.performance_macros.morph.tempo_sync =
                    runtime->duration_unit == MacroMorphDurationUnit::beats;
                state_.performance_macros.morph.progress = runtime->progress;
                state_.performance_macros.morph.start_frame =
                    runtime->start_frame;
                state_.performance_macros.morph.duration_frames =
                    runtime->end_frame >= runtime->start_frame
                        ? runtime->end_frame - runtime->start_frame
                        : 0;
            }
        }
        state_.performance_macros.effective_values.emplace_back(macro.id,
                                                                 effective);
    }
    state_.performance_macros.dropped_morph_commands =
        engine_.dropped_macro_morph_commands();
    state_.performance.generation = system_monitor_.generation();
    state_.performance.monotonic_milliseconds =
        system_monitor_.monotonic_milliseconds();
    state_.performance.audio = engine_.callback_statistics();
    state_.performance.engine = {engine_.sample_rate(),
                                 engine_.buffer_frames(),
                                 engine_.active_voice_count(),
                                 engine_.voice_capacity(),
                                 engine_.playing(),
                                 engine_.dropped_triggers(),
                                 engine_.dropped_patterns(),
                                 engine_.dropped_timelines(),
                                 engine_.timeline_event_overruns(),
                                 engine_.dropped_legacy_events(),
                                 engine_.dropped_transport_commands(),
                                 engine_.dropped_sample_updates(),
                                 engine_.missing_slot_triggers(),
                                 engine_.stolen_voices(),
                                 rejected_,
                                 engine_.dropped_dsp_commands(),
                                 engine_.synth_state().stolen_voices,
                                 engine_.instrument_nanoseconds(),
                                 engine_.effect_nanoseconds(),
                                 engine_.synth_state().active_voices,
                                 engine_.synth_state().maximum_voices,
                                 state_.recording.captured_frames,
                                 state_.recording.capacity_frames,
                                 state_.recording.dropped_frames,
                                 state_.recording.overflow_count,
                                 state_.recording.finalization_microseconds,
                                 state_.recording.memory_bytes,
                                 state_.mixer.rejected_publications};
    state_.performance.system = system_monitor_.snapshot();
    state_.available_actions.clear();
    for (const auto &value : actions_.actions())
        if (value.available &&
            (value.id != "edit.undo.v1" || history.can_undo) &&
            (value.id != "edit.redo.v1" || history.can_redo))
            state_.available_actions.push_back(value.id);
    if (external)
        ++state_.revision;
}
void ApplicationService::changed_locked() { ++state_.revision; }
ApplicationState ApplicationService::snapshot() const {
    return *snapshot_shared();
}
std::shared_ptr<const ApplicationState> ApplicationService::snapshot_shared() const {
    std::lock_guard lock(mutex_);
    synchronize_locked();
    if (!published_snapshot_ || published_snapshot_->revision != state_.revision)
        published_snapshot_ = std::make_shared<const ApplicationState>(state_);
    return published_snapshot_;
}
std::uint64_t ApplicationService::rejected_commands() const noexcept {
    std::lock_guard lock(mutex_);
    return rejected_;
}

const char *step_resolution_name(StepResolution value) noexcept {
    switch (value) {
    case StepResolution::quarter:
        return "1/4";
    case StepResolution::eighth:
        return "1/8";
    case StepResolution::sixteenth:
        return "1/16";
    case StepResolution::thirty_second:
        return "1/32";
    case StepResolution::quarter_triplet:
        return "1/4T";
    case StepResolution::eighth_triplet:
        return "1/8T";
    case StepResolution::sixteenth_triplet:
        return "1/16T";
    case StepResolution::thirty_second_triplet:
        return "1/32T";
    }
    return "1/16";
}
MusicalTick step_resolution_ticks(StepResolution value) noexcept {
    switch (value) {
    case StepResolution::quarter:
        return 960;
    case StepResolution::eighth:
        return 480;
    case StepResolution::sixteenth:
        return 240;
    case StepResolution::thirty_second:
        return 120;
    case StepResolution::quarter_triplet:
        return 640;
    case StepResolution::eighth_triplet:
        return 320;
    case StepResolution::sixteenth_triplet:
        return 160;
    case StepResolution::thirty_second_triplet:
        return 80;
    }
    return 240;
}
std::optional<StepResolution>
parse_step_resolution(std::string_view value) noexcept {
    for (const auto resolution :
         {StepResolution::quarter, StepResolution::eighth,
          StepResolution::sixteenth, StepResolution::thirty_second,
          StepResolution::quarter_triplet, StepResolution::eighth_triplet,
          StepResolution::sixteenth_triplet,
          StepResolution::thirty_second_triplet})
        if (value == step_resolution_name(resolution))
            return resolution;
    return std::nullopt;
}

void TextEntryModel::begin(std::string initial) {
    state_ = {std::move(initial), 0, 'A', true, false, false};
    state_.cursor = state_.text.size();
}
void TextEntryModel::direct(std::string_view text) {
    if (!state_.active)
        return;
    state_.text.insert(state_.cursor, text);
    state_.cursor += text.size();
}
void TextEntryModel::select_relative(int delta) {
    if (!state_.active)
        return;
    const auto available = characters();
    auto found = available.find(state_.selected);
    auto index = found == std::string_view::npos ? 0 : static_cast<int>(found);
    index = (index + delta % static_cast<int>(available.size()) +
             static_cast<int>(available.size())) %
            static_cast<int>(available.size());
    state_.selected = available[static_cast<std::size_t>(index)];
}
void TextEntryModel::select(char value) {
    if (state_.active && characters().find(value) != std::string_view::npos)
        state_.selected = value;
}
void TextEntryModel::insert() {
    if (state_.active) {
        state_.text.insert(state_.cursor, 1, state_.selected);
        ++state_.cursor;
    }
}
void TextEntryModel::erase_or_cancel() {
    if (!state_.active)
        return;
    if (state_.cursor > 0) {
        state_.text.erase(--state_.cursor, 1);
    } else
        discard();
}
void TextEntryModel::move_cursor(int delta) {
    if (!state_.active)
        return;
    const auto position = static_cast<std::int64_t>(state_.cursor) + delta;
    state_.cursor = static_cast<std::size_t>(std::clamp<std::int64_t>(
        position, 0, static_cast<std::int64_t>(state_.text.size())));
}
void TextEntryModel::commit() {
    if (state_.active) {
        state_.active = false;
        state_.committed = true;
    }
}
void TextEntryModel::discard() {
    if (state_.active) {
        state_.active = false;
        state_.discarded = true;
    }
}

NormalizedXY XYControl::apply(double x, double y) {
    if (behavior_.relative) {
        x += value_.x;
        y += value_.y;
    }
    if (behavior_.invert_x)
        x = 1 - x;
    if (behavior_.invert_y)
        y = 1 - y;
    value_.x = std::clamp(0.5 + (x - 0.5) * behavior_.scale_x, 0.0, 1.0);
    value_.y = std::clamp(0.5 + (y - 0.5) * behavior_.scale_y, 0.0, 1.0);
    return value_;
}
void XYControl::release() {
    if (behavior_.spring_to_center && !behavior_.latching)
        value_ = {0.5, 0.5};
}
const char *pad_mode_name(PadMode mode) noexcept {
    switch (mode) {
    case PadMode::step:
        return "step";
    case PadMode::pad:
        return "pad";
    case PadMode::levels:
        return "levels";
    case PadMode::notes:
        return "notes";
    }
    return "unknown";
}
} // namespace neurogenic
