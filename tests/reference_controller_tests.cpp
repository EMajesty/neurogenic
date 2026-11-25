#include "neurogenic/application/controller_session.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
ControllerInput input(std::string id, bool pressed, std::uint64_t sequence,
                      std::uint64_t generation = 1) {
    return {std::move(id), pressed ? 1.0 : 0.0, 0.0, pressed,
            sequence,      generation};
}
ControllerInput turn(std::string id, double value, std::uint64_t sequence,
                     std::uint64_t generation = 1) {
    return {std::move(id), value, 0.0, true, sequence, generation};
}
} // namespace

int main() try {
    auto descriptor = reference_controller_v2_descriptor();
    auto profile = reference_controller_v2_profile();
    require(descriptor.controls.size() == 67,
            "V2 inventory is not 58 buttons plus nine encoders");
    std::set<std::string> identities;
    std::size_t buttons{}, encoders{}, pads{};
    for (const auto &control : descriptor.controls) {
        require(identities.insert(control.id).second,
                "duplicate V2 logical identity");
        if (control.type == ControlType::relative_encoder)
            ++encoders;
        else
            ++buttons;
        if (control.id.starts_with("pad."))
            ++pads;
    }
    require(buttons == 58 && encoders == 9 && pads == 16,
            "V2 logical control class counts differ from the locked inventory");

    AudioEngine engine(48'000, 256);
    Timeline initial;
    for (TrackId id : {41U, 42U}) {
        Track track;
        track.id = id;
        track.name = id == 41 ? "Drums" : "Bass";
        track.instrument_id = std::string(subtractive_instrument_id);
        track.pattern.length = ticks_per_quarter * 8;
        initial.tracks.push_back(track);
    }
    PatternDefinition pattern;
    pattern.id = 91;
    pattern.name = "Controller pattern";
    pattern.length = ticks_per_quarter * 8;
    PatternTrack drums{41, {pattern.length, {}}};
    PatternTrack bass{42, {pattern.length, {}}};
    bass.pattern.events.push_back(
        {500, static_cast<MusicalTick>(16 * (ticks_per_quarter / 4)),
         MusicalEventType::note, 36, 0.7F, ticks_per_quarter / 4});
    bass.pattern.events.push_back(
        {501, static_cast<MusicalTick>(17 * (ticks_per_quarter / 4)),
         MusicalEventType::note, 37, 0.8F, ticks_per_quarter / 4});
    pattern.tracks = {std::move(drums), std::move(bass)};
    initial.arrangement.patterns.push_back(std::move(pattern));
    PatternDefinition other;
    other.id = 92;
    other.name = "Isolated pattern";
    other.length = ticks_per_quarter * 8;
    other.tracks = {{41, {other.length, {}}}, {42, {other.length, {}}}};
    initial.arrangement.patterns.push_back(std::move(other));
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    std::uint64_t now{1000};
    ControllerSession session(application, descriptor, profile, "reference-v2",
                              128, [&] { return now; });
    require(session.valid(), session.state().error);
    std::string error;
    require(
        session.configure_gestures(reference_controller_v2_gestures(), error),
        error);

    std::uint64_t sequence{1};
    for (std::size_t i = 0; i < 8; ++i) {
        require(session.input(
                    input("page." + std::to_string(i + 1), true, sequence++),
                    error),
                error);
        require(session.state().destination ==
                    reference_primary_destinations()[i].id,
                "primary page routing mismatch");
        require(session.input(
                    input("page." + std::to_string(i + 1), false, sequence++),
                    error),
                error);
    }
    require(session.input(input("edit.shift", true, sequence++), error), error);
    require(session.state().modifiers.shift,
            "held SHIFT did not become effective");
    for (std::size_t i = 0; i < 8; ++i) {
        require(session.input(
                    input("page." + std::to_string(i + 1), true, sequence++),
                    error),
                error);
        require(session.state().destination ==
                    reference_secondary_destinations()[i].id,
                "secondary page routing mismatch");
        require(session.state().modifiers.shift,
                "page context transition incorrectly cancelled global SHIFT");
        require(session.input(
                    input("page." + std::to_string(i + 1), false, sequence++),
                    error),
                error);
    }
    require(session.input(input("edit.shift", false, sequence++), error),
            error);
    require(session.state().destination == "setup" &&
                !session.state().modifiers.shift,
            "SHIFT release redirected the active secondary destination");

    for (int tap = 0; tap < 2; ++tap) {
        now += 60;
        require(session.input(input("edit.shift", true, sequence++), error),
                error);
        now += 20;
        require(session.input(input("edit.shift", false, sequence++), error),
                error);
    }
    require(session.state().modifiers.shift_latched,
            "double-tap SHIFT did not latch through ControllerSession");
    require(session.input(input("page.2", true, sequence++), error), error);
    require(session.state().destination == "song",
            "latched SHIFT did not select the secondary destination");
    require(session.input(input("page.2", false, sequence++), error), error);
    for (int tap = 0; tap < 2; ++tap) {
        now += 60;
        require(session.input(input("edit.shift", true, sequence++), error),
                error);
        now += 20;
        require(session.input(input("edit.shift", false, sequence++), error),
                error);
    }
    require(!session.state().modifiers.shift_latched,
            "double-tap SHIFT did not explicitly unlatch");

    const auto root = session.state().root_revision;
    require(session.input(input("page.8", true, sequence++), error), error);
    require(session.state().destination == "browser",
            "normal labels were not restored");
    require(session.input(input("page.8", false, sequence++), error), error);
    require(session.input(input("page.8", true, sequence++), error), error);
    require(session.state().root_revision > root,
            "active destination did not return to root");
    require(session.input(input("page.8", false, sequence++), error), error);

    session.set_modal_context("confirm-delete");
    require(!session.input(input("page.1", true, sequence++), error) &&
                session.state().destination == "browser",
            "page input bypassed modal ownership");
    session.set_modal_context({});

    require(session.input(input("edit.shift", true, sequence++), error), error);
    session.reconnect();
    require(!session.state().modifiers.shift &&
                session.state().interaction.effective_modifiers.empty(),
            "reconnect retained physical modifier ownership");
    const auto generation = session.state().interaction.connection_generation;
    require(!session.input(input("page.1", true, sequence++, generation - 1),
                           error),
            "stale-generation page input was accepted");
    require(session.input(input("page.1", true, sequence++, generation), error),
            error);
    require(session.state().destination == "performance",
            "new-generation navigation did not recover");

    const auto selected_pattern = application.execute(
        {"pattern.select.v1", {{"pattern_id", std::int64_t{91}}},
         "reference-v2-setup", 0});
    require(selected_pattern.accepted, selected_pattern.error);

    // Production cluster integration: all commands enter through the actual
    // profile, gesture arbiter and ControllerSession command queue.
    const auto current_generation =
        session.state().interaction.connection_generation;
    const auto send = [&](ControllerInput value) {
        require(session.input(value, error), error);
    };
    const auto dispatch = [&] {
        const auto results = application.dispatch_pending();
        require(!results.empty(), "controller input dispatched no command");
        for (const auto &result : results)
            require(result.accepted, result.error);
        std::array<float, 256> left{}, right{};
        engine.process(left, right);
        (void)application.snapshot_shared();
    };
    send(input("page.1", false, sequence++, current_generation));
    send(input("page.2", true, sequence++, current_generation));
    send(input("page.2", false, sequence++, current_generation));
    require(session.state().destination == "sequencer",
            "cluster scenario did not enter Sequencer");

    send(input("edit.track", true, sequence++, current_generation));
    send(input("pad.2", true, sequence++, current_generation));
    dispatch();
    send(input("pad.2", false, sequence++, current_generation));
    send(input("edit.track", false, sequence++, current_generation));
    require(application.snapshot_shared()->selected_track == 42,
            "TRACK + pad did not select the stable logical track");
    require(application.snapshot_shared()->selected_step == 0,
            "TRACK chord leaked the ordinary pad operation");

    send(input("edit.page", true, sequence++, current_generation));
    send(input("pad.2", true, sequence++, current_generation));
    dispatch();
    send(input("edit.page", false, sequence++, current_generation));
    send(input("pad.2", false, sequence++, current_generation));
    require(application.snapshot_shared()->step_page == 1 &&
                application.snapshot_shared()->selected_step == 16,
            "PAGE + pad did not select the absolute second step page");

    send(input("edit.erase", true, sequence++, current_generation));
    send(input("pad.2", true, sequence++, current_generation));
    dispatch();
    send(input("pad.2", false, sequence++, current_generation));
    send(input("edit.erase", false, sequence++, current_generation));
    auto state = application.snapshot_shared();
    const auto bass_track = std::find_if(state->tracks.begin(), state->tracks.end(),
        [](const auto &track) { return track.id == 42; });
    require(bass_track != state->tracks.end() &&
                std::none_of(bass_track->events.begin(), bass_track->events.end(),
                    [](const auto &event) {
                        return event.tick ==
                            static_cast<MusicalTick>(17 * (ticks_per_quarter / 4));
                    }),
            "ERASE + pad did not remove the intended reusable-pattern event");

    send(input("edit.undo", true, sequence++, current_generation));
    dispatch();
    send(input("edit.undo", false, sequence++, current_generation));
    require(application.snapshot_shared()->can_redo,
            "dedicated UNDO did not enter unified history");
    send(input("edit.shift", true, sequence++, current_generation));
    send(input("edit.undo", true, sequence++, current_generation));
    dispatch();
    send(input("edit.undo", false, sequence++, current_generation));
    send(input("edit.shift", false, sequence++, current_generation));

    send(input("edit.mute", true, sequence++, current_generation));
    send(input("pad.2", true, sequence++, current_generation));
    dispatch();
    send(input("pad.2", false, sequence++, current_generation));
    send(input("edit.mute", false, sequence++, current_generation));
    state = application.snapshot_shared();
    require(std::find_if(state->tracks.begin(), state->tracks.end(),
                         [](const auto &track) { return track.id == 42; })->muted,
            "MUTE + pad did not mutate the intended stable track");

    send(input("transport.fill", true, sequence++, current_generation));
    dispatch();
    require(application.snapshot_shared()->fill_active,
            "momentary Fill press was not authoritative");
    send(input("transport.fill", false, sequence++, current_generation));
    dispatch();
    require(!application.snapshot_shared()->fill_active,
            "momentary Fill release did not restore state");

    const auto tempo = application.snapshot_shared()->tempo_bpm;
    send(input("transport.tap", true, sequence++, current_generation));
    send(turn("data", 2.0, sequence++, current_generation));
    dispatch();
    send(input("transport.tap", false, sequence++, current_generation));
    require(application.snapshot_shared()->tempo_bpm > tempo,
            "TAP + DATA did not adjust authoritative tempo");

    send(input("transport.play", true, sequence++, current_generation));
    dispatch();
    send(input("transport.play", false, sequence++, current_generation));
    require(application.snapshot_shared()->playing,
            "dedicated PLAY did not start transport");
    send(input("transport.stop", true, sequence++, current_generation));
    dispatch();
    send(input("transport.stop", false, sequence++, current_generation));
    require(!application.snapshot_shared()->playing,
            "dedicated STOP did not stop transport");

    // Contextual sequencer pads defer only in this context. A plain tap
    // toggles once; an encoder chord consumes that tap and captures the
    // reusable-pattern target for one grouped history entry.
    send(input("pad.2", true, sequence++, current_generation));
    send(input("pad.2", false, sequence++, current_generation));
    dispatch();
    send(input("pad.4", true, sequence++, current_generation));
    require(application.dispatch_pending().empty(),
            "sequencer pad tap dispatched before release");
    send(input("pad.4", false, sequence++, current_generation));
    dispatch();
    auto event_at = [&](std::int64_t step, PatternId pattern_id = 91)
        -> std::optional<MusicalEvent> {
        const auto timeline_state = timeline.snapshot().timeline;
        const auto definition = std::find_if(
            timeline_state.arrangement.patterns.begin(),
            timeline_state.arrangement.patterns.end(),
            [&](const auto &value) { return value.id == pattern_id; });
        if (definition == timeline_state.arrangement.patterns.end()) return {};
        const auto pattern_track = std::find_if(
            definition->tracks.begin(), definition->tracks.end(),
            [](const auto &value) { return value.track == 42; });
        if (pattern_track == definition->tracks.end()) return {};
        const auto tick = step * (ticks_per_quarter / 4);
        const auto found = std::find_if(
            pattern_track->pattern.events.begin(), pattern_track->pattern.events.end(),
            [&](const auto &event) {
                return event.type == MusicalEventType::note && event.tick == tick;
            });
        return found == pattern_track->pattern.events.end()
            ? std::optional<MusicalEvent>{} : *found;
    };
    require(event_at(19).has_value(),
            "ordinary sequencer tap did not toggle exactly the intended step");

    const auto history_before = application.snapshot_shared()->undo_depth;
    const auto velocity_before = event_at(17)->velocity;
    send(input("pad.2", true, sequence++, current_generation));
    send(turn("parameter.2", -1.0, sequence++, current_generation));
    send(turn("parameter.2", -1.0, sequence++, current_generation));
    send(input("pad.2", false, sequence++, current_generation));
    dispatch();
    require(event_at(17)->velocity < velocity_before,
            "held-step velocity encoder did not edit the stable absolute step: " +
                std::to_string(velocity_before) + " -> " +
                std::to_string(event_at(17)->velocity));
    require(application.snapshot_shared()->undo_depth == history_before + 1,
            "held-step encoder increments were not coalesced into one history entry");

    const auto cutoff_before = std::get<double>(
        application.parameters().get("synth.cutoff")->current_value);
    send(input("pad.2", true, sequence++, current_generation));
    send(turn("parameter.8", 1.0, sequence++, current_generation));
    send(turn("parameter.8", 1.0, sequence++, current_generation));
    send(input("pad.2", false, sequence++, current_generation));
    dispatch();
    const auto locked_timeline = timeline.snapshot().timeline;
    const auto locked_definition = std::find_if(
        locked_timeline.arrangement.patterns.begin(),
        locked_timeline.arrangement.patterns.end(),
        [](const auto &value) { return value.id == 91; });
    const auto locked_track = std::find_if(
        locked_definition->tracks.begin(), locked_definition->tracks.end(),
        [](const auto &value) { return value.track == 42; });
    const auto &locked_events = locked_track->pattern.events;
    const auto locked = std::any_of(
        locked_events.begin(), locked_events.end(),
        [](const auto &event) {
            return event.type == MusicalEventType::parameter &&
                   event.tick == 17 * (ticks_per_quarter / 4) &&
                   event.parameter_destination == "synth.cutoff";
        });
    require(locked, "held-step encoder did not create the parameter lock");
    require(std::get<double>(application.parameters().get("synth.cutoff")
                                 ->current_value) == cutoff_before,
            "held-step lock editing mutated the stored base parameter");
    const auto isolation_timeline = timeline.snapshot().timeline;
    const auto isolation_definition = std::find_if(
        isolation_timeline.arrangement.patterns.begin(),
        isolation_timeline.arrangement.patterns.end(),
        [](const auto &value) { return value.id == 92; });
    const auto isolation_track = std::find_if(
        isolation_definition->tracks.begin(), isolation_definition->tracks.end(),
        [](const auto &value) { return value.track == 42; });
    require(isolation_track->pattern.events.empty(),
            "held-step editing leaked into another reusable pattern");

    send(input("edit.undo", true, sequence++, current_generation));
    dispatch();
    send(input("edit.undo", false, sequence++, current_generation));
    require(application.snapshot_shared()->can_redo,
            "parameter-lock gesture did not participate in unified history");
    send(input("edit.shift", true, sequence++, current_generation));
    send(input("edit.undo", true, sequence++, current_generation));
    dispatch();
    send(input("edit.undo", false, sequence++, current_generation));
    send(input("edit.shift", false, sequence++, current_generation));

    send(input("edit.select", true, sequence++, current_generation));
    send(input("pad.1", true, sequence++, current_generation));
    dispatch();
    send(input("pad.1", false, sequence++, current_generation));
    send(input("pad.2", true, sequence++, current_generation));
    dispatch();
    send(input("pad.2", false, sequence++, current_generation));
    send(input("edit.select", false, sequence++, current_generation));
    require(application.snapshot_shared()->selected_steps.size() == 2,
            "SELECT + pad did not create a bounded stable multi-step selection");
    const auto grouped_before = event_at(16)->velocity;
    send(input("pad.2", true, sequence++, current_generation));
    send(turn("parameter.2", 1.0, sequence++, current_generation));
    send(input("pad.2", false, sequence++, current_generation));
    dispatch();
    require(event_at(16)->velocity > grouped_before,
            "held encoder did not apply one grouped edit to selected steps");

    const auto v2_context = resolve_controller_context(
        *application.snapshot_shared(), "sequencer:v2", {});
    require(v2_context[0].role == "step-property" &&
                v2_context[7].role == "step-lock" &&
                v2_context[7].parameter_id == "synth.cutoff" &&
                v2_context[0].bank_id == "v2",
            "controller/frontend shared contextual descriptors are incomplete");

    send(input("edit.track", true, sequence++, current_generation));
    session.reconnect();
    require(session.state().interaction.held_controls.empty(),
            "cluster reconnect retained momentary ownership");

    std::cout << "neurogenic.reference-controller-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
