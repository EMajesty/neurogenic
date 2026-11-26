#include "neurogenic/application/control.hpp"
#include "neurogenic/application/tap_tempo.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace neurogenic;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
double peak(const std::vector<float> &values) {
    double result{};
    for (auto value : values)
        result = std::max(result, static_cast<double>(std::fabs(value)));
    return result;
}
}
int main() try {
    AudioEngine engine(48'000, 64);
    MetronomeConfiguration click;
    click.enabled = true;
    click.level = 0.5F;
    click.microseconds_per_quarter = 500'001; // 24000.048 samples per beat
    require(engine.publish_metronome(click), "metronome publication failed");
    std::vector<float> left(257), right(257);
    engine.process(left, right);
    require(peak(left) > 0.05, "downbeat produced no PCM");
    require(engine.metronome_state().click_count == 1,
            "initial downbeat was not scheduled exactly once");
    for (int i = 0; i < 94; ++i) engine.process(left, right);
    require(engine.metronome_state().click_count == 2,
            "non-divisor callback blocks missed the beat boundary");
    const auto clicks_before_stop = engine.metronome_state().click_count;
    engine.stop();
    engine.process(left, right);
    std::fill(left.begin(), left.end(), 0.0F);
    for (int i = 0; i < 100; ++i) engine.process(left, right);
    require(engine.metronome_state().click_count == clicks_before_stop,
            "stopped transport generated clicks");
    engine.restart();
    engine.process(left, right);
    require(engine.metronome_state().click_count == clicks_before_stop + 1,
            "transport restart did not produce one new downbeat");

    AudioEngine small_blocks(48'000, 64);
    require(small_blocks.publish_metronome(click), "small-block publication failed");
    std::array<float, 64> small_left{}, small_right{};
    for (int i = 0; i < 376; ++i)
        small_blocks.process(small_left, small_right);
    require(small_blocks.metronome_state().click_count == 2,
            "64-frame callbacks missed a rational beat boundary");
    click.microseconds_per_quarter = 400'000;
    require(small_blocks.publish_metronome(click), "tempo change failed");
    const auto before_change = small_blocks.metronome_state().click_count;
    for (int i = 0; i < 301; ++i)
        small_blocks.process(small_left, small_right);
    require(small_blocks.metronome_state().click_count > before_change,
            "tempo change did not schedule against the new beat duration");

    // Recorder taps precede the monitor click: a silent project recording
    // remains silent even while the audible output contains a click.
    RecordingService recorder(48'000, 4096);
    RecordingConfiguration recording;
    recording.source = RecordingSource::master_post_effects;
    std::string error;
    require(recorder.arm(recording, error), error.c_str());
    require(recorder.start(engine.callback_frame_position(), error), error.c_str());
    engine.set_recording_service(&recorder);
    click.microseconds_per_quarter = 1000; // force a boundary in this block
    require(engine.publish_metronome(click), "tempo publication failed");
    engine.process(left, right);
    require(recorder.status().peak_left == 0.0F,
            "monitor click contaminated master recording");
    engine.set_recording_service(nullptr);

    // Count-in clicks override ordinary click disable and terminate exactly at
    // the scheduled recording frame.
    click.enabled = false;
    click.microseconds_per_quarter = 500'000;
    require(engine.publish_metronome(click), "disable publication failed");
    const auto start = engine.callback_frame_position();
    require(engine.begin_count_in(start, start + 48'000),
            "count-in publication failed");
    engine.process(left, right);
    require(peak(left) > 0.05 && engine.metronome_state().count_in_active,
            "silent-count-in override did not render");
    while (engine.callback_frame_position() < start + 48'000)
        engine.process(left, right);
    engine.process(left, right);
    require(!engine.metronome_state().count_in_active,
            "count-in remained active after its boundary");

    TapTempoEstimator tap;
    require(!tap.observe(1'000'000'000ULL), "one tap changed tempo");
    require(!tap.observe(1'500'000'000ULL), "one interval changed tempo");
    const auto bpm = tap.observe(2'000'000'000ULL);
    require(bpm && std::fabs(*bpm - 120.0) < 0.01,
            "stable taps did not estimate 120 BPM");
    require(!tap.observe(5'000'000'001ULL), "inactivity did not reset taps");

    Timeline initial;
    TimelineService timeline([&](const Timeline &value, std::string &message) {
        return engine.queue_timeline(value, message);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    auto result = application.execute({"transport.count-in-set.v1", {{"bars", std::int64_t{1}}}});
    require(result.accepted, result.error.c_str());
    result = application.execute({"recording.arm.v1", {}});
    require(result.accepted, result.error.c_str());
    result = application.execute({"recording.start.v1", {}});
    require(result.accepted, result.error.c_str());
    engine.process(left, right);
    require(application.snapshot().recording.state == RecordingState::armed,
            "recording captured before count-in completed");
    result = application.execute({"transport.stop.v1", {}});
    require(result.accepted, result.error.c_str());
    engine.process(left, right);
    require(application.snapshot().recording.state == RecordingState::canceled,
            "STOP did not cancel pending count-in recording");

    std::cout << "metronome/count-in/tap-tempo tests passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
