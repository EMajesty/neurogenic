#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace neurogenic;

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    ApplicationState state;
    state.revision = 42;
    state.selected_track = 7;
    state.selected_pattern = 11;
    state.step_ticks = ticks_per_quarter / 4;
    state.patterns.push_back({11, "Verse", ticks_per_quarter * 8, 1, {}});
    state.tracks.push_back({7, "Synth"});
    state.selected_sample_slot = SampleSlotId{3};
    SampleSlotStatus slot;
    slot.id = SampleSlotId{3};
    slot.has_sample = true;
    slot.source_path = "kick.wav";
    state.sample_slots.push_back(slot);
    state.selected_slice = 9;
    state.slices.push_back({9, "Kick", 0, 1024, 1.0F, {}});
    state.available_actions = {
        "arrangement.clip-create.v1", "arrangement.clip-duplicate.v1",
        "edit.redo.v1", "edit.undo.v1", "effect.bypass.v1",
        "effect.create.v1", "effect.delete.v1", "effect.query.v1",
        "effect.reorder.v1", "macro.scene-capture.v1",
        "macro.scene-delete.v1", "macro.scene-duplicate.v1",
        "macro.scene-recall.v1", "macro.scene-rename.v1",
        "macro.scene-update.v1", "macro.value-set.v1",
        "mixer.channel-select.v1", "mixer.group-select.v1",
        "modulation.route-select.v1", "navigation.cancel.v1",
        "navigation.confirm.v1", "navigation.focus-next.v1",
        "navigation.focus-previous.v1", "pad.release.v1", "pad.trigger.v1",
        "pattern.launch.v1", "pattern.next.v1", "pattern.previous.v1",
        "pattern.select.v1", "preset.apply.v1", "preset.query.v1",
        "sample.preview.v1", "sample.slot-select.v1", "slice.delete.v1",
        "slice.preview-stop.v1", "slice.preview.v1", "slice.select.v1",
        "step.page-next.v1", "step.page-previous.v1", "step.selection-toggle.v1",
        "step.toggle.v1", "track.select.v1", "transport.play.v1"};
    state.parameters = {
        {"synth.cutoff", "Cutoff", "Hz", ParameterType::real, 20.0, 20000.0,
         1000.0, 1000.0, 1.0, true, true, true},
        {"sample.gain", "Gain", "dB", ParameterType::real, -60.0, 12.0,
         0.0, 0.0, .1, true, true, true},
        {"mixer.selected.gain", "Gain", "dB", ParameterType::real, -60.0,
         12.0, 0.0, 0.0, .1, true, true, true},
        {"effect.delay.mix", "Mix", "%", ParameterType::real, 0.0, 1.0,
         .25, .25, .01, true, true, true}};

    const std::array<std::string_view, 16> destinations{
        "performance", "xy", "sequencer", "song", "sample", "slice",
        "sound", "presets", "mixer", "fx", "modulation",
        "modulation-routing", "macros", "scenes", "browser", "setup"};
    std::set<std::string> identities;
    for (const auto destination : destinations) {
        for (const bool shifted : {false, true}) {
            ModifierState modifiers;
            modifiers.shift = shifted;
            const auto identity = std::string(destination) + ":v2";
            const auto controls = resolve_controller_context(state, identity, modifiers);
            const auto pads = resolve_controller_pads(state, identity, modifiers);
            require(controls.size() == 16 && pads.size() == 16,
                    "context inventory is incomplete");
            for (std::size_t index = 0; index < controls.size(); ++index) {
                require(controls[index].control_id == index,
                        "context control identity is unstable");
                require(!controls[index].bank_id.empty(),
                        "context control bank identity is missing");
                if (!controls[index].active)
                    require(controls[index].parameter_id.empty() ||
                                controls[index].action_id.empty(),
                            "unavailable contextual control can dispatch ambiguously");
            }
            for (std::size_t index = 0; index < pads.size(); ++index) {
                require(pads[index].position == index,
                        "context pad identity is unstable");
                require(!pads[index].bank_id.empty(),
                        "context pad bank identity is missing");
                if (pads[index].available) {
                    require(!pads[index].action_id.empty(),
                            "available context pad has no semantic action");
                    require(!pads[index].target_identity.empty(),
                            "available context pad has no stable target");
                }
            }
            identities.insert(identity + (shifted ? ":shift" : ":normal"));
        }
    }
    require(identities.size() == 32,
            "primary and secondary destination identities collapsed");

    const auto sound = resolve_controller_pads(state, "sound:v2", {});
    require(sound[0].action_id == "pad.trigger.v1" &&
                sound[0].release_action_id == "pad.release.v1" &&
                sound[0].available,
            "Sound pads do not retain matching note release ownership");
    ModifierState shift;
    shift.shift = true;
    const auto shifted_sound = resolve_controller_pads(state, "sound:v2", shift);
    require(!shifted_sound[0].available && shifted_sound[0].action_id.empty(),
            "unsupported shifted Sound selection can trigger a note");
    const auto sequencer = resolve_controller_pads(state, "sequencer:v2", {});
    const auto sequencer_shift = resolve_controller_pads(state, "sequencer:v2", shift);
    require(sequencer[0].action_id == "step.toggle.v1" &&
                sequencer_shift[0].action_id == "step.selection-toggle.v1",
            "Sequencer pad primary/selection policy regressed");
    require(sequencer[0].target_identity == sequencer_shift[0].target_identity,
            "Sequencer pad layers do not address the same stable step");

    std::cout << "neurogenic.contextual-coverage-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
