#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace neurogenic;
namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
ApplicationCommand track_command(std::string action, TrackId track) {
    return {std::move(action),
            {{"track_id", static_cast<std::int64_t>(track)}}};
}
}

int main() try {
    AudioEngine engine(48'000, 64);
    Timeline initial;
    Track sample_track;
    sample_track.id = 11;
    sample_track.name = "Sample";
    sample_track.instrument_id = std::string(subtractive_instrument_id);
    sample_track.instrument_instance_id = 1;
    Track synth_track;
    synth_track.id = 22;
    synth_track.name = "Synth";
    synth_track.instrument_id = std::string(subtractive_instrument_id);
    synth_track.instrument_instance_id = 2;
    MusicalEvent first;
    first.id = 1;
    first.pitch = 48;
    first.duration = ticks_per_quarter * 4;
    sample_track.pattern.events.push_back(first);
    MusicalEvent second = first;
    second.id = 2;
    second.pitch = 72;
    synth_track.pattern.events.push_back(second);
    Track external_track;
    external_track.id = 33;
    external_track.name = "External MIDI";
    external_track.instrument_id = std::string(external_midi_instrument_id);
    initial.tracks = {sample_track, synth_track, external_track};
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    std::array<float, 64> left{}, right{};
    engine.process(left, right); // activate initial timeline/mixer publications
    require(application.execute({"mixer.channel-select.v1", {{"index", std::int64_t{0}}}}).accepted,
            "first mixer channel selection failed");
    require(application.execute({"mixer.pan-set.v1", {{"value", -1.0}}}).accepted,
            "first mixer pan failed");
    require(application.execute({"mixer.channel-select.v1", {{"index", std::int64_t{1}}}}).accepted,
            "second mixer channel selection failed");
    require(application.execute({"mixer.pan-set.v1", {{"value", 1.0}}}).accepted,
            "second mixer pan failed");
    engine.process(left, right);

    auto result = application.execute(track_command("track.solo-toggle.v1", 11));
    require(result.accepted, result.error);
    auto state = application.snapshot();
    require(state.tracks[0].solo && state.tracks[0].effectively_audible &&
                !state.tracks[1].effectively_audible,
            "single solo did not isolate the stable track");
    AudioEngine isolated_engine(48'000, 64);
    MixerConfiguration isolated_mixer;
    isolated_mixer.revision = 1;
    isolated_mixer.track_count = 2;
    isolated_mixer.tracks[0].track = 11;
    isolated_mixer.tracks[0].pan = -1.0F;
    isolated_mixer.tracks[0].solo = true;
    isolated_mixer.tracks[1].track = 22;
    isolated_mixer.tracks[1].pan = 1.0F;
    std::string playback_error;
    isolated_engine.process(left, right);
    require(isolated_engine.publish_mixer(isolated_mixer, playback_error),
            playback_error);
    require(isolated_engine.queue_timeline(initial, playback_error), playback_error);
    require(isolated_engine.seek_frame(0), "isolated transport seek failed");
    double isolated_left{}, isolated_right{};
    for (int block = 0; block < 32; ++block) {
        isolated_engine.process(left, right);
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            isolated_left += std::abs(left[frame]);
            isolated_right += std::abs(right[frame]);
        }
    }
    require(isolated_left > 0.01 && isolated_right < isolated_left * 0.05,
            "solo did not isolate actual mixer PCM (L=" +
                std::to_string(isolated_left) + ", R=" +
                std::to_string(isolated_right) + ")");
    result = application.execute(track_command("track.solo-toggle.v1", 22));
    require(result.accepted, result.error);
    state = application.snapshot();
    require(state.tracks[0].solo && state.tracks[1].solo &&
                state.tracks[0].effectively_audible &&
                state.tracks[1].effectively_audible,
            "additive solo did not retain both tracks");
    result = application.execute({"track.solo-clear.v1", {}});
    require(result.accepted, result.error);
    state = application.snapshot();
    require(!state.tracks[0].solo && !state.tracks[1].solo,
            "clear solo retained transient state");

    result = application.execute(track_command("track.record-arm-toggle.v1", 11));
    require(result.accepted && application.snapshot().tracks[0].record_armed,
            result.error.empty() ? "track was not armed" : result.error);
    result = application.execute(track_command("track.record-arm-toggle.v1", 999));
    require(!result.accepted && result.error.find("does not exist") != std::string::npos,
            "missing recording owner was not rejected explicitly");
    result = application.execute(track_command("track.record-arm-toggle.v1", 33));
    require(!result.accepted && result.error.find("no capturable audio") !=
                                    std::string::npos,
            "incompatible external MIDI recording owner was not rejected");
    require(application.execute(track_command("track.record-arm-toggle.v1", 22)).accepted,
            "second bounded arm failed");
    result = application.execute({"recording.arm.v1", {}});
    require(!result.accepted && result.error.find("exactly one") != std::string::npos,
            "ambiguous multi-arm recording was not rejected");
    require(application.execute(track_command("track.record-arm-toggle.v1", 22)).accepted,
            "second arm clear failed");
    result = application.execute({"recording.arm.v1", {}});
    require(result.accepted, result.error);
    require(application.snapshot().recording.configuration.source ==
                RecordingSource::track_channel &&
                application.snapshot().recording.configuration.source_id == 11,
            "recording did not capture the armed stable owner");
    result = application.execute({"track.record-arm-clear.v1", {}});
    require(result.accepted &&
                application.snapshot().recording.state == RecordingState::canceled,
            "clearing arm did not cancel pending capture");

    MidiOutputScheduler midi;
    engine.set_midi_output_scheduler(&midi);
    require(engine.set_delay_parameter(DelayParameter::time, 0.001F) &&
                engine.set_delay_parameter(DelayParameter::feedback, 0.8F) &&
                engine.set_delay_parameter(DelayParameter::mix, 1.0F),
            "panic tail test delay configuration failed");
    engine.trigger_synth(60, 1.0F, true, 1);
    for (int block = 0; block < 4; ++block)
        engine.process(left, right);
    require(engine.synth_instance_state(1).active_voices != 0,
            "test synth voice did not start");
    const auto acknowledgement = engine.panic_acknowledgements();
    result = application.execute({"transport.panic.v1", {}});
    require(result.accepted, result.error);
    engine.process(left, right);
    double post_panic_energy{};
    for (int block = 0; block < 4; ++block) {
        engine.process(left, right);
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            post_panic_energy += std::abs(left[frame]) + std::abs(right[frame]);
    }
    require(engine.panic_acknowledgements() == acknowledgement + 1 &&
                engine.synth_instance_state(1).active_voices == 0 &&
                !engine.playing() && midi.queued() >= 32 &&
                post_panic_energy < 1e-7,
            "unified panic did not silence and acknowledge all authorities");
    engine.panic();
    engine.panic();
    engine.process(left, right);
    require(engine.panic_acknowledgements() == acknowledgement + 2,
            "repeated panic did not coalesce safely");

    std::cout << "solo/arm/unified panic tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
