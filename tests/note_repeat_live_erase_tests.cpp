#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    // Production MIDI scheduling proves exact grid placement independently of
    // callback size.  120 BPM at 48 kHz gives 6000 frames per sixteenth.
    AudioEngine repeat_engine(48'000, 64);
    MidiOutputScheduler midi;
    repeat_engine.set_midi_output_scheduler(&midi);
    NoteRepeatTarget target;
    target.owner = 7;
    target.generation = 3;
    target.kind = NoteRepeatTargetKind::external_midi;
    target.note = 64;
    target.velocity = 0.75F;
    target.midi_port = 1;
    target.midi_channel = 2;
    target.division = NoteRepeatDivision::sixteenth;
    require(repeat_engine.begin_note_repeat(target), "repeat begin rejected");
    std::array<float, 257> left{}, right{};
    std::vector<MidiPacket> packets;
    for (int block = 0; block < 50; ++block) {
        repeat_engine.process(left, right);
        midi.dispatch([&](const MidiPacket &packet) {
            packets.push_back(packet);
            return true;
        });
    }
    std::vector<std::uint64_t> note_ons, note_offs;
    for (const auto &packet : packets) {
        if ((packet.bytes[0] & 0xF0) == 0x90) note_ons.push_back(packet.frame);
        if ((packet.bytes[0] & 0xF0) == 0x80) note_offs.push_back(packet.frame);
    }
    require(note_ons.size() >= 3 && note_ons[0] == 0 && note_ons[1] == 6000 &&
                note_ons[2] == 12000,
            "straight repeat was not phase-exact across 257-frame blocks");
    require(note_offs.size() >= 2 && note_offs[0] == 3000 &&
                note_offs[1] == 9000,
            "repeat gate was not the documented half division");
    require(repeat_engine.end_note_repeat(7, 3), "repeat end rejected");
    repeat_engine.process(left, right);
    require(repeat_engine.note_repeat_state().active_targets == 0,
            "repeat target survived release");

    AudioEngine internal_engine(48'000, 64);
    internal_engine.set_sample(
        Sample{48'000, 1, std::vector<float>(12'000, 0.25F)});
    target.kind = NoteRepeatTargetKind::sample;
    target.note = 36;
    target.owner = 8;
    target.division = NoteRepeatDivision::thirty_second;
    require(internal_engine.begin_note_repeat(target),
            "sample repeat begin rejected");
    internal_engine.process(left, right);
    double sample_energy{};
    for (float value : left) sample_energy += std::abs(value);
    require(sample_energy > 0.1 && internal_engine.active_voice_count() != 0,
            "sample repeat did not use the production sample voice path");
    require(internal_engine.end_note_repeat(target.owner, target.generation),
            "sample repeat release rejected");
    internal_engine.process(left, right);

    target.kind = NoteRepeatTargetKind::synth;
    target.instrument = 1;
    target.note = 60;
    target.owner = 9;
    require(internal_engine.begin_note_repeat(target),
            "synth repeat begin rejected");
    for (int block = 0; block < 24; ++block)
        internal_engine.process(left, right);
    require(internal_engine.synth_instance_state(1).active_voices != 0,
            "synth repeat did not create a production synth voice");
    internal_engine.cancel_note_repeats();
    internal_engine.process(left, right);
    require(internal_engine.note_repeat_state().active_targets == 0,
            "synth repeat survived cancellation");

    AudioEngine triplet_engine(48'000, 64);
    MidiOutputScheduler triplet_midi;
    triplet_engine.set_midi_output_scheduler(&triplet_midi);
    target.kind = NoteRepeatTargetKind::external_midi;
    target.owner = 7;
    target.midi_port = 1;
    target.midi_channel = 2;
    target.division = NoteRepeatDivision::sixteenth_triplet;
    require(triplet_engine.begin_note_repeat(target), "triplet begin rejected");
    std::vector<std::uint64_t> triplet_on;
    for (int block = 0; block < 34; ++block) {
        triplet_engine.process(left, right);
        triplet_midi.dispatch([&](const MidiPacket &packet) {
            if ((packet.bytes[0] & 0xF0) == 0x90)
                triplet_on.push_back(packet.frame);
            return true;
        });
    }
    require(triplet_on.size() >= 3 && triplet_on[0] == 0 &&
                triplet_on[1] == 4000 && triplet_on[2] == 8000,
            "triplet repeat frames were " +
                (triplet_on.empty() ? std::string("empty")
                                    : std::to_string(triplet_on[0])) + "," +
                (triplet_on.size() < 2 ? std::string("missing")
                                       : std::to_string(triplet_on[1])) + "," +
                (triplet_on.size() < 3 ? std::string("missing")
                                       : std::to_string(triplet_on[2])));
    triplet_engine.panic();
    triplet_engine.process(left, right);
    require(triplet_engine.note_repeat_state().active_targets == 0,
            "panic did not cancel repeat ownership");

    AudioEngine fractional_engine(48'000, 64);
    MidiOutputScheduler fractional_midi;
    fractional_engine.set_midi_output_scheduler(&fractional_midi);
    Timeline fractional_timeline;
    fractional_timeline.transport.microseconds_per_quarter = 500'001;
    std::string fractional_error;
    require(fractional_engine.queue_timeline(fractional_timeline,
                                             fractional_error),
            fractional_error);
    std::array<float, 113> fractional_left{}, fractional_right{};
    fractional_engine.process(fractional_left, fractional_right);
    target.division = NoteRepeatDivision::sixteenth;
    require(fractional_engine.begin_note_repeat(target),
            "fractional repeat begin rejected");
    std::vector<std::uint64_t> fractional_on;
    for (int block = 0; block < 107; ++block) {
        fractional_engine.process(fractional_left, fractional_right);
        fractional_midi.dispatch([&](const MidiPacket &packet) {
            if ((packet.bytes[0] & 0xF0) == 0x90)
                fractional_on.push_back(packet.frame);
            return true;
        });
    }
    require(fractional_on.size() >= 2 && fractional_on[0] == 6001 &&
                fractional_on[1] == 12001,
            "non-integral repeat periods accumulated or fired early");

    // Live erase captures a stable pattern/track/pitch and commits one normal
    // prepared timeline transaction after the playhead encounters matches.
    AudioEngine erase_engine(48'000, 64);
    Timeline initial;
    Track track;
    track.id = 11;
    track.instrument_id = std::string(subtractive_instrument_id);
    track.pattern.length = ticks_per_quarter;
    track.pattern.events = {
        {1, 120, MusicalEventType::note, 60, 1.0F, 120},
        {2, 240, MusicalEventType::note, 61, 1.0F, 120},
        {3, 720, MusicalEventType::note, 60, 1.0F, 120}};
    initial.tracks.push_back(track);
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return erase_engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(erase_engine);
    ApplicationService application(erase_engine, timeline, samples);
    erase_engine.process(left, right);
    require(application.execute({"transport.play.v1", {}}).accepted,
            "explicit transport start failed");
    erase_engine.process(left, right);
    auto result = application.execute(
        {"sequencer.live-erase-begin.v1",
         {{"pattern_id", std::int64_t{0}}, {"track_id", std::int64_t{11}},
          {"pitch", std::int64_t{60}}, {"owner", std::int64_t{1}},
          {"generation", std::int64_t{1}}}});
    require(result.accepted, result.error);
    // 13,000 frames reaches tick 520: event 1 is visited, event 3 is not.
    for (int block = 0; block < 51; ++block) erase_engine.process(left, right);
    (void)application.snapshot();
    result = application.execute(
        {"sequencer.live-erase-end.v1",
         {{"owner", std::int64_t{1}}, {"generation", std::int64_t{1}}}});
    require(result.accepted, result.error);
    const auto erased = timeline.snapshot().timeline.tracks.front().pattern.events;
    require(erased.size() == 2 && erased[0].id == 2 && erased[1].id == 3,
            "live erase removed an unvisited or nonmatching event");
    result = application.execute({"edit.undo.v1", {}});
    require(result.accepted, result.error);
    erase_engine.process(left, right);
    (void)application.snapshot();
    require(timeline.snapshot().timeline.tracks.front().pattern.events.size() == 3,
            "unified Undo did not restore the grouped live erase");

    std::cout << "note repeat/triplet/live erase tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
