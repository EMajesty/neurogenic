#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller.hpp"
#include "neurogenic/application/controller_coordinator.hpp"
#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/lifecycle.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/diagnostics/hardware.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/dsp/voice_modulation.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/sequencer/pattern_adapter.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(float actual, float expected, const char *message) {
    if (std::fabs(actual - expected) > 0.0001F)
        throw std::runtime_error(message);
}

void test_pattern_clip_arrangement() {
    using namespace neurogenic;
    Timeline base;
    base.generation_seed = 44;
    base.tracks = {{1,
                    "Drums",
                    false,
                    {ticks_per_quarter * 4, {}},
                    std::string(sample_instrument_id)},
                   {2,
                    "MIDI",
                    false,
                    {ticks_per_quarter * 4, {}},
                    std::string(external_midi_instrument_id),
                    default_synth_instance_id,
                    1,
                    1}};
    std::vector<Timeline> published;
    TimelineService service(
        [&](const Timeline &timeline, std::string &) {
            published.push_back(timeline);
            return true;
        },
        base);
    std::string error;
    auto edit = service.edit();
    check(edit.create_pattern(10, "Verse", ticks_per_quarter * 4, error),
          "create arrangement pattern");
    MusicalPattern drums{
        ticks_per_quarter * 2,
        {{100, 0, MusicalEventType::note, 36, 0.8F, ticks_per_quarter / 4}}};
    drums.events[0].trigger_probability = 0.75F;
    drums.events[0].ratchet_count = 2;
    check(edit.set_pattern_track(10, 1, drums, error),
          "set independent pattern track");
    MusicalPattern midi{ticks_per_quarter * 4,
                        {{101, ticks_per_quarter, MusicalEventType::note, 60,
                          0.5F, ticks_per_quarter / 2}}};
    check(edit.set_pattern_track(10, 2, midi, error), "set MIDI pattern track");
    check(edit.duplicate_pattern(10, 11, "Chorus", error),
          "duplicate arrangement pattern");
    check(edit.rename_pattern(11, "Chorus A", error), "rename pattern");
    check(edit.set_follow_action(10, {FollowActionType::specific, 2, 1.0F, 11},
                                 error),
          "set pattern follow action");
    check(edit.set_pattern_chain(90, {{10, 2}, {11, 1}}, false, error) &&
              edit.set_chain_running(true, 0, error) &&
              edit.set_voice_transition(VoiceTransitionPolicy::release_outgoing,
                                        error) &&
              edit.set_song_end_behavior(SongEndBehavior::stop, 0, error),
          "configure pattern chain");
    check(edit.set_song_range(0, ticks_per_quarter * 8, error),
          "set song range");
    check(edit.create_clip(
              {20, 10, 0, ticks_per_quarter * 4, true, 12, 0.5F, true}, error),
          "create transformed clip");
    check(edit.create_clip({21, 11, ticks_per_quarter * 4,
                            ticks_per_quarter * 4, false, 0, 1.0F, true},
                           error),
          "create second clip");
    edit.timeline().arrangement.pattern_chain = {10, 11};
    check(edit.set_playback_mode(TimelinePlaybackMode::song, error),
          "set song playback mode");
    check(
        service.commit(edit, TimelineCommitPolicy::next_audio_callback, error),
        "commit arrangement transaction");

    PlaybackPlan plan;
    check(compile_timeline(service.snapshot().timeline, plan, error),
          "compile song arrangement");
    check(plan.tracks.size() == 2 &&
              plan.tracks[0].length == ticks_per_quarter * 8,
          "arrangement compiles into authoritative tracks");
    check(std::any_of(plan.events.begin(), plan.events.end(),
                      [](const auto &e) {
                          return e.pitch == 48 &&
                                 std::fabs(e.value - 0.4F) < 0.0001F;
                      }),
          "clip transpose and velocity transformation");
    check(std::any_of(plan.tracks.begin(), plan.tracks.end(),
                      [](const auto &t) {
                          return t.instrument == external_midi_instrument_key &&
                                 t.midi_channel == 1;
                      }),
          "arrangement preserves external MIDI routing");

    TimelineScheduler whole(48'000), partitioned(48'000);
    auto plan_copy = plan;
    whole.set_plan(&plan, 0);
    partitioned.set_plan(&plan_copy, 0);
    std::array<TimelineScheduledEvent, 128> a{}, b{};
    const auto whole_count = whole.schedule(0, 48'000, a);
    const auto first = partitioned.schedule(0, 24'000, b);
    const auto second =
        partitioned.schedule(24'000, 24'000, std::span(b).subspan(first));
    check(whole_count == first + second,
          "arrangement callback partition count");

    auto overlap = service.edit();
    check(!overlap.create_clip({22, 10, ticks_per_quarter,
                                ticks_per_quarter * 2, true, 0, 1, true},
                               error) &&
              error.find("overlap") != std::string::npos,
          "ambiguous clip overlap rejected");
    auto live = service.edit();
    check(live.launch_pattern(10, false, error), "activate live pattern");
    check(
        service.commit(live, TimelineCommitPolicy::next_audio_callback, error),
        "commit live pattern");
    check(service.snapshot().timeline.arrangement.playback_mode ==
              TimelinePlaybackMode::live_pattern,
          "live mode state");
    check(service.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              service.snapshot().timeline.arrangement.playback_mode ==
                  TimelinePlaybackMode::song &&
              service.redo(TimelineCommitPolicy::next_audio_callback, error),
          "arrangement undo and redo");
    check(launch_boundary(1, LaunchQuantization::bar, ticks_per_quarter * 4,
                          base.transport) == ticks_per_quarter * 4,
          "bar launch quantization");
    AudioEngine seek_engine;
    seek_engine.stop();
    check(seek_engine.seek_frame(12'345), "queue arrangement seek");
    std::array<float, 16> seek_left{}, seek_right{};
    seek_engine.process(seek_left, seek_right);
    check(seek_engine.frame_position() == 12'345,
          "arrangement seek updates authoritative transport");

    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-arrangement-roundtrip.ngt";
    save_timeline(service.snapshot().timeline, path);
    const auto restored = load_timeline(path);
    std::filesystem::remove(path);
    check(restored.arrangement.patterns.size() == 2 &&
              restored.arrangement.clips.size() == 2 &&
              restored.arrangement.pattern_chain ==
                  std::vector<PatternId>({10, 11}) &&
              restored.arrangement.pattern_chain_id == 90 &&
              restored.arrangement.chain_entries.size() == 2 &&
              restored.arrangement.patterns[0].follow.type ==
                  FollowActionType::specific,
          "arrangement persistence round trip");
    const auto project_path = std::filesystem::temp_directory_path() /
                              "neurogenic-arrangement-project";
    std::filesystem::remove_all(project_path);
    Project project;
    project.name = "Arrangement test";
    project.timeline = restored;
    save_project(project, project_path);
    const auto restored_project = load_project(project_path);
    std::filesystem::remove_all(project_path);
    check(restored_project.timeline.arrangement.patterns.size() == 2 &&
              restored_project.timeline.arrangement.clips.size() == 2,
          "arrangement project round trip");
    ActionRegistry actions;
    check(actions.find("pattern.create.v1") &&
              actions.find("pattern.launch.v1") &&
              actions.find("pattern.chain-play.v1") &&
              actions.find("pattern.follow-set.v1") &&
              actions.find("arrangement.clip-create.v1") &&
              actions.find("arrangement.loop-set.v1"),
          "arrangement semantic action discovery");
    AudioEngine action_engine;
    TimelineService action_timeline(
        [&](const Timeline &timeline, std::string &publish_error) {
            return action_engine.queue_timeline(timeline, publish_error);
        });
    SampleManager action_samples(action_engine);
    ApplicationService application(action_engine, action_timeline,
                                   action_samples);
    check(application
              .execute({"pattern.create.v1",
                        {{"id", std::int64_t{77}},
                         {"name", std::string("Action pattern")},
                         {"length", std::int64_t{3840}}}})
              .accepted,
          "pattern creation semantic action");
    check(application
              .execute({"pattern.launch.v1",
                        {{"id", std::int64_t{77}}, {"queued", false}}})
              .accepted,
          "pattern launch semantic action");
    const auto application_state = application.snapshot();
    check(application_state.patterns.size() == 1 &&
              application_state.active_pattern == 77 &&
              application_state.playback_mode ==
                  TimelinePlaybackMode::live_pattern,
          "arrangement discovery snapshot");
    check(!application
               .execute({"pattern.create.v1",
                         {{"id", std::int64_t{-1}},
                          {"name", std::string("Bad")},
                          {"length", std::int64_t{3840}}}})
               .accepted,
          "negative stable pattern ID rejection");

    Timeline chain = service.snapshot().timeline;
    chain.arrangement.playback_mode = TimelinePlaybackMode::live_pattern;
    chain.arrangement.active_pattern = 10;
    PlaybackPlan chain_plan_a, chain_plan_b;
    check(compile_timeline(chain, chain_plan_a, error) &&
              compile_timeline(chain, chain_plan_b, error) &&
              chain_plan_a.events.size() == chain_plan_b.events.size() &&
              chain_plan_a.events.front().id ==
                  chain_plan_b.events.front().id &&
              chain_plan_a.playback_length == ticks_per_quarter * 12 &&
              chain_plan_a.song_end_behavior == SongEndBehavior::stop,
          "deterministic repeated chain compilation");
    auto looping_chain = chain;
    looping_chain.arrangement.chain_loop = true;
    PlaybackPlan looping_chain_plan;
    check(compile_timeline(looping_chain, looping_chain_plan, error) &&
              looping_chain_plan.song_end_behavior == SongEndBehavior::loop,
          "looping chain playback policy");
    auto random_chain = chain;
    random_chain.arrangement.patterns[0].follow = {
        FollowActionType::random_eligible, 2, 1.0F, 0};
    PlaybackPlan random_a, random_b;
    check(compile_timeline(random_chain, random_a, error) &&
              compile_timeline(random_chain, random_b, error) &&
              random_a.events.size() == random_b.events.size() &&
              random_a.playback_length == random_b.playback_length &&
              random_a.events.front().pitch == random_b.events.front().pitch,
          "seeded random follow action");
    auto over_capacity = chain;
    over_capacity.arrangement.chain_entries.resize(4097, {10, 1});
    check(!validate_arrangement(over_capacity, error) &&
              error.find("capacity") != std::string::npos,
          "pattern chain capacity rejection");

    AudioEngine precise(48'000);
    Timeline empty_a, empty_b;
    empty_a.tracks.push_back({1, "A", false, {960, {}}});
    empty_b.tracks.push_back({1, "B", false, {960, {}}});
    check(precise.queue_timeline(empty_a, error), "initial launch plan");
    std::array<float, 64> precise_left{}, precise_right{};
    precise.process({precise_left.data(), 1}, {precise_right.data(), 1});
    const auto launch_frame = precise.frame_position() + 37;
    check(precise.queue_timeline_at(empty_b, launch_frame,
                                    VoiceTransitionPolicy::preserve, error),
          "queue exact launch plan");
    precise.process({precise_left.data(), 16}, {precise_right.data(), 16});
    check(precise.last_timeline_activation_frame() != launch_frame,
          "quantized launch activated early");
    precise.process({precise_left.data(), 32}, {precise_right.data(), 32});
    check(precise.last_timeline_activation_frame() == launch_frame &&
              precise.last_timeline_lateness_frames() == 0,
          "variable callback launch boundary");

    Timeline ending;
    PatternDefinition ending_pattern;
    ending_pattern.id = 1;
    ending_pattern.name = "Ending";
    ending_pattern.length = 1;
    ending_pattern.tracks.push_back({1, {1, {}}});
    ending.tracks.push_back({1, "End", false, {1, {}}});
    ending.arrangement.patterns.push_back(ending_pattern);
    ending.arrangement.clips.push_back({1, 1, 0, 1, false, 0, 1, true});
    ending.arrangement.song_end = 1;
    ending.arrangement.playback_mode = TimelinePlaybackMode::song;
    ending.arrangement.song_end_behavior = SongEndBehavior::stop;
    AudioEngine song_end_engine(48'000);
    check(song_end_engine.queue_timeline(ending, error), error.c_str());
    song_end_engine.process({precise_left.data(), 64},
                            {precise_right.data(), 64});
    check(!song_end_engine.playing() &&
              song_end_engine.frame_position() == 25 &&
              song_end_engine.song_end_count() == 1,
          "deterministic song end boundary");
}

void test_live_arrangement_recording() {
    using namespace neurogenic;
    Timeline timeline;
    timeline.generation_seed = 99;
    timeline.tracks.push_back({1,
                               "Synth",
                               false,
                               {3840, {}},
                               std::string(subtractive_instrument_id)});
    for (PatternId id = 1; id <= 2; ++id) {
        PatternDefinition pattern;
        pattern.id = id;
        pattern.name = "Pattern " + std::to_string(id);
        pattern.length = 960;
        pattern.tracks.push_back(
            {1,
             {960,
              {{id, 0, MusicalEventType::note,
                static_cast<std::uint8_t>(59 + id), 0.8F, 120}}}});
        timeline.arrangement.patterns.push_back(std::move(pattern));
    }
    TimelineService service(
        [](const Timeline &, std::string &) { return true; }, timeline);
    ArrangementRecordingService recorder(16);
    std::string error;
    check(recorder.arm(true, error) &&
              recorder.start(0, ArrangementRecordMode::replace, 0, true, error),
          "arm live arrangement recording");
    check(recorder.capture_pattern(1, 0) && recorder.capture_pattern(1, 480) &&
              recorder.capture_pattern(2, 960),
          "capture pattern transitions without fragmentation");
    check(recorder.capture_automation("synth.cutoff", 1000, 100, 20) &&
              recorder.capture_automation("synth.cutoff", 1200, 110, 20) &&
              recorder.capture_automation("synth.cutoff", 1400, 200, 20),
          "capture and thin automation");
    check(recorder.stop(1920, error) && recorder.commit(service, error),
          error.c_str());
    auto recorded = service.snapshot().timeline;
    check(recorded.arrangement.clips.size() == 2 &&
              recorded.arrangement.clips[0].pattern == 1 &&
              recorded.arrangement.clips[0].duration == 960 &&
              recorded.arrangement.clips[1].pattern == 2 &&
              recorded.arrangement.clips[1].duration == 960,
          "recording clip boundary construction");
    check(recorded.arrangement.automation.size() == 2 &&
              recorded.arrangement.automation[0].value == 1200 &&
              recorded.arrangement.automation[1].tick == 200,
          "recorded automation thinning result");
    PlaybackPlan plan;
    check(compile_timeline(recorded, plan, error) &&
              std::any_of(plan.events.begin(), plan.events.end(),
                          [](const auto &e) {
                              return e.type == MusicalEventType::parameter &&
                                     e.value == 1400;
                          }),
          "recorded automation compiles through timeline");
    check(service.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              service.snapshot().timeline.arrangement.clips.empty() &&
              service.redo(TimelineCommitPolicy::next_audio_callback, error) &&
              service.snapshot().timeline.arrangement.clips.size() == 2,
          "recording pass undo and redo");

    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-arrangement-recording.ngt";
    save_timeline(service.snapshot().timeline, path);
    const auto restored = load_timeline(path);
    std::filesystem::remove(path);
    check(restored.arrangement.clips.size() == 2 &&
              restored.arrangement.automation.size() == 2,
          "arrangement recording persistence");
    const auto project_path = std::filesystem::temp_directory_path() /
                              "neurogenic-live-recording-project";
    std::filesystem::remove_all(project_path);
    Project recording_project;
    recording_project.timeline = restored;
    save_project(recording_project, project_path);
    const auto restored_project = load_project(project_path);
    std::filesystem::remove_all(project_path);
    check(restored_project.timeline.arrangement.automation.size() == 2,
          "live arrangement project round trip");

    ArrangementRecordingService count_in;
    check(count_in.arm(true, error) &&
              count_in.start(100, ArrangementRecordMode::replace, 200, false,
                             error) &&
              !count_in.capture_pattern(1, 299) &&
              count_in.capture_pattern(1, 300),
          "arrangement recording count-in boundary");
    count_in.discard();

    ArrangementRecordingService chain;
    auto chain_timeline = timeline;
    chain_timeline.arrangement.chain_entries = {{1, 2}, {2, 1}};
    chain_timeline.arrangement.chain_running = true;
    check(chain.arm(true, error) &&
              chain.start(0, ArrangementRecordMode::replace, 0, false, error) &&
              chain.capture_chain(chain_timeline, 0) &&
              chain.patterns().size() == 2 && chain.patterns()[1].tick == 1920,
          "automatic chain and follow transition capture");

    ArrangementRecordingService bounded(1);
    check(
        bounded.arm(true, error) &&
            bounded.start(0, ArrangementRecordMode::replace, 0, true, error) &&
            bounded.capture_pattern(1, 0) &&
            !bounded.capture_automation("synth.cutoff", 1000, 1, 0) &&
            bounded.status().phase == ArrangementRecordPhase::overflow &&
            !bounded.stop(2, error),
        "arrangement capture overflow is explicit and uncommittable");

    ActionRegistry actions;
    check(actions.find("arrangement.record-arm.v1") &&
              actions.find("arrangement.record-start.v1") &&
              actions.find("arrangement.record-commit.v1") &&
              actions.find("arrangement.automation-thinning.v1"),
          "arrangement recording action discovery");
    const auto track_destination =
        resolve_parameter_destination("mixer.track.27.pan");
    const auto synth_destination =
        resolve_parameter_destination("synth.instance.9.cutoff");
    check(track_destination && track_destination->instance == 27 &&
              track_destination->target == ParameterTarget::mixer_track &&
              synth_destination && synth_destination->instance == 9 &&
              synth_destination->target == ParameterTarget::subtractive_synth,
          "instance-aware automation destination resolution");

    AudioEngine engine(48'000);
    TimelineService application_timeline(
        [&](const Timeline &value, std::string &publish_error) {
            return engine.queue_timeline(value, publish_error);
        },
        timeline);
    SampleManager samples(engine);
    ApplicationService application(engine, application_timeline, samples);
    check(application.execute({"arrangement.record-arm.v1", {{"armed", true}}})
                  .accepted &&
              application
                  .execute({"arrangement.record-start.v1",
                            {{"mode", std::string("replace")},
                             {"automation", true}}})
                  .accepted &&
              application
                  .execute({"pattern.launch.v1",
                            {{"id", std::int64_t{1}}, {"queued", false}}})
                  .accepted,
          "application arrangement recording lifecycle");
    std::array<float, 64> left{}, right{};
    engine.process(left, right);
    auto live_state = application.snapshot();
    check(live_state.arrangement_recording.captured_patterns == 1 &&
              live_state.captured_pattern_launches.front().pattern == 1 &&
              live_state.captured_pattern_launches.front().tick == 0,
          "application captures callback-acknowledged launch position");
    check(application
              .execute({"parameter.set-normalized.v1",
                        {{"parameter_id", std::string("synth.cutoff")},
                         {"value", 0.5}}})
              .accepted,
          "application automation capture command");
    check(application.execute({"mixer.master-gain-set.v1", {{"value", 0.75}}})
              .accepted,
          "application mixer automation capture command");
    engine.process(left, right);
    check(
        application.execute({"transport.stop.v1", {}}).accepted &&
            application.execute({"arrangement.record-commit.v1", {}}).accepted,
        "transport stop finalizes and application commits captured pass");
    const auto application_recorded = application_timeline.snapshot().timeline;
    check(application_recorded.arrangement.clips.size() == 1 &&
              application_recorded.arrangement.automation.size() == 2 &&
              application_recorded.arrangement.automation[1]
                      .parameter_destination == "mixer.master.gain",
          "application pass produced clips and automation");
    samples.shutdown();
}

void test_hardware_diagnostics() {
    using namespace neurogenic;
    HardwareDiagnosticSnapshot snapshot;
    snapshot.application_version = "test";
    snapshot.application_commit = "abc123";
    snapshot.architecture = "aarch64";
    snapshot.cpu_model = "simulated & safe";
    snapshot.kernel = "Linux test";
    snapshot.distribution = "Test \"Linux\"";
    snapshot.total_memory_bytes = 16ULL * 1024 * 1024 * 1024;
    snapshot.audio_backend = "simulated";
    snapshot.selected_buffer_frames = 256;
    snapshot.selected_sample_rate = 48000;
    snapshot.selected_input_channels = 6;
    snapshot.selected_output_channels = 6;
    snapshot.realtime.policy = "fifo";
    snapshot.realtime.priority = 60;
    snapshot.realtime.realtime_priority_limit = 70;
    snapshot.realtime.memory_lock_limit_bytes = UINT64_MAX;
    snapshot.realtime.memory_lock_unlimited = true;
    snapshot.realtime.available_cpus = 4;
    snapshot.realtime.cpu_governor = "performance";
    snapshot.audio_outputs.push_back({"six output", {{48000, 6}, {96000, 2}}});
    snapshot.audio_inputs.push_back({"six input", {{48000, 6}}});
    snapshot.midi_input_backend = "simulated";
    snapshot.midi_output_backend = "simulated";
    snapshot.midi_inputs.push_back({1, "controller", true});
    snapshot.midi_outputs.push_back({1, "synth", false});
    snapshot.display = {true, 1920, 1200, 1920, 1200, 1.5F, "dummy"};
    const auto json = hardware_diagnostics_json(snapshot);
    check(json.find("neurogenic.hardware-diagnostics.v2") !=
                  std::string::npos &&
              json.find("\"sample_rate\":48000,\"channels\":6") !=
                  std::string::npos &&
              json.find("Test \\\"Linux\\\"") != std::string::npos &&
              json.find("\"content_scale\":1.5") != std::string::npos &&
              json.find("\"selected_input_channels\":6") !=
                  std::string::npos &&
              json.find("\"policy\":\"fifo\"") != std::string::npos &&
              json.find("\"backend_xruns_available\":false") !=
                  std::string::npos,
          "hardware diagnostic JSON schema/capabilities");
    check(hardware_diagnostics_summary(snapshot).find("1920x1200") !=
                  std::string::npos &&
              hardware_diagnostics_summary(snapshot).find("XRUN counter: unavailable") !=
                  std::string::npos,
          "hardware diagnostic human summary");
    const auto scheduling = realtime_scheduling_diagnostics();
    check(!scheduling.policy.empty() && scheduling.available_cpus > 0,
          "real-time scheduling diagnostics are bounded and available");
    HardwareTestReport report;
    report.application_commit = "abc123";
    report.hardware_identity = "simulated";
    report.os_kernel = "test";
    report.audio_configuration = "48000 Hz / 256 frames / 6 channels";
    report.scenario = "six-channel-routing";
    report.status = "passed";
    report.limitations = "simulated hardware";
    report.sample_rate = 48000;
    report.requested_buffer_frames = 128;
    report.negotiated_buffer_frames = 128;
    report.input_channels = 6;
    report.output_channels = 6;
    report.device_starts = 1;
    report.device_stops = 1;
    report.realtime = snapshot.realtime;
    report.measurements.audio.available = true;
    report.measurements.audio.callback_count = 100;
    const auto report_json = hardware_test_report_json(report);
    check(report_json.find("neurogenic.hardware-test-report.v1") !=
                  std::string::npos &&
              report_json.find("six-channel-routing") != std::string::npos &&
              report_json.find("\"requested_buffer_frames\":128") !=
                  std::string::npos &&
              report_json.find("\"physical\":false") != std::string::npos &&
              report_json.find("\"starts\":1,\"stops\":1") !=
                  std::string::npos &&
              report_json.find("neurogenic.performance.v1") !=
                  std::string::npos,
          "versioned hardware report");
    const auto host = system_diagnostics();
    check(!host.architecture.empty() && !host.kernel.empty() &&
              !host.application_version.empty(),
          "host system diagnostic failure handling");
}

void test_appliance_lifecycle() {
    using namespace neurogenic;
    ApplianceLifecycle lifecycle;
    lifecycle.transition(StartupStage::configuration, 0.1F);
    lifecycle.issue(StartupStage::audio, "missing interface", true);
    lifecycle.transition(StartupStage::midi, 0.7F);
    lifecycle.issue(StartupStage::midi, "missing configured port", true);
    lifecycle.ready();
    auto state = lifecycle.snapshot();
    check(state.ready && state.degraded && state.issues.size() == 2 &&
              lifecycle_json(state).find("neurogenic.appliance-lifecycle.v1") !=
                  std::string::npos,
          "observable degraded startup lifecycle");
    std::string reason;
    check(!lifecycle.request_shutdown(true, UnsavedShutdownChoice::cancel,
                                      reason) &&
              !reason.empty() && lifecycle.snapshot().shutdown_cancelled,
          "dirty project shutdown cancellation");
    check(lifecycle.request_shutdown(true, UnsavedShutdownChoice::discard,
                                     reason),
          "explicit dirty discard shutdown");
    lifecycle.stopped();
    check(lifecycle.snapshot().stage == StartupStage::stopped,
          "coordinated lifecycle stop");

    const auto root = std::filesystem::temp_directory_path() /
                      "neurogenic-appliance-preferences-test";
    std::filesystem::remove_all(root);
    const auto path = root / "appliance.conf";
    AppliancePreferences preferences;
    preferences.reopen_last_project = true;
    preferences.last_project = root / "last project";
    preferences.audio_output = "SSL 12";
    preferences.audio_input = "SSL 12";
    preferences.audio_input_channels = 6;
    preferences.audio_output_channels = 6;
    preferences.audio_buffer_frames = 128;
    preferences.midi_inputs = {"A-Series Keyboard"};
    preferences.midi_outputs = {"External Synth"};
    preferences.fullscreen = true;
    preferences.ui_scale = 1.5F;
    std::string error;
    check(save_preferences(path, preferences, error),
          "save appliance preferences");
    AppliancePreferences restored;
    check(load_preferences(path, restored, error) &&
              restored.last_project == preferences.last_project &&
              restored.audio_output == "SSL 12" &&
              restored.midi_outputs == preferences.midi_outputs &&
              restored.ui_scale == preferences.ui_scale,
          "appliance preference round-trip");
    std::string warning;
    const auto explicit_project = root / "explicit.ngp";
    check(select_startup_project(restored, explicit_project, warning) ==
                  explicit_project &&
              warning.empty(),
          "explicit project did not override reopen-last");
    check(select_startup_project(restored, {}, warning).empty() &&
              warning.find("unavailable") != std::string::npos,
          "missing last project recovery");
    {
        std::ofstream invalid(path, std::ios::trunc);
        invalid << "version 1\naudio_buffer_frames 0\n";
    }
    check(!load_preferences(path, restored, error) && !error.empty(),
          "invalid preferences accepted");
    std::filesystem::remove_all(root);
}

void test_modulation_engine() {
    using namespace neurogenic;
    ModulationConfiguration configuration;
    configuration.update_interval = 4;
    configuration.sources[0].waveform = LfoWaveform::saw;
    configuration.sources[0].rate_hz = 10.0F;
    configuration.sources[0].scope = ModulationScope::track;
    configuration.sources[0].retrigger = true;
    configuration.route_count = 2;
    configuration.routes[0] = {
        1,
        ModulationSourceId::lfo_1,
        {ParameterTarget::subtractive_synth,
         static_cast<std::uint32_t>(SynthParameter::cutoff), 1},
        20,
        20000,
        10000,
        0.5F,
        true,
        ModulationPolarity::bipolar,
        ModulationScope::track,
        10};
    configuration.routes[1] = configuration.routes[0];
    configuration.routes[1].id = 2;
    configuration.routes[1].destination.parameter =
        static_cast<std::uint32_t>(SynthParameter::resonance);
    configuration.routes[1].minimum = 0;
    configuration.routes[1].maximum = 1;
    configuration.routes[1].base_value = 0.5F;
    configuration.routes[1].track_id = 20;
    ModulationEngine engine(1000);
    check(engine.configure(configuration), "track modulation configuration");
    float cutoff{}, resonance{};
    const auto process = [&](int frames) {
        for (int i = 0; i < frames; ++i)
            engine.process_frame(
                [&](ParameterDestination destination, float value) {
                    if (destination.parameter ==
                        static_cast<std::uint32_t>(SynthParameter::cutoff))
                        cutoff = value;
                    else
                        resonance = value;
                });
    };
    process(40);
    engine.note_on(10, 1.0F);
    process(4);
    check(cutoff < 6000 && resonance > 0.1F,
          "track LFOs maintain independent retrigger phase");

    ModulationConfiguration summed;
    summed.update_interval = 1;
    summed.route_count = 2;
    summed.routes[0] = {1,
                        ModulationSourceId::xy_x,
                        {ParameterTarget::stereo_delay,
                         static_cast<std::uint32_t>(DelayParameter::mix)},
                        0,
                        1,
                        0.25F,
                        0.25F};
    summed.routes[1] = summed.routes[0];
    summed.routes[1].id = 2;
    summed.routes[1].source = ModulationSourceId::xy_y;
    check(engine.configure(summed), "summed modulation configuration");
    engine.set_input(ModulationSourceId::xy_x, 1);
    engine.set_input(ModulationSourceId::xy_y, 1);
    float effective{};
    engine.process_frame(
        [&](ParameterDestination, float value) { effective = value; });
    near(effective, 0.75F, "multiple route normalized summation");
    engine.set_automation({ParameterTarget::stereo_delay,
                           static_cast<std::uint32_t>(DelayParameter::mix)},
                          0.1F);
    engine.process_frame(
        [&](ParameterDestination, float value) { effective = value; });
    near(effective, 0.6F, "automation and modulation remain separate");
    summed.routes[0].enabled = false;
    summed.routes[1].depth = -1.0F;
    check(engine.configure(summed), "bypass modulation configuration");
    engine.set_input(ModulationSourceId::xy_y, 1);
    engine.process_frame(
        [&](ParameterDestination, float value) { effective = value; });
    near(effective, 0.0F, "bipolar modulation clamps destination");

    auto invalid = summed;
    invalid.sources[static_cast<std::size_t>(ModulationSourceId::xy_y)].scope =
        ModulationScope::voice;
    check(!engine.configure(invalid),
          "unsupported per-voice scope rejected explicitly");

    ModulationConfiguration random_configuration;
    random_configuration.update_interval = 1;
    auto &random_source =
        random_configuration
            .sources[static_cast<std::size_t>(ModulationSourceId::sample_hold)];
    random_source.rate_hz = 100;
    random_source.seed = 12345;
    random_configuration.route_count = 1;
    random_configuration.routes[0] = {
        1,
        ModulationSourceId::sample_hold,
        {ParameterTarget::stereo_delay,
         static_cast<std::uint32_t>(DelayParameter::feedback)},
        0,
        1,
        0.5F,
        0.5F};
    ModulationEngine first(1000), second(1000);
    check(first.configure(random_configuration) &&
              second.configure(random_configuration),
          "deterministic random modulation configuration");
    float a{}, b{};
    for (int i = 0; i < 25; ++i) {
        first.process_frame(
            [&](ParameterDestination, float value) { a = value; });
        second.process_frame(
            [&](ParameterDestination, float value) { b = value; });
    }
    near(a, b, "sample-and-hold seed reproducibility");

    ModulationConfiguration expanded;
    expanded.update_interval = 1;
    expanded.route_count = 3;
    expanded.routes[0] = {
        1,
        ModulationSourceId::xy_x,
        {ParameterTarget::sample_voice,
         static_cast<std::uint32_t>(SampleModulationParameter::pitch)},
        -12,
        12,
        0,
        0.5F};
    expanded.routes[1] = {
        2,
        ModulationSourceId::xy_x,
        {ParameterTarget::mixer_track,
         static_cast<std::uint32_t>(MixerTrackModulationParameter::pan), 1},
        -1,
        1,
        0,
        1.0F};
    expanded.routes[2] = {
        3,
        ModulationSourceId::xy_x,
        {ParameterTarget::external_fx,
         static_cast<std::uint32_t>(ExternalFxModulationParameter::wet), 1},
        0,
        1,
        0.5F,
        0.5F};
    check(engine.configure(expanded),
          "expanded numeric modulation destinations");
    engine.set_input(ModulationSourceId::xy_x, 1.0F);
    std::array<float, 3> expanded_values{};
    engine.process_frame([&](ParameterDestination destination, float value) {
        if (destination.target == ParameterTarget::sample_voice)
            expanded_values[0] = value;
        if (destination.target == ParameterTarget::mixer_track)
            expanded_values[1] = value;
        if (destination.target == ParameterTarget::external_fx)
            expanded_values[2] = value;
    });
    near(expanded_values[0], 12.0F, "sample pitch modulation composition");
    near(expanded_values[1], 1.0F, "mixer pan modulation clamping");
    near(expanded_values[2], 1.0F, "external FX wet modulation composition");

    ModulationConfiguration follower;
    follower.update_interval = 10;
    auto &follower_source = follower.sources[static_cast<std::size_t>(
        ModulationSourceId::envelope_follower)];
    follower_source.follower_attack_seconds = 0.01F;
    follower_source.follower_release_seconds = 0.1F;
    follower.route_count = 1;
    follower.routes[0] = {1,
                          ModulationSourceId::envelope_follower,
                          {ParameterTarget::mixer_master, 1},
                          0,
                          2,
                          1,
                          0.5F};
    check(engine.configure(follower), "envelope follower configuration");
    engine.set_input(ModulationSourceId::envelope_follower, 1.0F);
    float follower_attack{};
    process(10);
    follower_attack = engine.snapshot().source_values[static_cast<std::size_t>(
        ModulationSourceId::envelope_follower)];
    engine.set_input(ModulationSourceId::envelope_follower, 0.0F);
    process(10);
    const auto follower_release =
        engine.snapshot().source_values[static_cast<std::size_t>(
            ModulationSourceId::envelope_follower)];
    check(follower_attack > follower_release && follower_release > -1.0F,
          "envelope follower attack and release smoothing");

    check(modulator_type_registry().size() == 24 &&
              find_modulator_type(ModulatorType::lfo)->parameters.size() >= 2,
          "modulator type discovery metadata");
    ModulationConfiguration dynamic;
    dynamic.update_interval = 1;
    dynamic.modulator_count = 3;
    dynamic.modulators[0].id = 41;
    dynamic.modulators[0].type = ModulatorType::lfo;
    dynamic.modulators[0].owner_kind = ModulatorOwnerKind::track;
    dynamic.modulators[0].owner_id = 10;
    dynamic.modulators[0].settings.rate_hz = 3;
    dynamic.modulators[1] = dynamic.modulators[0];
    dynamic.modulators[1].id = 99;
    dynamic.modulators[1].settings.rate_hz = 7;
    dynamic.modulators[2].id = 7;
    dynamic.modulators[2].type = ModulatorType::lfo;
    dynamic.modulators[2].owner_kind = ModulatorOwnerKind::master;
    dynamic.modulators[2].settings.rate_hz = 2;
    dynamic.route_count = 3;
    for (std::size_t i = 0; i < dynamic.route_count; ++i) {
        dynamic.routes[i] = {
            static_cast<ModulationRouteId>(i + 1),
            ModulationSourceId::lfo_1,
            {ParameterTarget::mixer_track,
             static_cast<std::uint32_t>(MixerTrackModulationParameter::pan),
             static_cast<std::uint32_t>(i + 1)},
            -1,
            1,
            0,
            0.5F,
            true,
            ModulationPolarity::bipolar,
            i < 2 ? ModulationScope::track : ModulationScope::global,
            i < 2 ? 10U : 0U};
        dynamic.routes[i].source_instance = dynamic.modulators[i].id;
    }
    check(engine.configure(dynamic),
          "dynamic modulator instance configuration");
    process(17);
    const auto dynamic_snapshot = engine.snapshot();
    check(dynamic_snapshot.modulator_count == 3 &&
              dynamic_snapshot.modulator_values[0] !=
                  dynamic_snapshot.modulator_values[1],
          "independent same-track LFO phase");
    auto invalid_owner = dynamic;
    invalid_owner.modulators[0].owner_id = 0;
    check(!engine.configure(invalid_owner), "invalid modulator owner rejected");
    auto duplicate_id = dynamic;
    duplicate_id.modulators[1].id = duplicate_id.modulators[0].id;
    check(!engine.configure(duplicate_id),
          "duplicate stable instance ID rejected");
    ModulationEngine partition_a(1000), partition_b(1000);
    check(partition_a.configure(dynamic) && partition_b.configure(dynamic),
          "partition test configuration");
    for (int frame = 0; frame < 100; ++frame)
        partition_a.process_frame([](auto, auto) {});
    for (int block : {13, 21, 7, 59})
        for (int frame = 0; frame < block; ++frame)
            partition_b.process_frame([](auto, auto) {});
    near(partition_a.snapshot().modulator_values[0],
         partition_b.snapshot().modulator_values[0],
         "dynamic modulator callback partition independence");

    ModulationConfiguration graph;
    graph.update_interval = 1;
    graph.modulator_count = 3;
    graph.modulators[0].id = 30;
    graph.modulators[0].type = ModulatorType::xy_manual;
    graph.modulators[0].owner_kind = ModulatorOwnerKind::track;
    graph.modulators[0].owner_id = 1;
    graph.modulators[0].shared = true;
    graph.modulators[1].id = 10;
    graph.modulators[1].type = ModulatorType::lfo;
    graph.modulators[1].owner_kind = ModulatorOwnerKind::track;
    graph.modulators[1].owner_id = 2;
    graph.modulators[1].settings.rate_hz = 1;
    graph.modulators[2].id = 20;
    graph.modulators[2].type = ModulatorType::lfo;
    graph.modulators[2].owner_kind = ModulatorOwnerKind::master;
    graph.route_count = 1;
    auto &graph_edge = graph.routes[0];
    graph_edge.id = 101;
    graph_edge.source_instance = 30;
    graph_edge.destination_kind =
        ModulationDestinationKind::modulator_parameter;
    graph_edge.destination_modulator = 10;
    graph_edge.destination_modulator_parameter = ModulatorParameterId::rate;
    graph_edge.depth = 0.5F;
    graph_edge.minimum = -1;
    graph_edge.maximum = 1;
    graph_edge.scope = ModulationScope::track;
    graph_edge.track_id = 1;
    graph_edge.destination_owner_explicit = true;
    graph_edge.destination_owner_kind = ModulatorOwnerKind::track;
    graph_edge.destination_owner_id = 2;
    ModulationGraphDiagnostics graph_diagnostics;
    check(validate_modulation_graph(graph, graph_diagnostics) &&
              graph_diagnostics.evaluation_count == 3 &&
              graph_diagnostics.evaluation_order[0] == 20 &&
              graph_diagnostics.evaluation_order[1] == 30 &&
              graph_diagnostics.evaluation_order[2] == 10,
          "stable topological order and explicit cross-track sharing");
    auto hidden_cross_track = graph;
    hidden_cross_track.modulators[0].shared = false;
    check(!validate_modulation_graph(hidden_cross_track, graph_diagnostics) &&
              graph_diagnostics.message.find("shared") != std::string::npos,
          "implicit cross-track visibility rejected");
    auto omitted_destination_owner = graph;
    omitted_destination_owner.routes[0].destination_owner_explicit = false;
    check(!validate_modulation_graph(omitted_destination_owner,
                                     graph_diagnostics) &&
              graph_diagnostics.message.find("shared") != std::string::npos,
          "cross-track modulator destination owner must be explicit");
    auto direct_cycle = graph;
    direct_cycle.routes[0].source_instance = 10;
    direct_cycle.routes[0].destination_modulator = 10;
    direct_cycle.routes[0].track_id = 2;
    check(!validate_modulation_graph(direct_cycle, graph_diagnostics) &&
              graph_diagnostics.message.find("direct") != std::string::npos,
          "direct modulation cycle rejected");
    auto indirect_cycle = graph;
    indirect_cycle.route_count = 2;
    indirect_cycle.modulators[1].shared = true;
    indirect_cycle.modulators[2].shared = true;
    indirect_cycle.routes[0].source_instance = 20;
    indirect_cycle.routes[1] = indirect_cycle.routes[0];
    indirect_cycle.routes[1].id = 102;
    indirect_cycle.routes[1].source_instance = 10;
    indirect_cycle.routes[1].destination_modulator = 20;
    indirect_cycle.routes[1].destination_owner_kind =
        ModulatorOwnerKind::master;
    indirect_cycle.routes[1].destination_owner_id = 0;
    indirect_cycle.routes[1].track_id = 2;
    check(!validate_modulation_graph(indirect_cycle, graph_diagnostics) &&
              graph_diagnostics.message.find("indirect") != std::string::npos,
          "indirect modulation cycle rejected");
    auto disabled_cycle = indirect_cycle;
    disabled_cycle.routes[1].enabled = false;
    check(validate_modulation_graph(disabled_cycle, graph_diagnostics),
          "disabled connection does not participate in graph cycle");
    auto over_budget = graph;
    over_budget.modulator_count =
        ModulationConfiguration::maximum_modulators + 1;
    check(!validate_modulation_graph(over_budget, graph_diagnostics) &&
              graph_diagnostics.message.find("budget") != std::string::npos,
          "graph capacity exhaustion preserves active configuration");
    auto prepared_graph = graph;
    check(prepare_modulation_graph(prepared_graph, graph_diagnostics) &&
              prepared_graph.graph_prepared &&
              prepared_graph.prepared_count == graph.modulator_count &&
              engine.configure(prepared_graph),
          "control-side graph preparation produces callback-ready order");
    check(engine.configure(graph), "compiled modulation graph configuration");
    engine.set_input(ModulationSourceId::xy_x, 1.0F);
    process(2);
    const auto graph_value = engine.snapshot().modulator_values[1];
    check(graph_value != 0.0F,
          "modulator parameter input evaluated before destination");
    check(engine.configure(graph), "modulator automation reset configuration");
    engine.set_input(ModulationSourceId::xy_x, 1.0F);
    engine.set_modulator_automation(10, ModulatorParameterId::rate, 20.0F);
    process(2);
    check(engine.snapshot().modulator_values[1] > graph_value,
          "modulator base automation composes before graph modulation");

    ModulationConfiguration tapped;
    tapped.update_interval = 1;
    tapped.modulator_count = 1;
    tapped.modulators[0].id = 1;
    tapped.modulators[0].type = ModulatorType::envelope_follower;
    tapped.modulators[0].owner_kind = ModulatorOwnerKind::auxiliary;
    tapped.modulators[0].owner_id = 0;
    tapped.modulators[0].settings.follower_tap = FollowerTapKind::track;
    tapped.modulators[0].settings.follower_tap_id = 9;
    check(engine.configure(tapped), "audio follower tap configuration");
    engine.set_follower_input(FollowerTapKind::track, 8, 1.0F);
    process(1);
    const auto ignored_tap = engine.snapshot().modulator_values[0];
    engine.set_follower_input(FollowerTapKind::track, 9, 1.0F);
    process(1);
    check(engine.snapshot().modulator_values[0] > ignored_tap,
          "envelope follower stable tap selection");
    tapped.modulators[0].owner_kind = ModulatorOwnerKind::group;
    tapped.modulators[0].owner_id = 100;
    tapped.modulators[0].settings.follower_tap = FollowerTapKind::group;
    tapped.modulators[0].settings.follower_tap_id = 100;
    check(engine.configure(tapped), "group follower tap configuration");
    engine.set_follower_input(FollowerTapKind::group, 100, 0.75F);
    process(1);
    check(engine.snapshot().modulator_values[0] > 0,
          "group follower tap observation");

    const auto run_frames = [](ModulationEngine &target, std::size_t frames) {
        for (std::size_t frame = 0; frame < frames; ++frame)
            target.process_frame([](auto, auto) {});
    };
    ModulationConfiguration step_config;
    step_config.update_interval = 1;
    step_config.modulator_count = 1;
    step_config.modulators[0].id = 1001;
    step_config.modulators[0].type = ModulatorType::step_sequencer;
    step_config.modulators[0].owner_kind = ModulatorOwnerKind::master;
    auto &step_settings = step_config.modulators[0].settings;
    step_settings.step_count = 4;
    step_settings.step_duration_beats = 0.1F;
    step_settings.step_values[0] = -1;
    step_settings.step_values[1] = -0.25F;
    step_settings.step_values[2] = 0.5F;
    step_settings.step_values[3] = 1;
    ModulationEngine step_engine(100);
    step_engine.set_tempo(60);
    check(step_engine.configure(step_config), "step modulator configuration");
    run_frames(step_engine, 1);
    near(step_engine.snapshot().modulator_values[0], -1,
         "step modulator initial value");
    run_frames(step_engine, 10);
    near(step_engine.snapshot().modulator_values[0], -0.25F,
         "step modulator tempo timing");
    step_config.modulators[0].settings.order = 1;
    check(step_engine.configure(step_config), "reverse step configuration");
    run_frames(step_engine, 11);
    near(step_engine.snapshot().modulator_values[0], 1,
         "step modulator reverse order");
    step_config.modulators[0].settings.order = 2;
    check(step_engine.configure(step_config), "ping-pong step configuration");
    for (const auto expected : {-0.25F, 0.5F, 1.0F, 0.5F}) {
        run_frames(step_engine, 11);
        near(step_engine.snapshot().modulator_values[0], expected,
             "step modulator ping-pong order");
    }
    step_config.modulators[0].settings.order = 3;
    step_config.modulators[0].settings.seed = 77;
    ModulationEngine random_step_a(100), random_step_b(100);
    random_step_a.set_tempo(60);
    random_step_b.set_tempo(60);
    check(random_step_a.configure(step_config) &&
              random_step_b.configure(step_config),
          "random-order step configurations");
    run_frames(random_step_a, 55);
    run_frames(random_step_b, 55);
    near(random_step_a.snapshot().modulator_values[0],
         random_step_b.snapshot().modulator_values[0],
         "step modulator deterministic random order");

    ModulationConfiguration euclidean_config;
    euclidean_config.update_interval = 1;
    euclidean_config.modulator_count = 1;
    euclidean_config.modulators[0].id = 1002;
    euclidean_config.modulators[0].type = ModulatorType::euclidean;
    euclidean_config.modulators[0].owner_kind = ModulatorOwnerKind::master;
    euclidean_config.modulators[0].settings.step_count = 8;
    euclidean_config.modulators[0].settings.pulses = 3;
    euclidean_config.modulators[0].settings.step_duration_beats = 0.1F;
    euclidean_config.modulators[0].settings.gate_length = 0.5F;
    ModulationEngine euclidean_engine(10);
    euclidean_engine.set_tempo(60);
    check(euclidean_engine.configure(euclidean_config),
          "Euclidean modulator configuration");
    std::size_t euclidean_hits{};
    for (std::size_t step = 0; step < 8; ++step) {
        run_frames(euclidean_engine, 1);
        euclidean_hits +=
            euclidean_engine.snapshot().modulator_outputs[0][0] > 0.5F;
    }
    check(euclidean_hits == 3, "Euclidean pulse distribution");

    ModulationConfiguration random_config;
    random_config.update_interval = 1;
    random_config.modulator_count = 1;
    random_config.modulators[0].id = 1003;
    random_config.modulators[0].type = ModulatorType::clocked_random;
    random_config.modulators[0].owner_kind = ModulatorOwnerKind::master;
    random_config.modulators[0].settings.seed = 0x12345678U;
    random_config.modulators[0].settings.step_duration_beats = 0.1F;
    random_config.modulators[0].settings.quantization = 5;
    random_config.modulators[0].settings.correlation = 0.5F;
    ModulationEngine random_a(10), random_b(10);
    random_a.set_tempo(60);
    random_b.set_tempo(60);
    check(random_a.configure(random_config) &&
              random_b.configure(random_config),
          "clocked random configurations");
    for (std::size_t step = 0; step < 12; ++step) {
        run_frames(random_a, 1);
        run_frames(random_b, 1);
        near(random_a.snapshot().modulator_values[0],
             random_b.snapshot().modulator_values[0],
             "clocked random deterministic seed");
        const auto quantized =
            (random_a.snapshot().modulator_values[0] + 1.0F) * 2.0F;
        check(std::fabs(quantized - std::round(quantized)) < 0.0001F,
              "clocked random quantization");
    }

    ModulationConfiguration function_config;
    function_config.update_interval = 1;
    function_config.modulator_count = 1;
    function_config.modulators[0].id = 1004;
    function_config.modulators[0].type = ModulatorType::function_generator;
    function_config.modulators[0].owner_kind = ModulatorOwnerKind::master;
    function_config.modulators[0].settings.rise_seconds = 0.02F;
    function_config.modulators[0].settings.fall_seconds = 0.02F;
    function_config.modulators[0].settings.looping = true;
    function_config.modulators[0].settings.unipolar = true;
    ModulationEngine function_engine(100);
    check(function_engine.configure(function_config),
          "function generator configuration");
    function_engine.note_on(0, 1);
    bool saw_eoc{};
    for (std::size_t frame = 0; frame < 10; ++frame) {
        run_frames(function_engine, 1);
        saw_eoc |= function_engine.snapshot().modulator_outputs[0][1] > 0.5F;
    }
    check(saw_eoc && function_engine.snapshot().modulator_values[0] >= 0,
          "looping function generator and EOC output");

    ModulationConfiguration math_config;
    math_config.update_interval = 1;
    math_config.modulator_count = 4;
    for (std::size_t i = 0; i < 2; ++i) {
        math_config.modulators[i].id =
            static_cast<ModulatorInstanceId>(1101 + i);
        math_config.modulators[i].type = ModulatorType::step_sequencer;
        math_config.modulators[i].owner_kind = ModulatorOwnerKind::master;
        math_config.modulators[i].settings.step_count = 1;
        math_config.modulators[i].settings.pulses = 1;
        math_config.modulators[i].settings.step_duration_beats = 1;
    }
    math_config.modulators[0].settings.step_values[0] = 0.25F;
    math_config.modulators[1].settings.step_values[0] = -0.5F;
    math_config.modulators[2].id = 1103;
    math_config.modulators[2].type = ModulatorType::sum;
    math_config.modulators[2].owner_kind = ModulatorOwnerKind::master;
    math_config.modulators[3].id = 1104;
    math_config.modulators[3].type = ModulatorType::rectify;
    math_config.modulators[3].owner_kind = ModulatorOwnerKind::master;
    math_config.route_count = 3;
    for (std::size_t i = 0; i < 2; ++i) {
        auto &route = math_config.routes[i];
        route.id = static_cast<ModulationRouteId>(1201 + i);
        route.source_instance = static_cast<ModulatorInstanceId>(1101 + i);
        route.destination_kind = ModulationDestinationKind::modulator_input;
        route.destination_modulator = 1103;
        route.destination_input = static_cast<std::uint32_t>(i + 1);
        route.depth = 1;
        route.minimum = -1;
        route.maximum = 1;
    }
    math_config.routes[2] = math_config.routes[0];
    math_config.routes[2].id = 1203;
    math_config.routes[2].source_output = 2;
    math_config.routes[2].destination_modulator = 1104;
    math_config.routes[2].destination_input = 1;
    ModulationEngine math_engine(100);
    check(math_engine.configure(math_config), "multi-input math configuration");
    run_frames(math_engine, 2);
    near(math_engine.snapshot().modulator_values[2], -0.25F,
         "multi-input sum processor");
    bool routed_secondary_output{};
    for (std::size_t frame = 0; frame < 100; ++frame) {
        run_frames(math_engine, 1);
        routed_secondary_output |=
            math_engine.snapshot().modulator_values[3] > 0.5F;
    }
    check(routed_secondary_output, "secondary output port graph routing");

    ModulationConfiguration chaos_config;
    chaos_config.update_interval = 1;
    chaos_config.modulator_count = 1;
    chaos_config.modulators[0].id = 1301;
    chaos_config.modulators[0].type = ModulatorType::logistic;
    chaos_config.modulators[0].owner_kind = ModulatorOwnerKind::master;
    chaos_config.modulators[0].settings.rate_hz = 50;
    chaos_config.modulators[0].settings.chaos = 4;
    ModulationEngine chaos_a(1000), chaos_b(1000);
    check(chaos_a.configure(chaos_config) && chaos_b.configure(chaos_config),
          "logistic generator configurations");
    run_frames(chaos_a, 1000);
    for (const auto partition : {13, 29, 7, 401, 550})
        run_frames(chaos_b, partition);
    const auto chaos_value = chaos_a.snapshot().modulator_values[0];
    check(std::isfinite(chaos_value) && chaos_value >= -1 && chaos_value <= 1,
          "chaos numerical stability");
    near(chaos_value, chaos_b.snapshot().modulator_values[0],
         "expanded modulator callback partition independence");

    Mixer numeric_mixer(1000);
    MixerConfiguration numeric_config;
    numeric_config.revision = 1;
    numeric_config.track_count = 1;
    numeric_config.tracks[0].track = 1;
    std::string numeric_error;
    check(numeric_mixer.publish(numeric_config, numeric_error),
          "numeric mixer configuration");
    numeric_mixer.begin_block(1);
    check(
        numeric_mixer.set_modulation(
            {ParameterTarget::mixer_track,
             static_cast<std::uint32_t>(MixerTrackModulationParameter::pan), 1},
            1.0F),
        "mixer numeric overlay");
    numeric_mixer.capture_modulation_frame(0);
    check(numeric_mixer.add_track(1, 0, 1.0F, 1.0F), "mixer test signal");
    std::array<float, 1> mix_left{}, mix_right{};
    numeric_mixer.process(mix_left, mix_right);
    near(mix_left[0], 0.0F, "mixer pan overlay left");
    near(mix_right[0], 1.0F, "mixer pan overlay right");

    const auto render_partitioned_mixer = [&](bool split) {
        Mixer mixer(1000);
        std::string error;
        check(mixer.publish(numeric_config, error), "partition mixer publish");
        std::array<float, 64> rendered_left{}, rendered_right{};
        const auto render = [&](std::size_t offset, std::size_t count,
                                float pan_value) {
            mixer.begin_block(count);
            check(mixer.set_modulation({ParameterTarget::mixer_track,
                                        static_cast<std::uint32_t>(
                                            MixerTrackModulationParameter::pan),
                                        1},
                                       pan_value),
                  "partition mixer modulation");
            for (std::size_t frame = 0; frame < count; ++frame) {
                mixer.capture_modulation_frame(frame);
                check(mixer.add_track(1, frame, 1, 1), "partition mixer input");
            }
            mixer.process(
                std::span<float>(rendered_left).subspan(offset, count),
                std::span<float>(rendered_right).subspan(offset, count));
        };
        if (split) {
            render(0, 32, -1);
            render(32, 32, 1);
        } else {
            mixer.begin_block(64);
            check(mixer.set_modulation({ParameterTarget::mixer_track,
                                        static_cast<std::uint32_t>(
                                            MixerTrackModulationParameter::pan),
                                        1},
                                       -1),
                  "whole mixer first modulation");
            for (std::size_t frame = 0; frame < 64; ++frame) {
                if (frame == 32)
                    check(mixer.set_modulation(
                              {ParameterTarget::mixer_track,
                               static_cast<std::uint32_t>(
                                   MixerTrackModulationParameter::pan),
                               1},
                              1),
                          "whole mixer second modulation");
                mixer.capture_modulation_frame(frame);
                check(mixer.add_track(1, frame, 1, 1), "whole mixer input");
            }
            mixer.process(rendered_left, rendered_right);
        }
        return std::pair{rendered_left, rendered_right};
    };
    const auto whole_mixer = render_partitioned_mixer(false);
    const auto split_mixer = render_partitioned_mixer(true);
    for (std::size_t frame = 0; frame < 64; ++frame) {
        near(whole_mixer.first[frame], split_mixer.first[frame],
             "mixer modulation left callback partition independence");
        near(whole_mixer.second[frame], split_mixer.second[frame],
             "mixer modulation right callback partition independence");
    }

    ModulationConfiguration voice_velocity;
    voice_velocity.update_interval = 1;
    voice_velocity
        .sources[static_cast<std::size_t>(ModulationSourceId::velocity)]
        .scope = ModulationScope::voice;
    voice_velocity.route_count = 1;
    voice_velocity.routes[0] = {
        1,
        ModulationSourceId::velocity,
        {ParameterTarget::subtractive_synth,
         static_cast<std::uint32_t>(SynthParameter::gain), 1},
        0,
        1,
        0.5F,
        0.5F,
        true,
        ModulationPolarity::bipolar,
        ModulationScope::voice};
    check(first.configure(voice_velocity),
          "per-voice modulation configuration validation");
    SubtractiveSynth high_only(1000, 4), high_and_zero(1000, 4);
    check(high_only.configure_voice_modulation(voice_velocity, 1) &&
              high_and_zero.configure_voice_modulation(voice_velocity, 1),
          "per-voice velocity modulation publication");
    high_only.note_on(60, 1.0F, {1, 1, 0});
    high_and_zero.note_on(60, 1.0F, {1, 1, 0});
    high_and_zero.note_on(60, 0.0F, {1, 2, 0});
    for (int i = 0; i < 32; ++i)
        near(high_and_zero.render_sample(), high_only.render_sample(),
             "velocity modulation is independent per voice");

    ModulationConfiguration voice_lfo = voice_velocity;
    voice_lfo.sources[0].scope = ModulationScope::voice;
    voice_lfo.sources[0].waveform = LfoWaveform::sine;
    voice_lfo.sources[0].rate_hz = 3.0F;
    voice_lfo.sources[0].retrigger = true;
    voice_lfo.routes[0].source = ModulationSourceId::lfo_1;
    voice_lfo.routes[0].destination.parameter =
        static_cast<std::uint32_t>(SynthParameter::pitch);
    voice_lfo.routes[0].minimum = -24;
    voice_lfo.routes[0].maximum = 24;
    voice_lfo.routes[0].base_value = 0;
    voice_lfo.routes[0].depth = 0.25F;
    SubtractiveSynth whole(48000, 4), partitioned(48000, 4);
    check(whole.configure_voice_modulation(voice_lfo, 1) &&
              partitioned.configure_voice_modulation(voice_lfo, 1),
          "per-voice LFO configuration");
    whole.note_on(64, 0.8F, {1, 1, 0});
    partitioned.note_on(64, 0.8F, {1, 1, 0});
    std::array<float, 256> whole_left{}, whole_right{}, split_left{},
        split_right{};
    whole.process(whole_left, whole_right);
    for (std::size_t offset = 0; offset < split_left.size(); offset += 64)
        partitioned.process(std::span<float>(split_left).subspan(offset, 64),
                            std::span<float>(split_right).subspan(offset, 64));
    for (std::size_t i = 0; i < whole_left.size(); ++i)
        near(split_left[i], whole_left[i],
             "per-voice modulation callback partition independence");

    ModulationConfiguration voice_envelope = voice_velocity;
    auto &envelope_settings =
        voice_envelope
            .sources[static_cast<std::size_t>(ModulationSourceId::envelope)];
    envelope_settings.scope = ModulationScope::voice;
    envelope_settings.attack_seconds = 1.0F;
    voice_envelope.routes[0].source = ModulationSourceId::envelope;
    voice_envelope.routes[0].base_value = 0.0F;
    voice_envelope.routes[0].depth = 1.0F;
    SubtractiveSynth envelope_single(1000, 4), envelope_overlap(1000, 4);
    check(envelope_single.configure_voice_modulation(voice_envelope, 1) &&
              envelope_overlap.configure_voice_modulation(voice_envelope, 1),
          "per-voice envelope configuration");
    envelope_single.note_on(60, 1.0F, {1, 1, 0});
    envelope_overlap.note_on(60, 1.0F, {1, 1, 0});
    for (int i = 0; i < 600; ++i) {
        (void)envelope_single.render_sample();
        (void)envelope_overlap.render_sample();
    }
    envelope_overlap.note_on(67, 1.0F, {1, 2, 0});
    near(envelope_overlap.render_sample(), envelope_single.render_sample(),
         "new overlapping voice did not start its own envelope");
    bool overlap_became_audible{};
    for (int i = 0; i < 600; ++i)
        overlap_became_audible |=
            std::abs(envelope_overlap.render_sample() -
                     envelope_single.render_sample()) > 0.001F;
    check(overlap_became_audible,
          "overlapping per-voice envelope never advanced independently");

    Timeline instances;
    for (std::uint64_t id = 1; id <= 2; ++id) {
        Track track;
        track.id = id;
        track.instrument_id = std::string(subtractive_instrument_id);
        track.instrument_instance_id = static_cast<InstrumentInstanceId>(id);
        track.pattern.length = ticks_per_quarter;
        track.pattern.events.push_back({id, 0, MusicalEventType::note,
                                        static_cast<std::uint8_t>(59 + id),
                                        1.0F, ticks_per_quarter});
        instances.tracks.push_back(std::move(track));
    }
    AudioEngine instance_audio(1000, 4);
    std::string instance_error;
    check(instance_audio.queue_timeline(instances, instance_error),
          "independent synth instance timeline publication");
    instance_audio.play();
    std::array<float, 32> instance_left{}, instance_right{};
    instance_audio.process(instance_left, instance_right);
    check(instance_audio.synth_instance_state(1).active_voices == 1 &&
              instance_audio.synth_instance_state(2).active_voices == 1,
          "timeline tracks reached independent synth instances");

    AudioEngine audio(1000);
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return audio.queue_timeline(value, error);
    });
    auto sample_track = timeline.edit();
    check(sample_track.create_track(7, "sample voice", ticks_per_quarter,
                                    instance_error) &&
              timeline.commit(std::move(sample_track),
                              TimelineCommitPolicy::next_audio_callback,
                              instance_error),
          "sample voice owner track setup");
    SampleManager samples(audio);
    ApplicationService application(audio, timeline, samples);
    check(application
                  .execute({"modulator.create.v1",
                            {{"type", std::int64_t{static_cast<std::int64_t>(
                                          ModulatorType::lfo)}},
                             {"owner", std::int64_t{static_cast<std::int64_t>(
                                           ModulatorOwnerKind::master)}},
                             {"owner_id", std::int64_t{0}},
                             {"name", std::string("Motion")}}})
                  .accepted &&
              application.snapshot().modulation.modulator_count == 1,
          "semantic modulator creation");
    check(application
                  .execute({"modulator.route-create.v1",
                            {{"source_id", std::int64_t{1}},
                             {"destination", std::string("effect.delay.mix")},
                             {"depth", 0.2}}})
                  .accepted &&
              application.snapshot().modulation.routes[0].source_instance == 1,
          "semantic instance route creation");
    check(
        application
                .execute({"modulator.duplicate.v1", {{"id", std::int64_t{1}}}})
                .accepted &&
            application.snapshot().modulation.modulator_count == 2,
        "semantic modulator duplication with stable identity");
    std::array<float, 64> left{}, right{};
    audio.process(left,
                  right); // acknowledge bounded configuration publications
    check(
        application
                .execute({"modulation.connection-create.v1",
                          {{"source_id", std::int64_t{1}},
                           {"destination_id", std::int64_t{2}},
                           {"parameter", std::int64_t{static_cast<std::int64_t>(
                                             ModulatorParameterId::rate)}},
                           {"depth", 0.25}}})
                .accepted &&
            application.snapshot().modulation_graph.valid,
        "semantic modulator graph connection");
    audio.process(left, right);
    check(application
                  .execute({"modulator.create.v1",
                            {{"type", std::int64_t{static_cast<std::int64_t>(
                                          ModulatorType::step_sequencer)}},
                             {"owner", std::int64_t{static_cast<std::int64_t>(
                                           ModulatorOwnerKind::master)}},
                             {"owner_id", std::int64_t{0}}}})
                  .accepted &&
              application
                  .execute({"modulator.parameter-set.v1",
                            {{"id", std::int64_t{3}},
                             {"parameter", std::string("steps")},
                             {"value", 4.0}}})
                  .accepted &&
              application
                  .execute({"modulator.step-set.v1",
                            {{"id", std::int64_t{3}},
                             {"step", std::int64_t{2}},
                             {"value", 0.75}}})
                  .accepted &&
              application.snapshot()
                      .modulation.modulators[2]
                      .settings.step_values[2] == 0.75F,
          "semantic step modulator editing");
    audio.process(left, right);
    check(application
                  .execute(
                      {"modulator.create.v1",
                       {{"type", std::int64_t{static_cast<std::int64_t>(
                                     ModulatorType::velocity)}},
                        {"owner", std::int64_t{static_cast<std::int64_t>(
                                      ModulatorOwnerKind::voice_definition)}},
                        {"owner_id", std::int64_t{7}}}})
                  .accepted &&
              application
                  .execute({"modulator.route-create.v1",
                            {{"source_id", std::int64_t{4}},
                             {"destination", std::string("sample.gain")},
                             {"depth", 0.5}}})
                  .accepted &&
              application.snapshot().modulation.modulators[3].owner_kind ==
                  ModulatorOwnerKind::voice_definition &&
              application.snapshot().modulation.routes[2].scope ==
                  ModulationScope::voice,
          "semantic voice-definition creation and sample routing");
    audio.process(left, right);
    auto created = application.execute(
        {"modulation.route-create.v1",
         {{"source",
           std::int64_t{static_cast<std::int64_t>(ModulationSourceId::xy_x)}},
          {"destination", std::string("synth.cutoff")},
          {"depth", 0.25},
          {"scope",
           std::int64_t{static_cast<std::int64_t>(ModulationScope::global)}}}});
    check(created.accepted &&
              application.snapshot().modulation.route_count == 4 &&
              application.snapshot().project_dirty,
          "semantic modulation route creation");
    check(application
              .execute({"instrument.instance-parameter-set.v1",
                        {{"instance_id", std::int64_t{2}},
                         {"parameter_id", std::string("synth.cutoff")},
                         {"value", 2500.0}}})
              .accepted,
          "instrument instance parameter action");
    check(application
              .execute(
                  {"modulation.xy.v1", {{"position", NormalizedXY{1.0, 0.5}}}})
              .accepted,
          "XY modulation action");
    audio.process(left, right);
    check(audio.modulation_snapshot().effective_values[3] > 8000,
          "application modulation reached audio engine");
    check(application
              .execute({"modulation.route-create.v1",
                        {{"source", std::int64_t{static_cast<std::int64_t>(
                                        ModulationSourceId::xy_x)}},
                         {"destination", std::string("mixer.track.pan")},
                         {"depth", 0.5},
                         {"scope", std::int64_t{static_cast<std::int64_t>(
                                       ModulationScope::global)}},
                         {"instance_id", std::int64_t{1}}}})
              .accepted,
          "semantic mixer modulation destination");
    check(!application
               .execute({"modulation.route-create.v1",
                         {{"source", std::int64_t{0}},
                          {"destination", std::string("transport.tempo")},
                          {"depth", 0.5},
                          {"scope", std::int64_t{0}}}})
               .accepted,
          "unsafe modulation destination rejection");
    check(application
                  .execute({"modulation.source-configure.v1",
                            {{"source", std::int64_t{static_cast<std::int64_t>(
                                            ModulationSourceId::velocity)}},
                             {"waveform", std::int64_t{0}},
                             {"rate", 0.0},
                             {"tempo_sync", false},
                             {"beats", 1.0},
                             {"scope", std::int64_t{static_cast<std::int64_t>(
                                           ModulationScope::voice)}}}})
                  .accepted &&
              application
                  .execute({"modulation.route-create.v1",
                            {{"source", std::int64_t{static_cast<std::int64_t>(
                                            ModulationSourceId::velocity)}},
                             {"destination", std::string("synth.gain")},
                             {"depth", 0.5},
                             {"scope", std::int64_t{static_cast<std::int64_t>(
                                           ModulationScope::voice)}},
                             {"instance_id", std::int64_t{1}}}})
                  .accepted,
          "semantic per-voice modulation route creation");
}

void test_performance_macros() {
    using namespace neurogenic;
    Timeline timeline;
    timeline.tracks.push_back({1, "Macro track", false,
                               {ticks_per_quarter * 4, {}}});
    MixerConfiguration mixer;
    mixer.track_count = 1;
    mixer.tracks[0].track = 1;
    PerformanceMacroService macros;
    PerformanceMacroId macro_id{};
    std::string error;
    check(macros.create("Tone", MacroOwnerKind::track, 1, 0.25F, macro_id,
                        error),
          "create track performance macro");
    MacroAssignment cutoff;
    cutoff.destination_id = "synth.instance.1.cutoff";
    cutoff.minimum = 100.0F;
    cutoff.maximum = 10100.0F;
    cutoff.curve = ModulationMappingCurve::s_curve;
    MacroAssignmentId assignment_id{};
    check(macros.assign(macro_id, cutoff, assignment_id, error),
          "assign macro destination");

    ModulationConfiguration compiled;
    check(macros.compile({}, compiled, error) && compiled.modulator_count == 1 &&
              compiled.route_count == 1 && compiled.graph_prepared,
          "compile macro into modulation graph");
    ModulationEngine engine(48000);
    check(engine.configure(compiled), "configure compiled macro graph");
    float effective{};
    for (int frame = 0; frame < 32; ++frame)
        engine.process_frame([&](ParameterDestination destination, float value) {
            if (destination.target == ParameterTarget::subtractive_synth)
                effective = value;
        });
    near(effective, 1662.5F, "S-curve macro default mapping");
    engine.set_modulator_value(compiled.modulators[0].id, 1.0F);
    for (int frame = 0; frame < 32; ++frame)
        engine.process_frame([&](ParameterDestination destination, float value) {
            if (destination.target == ParameterTarget::subtractive_synth)
                effective = value;
        });
    near(effective, 10100.0F, "macro reaches mapped maximum");

    PerformanceMacroId second{};
    check(macros.create("Second", MacroOwnerKind::project, 0, 0.5F, second,
                        error),
          "create second macro");
    cutoff.curve = ModulationMappingCurve::linear;
    cutoff.minimum = 100.0F;
    cutoff.maximum = 5100.0F;
    check(macros.assign(second, cutoff, assignment_id, error),
          "assign multiple macros to one destination");
    check(macros.compile({}, compiled, error), "compile summed macros");
    check(compiled.modulators[0].id != compiled.modulators[1].id,
          "stable independent macro source identities");

    PerformanceSceneId scene{};
    check(macros.capture_scene("A", 2.0F, false, MacroMorphCurve::linear,
                               scene, error),
          "capture performance scene");
    check(macros.set_value(macro_id, 1.0F, error), "change macro before recall");
    check(macros.start_morph(scene, 1.0F, false, 0, 100, 1000, 120.0,
                             error),
          "start scene morph");
    std::vector<std::pair<ModulatorInstanceId, float>> updates;
    check(macros.advance(0, 600, updates) && !updates.empty(),
          "advance scene morph");
    near(macros.snapshot().macros.front().value, 0.625F,
         "scene morph midpoint");
    check(macros.advance(0, 1100, updates) &&
              !macros.snapshot().morph.active,
          "complete scene morph");

    MacroProject invalid = macros.project();
    invalid.macros.front().owner_id = 99;
    check(!macros.replace(invalid, timeline, mixer, error),
          "reject missing macro owner");
    check(resolve_parameter_destination(
              "macro." + std::to_string(compiled.modulators[0].id) +
              ".value")
              .has_value(),
          "macro automation destination discovery");
}

void test_precise_macro_morphing() {
    using namespace neurogenic;
    MacroMorphProcessor processor;
    std::array<float, 9> values{};
    const auto apply = [&](ModulatorInstanceId instance, float value) {
        values[instance] = value;
    };
    MacroMorphCommand immediate;
    immediate.morph_id = 1;
    immediate.instance = 1;
    immediate.start_frame = 64;
    immediate.start_value = 0.1F;
    immediate.target_value = 0.9F;
    check(processor.command(immediate, 1000, 120),
          "queue immediate macro recall");
    processor.process(64, false, 1000, 120, apply);
    near(values[1], 0.9F, "immediate recall while transport stopped");

    MacroMorphCommand timed = immediate;
    timed.morph_id = 2;
    timed.instance = 2;
    timed.start_frame = 100;
    timed.start_value = 0;
    timed.target_value = 1;
    timed.duration = 1;
    check(processor.command(timed, 1000, 120), "queue timed macro morph");
    for (std::uint64_t frame = 0; frame <= 1124; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    near(values[2], 1.0F, "timed morph completion");
    const auto timed_state = std::find_if(
        processor.morphs().begin(), processor.morphs().end(),
        [](const auto &morph) { return morph.instance == 2; });
    check(timed_state != processor.morphs().end() && timed_state->completed &&
              timed_state->end_frame == 1100,
          "timed morph completion diagnostics");

    MacroMorphCommand beat = timed;
    beat.morph_id = 3;
    beat.instance = 3;
    beat.start_frame = 0;
    beat.duration_unit = MacroMorphDurationUnit::beats;
    beat.duration = 1;
    check(processor.command(beat, 1000, 60), "queue beat morph");
    for (std::uint64_t frame = 0; frame < 500; ++frame)
        processor.process(frame, true, 1000, 60, apply);
    check(values[3] > 0.43F && values[3] < 0.54F,
          "beat morph first half at 60 BPM");
    for (std::uint64_t frame = 500; frame <= 800; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    near(values[3], 1.0F, "beat morph follows tempo change");

    MacroMorphCommand paused = timed;
    paused.morph_id = 4;
    paused.instance = 4;
    paused.start_frame = 1000;
    paused.duration = 0.5;
    check(processor.command(paused, 1000, 120), "queue pausable morph");
    for (std::uint64_t frame = 1000; frame < 1200; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    const auto before_stop = values[4];
    for (int callback = 0; callback < 20; ++callback)
        processor.process(1200, false, 1000, 120, apply);
    near(values[4], before_stop, "transport stop freezes macro morph");
    for (std::uint64_t frame = 1200; frame <= 1600; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    near(values[4], 1.0F, "transport continue resumes macro morph");

    for (ModulatorInstanceId instance = 5; instance <= 8; ++instance) {
        auto simultaneous = timed;
        simultaneous.morph_id = 10 + instance;
        simultaneous.instance = instance;
        simultaneous.start_frame = 2000;
        simultaneous.duration = 0.25;
        simultaneous.curve = instance == 5 ? MacroMorphCurve::exponential
                                           : MacroMorphCurve::s_curve;
        check(processor.command(simultaneous, 1000, 120),
              "queue simultaneous morph");
    }
    for (std::uint64_t frame = 2000; frame <= 2300; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    for (ModulatorInstanceId instance = 5; instance <= 8; ++instance)
        near(values[instance], 1.0F, "simultaneous morph completion");

    MacroMorphCommand interrupted = timed;
    interrupted.morph_id = 30;
    interrupted.instance = 5;
    interrupted.start_frame = 3000;
    interrupted.duration = 1;
    check(processor.command(interrupted, 1000, 120), "start interrupt target");
    for (std::uint64_t frame = 3000; frame < 3300; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    interrupted.morph_id = 31;
    interrupted.start_frame = 3300;
    interrupted.start_value = values[5];
    interrupted.target_value = 0.2F;
    interrupted.duration = 0.1;
    check(processor.command(interrupted, 1000, 120), "replace active morph");
    for (std::uint64_t frame = 3300; frame <= 3450; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    near(values[5], 0.2F, "replacement morph target");

    MacroMorphCommand seeked = timed;
    seeked.morph_id = 40;
    seeked.instance = 6;
    seeked.start_frame = 4000;
    seeked.duration = 0.5;
    check(processor.command(seeked, 1000, 120), "queue seek-safe morph");
    for (std::uint64_t frame = 4000; frame < 4200; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    const auto before_seek = values[6];
    processor.process(1000, true, 1000, 120, apply);
    for (std::uint64_t frame = 1001; frame <= 1400; ++frame)
        processor.process(frame, true, 1000, 120, apply);
    check(values[6] >= before_seek && values[6] > 0.95F,
          "active morph rebases safely after transport seek");

    ParameterSmoother smoother;
    smoother.reset(0);
    smoother.set_target(1, 0.1F, 1000, ParameterSmoothingMode::linear);
    for (int i = 0; i < 100; ++i)
        smoother.process();
    near(smoother.value(), 1, "linear parameter smoothing");
    smoother.reset(0);
    smoother.set_target(1, 0.01F, 1000,
                        ParameterSmoothingMode::exponential);
    const auto first = smoother.process();
    for (int i = 0; i < 100; ++i)
        smoother.process();
    check(first > 0 && first < 1 && smoother.value() > 0.99F,
          "exponential parameter smoothing");
    smoother.set_target(0.25F, 0, 1000, ParameterSmoothingMode::bypass);
    near(smoother.value(), 0.25F, "discrete smoothing bypass");

    auto configured_engine = [] {
        auto engine = std::make_unique<AudioEngine>(48000);
        ModulationConfiguration configuration;
        configuration.modulator_count = 1;
        configuration.modulators[0].id = 0x40000001U;
        configuration.modulators[0].type = ModulatorType::performance_macro;
        configuration.modulators[0].owner_kind = ModulatorOwnerKind::master;
        configuration.modulators[0].settings.manual_value = 0;
        ModulationGraphDiagnostics diagnostics;
        check(prepare_modulation_graph(configuration, diagnostics),
              "prepare engine morph modulation");
        check(engine->publish_modulation(configuration),
              "publish engine morph modulation");
        engine->play();
        MacroMorphCommand command;
        command.morph_id = 100;
        command.instance = 0x40000001U;
        command.start_frame = 0;
        command.target_value = 1;
        command.duration = 0.02;
        check(engine->schedule_macro_morph(command), "schedule engine morph");
        return engine;
    };
    auto fixed = configured_engine();
    auto partitioned = configured_engine();
    std::vector<float> left(992), right(992);
    fixed->process(left, right);
    std::array<std::size_t, 9> partitions{17, 113, 64, 5, 251,
                                          97, 301, 112, 32};
    for (const auto frames : partitions)
        partitioned->process(std::span<float>(left).first(frames),
                             std::span<float>(right).first(frames));
    const auto fixed_morphs = fixed->macro_morphs();
    const auto partitioned_morphs = partitioned->macro_morphs();
    near(fixed_morphs[0].effective_value,
         partitioned_morphs[0].effective_value,
         "morph callback partition independence");
    check(fixed_morphs[0].completed && partitioned_morphs[0].completed,
          "engine morph completion acknowledgement");

    AudioEngine automated(48000);
    ModulationConfiguration automation_configuration;
    automation_configuration.modulator_count = 1;
    automation_configuration.modulators[0].id = 0x40000002U;
    automation_configuration.modulators[0].type =
        ModulatorType::performance_macro;
    automation_configuration.modulators[0].owner_kind =
        ModulatorOwnerKind::master;
    ModulationGraphDiagnostics automation_graph;
    check(prepare_modulation_graph(automation_configuration, automation_graph) &&
              automated.publish_modulation(automation_configuration),
          "prepare automated macro");
    Timeline automation_timeline;
    Track automation_track;
    automation_track.id = 1;
    automation_track.pattern.length = ticks_per_quarter;
    MusicalEvent automation_event{1, 0, MusicalEventType::parameter};
    automation_event.parameter_destination =
        "macro." + std::to_string(0x40000002U) + ".value";
    automation_event.value = 0.3F;
    automation_track.pattern.events.push_back(automation_event);
    automation_timeline.tracks.push_back(automation_track);
    std::string automation_error;
    check(automated.queue_timeline(automation_timeline, automation_error),
          "queue macro automation timeline");
    MacroMorphCommand automated_morph;
    automated_morph.morph_id = 200;
    automated_morph.instance = 0x40000002U;
    automated_morph.target_value = 1;
    automated_morph.duration = 1;
    check(automated.schedule_macro_morph(automated_morph),
          "queue morph before recorded automation");
    std::array<float, 64> automated_left{}, automated_right{};
    automated.process(automated_left, automated_right);
    const auto automated_state = automated.macro_morphs();
    check(automated_state[0].interrupted && !automated_state[0].active,
          "recorded macro automation interrupts active morph");
    near(automated_state[0].effective_value, 0.3F,
         "recorded macro automation value timing");

    AudioEngine overflow_engine;
    MacroMorphCommand queued;
    queued.morph_id = 1;
    queued.instance = 1;
    std::size_t accepted{};
    for (std::size_t i = 0; i < 160; ++i) {
        queued.morph_id = i + 1;
        queued.instance = static_cast<ModulatorInstanceId>(i + 1);
        accepted += overflow_engine.schedule_macro_morph(queued);
    }
    check(accepted < 160 && overflow_engine.dropped_macro_morph_commands() > 0,
          "bounded macro morph command queue overflow diagnostics");
}

void test_midi_routing_and_clock() {
    using namespace neurogenic;
    MidiOutputScheduler output;
    MidiClockGenerator clock(48000, 500000);
    const std::array<MidiPortId, 2> ports{1, 2};
    clock.start(output, ports);
    for (std::uint64_t frame = 0; frame < 480000; frame += 127)
        clock.process(static_cast<std::uint32_t>(
                          std::min<std::uint64_t>(127, 480000 - frame)),
                      frame, output, ports);
    std::vector<MidiPacket> sent;
    output.dispatch([&](const MidiPacket &packet) {
        sent.push_back(packet);
        return true;
    });
    check(clock.pulses() == 480 && sent.size() == 962 &&
              sent[0].bytes[0] == 0xFA && sent[1].bytes[0] == 0xFA,
          "24 PPQN clock generation and Start ordering");
    check(sent[2].frame < sent[4].frame, "MIDI clock timestamps ordered");

    MidiClockGenerator fractional(48000, 487805); // approximately 123 BPM
    fractional.start(output, std::span<const MidiPortId>(ports).first(1));
    fractional.process(4'800'000, 0, output,
                       std::span<const MidiPortId>(ports).first(1));
    const auto expected = static_cast<std::uint64_t>(
        std::floor(4'800'000.0 * 24'000'000.0 / (48000.0 * 487805.0)));
    check(fractional.pulses() == expected, "fractional BPM clock has no drift");
    const auto before_change = fractional.pulses();
    fractional.set_tempo(600000);
    fractional.process(48000, 4'800'000, output,
                       std::span<const MidiPortId>(ports).first(1));
    check(fractional.pulses() > before_change,
          "tempo change preserved clock phase");
    fractional.stop(output, std::span<const MidiPortId>(ports).first(1));
    fractional.continue_playback(output,
                                 std::span<const MidiPortId>(ports).first(1));
    output.dispatch([](const MidiPacket &) { return true; });

    MidiClockReceiver receiver;
    receiver.receive({1, {0xFA, 0, 0}, 1, 0});
    std::uint64_t timestamp{};
    for (int pulse = 0; pulse < 48; ++pulse) {
        timestamp += 20'833'333ULL +
                     static_cast<std::int64_t>((pulse % 3) - 1) * 100'000;
        receiver.receive({1, {0xF8, 0, 0}, 1, timestamp});
    }
    check(receiver.state().running && receiver.state().available &&
              std::fabs(receiver.state().tempo_bpm - 120.0) < 1.0,
          "external MIDI clock jitter filtering");
    receiver.update(timestamp + 600'000'000ULL);
    check(receiver.state().dropout, "external MIDI clock dropout detection");
    receiver.receive({1, {0xF8, 0, 0}, 1, timestamp + 620'000'000ULL});
    check(!receiver.state().dropout, "external MIDI clock recovery");
    receiver.receive({1, {0xFC, 0, 0}, 1, timestamp + 630'000'000ULL});
    check(!receiver.state().running, "external MIDI Stop");

    MidiClockFollower follower(48000);
    follower.receive({1, {0xFA, 0, 0}, 1, 1});
    timestamp = 1;
    for (int pulse = 0; pulse < 96; ++pulse) {
        timestamp += 20'833'333ULL + (pulse % 2 ? 80'000 : -80'000);
        follower.receive({1, {0xF8, 0, 0}, 1, timestamp});
        follower.advance(1000);
    }
    check(follower.state().sync_status == MidiSyncStatus::locked &&
              std::fabs(follower.state().tempo_bpm - 120.0) < 0.5 &&
              std::fabs(follower.state().phase_error_pulses) < 0.2,
          "external follower locks through bounded jitter");
    const auto locked_phase = follower.phase_q32();
    follower.advance(7000);
    check(follower.state().sync_status == MidiSyncStatus::holdover &&
              follower.phase_q32() > locked_phase,
          "external follower bounded holdover");
    follower.advance(24001);
    check(follower.state().sync_status == MidiSyncStatus::lost &&
              follower.state().dropout,
          "external follower freezes after dropout");
    const auto lost_phase = follower.phase_q32();
    follower.advance(4096);
    check(follower.phase_q32() == lost_phase,
          "lost external clock does not free-run indefinitely");
    timestamp += 700'000'000ULL;
    follower.receive({1, {0xF8, 0, 0}, 1, timestamp});
    check(follower.state().sync_status != MidiSyncStatus::lost &&
              follower.consume_discontinuity(),
          "external clock recovery reacquires with discontinuity");
    follower.receive({1, {0xFC, 0, 0}, 1, timestamp + 1});
    const auto stopped_phase = follower.phase_q32();
    follower.advance(4096);
    check(follower.phase_q32() == stopped_phase,
          "MIDI Stop freezes follower position");
    follower.receive({1, {0xF2, 8, 0}, 3, timestamp + 2});
    follower.receive({1, {0xFB, 0, 0}, 1, timestamp + 3});
    check(follower.state().song_position == 8 &&
              (follower.phase_q32() >> 32) == 48,
          "Song Position Pointer and Continue preserve requested position");
    follower.receive({1, {0xFA, 0, 0}, 1, timestamp + 4});
    check(follower.phase_q32() == 0 && follower.state().song_position == 0,
          "MIDI Start resets position unlike Continue");

    MidiClockFollower fractional_follower(48000);
    fractional_follower.receive({1, {0xFA, 0, 0}, 1, 1});
    timestamp = 1;
    for (int pulse = 0; pulse < 96; ++pulse) {
        const auto interval = static_cast<std::uint64_t>(
            21'500'000 - std::min(pulse, 80) * 12'000);
        timestamp += interval;
        fractional_follower.receive({1, {0xF8, 0, 0}, 1, timestamp});
        fractional_follower.advance(900);
    }
    check(fractional_follower.state().tempo_bpm > 120.0 &&
              fractional_follower.state().tempo_bpm < 125.0,
          "external follower tracks gradual acceleration");

    MidiRouter router;
    std::string error;
    const std::array<MidiRoute, 2> routes{
        {{1, 1, 2, 0, 5, true, true, true, true, false, false, true},
         {2, 1, 3, -1, -1, false, false, false, false, true, true, true}}};
    check(router.configure(routes, error), "MIDI route validation");
    check(router.route({1, {0x90, 64, 100}, 3, 1000}, output),
          "MIDI note thru");
    check(router.route({1, {0xB0, 74, 80}, 3, 2000}, output), "MIDI CC thru");
    check(router.route({1, {0xF8, 0, 0}, 1, 3000}, output),
          "MIDI clock forwarding");
    sent.clear();
    output.dispatch([&](const MidiPacket &packet) {
        sent.push_back(packet);
        return true;
    });
    const auto remapped =
        std::find_if(sent.begin(), sent.end(), [](const auto &packet) {
            return packet.port == 2 && packet.bytes[0] == 0x95;
        });
    check(remapped != sent.end(), "MIDI channel remapping");
    const std::array<MidiRoute, 1> loop{{{1, 1, 1, -1, -1}}};
    check(!router.configure(loop, error) &&
              error.find("loop") != std::string::npos,
          "MIDI routing-loop rejection");

    MidiOutputScheduler bounded;
    for (std::size_t i = 0; i < 1100; ++i)
        (void)bounded.enqueue({1, {0x90, 60, 100}, 3});
    check(bounded.dropped() > 0 && bounded.queued() == 1024,
          "MIDI output overflow is bounded and observable");
    check(bounded.enqueue({1, {0xF8, 0, 0}, 1}),
          "clock has reserved priority capacity under congestion");
    MidiPacket first_congested;
    bounded.dispatch([&](const MidiPacket &packet) {
        if (!first_congested.size)
            first_congested = packet;
        return true;
    });
    check(first_congested.bytes[0] == 0xF8,
          "clock dispatch is prioritized over dense note traffic");
    AudioEngine congested_input;
    for (int i = 0; i < 300; ++i)
        (void)congested_input.receive_midi(
            {1, {0xF8, 0, 0}, 1, static_cast<std::uint64_t>(i + 1)});
    check(congested_input.dropped_midi_inputs() > 0,
          "MIDI input congestion is bounded and observable");
    MidiOutputScheduler panic;
    panic.panic(1);
    check(panic.queued() == 48,
          "MIDI panic queues sustain release, All Notes and All Sound Off");

    MidiMapper mapper;
    std::vector<MidiMessage> raw;
    mapper.set_raw_handler(
        [&](const MidiMessage &message) { raw.push_back(message); });
    check(mapper.receive({0xF8, 0, 0, 99, 2, 1}) && mapper.dispatch_one() &&
              raw.size() == 1 && raw[0].port == 2,
          "controller mapper preserves realtime messages and logical port");

    Timeline external;
    external.transport.microseconds_per_quarter = 500000;
    Track midi_track;
    midi_track.id = 44;
    midi_track.instrument_id = std::string(external_midi_instrument_id);
    midi_track.midi_output_port = 2;
    midi_track.midi_channel = 9;
    midi_track.pattern.length = ticks_per_quarter;
    midi_track.pattern.events = {
        {1, 0, MusicalEventType::note, 60, 0.5F, 240},
        {2, 120, MusicalEventType::control_change, 60, 0, 0, 74, 0.75F},
        {3, 240, MusicalEventType::program_change, 60, 0, 0, 12, 0},
        {4, 360, MusicalEventType::pitch_bend, 60, 0, 0, 0, -0.5F}};
    external.tracks.push_back(midi_track);
    AudioEngine midi_audio(48000);
    MidiOutputScheduler timeline_output;
    midi_audio.set_midi_output_scheduler(&timeline_output);
    check(midi_audio.queue_timeline(external, error), "external MIDI timeline");
    midi_audio.restart();
    std::array<float, 24001> silence_left{}, silence_right{};
    midi_audio.process(silence_left, silence_right);
    sent.clear();
    timeline_output.dispatch([&](const MidiPacket &packet) {
        sent.push_back(packet);
        return true;
    });
    check(std::any_of(sent.begin(), sent.end(),
                      [](const auto &packet) {
                          return packet.port == 2 && packet.bytes[0] == 0x99 &&
                                 packet.bytes[1] == 60;
                      }) &&
              std::any_of(sent.begin(), sent.end(),
                          [](const auto &packet) {
                              return packet.bytes[0] == 0xB9 &&
                                     packet.bytes[1] == 74;
                          }) &&
              std::any_of(sent.begin(), sent.end(),
                          [](const auto &packet) {
                              return packet.bytes[0] == 0xC9 &&
                                     packet.bytes[1] == 12;
                          }) &&
              std::any_of(
                  sent.begin(), sent.end(),
                  [](const auto &packet) { return packet.bytes[0] == 0xE9; }),
          "timeline note/CC/program/bend MIDI scheduling");

    MidiRoutingConfiguration engine_routing;
    engine_routing.route_count = 1;
    engine_routing.routes[0] = {9,    1,    2,    0,     3,     true,
                                true, true, true, false, false, true};
    engine_routing.accept_external_clock = true;
    check(midi_audio.publish_midi_routing(engine_routing),
          "publish realtime MIDI routing");
    check(midi_audio.receive_midi({1, {0x90, 67, 100}, 3, 1'000}) &&
              midi_audio.receive_midi({1, {0xFC, 0, 0}, 1, 2'000}),
          "enqueue received MIDI");
    std::array<float, 16> routed_left{}, routed_right{};
    midi_audio.process(routed_left, routed_right);
    sent.clear();
    timeline_output.dispatch([&](const MidiPacket &packet) {
        sent.push_back(packet);
        return true;
    });
    check(std::any_of(sent.begin(), sent.end(),
                      [](const auto &packet) {
                          return packet.port == 2 && packet.bytes[0] == 0x93 &&
                                 packet.bytes[1] == 67;
                      }) &&
              !midi_audio.playing(),
          "audio-owned MIDI thru and external Stop");

    auto externally_clocked_note_frame = [&](std::uint32_t partition) {
        AudioEngine synced(48000);
        MidiOutputScheduler synced_output;
        synced.set_midi_output_scheduler(&synced_output);
        Timeline synced_timeline;
        synced_timeline.transport.microseconds_per_quarter = 500000;
        Track synced_track;
        synced_track.id = 71;
        synced_track.instrument_id = std::string(external_midi_instrument_id);
        synced_track.midi_output_port = 2;
        synced_track.midi_channel = 1;
        synced_track.pattern.length = ticks_per_quarter * 4;
        synced_track.pattern.events.push_back(
            {1, ticks_per_quarter, MusicalEventType::note, 64, 1.0F, 120});
        synced_timeline.tracks.push_back(std::move(synced_track));
        std::string sync_error;
        check(synced.queue_timeline(synced_timeline, sync_error),
              "queue externally clocked timeline");
        MidiRoutingConfiguration sync_configuration;
        sync_configuration.accept_external_clock = true;
        check(synced.publish_midi_routing(sync_configuration),
              "select external timeline clock");
        check(synced.receive_midi({1, {0xFA, 0, 0}, 1, 1}),
              "enqueue external Start");
        std::vector<float> l(partition), r(partition);
        std::uint64_t pulse_timestamp{1};
        std::uint64_t note_frame = std::numeric_limits<std::uint64_t>::max();
        for (int pulse = 0; pulse < 32; ++pulse) {
            pulse_timestamp += 20'833'333ULL;
            check(synced.receive_midi({1, {0xF8, 0, 0}, 1, pulse_timestamp}),
                  "enqueue external clock pulse");
            for (std::uint32_t advanced = 0; advanced < 1000;
                 advanced += partition) {
                synced.process(l, r);
                synced_output.dispatch([&](const MidiPacket &packet) {
                    if ((packet.bytes[0] & 0xF0U) == 0x90 &&
                        packet.bytes[1] == 64)
                        note_frame = std::min(note_frame, packet.frame);
                    return true;
                });
            }
        }
        return note_frame;
    };
    const auto clocked_1000 = externally_clocked_note_frame(1000);
    const auto clocked_250 = externally_clocked_note_frame(250);
    check(clocked_1000 != std::numeric_limits<std::uint64_t>::max() &&
              clocked_250 != std::numeric_limits<std::uint64_t>::max() &&
              std::llabs(static_cast<long long>(clocked_1000) -
                         static_cast<long long>(clocked_250)) <= 1000 &&
              clocked_1000 >= 22'000 && clocked_1000 <= 26'000,
          "timeline phase follows external clock across callback partitions");

    AudioEngine positioned(48000);
    MidiOutputScheduler positioned_output;
    positioned.set_midi_output_scheduler(&positioned_output);
    check(positioned.queue_timeline(external, error),
          "queue SPP timeline fixture");
    MidiRoutingConfiguration positioned_configuration;
    positioned_configuration.accept_external_clock = true;
    check(positioned.publish_midi_routing(positioned_configuration),
          "select external clock for SPP");
    check(positioned.receive_midi({1, {0xF2, 4, 0}, 3, 1}) &&
              positioned.receive_midi({1, {0xFB, 0, 0}, 1, 2}),
          "enqueue SPP and Continue");
    std::array<float, 64> positioned_left{}, positioned_right{};
    positioned.process(positioned_left, positioned_right);
    check(positioned.frame_position() >= 24000,
          "SPP seeks authoritative transport before Continue");

    AudioEngine forwarding(48000);
    MidiOutputScheduler forwarding_output;
    forwarding.set_midi_output_scheduler(&forwarding_output);
    MidiRoutingConfiguration forwarding_routing;
    forwarding_routing.accept_external_clock = true;
    check(forwarding.publish_midi_routing(forwarding_routing),
          "publish external forwarding mode");
    MidiClockOutputConfiguration forwarding_clock;
    forwarding_clock.forward_external = true;
    forwarding_clock.port_count = 2;
    forwarding_clock.ports[0] = 1;
    forwarding_clock.ports[1] = 2;
    check(forwarding.publish_midi_clock_output(forwarding_clock) &&
              forwarding.receive_midi({1, {0xF8, 0, 0}, 1, 20'833'333ULL}),
          "enqueue forwarded external clock");
    forwarding.process(positioned_left, positioned_right);
    sent.clear();
    forwarding_output.dispatch([&](const MidiPacket &packet) {
        sent.push_back(packet);
        return true;
    });
    check(std::count_if(sent.begin(), sent.end(),
                        [](const auto &packet) {
                            return packet.port == 2 && packet.bytes[0] == 0xF8;
                        }) == 1 &&
              std::none_of(sent.begin(), sent.end(),
                           [](const auto &packet) {
                               return packet.port == 1 &&
                                      packet.bytes[0] == 0xF8;
                           }),
          "external clock forwards explicitly without source-port feedback");

    const auto midi_path =
        std::filesystem::temp_directory_path() / "neurogenic-midi-timeline.ngt";
    save_timeline(external, midi_path);
    const auto restored = load_timeline(midi_path);
    std::filesystem::remove(midi_path);
    check(restored.tracks[0].midi_output_port == 2 &&
              restored.tracks[0].midi_channel == 9 &&
              restored.tracks[0].pattern.events.size() == 4,
          "external MIDI timeline persistence");
}

void test_performance_monitoring() {
    using namespace neurogenic;
    CallbackMonitor monitor;
    monitor.record(100, 256, 48000, 8);
    monitor.record(300, 256, 48000, 16);
    auto state = monitor.snapshot();
    check(state.available && state.current_nanoseconds == 300 &&
              state.average_nanoseconds == 103 &&
              state.peak_nanoseconds == 300 &&
              state.deadline_nanoseconds == 5'333'333 &&
              state.callback_count == 2 && monitor.active_voices() == 16,
          "callback timing statistics");
    monitor.record(6'000'000, 256, 48000, 1);
    check(monitor.snapshot().deadline_overruns == 1,
          "callback deadline overrun calculation");
    monitor.set_enabled(false);
    monitor.record(9'000'000, 256, 48000, 2);
    check(!monitor.snapshot().available &&
              monitor.snapshot().callback_count == 3,
          "disabled callback monitor state");
    check(std::fabs(cpu_percent(250'000'000, 1'000'000'000) - 25.0) < 0.001 &&
              cpu_percent(1, 0) == 0,
          "process CPU calculation");
    std::atomic<std::uint64_t> saturated{
        std::numeric_limits<std::uint64_t>::max()};
    saturating_increment(saturated);
    check(saturated.load() == std::numeric_limits<std::uint64_t>::max(),
          "performance counter saturation");
    CallbackMonitor concurrent;
    std::atomic<bool> done{};
    auto writer = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10000; ++i)
            concurrent.record(static_cast<std::uint64_t>(i + 1), 64, 48000, 4);
        done.store(true);
    });
    while (!done.load())
        (void)concurrent.snapshot();
    writer.get();
    check(concurrent.snapshot().callback_count == 10000,
          "concurrent performance snapshots");
    const auto system = sample_system_now();
#ifdef __linux__
    check(system.resident_memory_available &&
              system.resident_memory_bytes > 0 &&
              system.total_memory_available && system.total_memory_bytes > 0,
          "Linux memory reporting");
#else
    check(!system.resident_memory_available && !system.total_memory_available,
          "unsupported monitoring availability");
#endif
    PerformanceSnapshot json_state;
    json_state.audio.available = true;
    json_state.engine.sample_rate = 48000;
    const auto json = performance_json(json_state);
    check(json.find("\"schema\":\"neurogenic.performance.v1\"") !=
                  std::string::npos &&
              json.find("\"sample_rate\":48000") != std::string::npos,
          "machine-readable performance output");
}

void test_polyphonic_sample_modulation() {
    using namespace neurogenic;
    ModulationConfiguration configuration;
    configuration.update_interval = 1;
    configuration.modulator_count = 4;
    configuration.modulators[0].id = 2001;
    configuration.modulators[0].type = ModulatorType::adsr;
    configuration.modulators[0].owner_kind =
        ModulatorOwnerKind::voice_definition;
    configuration.modulators[0].owner_id = 7;
    configuration.modulators[0].settings.attack_seconds = 0.1F;
    configuration.modulators[0].settings.release_seconds = 0.1F;
    configuration.modulators[1] = configuration.modulators[0];
    configuration.modulators[1].id = 2002;
    configuration.modulators[1].type = ModulatorType::lfo;
    configuration.modulators[1].settings.rate_hz = 1;
    configuration.modulators[1].settings.retrigger = true;
    configuration.modulators[2] = configuration.modulators[0];
    configuration.modulators[2].id = 2003;
    configuration.modulators[2].type = ModulatorType::clocked_random;
    configuration.modulators[2].settings.seed = 99;
    configuration.modulators[2].settings.rate_hz = 10;
    configuration.modulators[3] = configuration.modulators[0];
    configuration.modulators[3].id = 2004;
    configuration.modulators[3].type = ModulatorType::velocity;
    configuration.route_count = 3;
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        auto &route = configuration.routes[i];
        route.id = static_cast<ModulationRouteId>(2101 + i);
        route.source_instance = configuration.modulators[i].id;
        route.source_output = 1;
        route.destination = {ParameterTarget::sample_voice,
                             static_cast<std::uint32_t>(
                                 i == 0   ? SampleModulationParameter::gain
                                 : i == 1 ? SampleModulationParameter::pitch
                                          : SampleModulationParameter::pan),
                             7};
        route.minimum = i == 0 ? 0.0F : (i == 1 ? -12.0F : -1.0F);
        route.maximum = i == 0 ? 1.0F : (i == 1 ? 12.0F : 1.0F);
        route.base_value = 0;
        route.depth = i == 1 ? 0.25F : 1.0F;
        route.scope = ModulationScope::voice;
        route.track_id = 7;
    }
    ModulationGraphDiagnostics diagnostics;
    check(prepare_modulation_graph(configuration, diagnostics),
          "voice-local graph preparation");
    SampleVoiceModulationState first, second;
    reset_sample_voice_modulation(first, configuration, 7, 0.25F, 1);
    reset_sample_voice_modulation(second, configuration, 7, 0.9F, 2);
    SampleVoiceModulationValues first_value, second_value;
    for (int frame = 0; frame < 5; ++frame) {
        first_value =
            process_sample_voice_modulation(first, configuration, 100, 120);
        second_value =
            process_sample_voice_modulation(second, configuration, 100, 120);
    }
    check(
        first_value.gain > 0 && second_value.gain > 0 &&
            first.instances[1].phase == second.instances[1].phase &&
            first.instances[2].held != second.instances[2].held,
        "overlapping sample voices own independent envelope and random state");
    for (int frame = 0; frame < 5; ++frame)
        (void)process_sample_voice_modulation(first, configuration, 100, 120);
    check(first.instances[1].phase != second.instances[1].phase,
          "sample voice LFO phases evolve independently");
    release_sample_voice_modulation(first);
    const auto second_before = second.instances[0].value;
    for (int frame = 0; frame < 5; ++frame) {
        first_value =
            process_sample_voice_modulation(first, configuration, 100, 120);
        second_value =
            process_sample_voice_modulation(second, configuration, 100, 120);
    }
    check(first.instances[0].value < second.instances[0].value &&
              second.instances[0].value >= second_before,
          "note-off releases only the corresponding voice envelope");

    auto cross_voice = configuration;
    cross_voice.modulators[1].owner_id = 8;
    cross_voice.routes[1].destination_kind =
        ModulationDestinationKind::modulator_input;
    cross_voice.routes[1].destination_modulator = 2001;
    cross_voice.routes[1].destination_input = 1;
    check(!validate_modulation_graph(cross_voice, diagnostics) &&
              !diagnostics.message.empty(),
          "ambiguous cross-voice route rejected");

    auto unsupported = configuration;
    unsupported.modulators[0].type = ModulatorType::envelope_follower;
    ModulationEngine validation_engine(100);
    check(!validation_engine.configure(unsupported),
          "unsupported voice-local modulator rejected");

    ModulationConfiguration partition = configuration;
    SampleVoiceModulationState partition_a, partition_b;
    reset_sample_voice_modulation(partition_a, partition, 7, 0.7F, 42);
    reset_sample_voice_modulation(partition_b, partition, 7, 0.7F, 42);
    for (int frame = 0; frame < 97; ++frame)
        (void)process_sample_voice_modulation(partition_a, partition, 48000,
                                              123.5F);
    for (const int block : {3, 17, 29, 48})
        for (int frame = 0; frame < block; ++frame)
            (void)process_sample_voice_modulation(partition_b, partition, 48000,
                                                  123.5F);
    near(partition_a.instances[1].outputs[0],
         partition_b.instances[1].outputs[0],
         "sample voice callback partition independence");

    auto render_velocity_voice = [&](bool modulated) {
        AudioEngine audio(100, 4);
        audio.set_sample({100, 1, std::vector<float>(64, 1.0F)});
        if (modulated)
            check(audio.publish_modulation(configuration),
                  "publish sample voice modulation");
        Timeline timeline;
        timeline.transport.microseconds_per_quarter = 500000;
        Track track;
        track.id = 7;
        track.instrument_id = std::string(sample_instrument_id);
        track.pattern.length = ticks_per_quarter;
        track.pattern.events.push_back(
            {1, 0, MusicalEventType::note, 60, 0.5F, ticks_per_quarter});
        timeline.tracks.push_back(std::move(track));
        std::string error;
        check(audio.queue_timeline(timeline, error), error.c_str());
        std::array<float, 16> left{}, right{};
        audio.process(left, right);
        float total{};
        for (const auto sample : left)
            total += sample;
        return total;
    };
    const auto plain_audio = render_velocity_voice(false);
    const auto modulated_audio = render_velocity_voice(true);
    check(modulated_audio > 0 && modulated_audio < plain_audio,
          "voice-local velocity modulation changes rendered sample audio");
}

void test_native_synthesis_and_dsp() {
    using namespace neurogenic;
    for (int waveform = 0; waveform < 4; ++waveform) {
        SubtractiveSynth synth(48000, 4);
        check(synth.set_parameter(
                  static_cast<std::uint32_t>(SynthParameter::waveform),
                  static_cast<float>(waveform)),
              "synth waveform parameter");
        synth.note_on(69, 1);
        std::vector<float> left(2048), right(2048);
        synth.process(left, right);
        check(std::all_of(left.begin(), left.end(),
                          [](float value) { return std::isfinite(value); }) &&
                  std::any_of(
                      left.begin(), left.end(),
                      [](float value) { return std::abs(value) > 1e-5F; }),
              "synth waveform stability");
    }
    SubtractiveSynth frequency(48000, 4);
    frequency.set_parameter(
        static_cast<std::uint32_t>(SynthParameter::waveform), 0);
    frequency.set_parameter(static_cast<std::uint32_t>(SynthParameter::attack),
                            0.001F);
    frequency.note_on(69, 1);
    std::vector<float> wave(48000), other(48000);
    frequency.process(wave, other);
    std::size_t crossings{};
    for (std::size_t i = 1; i < wave.size(); ++i)
        if (wave[i - 1] <= 0 && wave[i] > 0)
            ++crossings;
    check(crossings >= 438 && crossings <= 442, "synth oscillator frequency");
    frequency.note_off(69);
    frequency.process(wave, other);
    check(frequency.state().active_voices == 0, "synth ADSR release");
    SubtractiveSynth poly(48000, 2);
    poly.note_on(60, 0.2F, {1, 1, 0});
    poly.note_on(64, 1, {1, 2, 0});
    poly.note_on(67, 1, {1, 3, 0});
    check(poly.state().active_voices == 2 && poly.state().stolen_voices == 1,
          "synth deterministic voice stealing");
    poly.note_off(64, {1, 2, 0});
    check(poly.state().active_voices == 2,
          "stolen synth owner released unrelated voice");
    poly.set_parameter(static_cast<std::uint32_t>(SynthParameter::cutoff),
                       20000);
    poly.set_parameter(static_cast<std::uint32_t>(SynthParameter::resonance),
                       0.95F);
    std::array<float, 512> stable_left{}, stable_right{};
    poly.process(stable_left, stable_right);
    check(std::all_of(stable_left.begin(), stable_left.end(),
                      [](float value) { return std::isfinite(value); }),
          "synth filter stability");
    poly.set_sample_rate(44100);
    check(poly.state().active_voices == 0, "synth sample-rate reset");
    StereoDelay delay(48000, 0.1F);
    std::array<float, 6000> dl{}, dr{};
    dl[0] = dr[0] = 1;
    delay.set_bypass(true);
    delay.process(dl, dr);
    check(dl[0] == 1 && std::all_of(dl.begin() + 1, dl.end(),
                                    [](float value) { return value == 0; }),
          "effect bypass");
    delay.set_bypass(false);
    delay.set_parameter(static_cast<std::uint32_t>(DelayParameter::mix), 1);
    delay.reset();
    dl.fill(0);
    dr.fill(0);
    dl[0] = dr[0] = 1;
    delay.process(dl, dr);
    check(std::any_of(dl.begin() + 40, dl.end(),
                      [](float value) { return std::abs(value) > 1e-5F; }),
          "stereo delay processing");
    AudioEngine routed(48000, 8);
    Timeline synth_timeline;
    Track synth_track;
    synth_track.id = 1;
    synth_track.instrument_id = std::string(subtractive_instrument_id);
    synth_track.pattern.length = 960;
    synth_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 60, 1, 480});
    synth_timeline.tracks.push_back(synth_track);
    std::string error;
    check(routed.queue_timeline(synth_timeline, error),
          "synth timeline routing");
    std::array<float, 256> left{}, right{};
    routed.process(left, right);
    check(std::any_of(left.begin(), left.end(),
                      [](float value) { return std::abs(value) > 1e-5F; }) &&
              routed.synth_state().active_voices == 1,
          "timeline did not trigger synth");
    AudioEngine mixed(48000, 8);
    mixed.set_sample({48000, 1, std::vector<float>(512, 0.25F)});
    Timeline mixed_timeline = synth_timeline;
    Track sample_track;
    sample_track.id = 2;
    sample_track.pattern.length = 960;
    sample_track.pattern.events.push_back(
        {2, 0, MusicalEventType::note, 36, 1, 240});
    mixed_timeline.tracks.push_back(sample_track);
    check(mixed.queue_timeline(mixed_timeline, error),
          "mixed instrument timeline");
    mixed.process(left, right);
    check(mixed.active_voice_count() > 0 &&
              mixed.synth_state().active_voices > 0,
          "mixed sample and synth playback");
    synth_timeline.tracks[0].muted = true;
    AudioEngine muted;
    check(muted.queue_timeline(synth_timeline, error), "muted synth timeline");
    muted.process(left, right);
    check(std::all_of(left.begin(), left.end(),
                      [](float value) { return value == 0; }),
          "synth track mute");
    TimelineService service(
        [&](const Timeline &value, std::string &publish_error) {
            return routed.queue_timeline(value, publish_error);
        },
        synth_timeline);
    SampleManager samples(routed);
    ApplicationService app(routed, service, samples);
    ApplicationCommand instrument;
    instrument.action_id = "track.instrument-set.v1";
    instrument.arguments = {
        {"instrument_id", std::string(sample_instrument_id)}};
    check(app.execute(instrument).accepted &&
              service.snapshot().timeline.tracks[0].instrument_id ==
                  sample_instrument_id,
          "application instrument routing action");
    ApplicationCommand cutoff;
    cutoff.action_id = "parameter.set-normalized.v1";
    cutoff.arguments = {{"parameter_id", std::string("synth.cutoff")},
                        {"value", 0.5}};
    check(app.execute(cutoff).accepted, "application synth parameter command");
    routed.process(left, right);
    check(routed.dropped_dsp_commands() == 0, "DSP parameter publication");
    AudioEngine saturated_engine;
    TimelineService saturated_timeline(
        [](const Timeline &, std::string &) { return true; }, synth_timeline);
    SampleManager saturated_samples(saturated_engine);
    ApplicationService saturated_app(saturated_engine, saturated_timeline,
                                     saturated_samples);
    for (std::size_t i = 0; i < 128; ++i)
        check(saturated_engine.set_synth_parameter(SynthParameter::cutoff,
                                                   1000.0F),
              "fill DSP parameter queue");
    const auto old_cutoff =
        saturated_app.parameters().get("synth.cutoff")->current_value;
    const auto rejected = saturated_app.execute(cutoff);
    check(!rejected.accepted &&
              saturated_app.parameters().get("synth.cutoff")->current_value ==
                  old_cutoff &&
              saturated_engine.dropped_dsp_commands() == 1,
          "DSP queue overflow rolls back application parameter state");
    saturated_samples.shutdown();
    MidiMapper midi;
    bool on{}, off{};
    midi.set_note_event_handler([&](std::uint8_t, float, bool active) {
        active ? on = true : off = true;
    });
    check(midi.receive({0x90, 60, 100, 0}) && midi.receive({0x80, 60, 0, 1}),
          "MIDI synth note input");
    midi.dispatch_pending();
    check(on && off, "MIDI synth note-on/off dispatch");
    samples.shutdown();
}

neurogenic::Sample constant_sample(std::size_t frames, float value = 1.0F) {
    return {48000, 1, std::vector<float>(frames, value)};
}

void write_pcm16_wav(const std::filesystem::path &path, std::uint32_t rate,
                     std::uint16_t channels, const std::vector<float> &values) {
    std::ofstream output(path, std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(values.size() * 2);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char *>(&value), 4);
    };
    output.write("RIFF", 4);
    write32(36 + bytes);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(1);
    write16(channels);
    write32(rate);
    write32(rate * channels * 2);
    write16(channels * 2);
    write16(16);
    output.write("data", 4);
    write32(bytes);
    for (const auto value : values) {
        const auto pcm =
            static_cast<std::int16_t>(std::clamp(value, -1.0F, 1.0F) * 32767);
        output.write(reinterpret_cast<const char *>(&pcm), 2);
    }
}

void write_wav_payload(const std::filesystem::path &path, std::uint16_t format,
                       std::uint16_t bits, const std::vector<char> &payload) {
    std::ofstream output(path, std::ios::binary);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char *>(&value), 4);
    };
    const auto bytes = static_cast<std::uint32_t>(payload.size());
    output.write("RIFF", 4);
    write32(36 + bytes);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(format);
    write16(1);
    write32(48000);
    write32(48000 * bits / 8);
    write16(bits / 8);
    write16(bits);
    output.write("data", 4);
    write32(bytes);
    output.write(payload.data(), payload.size());
}

void test_native_effect_chains() {
    using namespace neurogenic;
    EffectRackConfiguration configuration;
    configuration.instance_count = 3;
    configuration.instances[0] = {
        101,  EffectProcessorType::gain, {{{1, 2.0F}}}, 1, 1.0F, 2, 2, true,
        false};
    configuration.instances[1] = {
        102,  EffectProcessorType::gain, {{{1, 0.5F}}}, 1, 1.0F, 2, 2, true,
        false};
    configuration.instances[2] = {103,
                                  EffectProcessorType::saturation,
                                  {{{1, 4.0F}, {2, 1.0F}}},
                                  2,
                                  1.0F,
                                  2,
                                  2,
                                  true,
                                  false};
    configuration.master.instances = {101, 102};
    configuration.master.count = 2;
    std::string error;
    check(validate_effect_rack(configuration, error),
          "valid bounded effect rack");
    NativeEffectRack rack(48000);
    rack.activate(configuration);
    std::array<float, 4> left{0.25F, -0.25F, 0.5F, -0.5F}, right = left;
    rack.process(configuration.master, left, right);
    near(left[0], 0.25F, "serial independent gain instances");
    configuration.instances[1].bypass = true;
    rack.activate(configuration);
    left.fill(0.25F);
    right = left;
    rack.process(configuration.master, left, right);
    near(left[0], 0.5F, "individual effect bypass");
    configuration.master.bypass = true;
    rack.activate(configuration);
    left.fill(0.25F);
    right = left;
    rack.process(configuration.master, left, right);
    near(left[0], 0.25F, "whole chain bypass");

    configuration.master.bypass = false;
    configuration.master.instances = {101, 103};
    configuration.instances[1].bypass = false;
    rack.activate(configuration);
    left.fill(0.2F);
    right = left;
    rack.process(configuration.master, left, right);
    const auto gain_then_saturate = left[0];
    configuration.master.instances = {103, 101};
    rack.activate(configuration);
    left.fill(0.2F);
    right = left;
    rack.process(configuration.master, left, right);
    check(std::fabs(left[0] - gain_then_saturate) > 0.01F,
          "effect chain serial order changes audio");

    Mixer mixer(48000);
    MixerConfiguration mixer_configuration;
    mixer_configuration.revision = 1;
    mixer_configuration.track_count = 1;
    mixer_configuration.tracks[0].track = 1;
    mixer_configuration.effects = configuration;
    mixer_configuration.effects.tracks[0] = {{101}, 1, false};
    mixer_configuration.effects.master = {{102}, 1, false};
    check(mixer.publish(mixer_configuration, error), "publish mixer FX chains");
    mixer.begin_block(4);
    for (std::size_t frame = 0; frame < 4; ++frame)
        check(mixer.add_track(1, frame, 0.25F, 0.25F),
              "route effect test track");
    left.fill(0);
    right.fill(0);
    mixer.process(left, right);
    near(left[0], 0.5F, "track chain runs before mixer fader");
    mixer.apply_master_gain(left, right);
    mixer.process_master_effects(left, right);
    near(left[0], 0.25F, "master chain runs after master gain");

    configuration.instances[0].type = static_cast<EffectProcessorType>(99);
    check(!validate_effect_rack(configuration, error),
          "unknown native processor type rejected");
}

void test_queue_and_scheduler() {
    using namespace neurogenic;
    SpscQueue<int, 4> queue;
    check(queue.push(1) && queue.push(2) && queue.push(3), "queue fill");
    check(!queue.push(4), "queue overflow");
    int item{};
    check(queue.pop(item) && item == 1, "queue order");

    Scheduler scheduler(1600, 60);
    Pattern first;
    first.steps[0] = {true, 42, 1};
    first.steps[4] = {true, 42, 1};
    first.revision = 1;
    scheduler.request_pattern(first);
    std::array<ScheduledEvent, 64> events{};
    auto count = scheduler.schedule(0, 1601, events);
    check(count == 2 && events[0].frame == 0 && events[1].frame == 1600,
          "sample offsets");
    Pattern second;
    second.steps[4] = {true, 43, 1};
    second.revision = 2;
    scheduler.request_pattern(second);
    scheduler.schedule(1601, 4799, events);
    check(scheduler.active_pattern().revision == 1, "early pattern swap");
    scheduler.schedule(6400, 2, events);
    check(scheduler.active_pattern().revision == 2, "bar pattern swap");

    Scheduler drift(44100, 123.0);
    Pattern every;
    for (auto &step : every.steps)
        step.enabled = true;
    drift.request_pattern(every);
    std::uint64_t absolute{}, seen{};
    for (std::size_t call = 0; seen < 5000; ++call) {
        const auto frames = static_cast<std::uint32_t>((call * 7919) % 997 + 1);
        auto n = drift.schedule(absolute, frames, events);
        for (std::size_t i = 0; i < n; ++i) {
            const auto expected = static_cast<std::uint64_t>(
                std::llround(static_cast<long double>(seen) * 44100.0L * 60.0L /
                             123.0L / 4.0L));
            check(absolute + events[i].frame == expected, "scheduler drift");
            ++seen;
        }
        absolute += frames;
    }
}

void test_polyphony_and_events() {
    using namespace neurogenic;
    AudioEngine overlap(48000, 4);
    overlap.set_sample(constant_sample(8));
    overlap.trigger(60, 1, 0);
    overlap.trigger(61, 1, 1);
    std::array<float, 8> left{}, right{};
    overlap.process(left, right);
    near(left[0], 0.8F, "first voice start");
    near(left[1], 1.6F, "overlapping retrigger");

    AudioEngine simultaneous(48000, 8);
    simultaneous.set_sample(constant_sample(4));
    simultaneous.trigger(60, 0.25F);
    simultaneous.trigger(61, 0.5F);
    simultaneous.trigger(62, 1.0F);
    simultaneous.process(left, right);
    near(left[0], 1.4F, "simultaneous triggers were not all consumed");

    AudioEngine ordered(48000, 2);
    ordered.set_sample(constant_sample(4));
    ordered.trigger(60, 0.25F);
    ordered.trigger(61, 0.5F);
    ordered.trigger(62, 1.0F);
    ordered.process(left, right);
    near(left[0], 1.2F, "equal-offset enqueue ordering");

    AudioEngine offsets(48000, 4);
    offsets.set_sample(constant_sample(8));
    offsets.trigger(60, 1, 5);
    offsets.process({left.data(), 2}, {right.data(), 2});
    near(left[0], 0, "early frame offset");
    offsets.process({left.data(), 4}, {right.data(), 4});
    near(left[2], 0, "frame before carried event");
    near(left[3], 0.8F, "carried frame offset");

    AudioEngine stealing(48000, 2);
    stealing.set_sample(constant_sample(8));
    stealing.trigger(60, 0.25F, 0);
    stealing.trigger(61, 0.5F, 1);
    stealing.trigger(62, 1.0F, 2);
    stealing.process(left, right);
    near(left[2], 1.2F, "oldest voice stealing policy");
    check(stealing.stolen_voices() == 1, "voice stealing counter");

    AudioEngine overflow;
    overflow.set_sample(constant_sample(2));
    for (int i = 0; i < 100; ++i)
        overflow.trigger(60, 1);
    check(overflow.dropped_triggers() == 36, "trigger overflow count");
    overflow.process(left, right);
    check(overflow.stolen_voices() == 32, "burst voice allocation");
    near(left[0], 25.6F, "burst trigger delivery");

    AudioEngine variable(48000, 4);
    variable.set_sample(constant_sample(32));
    variable.trigger(60, 1, 12);
    std::size_t elapsed{};
    for (auto size : {1U, 3U, 5U, 7U}) {
        variable.process({left.data(), size}, {right.data(), size});
        for (std::size_t i = 0; i < size; ++i)
            if (elapsed + i == 12)
                near(left[i], 0.8F, "variable callback event offset");
        elapsed += size;
    }

    Pattern invalid;
    invalid.steps[0].velocity = 2;
    check(!overflow.queue_pattern(invalid), "invalid pattern accepted");
    overflow.set_sample(constant_sample(1));
    overflow.process({left.data(), 1}, {right.data(), 1});
}

void test_live_sample_management_and_resampling() {
    using namespace neurogenic;
    std::array<float, 16> left{}, right{};
    AudioEngine live(48000, 8);
    live.set_sample_slot(SampleSlotId{0}, constant_sample(6, 0.25F));
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.2F, "initial live sample");
    check(live.publish_sample_slot(SampleSlotId{0}, constant_sample(4, 0.5F)),
          "live replacement publication");
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F, "old voice did not survive replacement");
    check(live.unload_sample_slot(SampleSlotId{0}), "live unload publication");
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F, "active voices did not survive unload");
    check(live.missing_slot_triggers() == 1,
          "unloaded slot accepted a new trigger");
    live.process({left.data(), 8}, {right.data(), 8});
    live.reclaim_sample_versions();
    check(live.reclaimed_sample_versions() >= 2,
          "retired live samples were not reclaimed");

    AudioEngine independent(48000, 8);
    independent.set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    independent.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    independent.process({left.data(), 1}, {right.data(), 1});
    check(independent.publish_sample_slot(SampleSlotId{0},
                                          constant_sample(8, 1.0F)),
          "independent slot replacement");
    independent.trigger(36, 1);
    independent.trigger(37, 1);
    independent.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 1.2F, "independent slots changed together");

    WindowedSincConverter converter;
    Sample mono{44100, 1, std::vector<float>(441, 0.25F)};
    auto up = prepare_sample(mono, 48000, converter);
    check(up.sample_rate == 48000 && up.channels == 1 && up.frames() == 480,
          "44.1 kHz conversion duration");
    auto unchanged = prepare_sample(mono, 44100, converter);
    check(unchanged.interleaved == mono.interleaved,
          "matching-rate sample was unnecessarily changed");
    Sample stereo{96000, 2, std::vector<float>(1920)};
    for (std::size_t i = 0; i < 960; ++i) {
        stereo.interleaved[i * 2] = 0.25F;
        stereo.interleaved[i * 2 + 1] = -0.5F;
    }
    auto down = prepare_sample(stereo, 48000, converter);
    check(down.channels == 2 && down.frames() == 480,
          "96 kHz stereo conversion duration");
    near(down.interleaved[200], 0.25F, "resampler left channel");
    near(down.interleaved[201], -0.5F, "resampler right channel");
    Sample tone{44100, 1, std::vector<float>(4410)};
    constexpr double pi = 3.141592653589793;
    for (std::size_t i = 0; i < tone.frames(); ++i)
        tone.interleaved[i] =
            static_cast<float>(std::sin(2 * pi * 1000 * i / 44100.0));
    auto converted = prepare_sample(tone, 48000, converter);
    std::size_t crossings{};
    for (std::size_t i = 1; i < converted.frames(); ++i)
        if (converted.interleaved[i - 1] <= 0 && converted.interleaved[i] > 0)
            ++crossings;
    check(crossings >= 99 && crossings <= 101,
          "resampling did not preserve pitch");
    bool bad_rate = false;
    try {
        (void)prepare_sample({0, 1, {1}}, 48000, converter);
    } catch (const std::invalid_argument &) {
        bad_rate = true;
    }
    check(bad_rate, "invalid source rate accepted");

    const auto base = std::filesystem::temp_directory_path();
    const auto wav44 = base / "neurogenic-live-44100.wav";
    const auto wav96 = base / "neurogenic-live-96000.wav";
    const auto invalid = base / "neurogenic-live-invalid.wav";
    write_pcm16_wav(wav44, 44100, 1, std::vector<float>(441, 0.25F));
    write_pcm16_wav(wav96, 96000, 2, std::vector<float>(1920, 0.5F));
    {
        std::ofstream broken(invalid);
        broken << "invalid";
    }
    const auto pcm24 = base / "neurogenic-pcm24.wav";
    const auto pcm32 = base / "neurogenic-pcm32.wav";
    const auto float32 = base / "neurogenic-float32.wav";
    write_wav_payload(pcm24, 1, 24, {0, 0, 0x40});
    std::int32_t pcm32_value = 1073741824;
    std::vector<char> pcm32_bytes(4);
    std::memcpy(pcm32_bytes.data(), &pcm32_value, 4);
    write_wav_payload(pcm32, 1, 32, pcm32_bytes);
    float float_value = 0.25F;
    std::vector<char> float_bytes(4);
    std::memcpy(float_bytes.data(), &float_value, 4);
    write_wav_payload(float32, 3, 32, float_bytes);
    near(decode_wav(pcm24).interleaved[0], 0.5F, "PCM24 decoding");
    near(decode_wav(pcm32).interleaved[0], 0.5F, "PCM32 decoding");
    near(decode_wav(float32).interleaved[0], 0.25F, "float32 decoding");
    AudioEngine managed(48000, 8);
    SampleManager manager(managed, 16);
    check(manager.load(SampleSlotId{2}, wav44), "sample load command");
    manager.wait_idle();
    auto state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              !state.has_sample && state.pending_source_path == wav44,
          "prepared sample reported active before callback");
    check(managed.map_note_to_slot(52, SampleSlotId{2}),
          "managed slot mapping");
    managed.process({left.data(), 0}, {right.data(), 0});
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample &&
              state.source_sample_rate == 44100 &&
              state.engine_sample_rate == 48000 && state.frames == 480,
          "loaded slot state");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.2F, "managed resampled playback");
    check(manager.replace(SampleSlotId{2}, wav96), "replace command");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample && state.source_path == wav44 &&
              state.pending_source_path == wav96,
          "pending replacement obscured active metadata");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    check(manager.status(SampleSlotId{2}).channels == 2,
          "stereo replacement state");
    check(manager.load(SampleSlotId{2}, invalid),
          "failed load command submission");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::error && state.has_sample &&
              state.source_path == wav96,
          "failed load did not preserve previous sample state");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    check(left[0] != 0, "failed load replaced active sample");
    check(manager.unload(SampleSlotId{2}), "unload command");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample,
          "unload reported empty before callback");
    managed.process({left.data(), 0}, {right.data(), 0});
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::empty && !state.has_sample,
          "unload state");

    AudioEngine consistent(48000, 8);
    SampleManager consistent_manager(consistent, 16);
    check(consistent_manager.load(SampleSlotId{4}, wav44), "first rapid load");
    check(consistent_manager.load(SampleSlotId{4}, wav96), "second rapid load");
    state = consistent_manager.status(SampleSlotId{4});
    check(state.revision == 2 && state.pending_source_path == wav96 &&
              !state.has_sample,
          "older command overwrote newest queued state");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.revision == 2 &&
              state.state == SampleSlotLoadState::pending_activation &&
              state.pending_source_path == wav96 && !state.has_sample,
          "older completion overwrote newest prepared state");
    consistent.process({left.data(), 0}, {right.data(), 0});
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::loaded &&
              state.source_path == wav96 && state.source_sample_rate == 96000,
          "rapid replacement activated stale metadata");
    check(consistent_manager.unload(SampleSlotId{4}), "mixed unload");
    check(consistent_manager.load(SampleSlotId{4}, wav44), "mixed reload");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample && state.source_path == wav96 &&
              state.pending_source_path == wav44,
          "mixed operations corrupted pending or active state");
    consistent.process({left.data(), 0}, {right.data(), 0});
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample &&
              state.source_path == wav44,
          "mixed unload/load final state");
    check(consistent_manager.replace(SampleSlotId{4}, invalid),
          "consistent failed replacement submission");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::error && state.has_sample &&
              state.source_path == wav44 && state.pending_source_path.empty(),
          "failed replacement changed active metadata");

    AudioEngine rapid(48000, 4, 512, 8);
    for (int i = 0; i < 100; ++i) {
        check(rapid.publish_sample_slot(
                  SampleSlotId{0},
                  constant_sample(2, static_cast<float>(i + 1) / 100)),
              "rapid replacement publication");
        rapid.process({left.data(), 1}, {right.data(), 1});
        rapid.reclaim_sample_versions();
    }
    check(rapid.reclaimed_sample_versions() >= 99,
          "rapid replacements leaked versions");

    AudioEngine concurrent(48000, 8);
    SampleManager concurrent_manager(concurrent, 32);
    std::atomic<bool> rendering{true};
    std::thread audio([&] {
        std::array<float, 17> l{}, r{};
        while (rendering.load())
            concurrent.process(l, r);
    });
    for (int i = 0; i < 20; ++i)
        check(concurrent_manager.load(
                  SampleSlotId{static_cast<std::uint32_t>(i % 2)},
                  i % 2 ? wav44 : wav96),
              "concurrent sample command");
    concurrent_manager.wait_idle();
    // Worker-idle means decoding/publication preparation is complete; the
    // audio callback still owns the bounded activation acknowledgement. Wait
    // for that authoritative state instead of racing the render thread stop.
    for (int attempt = 0; attempt < 10'000; ++attempt) {
        const auto observed = concurrent_manager.states();
        if (observed.size() == 2 && observed[0].has_sample &&
            observed[1].has_sample)
            break;
        std::this_thread::yield();
    }
    rendering = false;
    audio.join();
    concurrent_manager.shutdown();
    const auto concurrent_states = concurrent_manager.states();
    check(concurrent_states.size() == 2 && concurrent_states[0].has_sample &&
              concurrent_states[1].has_sample,
          "concurrent command state");
    AudioEngine shutdown_engine;
    SampleManager shutdown_manager(shutdown_engine, 4);
    check(shutdown_manager.load(SampleSlotId{0}, wav44),
          "pending shutdown command");
    shutdown_manager.shutdown();
    state = shutdown_manager.status(SampleSlotId{0});
    check(state.state == SampleSlotLoadState::pending_activation &&
              !state.has_sample,
          "shutdown mislabeled unpublished sample");
    shutdown_engine.process({left.data(), 0}, {right.data(), 0});
    state = shutdown_manager.status(SampleSlotId{0});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample,
          "shutdown lost pending sample publication");
    std::filesystem::remove(wav44);
    std::filesystem::remove(wav96);
    std::filesystem::remove(invalid);
    std::filesystem::remove(pcm24);
    std::filesystem::remove(pcm32);
    std::filesystem::remove(float32);
}

void test_sample_slicing() {
    using namespace neurogenic;
    const auto base = std::filesystem::temp_directory_path();
    const auto wav = base / "neurogenic-slices.wav";
    std::vector<float> pcm(480);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = i < 120 ? 0.1F : i < 240 ? 0.5F : 0.25F;
    write_pcm16_wav(wav, 48000, 1, pcm);
    AudioEngine engine(48000, 8);
    SampleManager manager(engine, 16);
    check(manager.load(SampleSlotId{0}, wav), "slice fixture load");
    manager.wait_idle();
    std::array<float, 512> left{}, right{};
    engine.process({left.data(), 0}, {right.data(), 0});
    SliceService slices(engine, manager);
    auto state = slices.snapshot(SampleSlotId{0});
    check(state.sample_frames == 480 && state.waveform.size() == 480,
          "waveform peak preparation");
    SliceId first{}, overlap{};
    std::string error;
    check(slices.create(SampleSlotId{0}, state.revision, 100, 200, "First",
                        first, error),
          "slice creation");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.create(SampleSlotId{0}, state.revision, 150, 300, "Overlap",
                        overlap, error),
          "overlapping slice creation");
    state = slices.snapshot(SampleSlotId{0});
    check(state.slices[0].id == first && state.slices[1].id == overlap,
          "stable slice identifiers");
    check(!slices.set_bounds(first, state.revision, 200, 100, error),
          "invalid slice boundaries accepted");
    check(!slices.rename(first, state.revision - 1, "stale", error),
          "stale slice revision accepted");
    check(slices.assign_pad(0, first, state.revision, error),
          "slice pad assignment");
    engine.process({left.data(), 0}, {right.data(), 0});
    engine.trigger(36, 1.0F);
    engine.process({left.data(), 128}, {right.data(), 128});
    check(std::any_of(left.begin(), left.begin() + 100,
                      [](float value) { return std::abs(value) > 0.1F; }) &&
              std::all_of(left.begin() + 100, left.begin() + 128,
                          [](float value) { return value == 0; }),
          "slice playback boundaries");
    state = slices.snapshot(SampleSlotId{0});
    RegionId region{}, one_frame{};
    check(slices.create_region_from_slice(first, state.revision, region, error),
          "slice-to-region conversion");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_direction(region, state.revision,
                                      RegionDirection::reverse, error),
          "reverse region editing");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.preview_region(region, 1.0F, error),
          "reverse region preview");
    engine.process({left.data(), 1}, {right.data(), 1});
    if (!(left[0] > 0.35F))
        throw std::runtime_error(
            "reverse region did not start at its end boundary: " +
            std::to_string(left[0]));
    slices.stop_preview();
    engine.process({left.data(), 1}, {right.data(), 1});
    state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_direction(region, state.revision,
                                      RegionDirection::forward, error),
          "forward region editing");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_fades(region, state.revision, 2, 2,
                                  RegionFadeCurve::linear, error),
          "region fade editing");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_loop(region, state.revision, true, 120, 140, 4,
                                 error),
          "region loop editing");
    state = slices.snapshot(SampleSlotId{0});
    auto region_pad_settings = SamplePlaybackSettings{};
    region_pad_settings.mode = SamplePlaybackMode::toggle;
    check(slices.set_pad_settings(SampleSlotId{0}, 1, state.revision,
                                  region_pad_settings, error),
          "region toggle settings");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.assign_region_pad(1, region, state.revision, error),
          "region pad assignment");
    engine.process({left.data(), 0}, {right.data(), 0});
    left.fill(0);
    right.fill(0);
    engine.trigger(37, 1.0F);
    engine.process({left.data(), 64}, {right.data(), 64});
    check(left[0] == 0 && left[1] > 0 && left[63] != 0,
          "region fades and bounded looping");
    check(engine.trigger_pad(37, 1.0F), "region toggle stop trigger");
    engine.process({left.data(), 256}, {right.data(), 256});
    state = slices.snapshot(SampleSlotId{0});
    check(slices.preview_region(region, 1.0F, error),
          "region preview before edit");
    engine.process({left.data(), 1}, {right.data(), 1});
    state = slices.snapshot(SampleSlotId{0});
    check(slices.clear_pad(1, state.revision, error),
          "region pad clear");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.erase_region(region, state.revision, error),
          "region deletion after unassignment");
    engine.process({left.data(), 8}, {right.data(), 8});
    check(std::any_of(left.begin(), left.begin() + 8,
                      [](float value) { return value != 0; }),
          "active region voice did not retain its copied configuration");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.create_region(SampleSlotId{0}, state.revision, 7, 8,
                               "One frame", one_frame, error),
          "one-frame region creation");
    state = slices.snapshot(SampleSlotId{0});
    check(!slices.set_region_fades(one_frame, state.revision, 2, 0,
                                   RegionFadeCurve::equal_power, error),
          "oversized region fade accepted");
    state = slices.snapshot(SampleSlotId{0});
    check(slices.equal_divisions(SampleSlotId{0}, state.revision, 16, error),
          "equal slice generation");
    state = slices.snapshot(SampleSlotId{0});
    check(state.slices.size() == 16 && state.slices.front().start_frame == 0 &&
              state.slices.back().end_frame == 480 && !state.pad_assignments[0],
          "equal slice coverage and stale pad clearing");
    check(slices.detect_transients(SampleSlotId{0}, state.revision, 0.25F, 32,
                                   error),
          "transient slicing");
    state = slices.snapshot(SampleSlotId{0});
    check(state.slices.size() >= 2 && state.slices.front().start_frame == 0,
          "deterministic transient boundaries");
    const auto before_replace_revision = state.revision;
    check(slices.preview(state.slices.front().id, 1, error),
          "slice before replacement");
    engine.process({left.data(), 1}, {right.data(), 1});
    check(manager.replace(SampleSlotId{0}, wav), "slice sample replacement");
    manager.wait_idle();
    engine.process({left.data(), 1}, {right.data(), 1});
    check(left[0] != 0, "old slice voice did not survive replacement");
    state = slices.snapshot(SampleSlotId{0});
    check(state.revision > before_replace_revision && state.slices.empty() &&
              state.regions.empty(),
          "sample replacement invalidated stale slices");
    TimelineService timeline(
        [&](const Timeline &value, std::string &timeline_error) {
            return engine.queue_timeline(value, timeline_error);
        });
    ApplicationService application(engine, timeline, manager);
    auto result = application.execute(
        {"slice.equal-divisions.v1", {{"divisions", std::int64_t{8}}}});
    check(result.accepted && application.snapshot().slices.size() == 8,
          "slice application action/state");
    result = application.execute(
        {"slice.select.v1",
         {{"slice_id",
           static_cast<std::int64_t>(application.snapshot().slices[0].id)}}});
    check(result.accepted, "slice application selection");
    result = application.execute(
        {"region.from-slice.v1", {}});
    check(result.accepted && application.snapshot().regions.size() == 1 &&
              application.snapshot().selected_region != 0 &&
              application.snapshot().project_dirty,
          "region application action/state");
    check(application
                  .execute({"region.fades-set.v1",
                            {{"fade_in_frames", std::int64_t{2}},
                             {"fade_out_frames", std::int64_t{3}},
                             {"curve", std::string{"equal-power"}}}})
                  .accepted &&
              application
                  .execute({"region.direction-set.v1",
                            {{"direction", std::string{"reverse"}}}})
                  .accepted &&
              application
                  .execute({"region.pad-assign.v1",
                            {{"pad", std::int64_t{2}}}})
                  .accepted &&
              application.snapshot().region_pad_assignments[2].has_value(),
          "region edit and assignment actions");
    check(application.execute({"pad.select.v1", {{"pad", std::int64_t{3}}}})
              .accepted,
          "pad selection action");
    check(application
                  .execute({"pad.playback-mode-set.v1",
                            {{"mode", std::string{"gate"}}}})
                  .accepted &&
              application.execute({"pad.pitch-set.v1", {{"semitones", 7.0}}})
                  .accepted &&
              application.execute({"pad.gain-set.v1", {{"gain", 1.25}}})
                  .accepted &&
              application.execute({"pad.reverse-set.v1", {{"reverse", true}}})
                  .accepted,
          "pad playback setting actions");
    check(
        application
                .execute({"pad.stretch-mode-set.v1",
                          {{"mode", std::string{"realtime"}}}})
                .accepted &&
            application.execute({"pad.stretch-ratio-set.v1", {{"ratio", 1.5}}})
                .accepted &&
            application.execute({"pad.source-beats-set.v1", {{"beats", 4.0}}})
                .accepted &&
            application
                .execute({"pad.stretch-quality-set.v1",
                          {{"quality", std::string{"smooth"}}}})
                .accepted,
        "stretch application actions");
    auto application_state = application.snapshot();
    check(application_state.selected_pad == 3 &&
              application_state.selected_pad_settings.mode ==
                  SamplePlaybackMode::gate &&
              application_state.selected_pad_settings.semitones == 7 &&
              application_state.selected_pad_settings.gain == 1.25F &&
              application_state.selected_pad_settings.reverse &&
              application_state.selected_pad_settings.stretch_mode ==
                  StretchMode::realtime &&
              application_state.selected_pad_settings.stretch_quality ==
                  StretchQuality::smooth &&
              application_state.stretch_latency_frames == 512,
          "pad playback observable state");
    check(application.execute({"pad.stretch-reset.v1", {}}).accepted &&
              application.snapshot().selected_pad_settings.stretch_mode ==
                  StretchMode::off,
          "stretch reset action");
    check(
        !application
             .execute({"pad.choke-group-set.v1", {{"group", std::int64_t{17}}}})
             .accepted,
        "invalid pad playback setting accepted");
    GuiModel gui(application);
    check(gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200),
          "slice GUI view navigation");
    const auto &gui_frame = gui.frame(1920, 1200);
    check(std::any_of(gui_frame.commands.begin(), gui_frame.commands.end(),
                      [](const auto &item) {
                          return item.type == DrawCommand::Type::line;
                      }),
          "slice GUI waveform rendering");
    check(std::any_of(gui_frame.commands.begin(), gui_frame.commands.end(),
                      [](const auto &item) {
                          return item.type == DrawCommand::Type::text &&
                                 item.text.rfind("STRETCH ", 0) == 0;
                      }),
          "stretch GUI inspector rendering");
#ifdef NEUROGENIC_HAS_LUA
    ScriptRuntime lua([](Pattern) { return true; }, [] { return 0ULL; },
                      &timeline, &application.slices());
    check(lua.execute("neurogenic.v1.slices.equal_divisions(0, 4)", error) &&
              application.slices().snapshot(SampleSlotId{0}).slices.size() == 4,
          "Lua slice adapter");
#endif
    const auto preview_state = application.slices().snapshot(SampleSlotId{0});
    check(application.slices().preview(preview_state.slices[0].id, 1, error),
          "slice preview");
    engine.process({left.data(), 1}, {right.data(), 1});
    application.slices().stop_preview();
    engine.process({left.data(), 1}, {right.data(), 1});
    check(engine.active_voice_count() == 0, "slice preview stop ownership");
    SliceProject project;
    project.sample_paths[0] = wav;
    project.slices.push_back(
        {42, SampleSlotId{0}, 1, 1, 0, 100, "Persisted", 60, 0.8F});
    project.pads[3] = 42;
    project.regions.push_back({77,
                               SampleSlotId{0},
                               1,
                               1,
                               10,
                               90,
                               "Loop",
                               RegionDirection::reverse,
                               RegionFadeCurve::equal_power,
                               4,
                               5,
                               0.7F,
                               true,
                               20,
                               80,
                               3});
    project.region_pads[4] = 77;
    project.pad_settings[3].mode = SamplePlaybackMode::toggle;
    project.pad_settings[3].choke_group = 4;
    project.pad_settings[3].semitones = -5;
    project.pad_settings[3].reverse = true;
    project.pad_settings[3].stretch_mode = StretchMode::realtime;
    project.pad_settings[3].stretch_quality = StretchQuality::smooth;
    project.pad_settings[3].stretch_ratio = 1.5F;
    project.pad_settings[3].source_beats = 4;
    const auto project_path = base / "neurogenic-slices.project";
    save_slice_project(project, project_path);
    const auto restored = load_slice_project(project_path);
    check(restored.slices.size() == 1 && restored.slices[0].id == 42 &&
              restored.pads[3] == 42 &&
              restored.pad_settings[3].mode == SamplePlaybackMode::toggle &&
              restored.pad_settings[3].choke_group == 4 &&
              restored.pad_settings[3].reverse &&
              restored.pad_settings[3].stretch_mode == StretchMode::realtime &&
              restored.pad_settings[3].stretch_quality ==
                  StretchQuality::smooth &&
              restored.pad_settings[3].stretch_ratio == 1.5F &&
              restored.pad_settings[3].source_beats == 4,
          "slice persistence round trip");
    check(restored.regions.size() == 1 && restored.regions[0].id == 77 &&
              restored.regions[0].direction == RegionDirection::reverse &&
              restored.regions[0].fade_curve ==
                  RegionFadeCurve::equal_power &&
              restored.regions[0].loop_enabled &&
              restored.region_pads[4] == 77,
          "region persistence round trip");
    check(application.slices().restore_project(restored, error),
          "slice project activation");
    const auto activated = application.slices().snapshot(SampleSlotId{0});
    check(activated.slices.size() == 1 && activated.slices[0].id == 42 &&
              activated.pad_assignments[3] == 42 &&
              activated.pad_settings[3].stretch_mode == StretchMode::realtime,
          "slice and pad settings restoration");
    check(activated.regions.size() == 1 &&
              activated.region_pad_assignments[4] == 77,
          "region restoration");
    const auto legacy_path = base / "neurogenic-slices-v1.project";
    {
        std::ofstream legacy(legacy_path);
        legacy << "NEUROGENIC_SLICES 1\n"
               << "sample 0 " << std::quoted(wav.string()) << "\n"
               << "slice 7 0 1 0 20 1 -1 \"Legacy\"\n"
               << "pad 0 7\n";
    }
    const auto legacy = load_slice_project(legacy_path);
    check(legacy.slices.size() == 1 && legacy.pads[0] == 7 &&
              legacy.pad_settings[0] == SamplePlaybackSettings{},
          "version-one slice sidecar compatibility");
    const auto version_two_path = base / "neurogenic-slices-v2.project";
    {
        std::ofstream version_two(version_two_path);
        version_two << "NEUROGENIC_SLICES 2\n"
                    << "sample 0 " << std::quoted(wav.string()) << "\n"
                    << "playback 0 1 2 1 3 4 1 -1 0 0 0 1 .005\n";
    }
    const auto version_two = load_slice_project(version_two_path);
    check(version_two.pad_settings[0].mode == SamplePlaybackMode::gate &&
              version_two.pad_settings[0].stretch_mode == StretchMode::off,
          "version-two slice sidecar compatibility");
    auto partition_engine = [](std::size_t block) {
        AudioEngine engine(48000, 4);
        Sample source;
        source.sample_rate = 48000;
        source.channels = 1;
        source.interleaved.resize(128);
        for (std::size_t i = 0; i < source.interleaved.size(); ++i)
            source.interleaved[i] = static_cast<float>(i) / 128.0F;
        std::uint64_t publication{};
        check(engine.publish_sample_slot(
                  SampleSlotId{0},
                  std::make_shared<const Sample>(std::move(source)),
                  &publication),
              "partition region publication");
        std::array<float, 64> empty_left{}, empty_right{};
        engine.process({empty_left.data(), 0}, {empty_right.data(), 0});
        SampleRegionPlayback region{4, 4, 16, 32, 4,
                                    RegionFadeCurve::equal_power, true};
        check(engine.trigger_region(SampleSlotId{0}, publication, 8, 48, 1,
                                    {}, region, 1),
              "partition region trigger");
        std::array<float, 64> output{}, right{};
        for (std::size_t offset = 0; offset < output.size(); offset += block)
            engine.process({output.data() + offset, block},
                           {right.data() + offset, block});
        return output;
    };
    const auto whole = partition_engine(64);
    const auto partitioned = partition_engine(16);
    for (std::size_t i = 0; i < whole.size(); ++i)
        check(std::abs(whole[i] - partitioned[i]) < 1e-6F,
              "region callback partition dependence");
    std::filesystem::remove(wav);
    bool missing = false;
    try {
        (void)load_slice_project(project_path);
    } catch (const std::runtime_error &) {
        missing = true;
    }
    check(missing, "missing slice sample reference accepted");
    std::filesystem::remove(project_path);
    std::filesystem::remove(legacy_path);
    std::filesystem::remove(version_two_path);
}

void test_expressive_sample_playback() {
    using namespace neurogenic;
    auto make_engine = [] {
        auto engine = std::make_unique<AudioEngine>(48000, 8);
        Sample sample;
        sample.sample_rate = 48000;
        sample.channels = 1;
        sample.interleaved.resize(1024);
        for (std::size_t i = 0; i < sample.frames(); ++i)
            sample.interleaved[i] = static_cast<float>(i % 8) / 8.0F;
        engine->set_sample(std::move(sample));
        std::array<float, 1> left{}, right{};
        engine->process({left.data(), 0}, {right.data(), 0});
        return engine;
    };
    auto configure = [](AudioEngine &engine, std::uint8_t note,
                        SamplePlaybackSettings settings) {
        check(engine.configure_note_playback(note, settings),
              "sample playback settings queue");
        std::array<float, 1> left{}, right{};
        engine.process({left.data(), 0}, {right.data(), 0});
    };
    std::array<float, 16> left{}, right{};
    auto engine = make_engine();
    SamplePlaybackSettings settings;
    settings.mode = SamplePlaybackMode::one_shot;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 1, true), "one-shot trigger");
    engine->process({left.data(), 2}, {right.data(), 2});
    check(engine->trigger_pad(36, 0, false), "one-shot release command");
    engine->process({left.data(), 2}, {right.data(), 2});
    check(left[0] != 0, "one-shot was stopped by note-off");

    engine = make_engine();
    settings.mode = SamplePlaybackMode::gate;
    settings.release_seconds = 0;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 1), "gate trigger");
    engine->process({left.data(), 2}, {right.data(), 2});
    check(engine->trigger_pad(36, 0, false), "gate release");
    engine->process({left.data(), 2}, {right.data(), 2});
    check(left[0] == 0, "gate note-off did not stop voice");

    engine = make_engine();
    settings.mode = SamplePlaybackMode::toggle;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 1) && engine->trigger_pad(36, 1),
          "toggle commands");
    engine->process({left.data(), 1}, {right.data(), 1});
    check(engine->active_voice_count() == 0,
          "second toggle trigger did not stop voice");

    engine = make_engine();
    settings.mode = SamplePlaybackMode::one_shot;
    settings.choke_group = 1;
    settings.choke_release = false;
    configure(*engine, 36, settings);
    configure(*engine, 37, settings);
    check(engine->trigger_pad(36, 1) && engine->trigger_pad(37, 1),
          "simultaneous choke commands");
    engine->process({left.data(), 1}, {right.data(), 1});
    check(engine->active_voice_count() == 1, "simultaneous choke ordering");

    engine = make_engine();
    settings.choke_release = true;
    settings.release_seconds = 0.01F;
    configure(*engine, 36, settings);
    configure(*engine, 37, settings);
    check(engine->trigger_pad(36, 1), "release choke first trigger");
    engine->process({left.data(), 1}, {right.data(), 1});
    check(engine->trigger_pad(37, 1), "release choke second trigger");
    engine->process({left.data(), 1}, {right.data(), 1});
    check(engine->active_voice_count() == 2,
          "short-release choke stopped victim immediately");

    engine = make_engine();
    settings = {};
    settings.semitones = 12;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 1), "pitched trigger");
    engine->process({left.data(), 3}, {right.data(), 3});
    near(left[1], 0.2F, "octave playback increment");
    engine = make_engine();
    settings.semitones = 0;
    settings.fine_cents = 50;
    configure(*engine, 37, settings);
    check(engine->trigger_pad(37, 1), "fine-tuned trigger");
    engine->process({left.data(), 3}, {right.data(), 3});
    check(left[2] > left[1], "fractional interpolation");

    engine = make_engine();
    settings = {};
    settings.reverse = true;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 1), "reverse trigger");
    engine->process({left.data(), 2}, {right.data(), 2});
    near(left[0], 0.7F, "reverse end boundary");
    near(left[1], 0.6F, "reverse direction");

    engine = make_engine();
    settings = {};
    settings.mode = SamplePlaybackMode::gate;
    settings.attack_seconds = 2.0F / 48000.0F;
    settings.decay_seconds = 2.0F / 48000.0F;
    settings.sustain = 0.5F;
    settings.release_seconds = 2.0F / 48000.0F;
    settings.gain = 0.5F;
    configure(*engine, 36, settings);
    check(engine->trigger_pad(36, 0.5F), "enveloped trigger");
    engine->process({left.data(), 4}, {right.data(), 4});
    check(left[0] == 0 && left[2] > left[1], "ADSR attack stage");
    check(engine->trigger_pad(36, 0, false), "ADSR release command");
    engine->process({left.data(), 3}, {right.data(), 3});
    check(engine->active_voice_count() == 0, "ADSR release stage");
}

void test_realtime_time_stretch() {
    using namespace neurogenic;
    Sample sine;
    sine.sample_rate = 48000;
    sine.channels = 2;
    sine.interleaved.resize(4800 * 2);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < 4800; ++frame) {
        const auto value =
            static_cast<float>(std::sin(2.0 * pi * 440.0 * frame / 48000.0));
        sine.interleaved[frame * 2] = value;
        sine.interleaved[frame * 2 + 1] = value;
    }
    TimeStretchConfiguration configuration{
        StretchMode::realtime, StretchQuality::balanced, 1.5F, 1.0F, 48000};
    check(RealtimeTimeStretcher::valid(configuration),
          "valid realtime stretch rejected");
    check(RealtimeTimeStretcher::latency_frames(configuration) == 256,
          "balanced stretch latency");
    TimeStretchState state;
    RealtimeTimeStretcher::reset(state, configuration);
    std::vector<float> output(7200);
    for (auto &value : output) {
        value = RealtimeTimeStretcher::process_channel(sine, 0, sine.frames(),
                                                       configuration, state, 0);
        const auto right = RealtimeTimeStretcher::process_channel(
            sine, 0, sine.frames(), configuration, state, 1);
        check(std::abs(value - right) < 1.0e-6F,
              "stereo stretch channel drift");
        check(std::isfinite(value), "stretch produced non-finite output");
        RealtimeTimeStretcher::advance(state);
    }
    std::size_t crossings{};
    for (std::size_t i = 1025; i < output.size(); ++i)
        crossings += output[i - 1] <= 0 && output[i] > 0;
    const auto measured = crossings * 48000.0 / (output.size() - 1025);
    check(measured > 390 && measured < 490,
          "realtime stretch did not preserve pitch");
    near(RealtimeTimeStretcher::resolve_time_ratio(1, 4, 0, 96000, 48000, 120),
         1, "beat-length tempo ratio");
    near(RealtimeTimeStretcher::resolve_time_ratio(1, 0, 90, 48000, 48000, 120),
         .75F, "source BPM tempo ratio");
    for (const auto ratio : {.5F, 1.5F, 2.0F, .25F, 4.0F}) {
        configuration.time_ratio = ratio;
        check(RealtimeTimeStretcher::valid(configuration),
              "supported stretch ratio rejected");
    }
    configuration.time_ratio = .1F;
    check(!RealtimeTimeStretcher::valid(configuration),
          "invalid stretch ratio accepted");
    configuration.time_ratio = 1;
    RealtimeTimeStretcher::reset(state, configuration);
    std::array<float, 32> block_left{}, block_right{};
    check(RealtimeTimeStretcher::process_block(sine, 0, sine.frames(),
                                               configuration, state, block_left,
                                               block_right, 12) == 12 &&
              state.output_frame == 12,
          "bounded stretch block processing");

    configuration.time_ratio = 1.5F;
    configuration.loop_enabled = true;
    configuration.loop_start_frame = 600;
    configuration.loop_end_frame = 1800;
    configuration.loop_crossfade_frames = 64;
    RealtimeTimeStretcher::reset(state, configuration, 0, sine.frames(), 3600);
    near(static_cast<float>(RealtimeTimeStretcher::source_position(
             state, configuration)),
         2400, "stretched seek source phase");
    std::array<float, 2048> loop_left{}, loop_right{};
    const auto loop_count = RealtimeTimeStretcher::process_block(
        sine, 0, sine.frames(), configuration, state, loop_left, loop_right,
        10000);
    check(loop_count == loop_left.size() &&
              std::all_of(loop_left.begin(), loop_left.end(),
                          [](float value) { return std::isfinite(value); }),
          "loop-aware stretch did not remain bounded");
    check(std::any_of(loop_left.begin(), loop_left.end(),
                      [](float value) { return std::abs(value) > 1.0e-4F; }),
          "loop-aware stretch became silent after wrap");

    TimeStretchState partitioned_state;
    RealtimeTimeStretcher::reset(partitioned_state, configuration, 0,
                                 sine.frames(), 3600);
    std::array<float, 2048> partitioned_left{}, partitioned_right{};
    std::size_t partition_offset{};
    for (const auto block : {std::size_t{17}, std::size_t{255},
                             std::size_t{3}, std::size_t{511},
                             std::size_t{1262}}) {
        check(RealtimeTimeStretcher::process_block(
                  sine, 0, sine.frames(), configuration, partitioned_state,
                  std::span<float>(partitioned_left).subspan(partition_offset,
                                                              block),
                  std::span<float>(partitioned_right).subspan(partition_offset,
                                                               block),
                  10000) == block,
              "partitioned stretch block was truncated");
        partition_offset += block;
    }
    for (std::size_t i = 0; i < loop_left.size(); ++i)
        check(std::abs(loop_left[i] - partitioned_left[i]) < 1.0e-7F,
              "stretch output depends on callback partition");

    const auto before_ratio_change =
        RealtimeTimeStretcher::source_position(state, configuration);
    RealtimeTimeStretcher::set_time_ratio(state, configuration, 0.75F);
    near(static_cast<float>(RealtimeTimeStretcher::source_position(
             state, configuration)),
         static_cast<float>(before_ratio_change),
         "tempo change preserves stretcher phase");
    for (std::size_t i = 0; i < 48000; ++i)
        RealtimeTimeStretcher::advance(state);
    check(std::abs(RealtimeTimeStretcher::source_position(state, configuration) -
                   before_ratio_change - 64000.0) < 0.05,
          "tempo change cumulative source phase");

    configuration.reverse = true;
    configuration.loop_enabled = false;
    RealtimeTimeStretcher::reset(state, configuration, 100, 1100, 150);
    near(static_cast<float>(RealtimeTimeStretcher::source_position(
             state, configuration)),
         899, "reverse stretched seek source phase");

    AudioEngine engine(48000, 4);
    Sample ramp;
    ramp.sample_rate = 48000;
    ramp.channels = 1;
    ramp.interleaved.resize(1024);
    for (std::size_t i = 0; i < ramp.frames(); ++i)
        ramp.interleaved[i] = static_cast<float>(i % 16) / 16.0F;
    engine.set_sample(std::move(ramp));
    SamplePlaybackSettings settings;
    settings.stretch_mode = StretchMode::realtime;
    settings.stretch_ratio = 2;
    check(engine.configure_note_playback(36, settings),
          "stretch settings publication");
    std::vector<float> left(2048), right(2048);
    engine.process({left.data(), 0}, {right.data(), 0});
    check(engine.trigger_pad(36, 1), "stretched voice trigger");
    engine.process(left, right);
    check(engine.active_voice_count() == 1,
          "stretched voice ended before configured duration");
    std::array<float, 1> tail_left{}, tail_right{};
    engine.process(tail_left, tail_right);
    check(engine.active_voice_count() == 0,
          "stretched voice exceeded configured duration");

    AudioEngine reverse_engine(48000, 2);
    Sample reverse_sample;
    reverse_sample.sample_rate = 48000;
    reverse_sample.channels = 1;
    reverse_sample.interleaved = {0.1F, 0.2F, 0.3F, 0.4F};
    reverse_engine.set_sample(std::move(reverse_sample));
    settings.reverse = true;
    check(reverse_engine.configure_note_playback(36, settings),
          "reverse stretch fallback publication");
    reverse_engine.process({tail_left.data(), 0}, {tail_right.data(), 0});
    check(reverse_engine.trigger_pad(36, 1), "reverse fallback trigger");
    reverse_engine.process(tail_left, tail_right);
    check(tail_left[0] > 0.25F,
          "reverse stretched playback did not begin at the end boundary");

    AudioEngine loop_engine(48000, 4);
    auto loop_sample = std::make_shared<Sample>();
    loop_sample->sample_rate = 48000;
    loop_sample->channels = 1;
    loop_sample->interleaved.resize(1024);
    for (std::size_t i = 0; i < loop_sample->frames(); ++i)
        loop_sample->interleaved[i] =
            static_cast<float>((i % 31) + 1) / 32.0F;
    std::uint64_t publication{};
    check(loop_engine.publish_sample_slot(
              SampleSlotId{0}, std::shared_ptr<const Sample>(loop_sample),
              &publication),
          "stretched loop sample publication");
    SampleRegionPlayback loop_region;
    loop_region.loop_enabled = true;
    loop_region.loop_start_frame = 128;
    loop_region.loop_end_frame = 768;
    loop_region.loop_crossfade_frames = 32;
    settings = {};
    settings.stretch_mode = StretchMode::realtime;
    settings.stretch_ratio = 1.25F;
    check(loop_engine.map_note_to_region(36, SampleSlotId{0}, publication, 0,
                                         1024, 1.0F, settings, loop_region),
          "stretched region loop mapping");
    loop_engine.process({tail_left.data(), 0}, {tail_right.data(), 0});
    check(loop_engine.trigger_pad(36, 1), "stretched region loop trigger");
    std::vector<float> loop_engine_left(4096), loop_engine_right(4096);
    loop_engine.process(loop_engine_left, loop_engine_right);
    check(loop_engine.active_voice_count() == 1 &&
              std::any_of(loop_engine_left.end() - 512,
                          loop_engine_left.end(),
                          [](float value) { return std::abs(value) > 1.0e-4F; }),
          "stretched region loop did not continue beyond source duration");
}

void test_sample_slots() {
    using namespace neurogenic;
    std::array<float, 8> left{}, right{};
    auto engine = std::make_unique<AudioEngine>(48000, 8);
    engine->set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    engine->set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    check(engine->sample_slot_count() == 2 &&
              engine->has_sample_slot(SampleSlotId{1}),
          "sample slot installation");
    engine->trigger(36, 1);
    engine->trigger(37, 1);
    engine->process(left, right);
    near(left[0], 0.6F, "different notes did not select different samples");

    auto same = std::make_unique<AudioEngine>(48000, 4);
    same->set_sample_slot(SampleSlotId{0}, constant_sample(4, 0.25F));
    same->trigger(36, 1);
    same->trigger(36, 1);
    same->process(left, right);
    near(left[0], 0.4F, "same-slot polyphony");

    auto missing = std::make_unique<AudioEngine>();
    missing->set_sample_slot(SampleSlotId{0}, constant_sample(4));
    missing->trigger(37, 1);
    missing->trigger(52, 1);
    missing->process(left, right);
    near(left[0], 0, "missing slots produced audio");
    check(missing->missing_slot_triggers() == 2, "missing slot counter");
    check(!missing->map_note_to_slot(52, SampleSlotId{999}),
          "invalid slot mapping accepted");

    auto arbitrary = std::make_unique<AudioEngine>();
    arbitrary->set_sample_slot(SampleSlotId{1000}, constant_sample(4, 0.75F));
    check(arbitrary->map_note_to_slot(52, SampleSlotId{1000}),
          "arbitrary slot mapping rejected");
    arbitrary->trigger(52, 1);
    arbitrary->process(left, right);
    near(left[0], 0.6F, "arbitrary slot did not play");

    auto stealing = std::make_unique<AudioEngine>(48000, 2);
    stealing->set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    stealing->set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    stealing->set_sample_slot(SampleSlotId{2}, constant_sample(8, 1.0F));
    stealing->trigger(36, 1);
    stealing->trigger(37, 1);
    stealing->trigger(38, 1);
    stealing->process(left, right);
    near(left[0], 1.2F, "voice stealing across samples");
    check(stealing->stolen_voices() == 1, "cross-sample steal counter");

    auto sequenced = std::make_unique<AudioEngine>();
    sequenced->set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    Pattern pattern;
    pattern.steps[0] = {true, 37, 1};
    pattern.revision = 1;
    check(sequenced->queue_pattern(pattern), "sequencer slot pattern queue");
    sequenced->process(left, right);
    near(left[0], 0.4F, "sequencer note did not select slot");

    auto midi_engine = std::make_unique<AudioEngine>();
    midi_engine->set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    MidiMapper mapper;
    mapper.set_note_handler([&](auto note, auto velocity) {
        midi_engine->trigger(note, velocity);
    });
    check(mapper.receive({0x90, 37, 127, 0}), "slot MIDI enqueue");
    check(mapper.dispatch_pending() == 1, "slot MIDI dispatch");
    midi_engine->process(left, right);
    near(left[0], 0.4F, "MIDI note did not select slot");

    bool invalid_sample = false;
    try {
        AudioEngine invalid;
        invalid.set_sample_slot(SampleSlotId{0}, {44100, 1, {1.0F}});
    } catch (const std::invalid_argument &) {
        invalid_sample = true;
    }
    check(invalid_sample, "mismatched sample rate accepted");
    bool invalid_id = false;
    try {
        AudioEngine invalid;
        invalid.set_sample_slot(invalid_sample_slot, constant_sample(1));
    } catch (const std::invalid_argument &) {
        invalid_id = true;
    }
    check(invalid_id, "reserved sample slot identifier accepted");
    check(!AudioEngine{}.map_note_to_slot(36, SampleSlotId{0}),
          "unloaded default slot mapped");

    const auto invalid_path =
        std::filesystem::temp_directory_path() / "neurogenic-invalid.wav";
    {
        std::ofstream invalid(invalid_path, std::ios::binary);
        invalid << "not a wave";
    }
    bool invalid_wav = false;
    try {
        (void)load_wav(invalid_path);
    } catch (const std::runtime_error &) {
        invalid_wav = true;
    }
    std::filesystem::remove(invalid_path);
    check(invalid_wav, "invalid WAV accepted");
    bool missing_wav = false;
    try {
        (void)load_wav(invalid_path);
    } catch (const std::runtime_error &) {
        missing_wav = true;
    }
    check(missing_wav, "missing WAV accepted");
}

void test_timeline_and_tracks() {
    using namespace neurogenic;
    Timeline timeline;
    timeline.transport = {500000, 7, 8};
    Track second;
    second.id = 20;
    second.pattern.length = 720;
    second.pattern.events.push_back(
        {3, 0, MusicalEventType::note, 37, 0.5F, 240, 0, 0});
    Track first;
    first.id = 10;
    first.pattern.length = 960;
    first.pattern.events.push_back(
        {2, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    first.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 38, 0.25F, 0, 0, 0});
    first.pattern.events.push_back(
        {4, 480, MusicalEventType::note_off, 36, 0, 0, 0, 0, 2});
    first.pattern.events.push_back(
        {5, 240, MusicalEventType::parameter, 0, 0, 0, 77, 0.75F});
    timeline.tracks = {second, first};

    PlaybackPlan plan;
    std::string error;
    check(compile_timeline(timeline, plan, error), "timeline compilation");
    check(plan.tracks.size() == 2 && plan.events.size() == 6,
          "compiled timeline dimensions");
    TimelineScheduler scheduler(48000);
    scheduler.set_plan(&plan, 0);
    check(scheduler.frame_at_tick(960) == 24000, "tempo-to-frame conversion");
    std::array<TimelineScheduledEvent, 32> events{};
    auto count = scheduler.schedule(0, 1, events);
    check(count == 3, "simultaneous multi-track events");
    check(events[0].track == 10 && events[0].id == 1 && events[1].id == 2 &&
              events[2].track == 20,
          "deterministic event ordering");

    Timeline variable;
    Track odd;
    odd.id = 1;
    odd.pattern.length = 721;
    odd.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    variable.tracks.push_back(odd);
    check(compile_timeline(variable, plan, error),
          "arbitrary pattern length compilation");
    scheduler = TimelineScheduler(44100);
    scheduler.set_plan(&plan, 0);
    std::uint64_t absolute{};
    std::vector<std::uint64_t> observed;
    for (std::size_t call = 0; observed.size() < 20; ++call) {
        const auto frames = static_cast<std::uint32_t>((call * 37) % 509 + 1);
        count = scheduler.schedule(absolute, frames, events);
        for (std::size_t i = 0; i < count; ++i)
            observed.push_back(absolute + events[i].frame);
        absolute += frames;
    }
    for (std::size_t i = 0; i < observed.size(); ++i) {
        const auto expected = static_cast<std::uint64_t>(
            std::llround(static_cast<long double>(i) * 721 * 44100 * 500000 /
                         (960.0L * 1000000)));
        check(observed[i] == expected,
              "timeline timing drift across variable buffers");
    }

    Timeline muted = variable;
    muted.tracks[0].muted = true;
    check(compile_timeline(muted, plan, error), "muted timeline compilation");
    scheduler = TimelineScheduler(48000);
    scheduler.set_plan(&plan, 0);
    check(scheduler.schedule(0, 48000, events) == 0,
          "muted track produced events");

    Timeline boundary;
    Track looping;
    looping.id = 9;
    looping.pattern.length = 960;
    looping.pattern.events = {
        {1, 959, MusicalEventType::note_on, 36, 1, 0, 0, 0},
        {2, 0, MusicalEventType::note_on, 37, 1, 0, 0, 0}};
    boundary.tracks.push_back(looping);
    check(compile_timeline(boundary, plan, error), "loop boundary compilation");
    scheduler = TimelineScheduler(48000);
    scheduler.set_plan(&plan, 0);
    count = scheduler.schedule(0, 24001, events);
    check(count == 3 && events[0].pitch == 37 && events[1].pitch == 36 &&
              events[2].pitch == 37 && events[2].frame == 24000,
          "loop boundary scheduling");

    Timeline invalid = boundary;
    invalid.tracks[0].pattern.events[1].id = 1;
    check(!compile_timeline(invalid, plan, error) && !error.empty(),
          "duplicate event identifier accepted");
    Timeline scalable;
    Track crowded;
    crowded.id = 1;
    crowded.pattern.length = 2000;
    for (std::size_t i = 0; i < 1000; ++i)
        crowded.pattern.events.push_back({i + 1, static_cast<MusicalTick>(i),
                                          MusicalEventType::note_on, 36, 1, 0,
                                          0, 0});
    scalable.tracks.push_back(std::move(crowded));
    check(compile_timeline(scalable, plan, error) && plan.events.size() == 1000,
          "preparation-time timeline scaling");

    AudioEngine plan_queue;
    Timeline one;
    Track one_track;
    one_track.id = 1;
    one_track.pattern.length = 960;
    one_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    one.tracks.push_back(one_track);
    check(plan_queue.queue_timeline(one, error) &&
              plan_queue.queue_timeline(one, error) &&
              plan_queue.queue_timeline(one, error),
          "timeline queue usable capacity");
    check(!plan_queue.queue_timeline(one, error) &&
              plan_queue.dropped_timelines() == 1,
          "timeline queue overflow behavior");
    AudioEngine reclamation;
    check(reclamation.queue_timeline(one, error), "initial reclamation plan");
    std::array<float, 1> reclaim_left{}, reclaim_right{};
    reclamation.process(reclaim_left, reclaim_right);
    for (int i = 0; i < 100; ++i) {
        check(reclamation.queue_timeline(one, error),
              "rapid replacement queue");
        reclamation.process(reclaim_left, reclaim_right);
    }
    check(reclamation.queue_timeline(one, error) &&
              reclamation.reclaimed_timeline_plans() >= 99,
          "retired plans were not reclaimed on control thread");

    AudioEngine engine(48000, 8);
    engine.set_sample_slot(SampleSlotId{0}, constant_sample(30000, 0.25F));
    engine.set_sample_slot(SampleSlotId{1}, constant_sample(30000, 0.5F));
    Timeline audio;
    Track audio_track;
    audio_track.id = 1;
    audio_track.pattern.length = 960;
    audio_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 1, 480, 0, 0},
        {2, 0, MusicalEventType::note, 37, 1, 480, 0, 0}};
    audio.tracks.push_back(audio_track);
    check(engine.queue_timeline(audio, error), "audio timeline queue");
    std::vector<float> left(12000), right(12000);
    engine.process(left, right);
    near(left[0], 0.6F, "timeline tracks did not select sample slots");
    near(left.back(), 0.6F, "note duration ended early");
    std::array<float, 2> tail_left{}, tail_right{};
    engine.process(tail_left, tail_right);
    near(tail_left[0], 0, "note-off did not stop timeline voices");

    AudioEngine durations(48000, 4);
    durations.set_sample_slot(SampleSlotId{0}, constant_sample(30000));
    Timeline overlap;
    Track overlap_track;
    overlap_track.id = 2;
    overlap_track.pattern.length = 1920;
    overlap_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 0.25F, 480, 0, 0},
        {2, 0, MusicalEventType::note, 36, 0.5F, 720, 0, 0}};
    overlap.tracks.push_back(overlap_track);
    check(durations.queue_timeline(overlap, error),
          "overlapping duration queue");
    durations.process(left, right);
    near(left[0], 0.6F, "overlapping duration notes did not start");
    durations.process(tail_left, tail_right);
    near(tail_left[0], 0.4F,
         "one note-off stopped an unrelated overlapping note");
    std::vector<float> remainder_left(5998), remainder_right(5998);
    durations.process(remainder_left, remainder_right);
    durations.process(tail_left, tail_right);
    near(tail_left[0], 0, "second duration note did not stop");

    Pattern legacy;
    legacy.steps[3] = {true, 39, 0.75F};
    legacy.revision = 4;
    auto adapted = adapt_step_pattern(legacy, 42);
    check(adapted.tracks.size() == 1 && adapted.tracks[0].id == 42,
          "legacy pattern track adapter");
    check(adapted.tracks[0].pattern.length == ticks_per_quarter * 4 &&
              adapted.tracks[0].pattern.events[0].tick ==
                  ticks_per_quarter * 3 / 4,
          "legacy pattern timing adapter");
    check(compile_timeline(adapted, plan, error),
          "legacy pattern did not compile");
}

void test_unified_project_persistence() {
    using namespace neurogenic;
    const auto root = std::filesystem::temp_directory_path() /
                      "neurogenic-unified-project-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "assets");
    const auto sample_path = root / "assets" / "kick.wav";
    write_pcm16_wav(sample_path, 44100, 1, std::vector<float>(128, 0.25F));

    Project project;
    project.name = "Round Trip";
    project.timeline.transport = {428571, 7, 8};
    project.timeline.generation_seed = 0x12345678;
    Track track;
    track.id = 77;
    track.name = "Synth lead";
    track.instrument_id = std::string(subtractive_instrument_id);
    track.instrument_instance_id = 2;
    track.pattern.length = 2880;
    MusicalEvent note{91, 120, MusicalEventType::note, 64, 0.7F, 360};
    note.trigger_probability = 0.625F;
    note.trigger_condition = TriggerCondition::every_nth_loop;
    note.condition_n = 2;
    note.condition_m = 3;
    note.ratchet_count = 4;
    track.pattern.events.push_back(note);
    MusicalEvent lock{92, 120, MusicalEventType::parameter};
    lock.parameter_destination = "synth.cutoff";
    lock.value = 1234.0F;
    track.pattern.events.push_back(lock);
    project.timeline.tracks.push_back(track);
    project.samples.sample_paths[3] = "assets/kick.wav";
    project.samples.slices.push_back(
        {501, SampleSlotId{3}, 0, 0, 4, 100, "Kick tail", 36, 0.8F});
    project.samples.pads[5] = 501;
    auto &settings = project.samples.pad_settings[5];
    settings.mode = SamplePlaybackMode::gate;
    settings.choke_group = 2;
    settings.semitones = -3;
    settings.fine_cents = 12.5F;
    settings.reverse = true;
    settings.stretch_mode = StretchMode::realtime;
    settings.stretch_quality = StretchQuality::smooth;
    settings.stretch_ratio = 1.5F;
    settings.source_beats = 7;
    project.parameters["synth.cutoff"] = 4321.0;
    project.parameters["effect.delay.mix"] = 0.25;
    project.parameters["instance.2.synth.cutoff"] = 2500.0;
    project.mixer.effects.instance_count = 1;
    project.mixer.effects.instances[0] = {700,
                                          EffectProcessorType::low_pass_filter,
                                          {{{1, 2400.0F}, {2, 0.4F}}},
                                          2,
                                          0.75F,
                                          2,
                                          2,
                                          true,
                                          false};
    project.mixer.effects.master = {{700}, 1, false};
    project.modulation.sources[0].waveform = LfoWaveform::triangle;
    project.modulation.sources[0].tempo_sync = true;
    project.modulation.sources[0].beats_per_cycle = 2;
    auto &persisted_follower =
        project.modulation.sources[static_cast<std::size_t>(
            ModulationSourceId::envelope_follower)];
    persisted_follower.follower_attack_seconds = 0.025F;
    persisted_follower.follower_release_seconds = 0.3F;
    persisted_follower.follower_gain = 2.0F;
    persisted_follower.follower_threshold = 0.1F;
    project.modulation.route_count = 1;
    project.modulation.routes[0] = {
        81,
        ModulationSourceId::lfo_1,
        {ParameterTarget::subtractive_synth,
         static_cast<std::uint32_t>(SynthParameter::cutoff), 2},
        20,
        20000,
        4321,
        -0.25F};
    project.modulation.modulator_count = 1;
    project.modulation.modulators[0].id = 9001;
    project.modulation.modulators[0].type = ModulatorType::lfo;
    project.modulation.modulators[0].owner_kind = ModulatorOwnerKind::master;
    project.modulation.modulators[0].settings = project.modulation.sources[0];
    project.modulation.modulators[0].shared = true;
    std::copy_n("Project LFO", 11,
                project.modulation.modulators[0].display_name.begin());
    project.modulation.routes[0].source_instance = 9001;
    project.modulation.modulator_count = 2;
    project.modulation.modulators[1].id = 9002;
    project.modulation.modulators[1].type = ModulatorType::lfo;
    project.modulation.modulators[1].owner_kind = ModulatorOwnerKind::master;
    project.modulation.modulators[1].settings.rate_hz = 0.5F;
    project.modulation.route_count = 2;
    project.modulation.routes[1].id = 82;
    project.modulation.routes[1].source_instance = 9001;
    project.modulation.routes[1].destination_kind =
        ModulationDestinationKind::modulator_parameter;
    project.modulation.routes[1].destination_modulator = 9002;
    project.modulation.routes[1].destination_modulator_parameter =
        ModulatorParameterId::rate;
    project.modulation.routes[1].minimum = -1;
    project.modulation.routes[1].maximum = 1;
    project.modulation.routes[1].depth = 0.2F;
    project.modulation.modulator_count = 4;
    project.modulation.modulators[2].id = 9003;
    project.modulation.modulators[2].type = ModulatorType::step_sequencer;
    project.modulation.modulators[2].owner_kind = ModulatorOwnerKind::master;
    project.modulation.modulators[2].settings.step_count = 3;
    project.modulation.modulators[2].settings.pulses = 2;
    project.modulation.modulators[2].settings.step_values[0] = -0.75F;
    project.modulation.modulators[2].settings.step_values[1] = 0.25F;
    project.modulation.modulators[2].settings.step_values[2] = 1.0F;
    project.modulation.modulators[3].id = 9004;
    project.modulation.modulators[3].type = ModulatorType::rectify;
    project.modulation.modulators[3].owner_kind = ModulatorOwnerKind::master;
    project.modulation.route_count = 3;
    project.modulation.routes[2].id = 83;
    project.modulation.routes[2].source_instance = 9003;
    project.modulation.routes[2].source_output = 2;
    project.modulation.routes[2].destination_kind =
        ModulationDestinationKind::modulator_input;
    project.modulation.routes[2].destination_modulator = 9004;
    project.modulation.routes[2].destination_input = 1;
    project.modulation.routes[2].minimum = -1;
    project.modulation.routes[2].maximum = 1;
    project.modulation.routes[2].depth = 1;
    project.modulation.modulator_count = 5;
    project.modulation.modulators[4].id = 9005;
    project.modulation.modulators[4].type = ModulatorType::velocity;
    project.modulation.modulators[4].owner_kind =
        ModulatorOwnerKind::voice_definition;
    project.modulation.modulators[4].owner_id = 77;
    project.modulation.route_count = 4;
    project.modulation.routes[3].id = 84;
    project.modulation.routes[3].source_instance = 9005;
    project.modulation.routes[3].destination = {
        ParameterTarget::sample_voice,
        static_cast<std::uint32_t>(SampleModulationParameter::gain), 77};
    project.modulation.routes[3].minimum = 0;
    project.modulation.routes[3].maximum = 1;
    project.modulation.routes[3].depth = 1;
    project.modulation.routes[3].scope = ModulationScope::voice;
    project.modulation.routes[3].track_id = 77;
    PerformanceMacro persisted_macro;
    persisted_macro.id = 41;
    persisted_macro.source_instance = 0x40000041U;
    persisted_macro.name = "Performance";
    persisted_macro.description = "Round-trip macro";
    persisted_macro.owner_kind = MacroOwnerKind::track;
    persisted_macro.owner_id = 77;
    persisted_macro.value = 0.75F;
    persisted_macro.default_value = 0.25F;
    persisted_macro.assignments.push_back(
        {71, "synth.instance.2.cutoff", 100.0F, 8000.0F,
         ModulationPolarity::unipolar, ModulationMappingCurve::exponential,
         true, 0.02F, 2});
    project.performance_macros.macros.push_back(persisted_macro);
    project.performance_macros.scenes.push_back(
        {81, "Drop", {{41, 0.1F}}, {}, 4.0F, true,
         MacroMorphCurve::s_curve});
    project.midi.clock_source = MidiClockSource::external;
    project.midi.forward_external_clock = true;
    project.midi.route_count = 1;
    project.midi.routes[0] = {51,   1,     2,     0,     4,     true,
                              true, false, false, false, false, true};
    project.midi.output_selectors[1][0] = '2';

    ProjectIoMetrics save_metrics, load_metrics;
    save_project(project, root, &save_metrics);
    std::filesystem::create_directories(root / "generations" /
                                        "interrupted.tmp");
    {
        std::ofstream partial(root / "generations" / "interrupted.tmp" /
                              "timeline.ngt");
        partial << "partial";
    }
    auto loaded = load_project(root, &load_metrics);
    check(
        loaded.name == project.name && loaded.timeline.tracks.size() == 1 &&
            loaded.timeline.tracks[0].id == 77 &&
            loaded.timeline.tracks[0].pattern.events[0].id == 91 &&
            loaded.timeline.tracks[0].pattern.events[0].ratchet_count == 4 &&
            loaded.samples.slices.size() == 1 &&
            loaded.samples.slices[0].id == 501 &&
            loaded.samples.pads[5] == 501 &&
            loaded.samples.pad_settings[5].stretch_mode ==
                StretchMode::realtime &&
            loaded.parameters.at("synth.cutoff") == 4321.0 &&
            loaded.parameters.at("instance.2.synth.cutoff") == 2500.0 &&
            loaded.mixer.effects.instance_count == 1 &&
            loaded.mixer.effects.instances[0].id == 700 &&
            loaded.mixer.effects.instances[0].type ==
                EffectProcessorType::low_pass_filter &&
            loaded.mixer.effects.master.instances[0] == 700 &&
            loaded.modulation.route_count == 4 &&
            loaded.modulation.routes[0].id == 81 &&
            loaded.modulation.routes[0].destination.instance == 2 &&
            loaded.modulation.routes[0].source_instance == 9001 &&
            loaded.modulation.modulator_count == 5 &&
            loaded.modulation.modulators[0].id == 9001 &&
            std::string(loaded.modulation.modulators[0].display_name.data()) ==
                "Project LFO" &&
            loaded.modulation.modulators[0].shared &&
            loaded.modulation.routes[1].destination_kind ==
                ModulationDestinationKind::modulator_parameter &&
            loaded.modulation.routes[1].destination_modulator == 9002 &&
            loaded.modulation.modulators[2].type ==
                ModulatorType::step_sequencer &&
            loaded.modulation.modulators[2].settings.step_count == 3 &&
            loaded.modulation.modulators[2].settings.step_values[1] == 0.25F &&
            loaded.modulation.routes[2].source_output == 2 &&
            loaded.modulation.routes[2].destination_kind ==
                ModulationDestinationKind::modulator_input &&
            loaded.modulation.routes[2].destination_input == 1 &&
            loaded.modulation.modulators[4].type == ModulatorType::velocity &&
            loaded.modulation.modulators[4].owner_kind ==
                ModulatorOwnerKind::voice_definition &&
            loaded.modulation.routes[3].scope == ModulationScope::voice &&
            loaded.performance_macros.macros.size() == 1 &&
            loaded.performance_macros.macros[0].id == 41 &&
            loaded.performance_macros.macros[0].assignments[0].curve ==
                ModulationMappingCurve::exponential &&
            loaded.performance_macros.scenes.size() == 1 &&
            loaded.performance_macros.scenes[0].macro_values[0].first == 41 &&
            loaded.performance_macros.scenes[0].curve ==
                MacroMorphCurve::s_curve &&
            loaded.modulation.sources[0].tempo_sync &&
            loaded.midi.clock_source == MidiClockSource::external &&
            loaded.midi.forward_external_clock &&
            loaded.midi.route_count == 1 &&
            loaded.midi.routes[0].output_channel == 4 &&
            std::fabs(loaded.modulation
                          .sources[static_cast<std::size_t>(
                              ModulationSourceId::envelope_follower)]
                          .follower_release_seconds -
                      0.3F) < 0.0001F &&
            loaded.timeline.tracks[0].instrument_instance_id == 2 &&
            loaded.missing_assets.empty() && save_metrics.metadata_bytes > 0 &&
            load_metrics.metadata_bytes > 0,
        "unified project round trip");

    auto missing_owner = project;
    missing_owner.modulation.modulator_count = 6;
    missing_owner.modulation.modulators[5].id = 9999;
    missing_owner.modulation.modulators[5].type = ModulatorType::lfo;
    missing_owner.modulation.modulators[5].owner_kind =
        ModulatorOwnerKind::track;
    missing_owner.modulation.modulators[5].owner_id = 999999;
    bool rejected_missing_owner{};
    try {
        save_project(missing_owner, root / "missing-owner");
    } catch (const std::invalid_argument &error) {
        rejected_missing_owner =
            std::string(error.what()).find("missing owner") !=
            std::string::npos;
    }
    check(rejected_missing_owner,
          "project persistence rejects missing modulation owner");

    const auto invalid_target = root / "not-a-directory";
    {
        std::ofstream file(invalid_target);
        file << "occupied";
    }
    bool write_failed{};
    try {
        save_project(project, invalid_target);
    } catch (const std::filesystem::filesystem_error &) {
        write_failed = true;
    }
    check(write_failed && load_project(root).name == "Round Trip",
          "failed save damaged previous project");

    const auto relocated = std::filesystem::temp_directory_path() /
                           "neurogenic-unified-project-relocated";
    std::filesystem::remove_all(relocated, ignored);
    std::filesystem::rename(root, relocated);
    loaded = load_project(relocated);
    check(loaded.samples.sample_paths.at(3) ==
              relocated / "assets" / "kick.wav",
          "relative project sample relocation");

    std::filesystem::remove(relocated / "assets" / "kick.wav");
    loaded = load_project(relocated);
    check(loaded.missing_assets.size() == 1 &&
              loaded.samples.slices.size() == 1 &&
              loaded.samples.pads[5] == 501,
          "missing project asset metadata preservation");
    save_project(loaded, relocated);
    check(load_project(relocated).missing_assets.size() == 1,
          "missing asset save preservation");
    const auto replacement = relocated / "assets" / "replacement.wav";
    write_pcm16_wav(replacement, 48000, 1, std::vector<float>(64, 0.5F));
    std::string error;
    check(relink_project_sample(loaded, loaded.missing_assets.front(),
                                replacement, error) &&
              loaded.missing_assets.empty() &&
              loaded.samples.sample_paths.at(3) == replacement,
          "project sample relinking");

    Project multi_relink = loaded;
    multi_relink.modulation = {};
    multi_relink.performance_macros = {};
    multi_relink.mixer = {};
    const auto old_assets = relocated / "old-assets";
    multi_relink.samples.sample_paths = {
        {3, old_assets / "kick.wav"}, {4, old_assets / "snare.wav"}};
    multi_relink.missing_assets = {old_assets / "kick.wav",
                                   old_assets / "snare.wav"};
    const auto missing_multi_project = relocated / "missing-multi-project";
    save_project(multi_relink, missing_multi_project);
    const auto repaired_assets = relocated / "repaired-assets";
    std::filesystem::create_directories(repaired_assets);
    write_pcm16_wav(repaired_assets / "kick.wav", 48000, 1,
                    std::vector<float>(4096, 0.25F));
    std::vector<std::filesystem::path> unresolved;
    const auto before_relink = multi_relink.samples.sample_paths;
    check(!relink_project_assets(
              multi_relink, {}, {{old_assets, relocated / "invalid-assets"}},
              48000, unresolved, error) &&
              multi_relink.samples.sample_paths == before_relink,
          "invalid multi-asset recovery is atomic");
    write_pcm16_wav(repaired_assets / "snare.wav", 48000, 1,
                    std::vector<float>(4096, -0.25F));
    check(relink_project_assets(multi_relink, {},
                                {{old_assets, repaired_assets}}, 48000,
                                unresolved, error) &&
              unresolved.empty() && multi_relink.missing_assets.empty() &&
              multi_relink.samples.sample_paths.at(3) ==
                  repaired_assets / "kick.wav" &&
              multi_relink.samples.sample_paths.at(4) ==
                  repaired_assets / "snare.wav",
          "directory-prefix recovery commits all project dependencies");

    {
        AudioEngine recovery_engine;
        SampleManager recovery_manager(recovery_engine);
        TimelineService recovery_timeline(
            [&](const Timeline &timeline, std::string &publish_error) {
                return recovery_engine.queue_timeline(timeline, publish_error);
            });
        ApplicationService recovery_application(
            recovery_engine, recovery_timeline, recovery_manager);
        const auto opened = recovery_application.execute(
            {"project.open.v1",
             {{"path", missing_multi_project.string()}, {"discard", false}}});
        if (!opened.accepted)
            throw std::runtime_error(
                "open project with multiple missing dependencies: " +
                opened.error);
        const auto rejected = recovery_application.execute(
            {"project.relink-prefix.v1",
             {{"missing_prefix", old_assets.string()},
              {"replacement_prefix", (relocated / "invalid-assets").string()}}});
        check(!rejected.accepted &&
                  recovery_application.snapshot().missing_assets.size() == 2,
              "failed semantic recovery leaves every reference unchanged");
        check(recovery_application
                  .execute({"project.relink-prefix.v1",
                            {{"missing_prefix", old_assets.string()},
                             {"replacement_prefix", repaired_assets.string()}}})
                  .accepted,
              "prepared semantic multi-asset recovery");
        std::array<float, 128> recovery_left{}, recovery_right{};
        recovery_engine.process(recovery_left, recovery_right);
        const auto recovered_state = recovery_application.snapshot();
        const auto recovered_three =
            recovery_manager.status(SampleSlotId{3});
        const auto recovered_four = recovery_manager.status(SampleSlotId{4});
        if (!recovered_state.missing_assets.empty() ||
            !recovered_three.has_sample || !recovered_four.has_sample)
            throw std::runtime_error(
                "multi-asset recovery acknowledgement: missing=" +
                std::to_string(recovered_state.missing_assets.size()) +
                " slot3=" + std::to_string(recovered_three.has_sample) +
                " slot4=" + std::to_string(recovered_four.has_sample) +
                " error=" + recovered_state.project_error);
    }

    const auto action_project = relocated / "action-project";
    AudioEngine engine;
    SampleManager manager(engine);
    TimelineService service(
        [&](const Timeline &timeline, std::string &publish_error) {
            return engine.queue_timeline(timeline, publish_error);
        },
        Timeline{});
    ApplicationService application(engine, service, manager);
    check(application.execute({"project.new.v1", {{"name", "Actions"}}})
                  .accepted &&
              application.snapshot().project_dirty,
          "new project action and dirty state");
    check(application
                  .execute({"project.save-as.v1",
                            {{"path", action_project.string()},
                             {"name", std::string("Actions")}}})
                  .accepted &&
              !application.snapshot().project_dirty,
          "save-as action and clean state");
    GuiModel project_gui(application);
    for (int i = 0; i < 5; ++i)
        project_gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200);
    const auto &project_frame = project_gui.frame(1920, 1200);
    check(std::any_of(project_frame.commands.begin(),
                      project_frame.commands.end(),
                      [](const auto &draw) {
                          return draw.type == DrawCommand::Type::text &&
                                 draw.text.find("PROJECT: Actions") !=
                                     std::string::npos;
                      }),
          "GUI project status");
    check(application
                  .execute({"parameter.set-normalized.v1",
                            {{"parameter_id", std::string("synth.cutoff")},
                             {"value", 0.25}}})
                  .accepted &&
              application.snapshot().project_dirty,
          "project parameter dirty tracking");
    check(!application
                  .execute(
                      {"project.open.v1", {{"path", action_project.string()}}})
                  .accepted &&
              application.snapshot().project_dirty,
          "dirty project open lacked unsaved-work protection");
    check(application
                  .execute(
                      {"project.open.v1",
                       {{"path", action_project.string()}, {"discard", true}}})
                  .accepted &&
              !application.snapshot().project_dirty &&
              application.snapshot().project_name == "Actions",
          "project open action");
    manager.shutdown();
    std::filesystem::remove_all(action_project, ignored);

    {
        std::ofstream bad(relocated / "manifest.ngp", std::ios::trunc);
        bad << "NEUROGENIC_PROJECT 999\n";
    }
    bool rejected{};
    try {
        (void)load_project(relocated);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "forward project version accepted");
    std::filesystem::remove_all(relocated, ignored);
}

void test_audio_recording_and_resampling() {
    using namespace neurogenic;
    AudioEngine engine;
    SampleManager samples(engine);
    RecordingService recorder(engine.sample_rate(), 32);
    engine.set_recording_service(&recorder);
    engine.set_sample(constant_sample(256, 0.25F));
    std::string error;
    RecordingConfiguration configuration;
    configuration.source = RecordingSource::master_pre_effects;
    configuration.destination = SampleSlotId{9};
    configuration.channels = 2;
    configuration.fixed_frames = 16;
    check(recorder.arm(configuration, error) &&
              recorder.start(engine.callback_frame_position(), error),
          "arm internal resampling");
    engine.trigger(36, 1.0F);
    std::array<float, 32> left{}, right{};
    engine.process(left, right);
    auto status = recorder.status();
    check(status.state == RecordingState::completed &&
              status.captured_frames == 16 && status.peak_left > 0 &&
              status.overflow_count == 0,
          "pre-effects internal resampling");
    check(recorder.finalize_to_slot(samples, error),
          "recorded sample finalization");
    std::array<float, 0> no_audio{};
    engine.process(no_audio, no_audio);
    const auto recorded = samples.status(SampleSlotId{9});
    check(recorded.has_sample && recorded.frames == 16 &&
              recorded.channels == 2,
          "recorded sample publication");

    recorder.reset();
    check(engine.set_delay_parameter(DelayParameter::mix, 1.0F),
          "queue delay for post-effects capture");
    engine.process(no_audio, no_audio);
    configuration.source = RecordingSource::master_post_effects;
    configuration.destination = SampleSlotId{13};
    configuration.fixed_frames = 4;
    check(recorder.arm(configuration, error) &&
              recorder.start(engine.callback_frame_position(), error),
          "arm post-effects resampling");
    engine.trigger(36, 1.0F);
    engine.process(left, right);
    const auto post_effects = recorder.completed_sample();
    check(post_effects.frames() == 4 &&
              recorder.status().configuration.source ==
                  RecordingSource::master_post_effects,
          "post-effects recording tap");

    const auto root =
        std::filesystem::temp_directory_path() / "neurogenic-recording-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto pcm_path = root / "capture-pcm16.wav";
    const auto float_path = root / "capture-float.wav";
    check(
        recorder.save_wav(pcm_path, RecordingWavFormat::pcm16, error) &&
            recorder.save_wav(float_path, RecordingWavFormat::float32, error) &&
            decode_wav(pcm_path).frames() == 4 &&
            decode_wav(float_path).frames() == 4,
        "recording WAV export");

    RecordingService external(engine.sample_rate(), 8);
    engine.set_recording_service(&external);
    configuration = {};
    configuration.source = RecordingSource::external_stereo;
    configuration.destination = SampleSlotId{10};
    configuration.channels = 2;
    configuration.monitoring = RecordingMonitorMode::on;
    configuration.fixed_frames = 4;
    check(external.arm(configuration, error) &&
              external.start(engine.callback_frame_position(), error),
          "arm simulated stereo input");
    const std::array<float, 8> stereo_input{0.1F, -0.1F, 0.2F, -0.2F,
                                            0.3F, -0.3F, 0.4F, -0.4F};
    std::array<float, 4> duplex_left{}, duplex_right{};
    engine.process_duplex(duplex_left, duplex_right, stereo_input, 2);
    const auto external_sample = external.completed_sample();
    check(external_sample.channels == 2 && external_sample.frames() == 4 &&
              std::fabs(external_sample.interleaved[2] - 0.2F) < 0.0001F &&
              duplex_left[0] != 0,
          "simulated stereo input and monitoring");

    external.reset();
    configuration.source = RecordingSource::external_mono;
    configuration.destination = SampleSlotId{11};
    configuration.channels = 1;
    configuration.fixed_frames = 0;
    check(external.arm(configuration, error) &&
              external.start(engine.callback_frame_position(), error),
          "arm simulated mono input");
    const std::array<float, 12> too_long{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    engine.process_duplex(duplex_left, duplex_right, too_long, 1);
    status = external.status();
    check(status.state == RecordingState::completed &&
              status.captured_frames == 8 && status.dropped_frames == 4 &&
              status.overflow_count == 1,
          "bounded recording overflow");
    external.reset();
    check(external.arm(configuration, error), "rearm recording");
    external.cancel();
    check(external.status().state == RecordingState::canceled &&
              external.status().captured_frames == 0,
          "recording cancellation");
    external.reset();
    check(external.arm(configuration, error), "arm before device failure");
    external.device_stopped();
    check(external.status().state == RecordingState::error &&
              !external.status().error.empty(),
          "recording device failure");

    engine.set_recording_service(nullptr);
    AudioEngine application_engine;
    SampleManager application_samples(application_engine);
    Timeline timeline;
    TimelineService timeline_service(
        [&](const Timeline &value, std::string &publish_error) {
            return application_engine.queue_timeline(value, publish_error);
        },
        timeline);
    ApplicationService application(application_engine, timeline_service,
                                   application_samples);
    const auto recording_revision = application.snapshot().revision;
    check(application
                  .execute({"recording.source-select.v1",
                            {{"source", std::string("master-post")}}})
                  .accepted &&
              application
                  .execute({"recording.destination-select.v1",
                            {{"slot_id", std::int64_t{12}}}})
                  .accepted,
          "recording semantic configuration actions");
    const auto configured_recording = application.snapshot();
    check(configured_recording.revision > recording_revision &&
              configured_recording.recording.configuration.destination ==
                  SampleSlotId{12},
          "recording configuration produces observable revisioned state");
    RecordingConfiguration app_configuration;
    app_configuration.source = RecordingSource::master_post_effects;
    app_configuration.destination = SampleSlotId{12};
    app_configuration.channels = 2;
    app_configuration.fixed_frames = 8;
    check(application.recording().arm(app_configuration, error) &&
              application.recording().start(
                  application_engine.callback_frame_position(), error),
          "application recording arm");
    application_engine.trigger_synth(60, 1.0F);
    application_engine.process(left, right);
    check(application.snapshot().recording.state ==
              RecordingState::publication_pending,
          "application recording completion publication");
    application_engine.process(no_audio, no_audio);
    (void)application.snapshot();
    check(application
                  .execute(
                      {"recording.assign-pad.v1", {{"pad", std::int64_t{2}}}})
                  .accepted &&
              application.slices()
                  .snapshot(SampleSlotId{12})
                  .pad_assignments[2]
                  .has_value(),
          "recording pad assignment");
    const auto project_path = root / "recorded-project";
    check(application
              .execute({"project.save-as.v1",
                        {{"path", project_path.string()},
                         {"name", std::string("Recorded")}}})
              .accepted,
          "recorded project save");
    const auto restored_project = load_project(project_path);
    check(restored_project.samples.sample_paths.contains(12) &&
              std::filesystem::exists(
                  restored_project.samples.sample_paths.at(12)) &&
              restored_project.samples.pads[2].has_value(),
          "recorded project asset restoration");
    application.recording().reset();
    check(application
                  .execute({"recording.quantization-set.v1",
                            {{"quantization", std::string("beat")}}})
                  .accepted &&
              application
                  .execute({"recording.fixed-bars-set.v1",
                            {{"bars", std::int64_t{1}}}})
                  .accepted,
          "quantized recording configuration");
    (void)application.execute({"transport.stop.v1", {}});
    application_engine.process(no_audio, no_audio);
    check(!application.execute({"recording.arm.v1", {}}).accepted,
          "quantized recording armed while transport stopped");
    (void)application.execute({"transport.play.v1", {}});
    application_engine.process(no_audio, no_audio);
    check(application.execute({"recording.arm.v1", {}}).accepted &&
              application.execute({"recording.start.v1", {}}).accepted,
          "quantized recording actions");
    const auto quantized = application.recording().status();
    check(quantized.configuration.fixed_frames == 96000,
          "fixed bar recording length");
    const auto frames_to_boundary =
        quantized.configuration.start_frame -
        application_engine.callback_frame_position();
    std::vector<float> before_boundary(frames_to_boundary),
        before_boundary_right(frames_to_boundary);
    application_engine.process(before_boundary, before_boundary_right);
    check(application.recording().status().state == RecordingState::armed,
          "quantized recording started before boundary");
    std::array<float, 4> boundary_left{}, boundary_right{};
    application_engine.process(boundary_left, boundary_right);
    check(application.recording().status().state == RecordingState::recording,
          "quantized recording boundary start");
    check(application.execute({"recording.stop.v1", {}}).accepted,
          "quantized recording stop action");
    std::vector<float> stop_boundary(24001), stop_boundary_right(24001);
    application_engine.process(stop_boundary, stop_boundary_right);
    check(application.snapshot().recording.state ==
              RecordingState::publication_pending,
          "manual recording stop completion");
    GuiModel gui(application);
    for (int i = 0; i < 6; ++i)
        gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200);
    const auto &frame = gui.frame(1920, 1200);
    check(std::any_of(frame.commands.begin(), frame.commands.end(),
                      [](const auto &draw) {
                          return draw.type == DrawCommand::Type::text &&
                                 draw.text.find("AUDIO RECORDING") !=
                                     std::string::npos;
                      }),
          "recording GUI state");
    const auto prior_slot =
        application.snapshot().recording.configuration.destination.value;
    check(
        gui.input({GuiInput::Type::key_down, GuiKey::up}, 1920, 1200) &&
            application.snapshot().recording.configuration.destination.value ==
                prior_slot + 1,
        "encoder-only recording destination navigation");
    check(gui.input({GuiInput::Type::pointer_down, GuiKey{}, 0,
                     frame.layout.rows[0].x + 2, frame.layout.rows[0].y + 2},
                    1920, 1200),
          "pointer recording start");
    gui.input({GuiInput::Type::key_down, GuiKey::escape}, 1920, 1200);
    for (const auto size : {std::pair{1024, 800}, std::pair{1024, 600}}) {
        const auto &compact = gui.frame(size.first, size.second);
        check(std::any_of(compact.commands.begin(), compact.commands.end(),
                          [](const auto &draw) {
                              return draw.type == DrawCommand::Type::text &&
                                     draw.text.find("AUDIO RECORDING") !=
                                         std::string::npos;
                          }),
              "compact recording GUI layout");
    }
    application_samples.shutdown();
    samples.shutdown();
    std::filesystem::remove_all(root, ignored);
}

void test_transport_integration_and_timeline_io() {
    using namespace neurogenic;
    std::string error;
    Timeline timeline;
    Track track;
    track.id = 1;
    track.pattern.length = 3840;
    track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1, 1920, 0, 0});
    timeline.tracks.push_back(track);
    AudioEngine transport(48000, 4);
    transport.set_sample_slot(SampleSlotId{0}, constant_sample(100000));
    check(transport.queue_timeline(timeline, error),
          "transport timeline queue");
    std::array<float, 4> left{}, right{};
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "transport did not initially play");
    check(transport.frame_position() == 1 && transport.playing(),
          "initial transport state");
    transport.stop();
    transport.process({left.data(), 2}, {right.data(), 2});
    near(left[0], 0, "stop did not clean voices");
    check(transport.frame_position() == 1 && !transport.playing(),
          "stopped transport advanced");
    transport.play();
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "play unexpectedly restarted timeline");
    check(transport.frame_position() == 2 && transport.playing(),
          "play did not resume");
    transport.restart();
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "restart did not return to tick zero");
    check(transport.frame_position() == 1, "restart frame position");
    AudioEngine command_overflow;
    for (int i = 0; i < 20; ++i)
        command_overflow.play();
    check(command_overflow.dropped_transport_commands() == 5,
          "transport command overflow counter");

    Timeline replacement;
    Track muted;
    muted.id = 2;
    muted.muted = true;
    muted.pattern.length = 960;
    replacement.tracks.push_back(muted);
    check(transport.queue_timeline(replacement, error), "replacement queue");
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "timeline replacement did not clean active notes");

    Timeline crossing;
    Track short_loop;
    short_loop.id = 8;
    short_loop.pattern.length = 480;
    short_loop.pattern.events.push_back(
        {99, 0, MusicalEventType::note, 36, 1, 720, 0, 0});
    crossing.tracks.push_back(short_loop);
    PlaybackPlan crossing_plan;
    check(compile_timeline(crossing, crossing_plan, error),
          "cross-loop duration compilation");
    TimelineScheduler crossing_scheduler(48000);
    crossing_scheduler.set_plan(&crossing_plan, 0);
    std::array<TimelineScheduledEvent, 16> scheduled{};
    const auto crossing_count =
        crossing_scheduler.schedule(0, 30001, scheduled);
    check(crossing_count == 5 &&
              scheduled[0].type == MusicalEventType::note_on &&
              scheduled[1].frame == 12000 &&
              scheduled[2].type == MusicalEventType::note_off &&
              scheduled[2].frame == 18000,
          "cross-loop note scheduling");
    Timeline tiny;
    Track tiny_track;
    tiny_track.id = 1;
    tiny_track.pattern.length = 1;
    tiny_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    tiny.tracks.push_back(tiny_track);
    PlaybackPlan tiny_plan;
    check(compile_timeline(tiny, tiny_plan, error),
          "one-tick loop compilation");
    TimelineScheduler tiny_scheduler(48000);
    tiny_scheduler.set_plan(&tiny_plan, 0);
    check(tiny_scheduler.schedule(0, 100, scheduled) == 4,
          "one-tick loop scheduling");

    AudioEngine edges(48000, 2);
    edges.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline edge_timeline;
    Track edge_track;
    edge_track.id = 5;
    edge_track.pattern.length = 960;
    edge_track.pattern.events = {
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0},
        {2, 1, MusicalEventType::note_off, 36, 0, 0, 0, 0, 1}};
    edge_timeline.tracks.push_back(edge_track);
    check(edges.queue_timeline(edge_timeline, error),
          "explicit note edges queue");
    std::array<float, 26> edge_left{}, edge_right{};
    edges.process(edge_left, edge_right);
    near(edge_left[0], 0.8F, "explicit note-on missing");
    near(edge_left[25], 0, "explicit note-off did not match target");
    auto invalid_edges = edge_timeline;
    invalid_edges.tracks[0].pattern.events[1].target_event_id = 0;
    PlaybackPlan invalid_edge_plan;
    check(!compile_timeline(invalid_edges, invalid_edge_plan, error),
          "untargeted explicit note-off accepted");

    AudioEngine stolen(48000, 1);
    stolen.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline stolen_timeline;
    Track stolen_track;
    stolen_track.id = 1;
    stolen_track.pattern.length = 960;
    stolen_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 0.5F, 1, 0, 0},
        {2, 0, MusicalEventType::note, 36, 1, 2, 0, 0}};
    stolen_timeline.tracks.push_back(stolen_track);
    check(stolen.queue_timeline(stolen_timeline, error),
          "stolen voice timeline queue");
    stolen.process(edge_left, edge_right);
    near(edge_left[25], 0.8F,
         "note-off for stolen voice stopped unrelated owner");

    AudioEngine overflow(48000, 4, 2);
    overflow.set_sample_slot(SampleSlotId{0}, constant_sample(2));
    Timeline burst;
    Track burst_track;
    burst_track.id = 1;
    burst_track.pattern.length = 960;
    for (std::uint64_t id = 1; id <= 5; ++id)
        burst_track.pattern.events.push_back(
            {id, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    burst.tracks.push_back(burst_track);
    check(overflow.queue_timeline(burst, error),
          "staging overflow timeline queue");
    overflow.process(left, right);
    check(overflow.dropped_timeline_events() == 1,
          "timeline staging deferral counter");
    overflow.process(left, right);
    overflow.process(left, right);
    check(overflow.dropped_timeline_events() == 2, "dense backlog diagnostics");

    AudioEngine cleanup(48000, 2, 1);
    cleanup.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline cleanup_timeline;
    Track cleanup_track;
    cleanup_track.id = 1;
    cleanup_track.pattern.length = 960;
    cleanup_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1, 1, 0, 0});
    cleanup_timeline.tracks.push_back(cleanup_track);
    check(cleanup.queue_timeline(cleanup_timeline, error),
          "cleanup overflow queue");
    cleanup.process(edge_left, edge_right);
    near(edge_left[25], 0.8F, "deferred note-off ended voice without delivery");
    cleanup.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "deferred note-off left a stuck voice");

    AudioEngine shared(48000, 8);
    shared.set_sample_slot(SampleSlotId{0}, constant_sample(2, 0.25F));
    shared.set_sample_slot(SampleSlotId{1}, constant_sample(2, 0.5F));
    Timeline slow;
    slow.transport.microseconds_per_quarter = 1'000'000;
    Track slow_track;
    slow_track.id = 1;
    slow_track.pattern.length = 3840;
    slow_track.pattern.events.push_back(
        {1, 960, MusicalEventType::note_on, 37, 1, 0, 0, 0});
    slow.tracks.push_back(slow_track);
    Pattern legacy;
    legacy.steps[4] = {true, 36, 1};
    legacy.revision = 1;
    check(shared.queue_pattern(legacy) && shared.queue_timeline(slow, error),
          "shared transport setup");
    std::vector<float> before_left(48000), before_right(48000);
    shared.process(before_left, before_right);
    near(before_left.back(), 0, "shared transport events arrived early");
    shared.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F,
         "legacy and timeline workflows do not share tempo/position");

    AudioEngine tempo_change(48000, 2);
    tempo_change.set_sample_slot(SampleSlotId{0}, constant_sample(8));
    Timeline empty;
    Track empty_track;
    empty_track.id = 1;
    empty_track.pattern.length = 960;
    empty.tracks.push_back(empty_track);
    check(tempo_change.queue_timeline(empty, error), "initial tempo plan");
    std::vector<float> advance_left(12000), advance_right(12000);
    tempo_change.process(advance_left, advance_right);
    Timeline changed;
    changed.transport.microseconds_per_quarter = 1'000'000;
    Track changed_track;
    changed_track.id = 1;
    changed_track.pattern.length = 960;
    changed_track.pattern.events.push_back(
        {1, 240, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    changed.tracks.push_back(changed_track);
    check(tempo_change.queue_timeline(changed, error), "tempo transition plan");
    tempo_change.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "tempo transition did not use authoritative position");

    Timeline large;
    Track large_track;
    large_track.id = 1;
    large_track.pattern.length = std::numeric_limits<MusicalTick>::max();
    large_track.pattern.events.push_back(
        {1, std::numeric_limits<MusicalTick>::max() - 1,
         MusicalEventType::note_on, 36, 1, 0, 0, 0});
    large.tracks.push_back(large_track);
    PlaybackPlan large_plan;
    check(compile_timeline(large, large_plan, error),
          "large-position compilation");
    TimelineScheduler large_scheduler(192000);
    large_scheduler.set_plan(&large_plan, 0);
    check(large_scheduler.frame_at_tick(
              std::numeric_limits<MusicalTick>::max() - 1) ==
              std::numeric_limits<std::uint64_t>::max(),
          "large position did not saturate safely");

    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-timeline-test.ngt";
    save_timeline(timeline, path);
    auto loaded = load_timeline(path);
    std::filesystem::remove(path);
    check(loaded.tracks.size() == 1 &&
              loaded.tracks[0].pattern.events[0].duration == 1920 &&
              loaded.tracks[0].id == 1,
          "timeline file round trip");
    const auto invalid_path = std::filesystem::temp_directory_path() /
                              "neurogenic-invalid-timeline.ngt";
    {
        std::ofstream invalid(invalid_path);
        invalid << "NEUROGENIC_TIMELINE 99\n";
    }
    bool rejected = false;
    try {
        (void)load_timeline(invalid_path);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    std::filesystem::remove(invalid_path);
    check(rejected, "unsupported timeline version accepted");
    const auto legacy_path = std::filesystem::temp_directory_path() /
                             "neurogenic-legacy-note-off.ngt";
    {
        std::ofstream legacy_file(legacy_path);
        legacy_file
            << "NEUROGENIC_TIMELINE 1\ntransport 500000 4 4\ntrack 1 960 0 "
               "\"legacy\"\nnote_on 1 10 0 36 1\nnote_off 1 11 120 36 0\n";
    }
    auto legacy_loaded = load_timeline(legacy_path);
    std::filesystem::remove(legacy_path);
    check(legacy_loaded.tracks[0].pattern.events[1].target_event_id == 10,
          "legacy note-off target resolution");
    const auto singleton_path = std::filesystem::temp_directory_path() /
                                "neurogenic-v2-synth-instance.ngt";
    {
        std::ofstream legacy(singleton_path);
        legacy << "NEUROGENIC_TIMELINE 2\ntransport 500000 4 4\nseed 0\n"
                  "track 9 960 0 \"legacy synth\" \"synth.subtractive.v1\"\n"
                  "note 9 1 0 60 1 240 1 0 1 1 1\n";
    }
    const auto singleton = load_timeline(singleton_path);
    std::filesystem::remove(singleton_path);
    check(singleton.tracks[0].instrument_instance_id ==
              default_synth_instance_id,
          "legacy synth did not migrate to shared instance 1");
}

void test_midi_control_dispatch() {
    using namespace neurogenic;
    MidiMapper mapper;
    std::atomic<int> notes{}, actions{};
    std::thread::id action_thread;
    mapper.set_note_handler([&](auto, float) { ++notes; });
    mapper.map_button(0, 12, "test", [&] {
        action_thread = std::this_thread::get_id();
        ++actions;
    });
    check(mapper.receive({0x90, 12, 1, 0}), "MIDI enqueue");
    check(actions == 0, "action ran in MIDI callback");
    check(mapper.dispatch_pending() == 1 && notes == 1 && actions == 1,
          "deterministic MIDI dispatch");
    check(mapper.receive({0x80, 12, 1, 0}) && mapper.dispatch_pending() == 1 &&
              notes == 1 && actions == 1,
          "MIDI note-off semantic dispatch");

    MidiMapper threaded;
    std::promise<void> delivered;
    auto ready = delivered.get_future();
    std::thread::id worker_id;
    threaded.map_button(0, 13, "threaded", [&] {
        worker_id = std::this_thread::get_id();
        delivered.set_value();
    });
    ControlDispatcher dispatcher(threaded);
    dispatcher.start();
    check(threaded.receive({0x90, 13, 127, 0}), "threaded MIDI enqueue");
    ready.get();
    dispatcher.stop();
    check(worker_id != std::this_thread::get_id(),
          "action did not run on control thread");

    MidiMapper pending;
    std::atomic<int> drained{};
    pending.map_button(0, 14, "drain", [&] { ++drained; });
    for (int i = 0; i < 20; ++i)
        check(pending.receive({0x90, 14, 1, 0}), "pending MIDI enqueue");
    ControlDispatcher shutdown(pending);
    shutdown.start();
    shutdown.stop();
    check(drained == 20, "shutdown did not drain pending events");

    MidiMapper full;
    for (int i = 0; i < 300; ++i)
        full.receive({0x90, 15, 1, 0});
    check(full.dropped_messages() == 44, "MIDI queue overflow count");
    check(full.dispatch_pending() == 256, "MIDI queue capacity");
}

void test_timeline_editing_and_generation() {
    using namespace neurogenic;
    std::string error;
    Timeline published;
    std::size_t publications{};
    TimelineService service([&](const Timeline &timeline, std::string &) {
        published = timeline;
        ++publications;
        return true;
    });
    auto edit = service.edit();
    check(edit.create_track(1, "drums", 960, error), "track creation");
    check(edit.create_track(2, "hats", 720, error),
          "independent track creation");
    const auto first_id = edit.next_event_id();
    check(edit.add_note(1, first_id, 13, 36, 0.8F, 120, error),
          "note insertion");
    check(edit.modify_note(1, first_id, 17, 38, 0.5F, 180, error),
          "note modification");
    const auto parameter_id = edit.next_event_id();
    check(edit.add_parameter(1, parameter_id, 120, 42, 0.25F, error) &&
              edit.modify_parameter(1, parameter_id, 240, 42, 0.5F, error),
          "parameter event editing");
    check(edit.add_note(2, edit.next_event_id(), 0, 42, 0.7F, 60, error),
          "second-track insertion");
    check(edit.set_track_muted(2, true, error), "track mute");
    check(edit.set_tempo_bpm(123, error) &&
              edit.set_time_signature(7, 8, error),
          "transport editing");
    edit.set_generation_seed(77);
    check(service.commit(std::move(edit),
                         TimelineCommitPolicy::next_audio_callback, error),
          "timeline transaction commit");
    auto snapshot = service.snapshot();
    check(snapshot.revision == 2 && snapshot.generation_seed == 77 &&
              snapshot.timeline.tracks.size() == 2 &&
              snapshot.timeline.tracks[0].pattern.events[0].id == first_id &&
              snapshot.timeline.tracks[1].pattern.length == 720,
          "committed timeline state");
    check(publications == 1 && published.tracks.size() == 2,
          "timeline publication callback");

    auto invalid = service.edit();
    check(!invalid.set_track_length(1, 10, error),
          "invalid shortening accepted");
    check(service.snapshot().revision == 2 &&
              service.snapshot().timeline.tracks[0].pattern.length == 960,
          "invalid edit escaped working copy");
    auto winner = service.edit(), stale = service.edit();
    check(winner.set_track_name(1, "winner", error) &&
              service.commit(std::move(winner),
                             TimelineCommitPolicy::next_audio_callback, error),
          "winning concurrent edit");
    check(stale.set_track_name(1, "stale", error) &&
              !service.commit(std::move(stale),
                              TimelineCommitPolicy::next_audio_callback,
                              error) &&
              error.find("conflict") != std::string::npos,
          "stale edit silently overwrote winner");
    auto removal = service.edit();
    check(removal.remove_event(1, first_id, error) &&
              removal.remove_event(1, parameter_id, error) &&
              removal.delete_track(2, error) &&
              service.commit(std::move(removal),
                             TimelineCommitPolicy::next_audio_callback, error),
          "event and track deletion");
    check(service.snapshot().timeline.tracks.size() == 1 &&
              service.snapshot().timeline.tracks[0].pattern.events.empty(),
          "deletion state");

    Timeline reusable_timeline;
    reusable_timeline.tracks.push_back({11, "one", false, {960, {}}});
    reusable_timeline.tracks.push_back({12, "two", false, {960, {}}});
    TimelineService reusable_service(
        [](const Timeline &, std::string &) { return true; }, reusable_timeline);
    auto reusable = reusable_service.edit();
    check(reusable.create_pattern(101, "A", 960, error) &&
              reusable.create_pattern(102, "B", 960, error) &&
              reusable.add_note(101, 11, reusable.next_event_id(), 0, 60,
                                1.0F, 120, error) &&
              reusable.add_note(102, 11, reusable.next_event_id(), 0, 67,
                                1.0F, 120, error) &&
              reusable.add_note(101, 12, reusable.next_event_id(), 240, 72,
                                0.5F, 120, error),
          "reusable pattern-track editing");
    const auto *pattern_a_track_one = reusable.find_edit_pattern(101, 11);
    const auto *pattern_a_track_two = reusable.find_edit_pattern(101, 12);
    const auto *pattern_b_track_one = reusable.find_edit_pattern(102, 11);
    check(pattern_a_track_one && pattern_a_track_two && pattern_b_track_one &&
              pattern_a_track_one->events[0].pitch == 60 &&
              pattern_b_track_one->events[0].pitch == 67 &&
              pattern_a_track_two->events[0].pitch == 72,
          "pattern and track edit isolation");
    check(reusable.duplicate_pattern(101, 103, "A Copy", error),
          "reusable pattern duplication");
    check(reusable.modify_note(103, 11,
              reusable.find_edit_pattern(103, 11)->events[0].id,
              0, 62, 1.0F, 120, error) &&
              reusable.find_edit_pattern(101, 11)->events[0].pitch == 60,
          "duplicated reusable pattern is independently mutable");
    check(!reusable.delete_track(11, error) &&
              error.find("referenced") != std::string::npos,
          "track deletion ignored reusable pattern references");

    MusicalPattern pattern;
    pattern.length = 960;
    pattern.events = {{1, 13, MusicalEventType::note, 60, 0.5F, 120, 0, 0, 0},
                      {2, 240, MusicalEventType::note, 64, 1.0F, 120, 0, 0, 0}};
    check(quantize(pattern, 120, error) && pattern.events[0].tick == 0,
          "quantization boundary");
    check(rotate(pattern, 120, error) && pattern.events[0].tick == 120,
          "pattern rotation");
    check(reverse(pattern, error) && pattern.events[0].tick == 840,
          "pattern reversal");
    check(transpose(pattern, 12, error) && pattern.events[0].pitch == 72,
          "transpose");
    check(scale_velocities(pattern, 0.5, error) &&
              std::fabs(pattern.events[1].velocity - 0.5F) < 0.0001F,
          "velocity scaling");
    EventId next = 3;
    const std::array<MusicalTick, 1> offsets{240};
    check(duplicate(pattern, offsets, next, error) &&
              pattern.events.size() == 4 && pattern.events[2].id == 3,
          "event duplication IDs");
    DeterministicRandom random_a(12345), random_b(12345);
    for (int i = 0; i < 100; ++i)
        check(random_a.next_u64() == random_b.next_u64(),
              "deterministic random stream");
    auto rhythm = euclidean_rhythm(8, 3, 0, error);
    check(rhythm.size() == 8 && rhythm[0] && rhythm[3] && rhythm[6],
          "Euclidean rhythm");
    check(!euclidean_rhythm(8, 9, 0, error).size(),
          "invalid Euclidean rhythm accepted");
    DeterministicRandom human_a(9), human_b(9);
    auto first = pattern, second = pattern;
    check(humanize(first, human_a, 12, 0.1, error) &&
              humanize(second, human_b, 12, 0.1, error) &&
              first.events[0].tick == second.events[0].tick &&
              first.events[0].velocity == second.events[0].velocity,
          "deterministic humanize");

    AudioEngine engine;
    TimelineService live([&](const Timeline &timeline, std::string &message) {
        return engine.queue_timeline(timeline, message);
    });
    for (int i = 0; i < 50; ++i) {
        auto transaction = live.edit();
        if (transaction.timeline().tracks.empty())
            check(transaction.create_track(1, "live", 960, error),
                  "live track creation");
        check(transaction.set_track_name(1, "revision-" + std::to_string(i),
                                         error),
              "rapid edit");
        check(live.commit(std::move(transaction),
                          TimelineCommitPolicy::next_audio_callback, error),
              "rapid live commit");
        std::array<float, 1> left{}, right{};
        engine.process(left, right);
    }
    check(live.snapshot().revision == 51, "rapid commit revision");
}

void test_application_control_and_controllers() {
    using namespace neurogenic;
    std::string error;
    ActionRegistry actions;
    check(actions.find("transport.play.v1") &&
              actions.find("track.mute-toggle.v1") &&
              !actions.find("transport.play.v1")->description.empty(),
          "action discovery");
    check(actions.find("edit.undo.v1")->available &&
              actions.find("edit.redo.v1")->available,
          "history actions unavailable");
    check(actions.validate("track.select.v1", {{"track_id", std::int64_t{1}}},
                           error),
          "typed action validation");
    check(!actions.validate("track.select.v1",
                            {{"track_id", std::string("bad")}}, error),
          "invalid action argument accepted");
    check(actions.validate("edit.undo.v1", {}, error),
          "undo action validation");

    ParameterRegistry parameters;
    ParameterDescriptor integer{"test.integer",
                                "Integer",
                                "units",
                                ParameterType::integer,
                                std::int64_t{0},
                                std::int64_t{10},
                                std::int64_t{5},
                                std::int64_t{5},
                                2,
                                false,
                                true,
                                false,
                                {}};
    check(parameters.register_parameter(integer, error),
          "parameter registration");
    check(parameters.adjust("test.integer", 2, error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 9,
          "relative encoder adjustment");
    check(parameters.set("test.integer", std::int64_t{10}, error) &&
              parameters.reset("test.integer", error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 5,
          "parameter set/reset");
    check(!parameters.set("test.integer", std::int64_t{11}, error),
          "out-of-range parameter accepted");
    check(parameters.set_normalized("test.integer", 0.5, error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 5,
          "absolute normalized parameter mapping");
    ParameterDescriptor enumeration{"test.mode",
                                    "Mode",
                                    "",
                                    ParameterType::enumeration,
                                    std::int64_t{0},
                                    std::int64_t{2},
                                    std::int64_t{0},
                                    std::int64_t{0},
                                    1,
                                    false,
                                    false,
                                    false,
                                    {"a", "b", "c"}};
    check(parameters.register_parameter(enumeration, error),
          "enumeration registration");

    std::ifstream profile_file(std::filesystem::path(NEUROGENIC_SOURCE_DIR) /
                               "examples/controllers/generic-midi.ngc");
    std::string profile_source((std::istreambuf_iterator<char>(profile_file)),
                               {});
    ControllerProfile parsed;
    check(parse_controller_profile(profile_source, parsed, error) &&
              validate_profile(parsed, actions, error),
          error.c_str());
    check(!parse_controller_profile("controller|bad|Bad\ncontrol|x|mystery|X\n",
                                    parsed, error),
          "invalid profile parsed");
    auto full = full_performance_profile(),
         minimal = minimal_controller_profile(), midi = generic_midi_profile();
    check(validate_profile(full, actions, error) &&
              validate_profile(minimal, actions, error) &&
              validate_profile(midi, actions, error),
          "built-in controller profile validation");
    check(full.capabilities.controls.size() >= 36 &&
              minimal.capabilities.controls.size() >= 4,
          "controller capabilities missing");
    ControllerMapper midi_mapper(midi, actions, error);
    check(midi_mapper.valid(), error.c_str());
    auto absolute =
        midi_mapper.resolve({"fader.1", 0.75, 0, true, 1}, {}, error);
    check(absolute.parameter_value &&
              std::fabs(std::get<double>(*absolute.parameter_value) - 0.75) <
                  0.0001,
          "absolute fader mapping");
    auto relative =
        midi_mapper.resolve({"encoder.1", -2, 0, true, 2}, {}, error);
    check(relative.parameter_id == "transport.tempo" &&
              relative.relative_steps == -2,
          "relative encoder mapping");
    ControllerProfile shifted;
    shifted.capabilities = {
        "shift.test",
        "Shift test",
        {{"button", "Button", ControlType::momentary_button, {}}},
        false};
    ControlBinding base;
    base.control_id = "button";
    base.action_id = "transport.play.v1";
    ControlBinding layer = base;
    layer.action_id = "transport.stop.v1";
    layer.shift = true;
    shifted.bindings = {base, layer};
    ControllerMapper shifted_mapper(shifted, actions, error);
    check(shifted_mapper.resolve({"button", 1, 0, true, 1}, {}, error)
                  .action->action_id == "transport.play.v1",
          "base layer resolution");
    ModifierState shift;
    shift.shift = true;
    check(shifted_mapper.resolve({"button", 1, 0, true, 2}, shift, error)
                  .action->action_id == "transport.stop.v1",
          "shift layer resolution");
    shifted.bindings = {base};
    ControllerMapper fallback_mapper(shifted, actions, error);
    check(fallback_mapper.resolve({"button", 1, 0, true, 3}, shift, error)
                  .action->action_id == "transport.play.v1",
          "unbound shift fallback");
    shifted.fallback_to_base = false;
    ControllerMapper strict_mapper(shifted, actions, error);
    check(
        !strict_mapper.resolve({"button", 1, 0, true, 4}, shift, error).action,
        "disabled shift fallback");

    PerformanceGrid grid;
    grid.set_step_page(3);
    check(grid.press(2).type == GridEventType::toggle_step &&
              grid.press(2).index == 50,
          "paged step toggle");
    check(grid.press(2, true).type == GridEventType::select_step,
          "Select-modified step semantics");
    const auto grid_command =
        grid_application_command(grid.press(2), "grid", 9);
    check(grid_command && grid_command->action_id == "step.toggle.v1" &&
              std::get<std::int64_t>(grid_command->arguments.at("step")) == 50,
          "grid application command");
    grid.set_mode(PadMode::pad);
    check(grid.press(4).type == GridEventType::trigger_pad &&
              grid.press(4).slot == SampleSlotId{4},
          "pad semantics");
    const auto pad_command = grid_application_command(grid.press(4));
    check(pad_command && pad_command->action_id == "pad.trigger.v1" &&
              std::get<std::int64_t>(pad_command->arguments.at("pad")) == 4,
          "PAD event did not resolve semantic trigger");
    grid.set_mode(PadMode::levels);
    grid.set_selected_slot(SampleSlotId{9});
    check(grid.press(15).type == GridEventType::trigger_level &&
              grid.press(15).velocity == 127 &&
              grid.press(15).slot == SampleSlotId{9},
          "16-level semantics");
    grid.set_mode(PadMode::notes);
    grid.set_root_note(60);
    check(grid.press(15).note == 36 && grid.press(15).pitch_offset == 15,
          "16-note semantics");
    grid.set_root_note(48);
    check(grid.press(0).pitch_offset == -12,
          "16-note root/octave control semantics");
    grid.set_mode(PadMode::step);
    grid.set_step_page(3);
    const auto select_command = grid_application_command(grid.press(2, true));
    check(select_command && select_command->action_id == "step.event-select.v1",
          "grid Select did not resolve event selection");

    TextEntryModel text;
    text.begin();
    for (char target : std::string("Hi")) {
        while (text.state().selected != target)
            text.select_relative(1);
        text.insert();
    }
    text.commit();
    check(text.state().committed && text.state().text == "Hi",
          "encoder-only text entry");
    text.begin("Hxy");
    text.move_cursor(-1);
    text.erase_or_cancel();
    text.direct("e");
    text.commit();
    check(text.state().text == "Hey", "text cursor/delete/direct editing");
    text.begin("discard");
    text.discard();
    check(text.state().discarded, "text discard");
    XYControl xy({true, false, false, true, false, 0.5, 2});
    auto position = xy.apply(0.0, 0.75);
    check(std::fabs(position.x - 0.75) < 0.0001 && position.y == 1.0,
          "X/Y normalization and mapping");
    xy.release();
    check(xy.value().x == 0.5 && xy.value().y == 0.5, "X/Y spring return");

    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{1, "one", false, {960, {}}},
                      {2, "two", false, {720, {}}}};
    TimelineService timeline(
        [&](const Timeline &value, std::string &message) {
            return engine.queue_timeline(value, message);
        },
        initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 64);
    check(application
              .execute({"macro.create.v1",
                        {{"name", std::string("Live")},
                         {"owner", std::string("project")},
                         {"value", 0.25}},
                        "test",
                        1})
              .accepted,
          "macro create semantic action");
    const auto created_macro =
        application.snapshot().performance_macros.macros.front().id;
    check(application
              .execute({"macro.assignment-set.v1",
                        {{"macro_id", static_cast<std::int64_t>(created_macro)},
                         {"destination", std::string("effect.delay.mix")},
                         {"minimum", 0.0},
                         {"maximum", 1.0},
                         {"curve", std::string("logarithmic")}},
                        "test",
                        2})
              .accepted &&
              application.snapshot()
                      .performance_macros.macros.front()
                      .assignments.size() == 1,
          "macro assignment semantic action and discovery state");
    check(application
              .execute({"macro.value-set.v1",
                        {{"id", static_cast<std::int64_t>(created_macro)},
                         {"value", 0.8}},
                        "test",
                        3})
              .accepted &&
              std::fabs(application.snapshot()
                            .performance_macros.macros.front()
                            .value -
                        0.8F) < 0.0001F,
          "macro value semantic action");
    check(application
              .execute({"macro.scene-capture.v1",
                        {{"name", std::string("Snapshot")},
                         {"curve", std::string("s-curve")}},
                        "test",
                        4})
              .accepted,
          "capture macro scene action");
    check(application
              .execute({"macro.value-set.v1",
                        {{"id", static_cast<std::int64_t>(created_macro)},
                         {"value", 0.2}},
                        "test",
                        5})
              .accepted,
          "move macro before scene recall");
    check(application
              .execute({"macro.scene-recall.v1",
                        {{"id", std::int64_t{1}}, {"duration", 0.0}},
                        "test",
                        6})
              .accepted,
          "schedule immediate scene recall action");
    std::array<float, 64> macro_left{}, macro_right{};
    engine.process(macro_left, macro_right);
    const auto macro_state = application.snapshot().performance_macros;
    check(!macro_state.effective_values.empty() &&
              std::fabs(macro_state.effective_values.front().second - 0.8F) <
                  0.0001F &&
              !macro_state.active_morphs.empty() &&
              macro_state.active_morphs.front().completed,
          "immediate scene recall state acknowledgement");
    auto selection = application.execute(
        {"track.select.v1", {{"track_id", std::int64_t{2}}}, "test", 1});
    check(selection.accepted && application.snapshot().selected_track == 2,
          "track selection command");
    auto mute = application.execute({"track.mute-toggle.v1", {}, "test", 2});
    check(mute.accepted && timeline.snapshot().timeline.tracks[1].muted,
          "track mute command");
    auto mode = application.execute(
        {"pad.mode-select.v1", {{"mode", std::string("notes")}}, "test", 3});
    check(mode.accepted && application.snapshot().pad_mode == PadMode::notes,
          "mode switch");
    auto focus_before = application.snapshot().revision;
    check(application.execute({"navigation.focus-next.v1", {}, "test", 4})
                  .accepted &&
              application.snapshot().revision > focus_before,
          "focus navigation revision");
    check(application
              .execute({"modulation.xy.v1",
                        {{"position", NormalizedXY{0.25, 0.8}}},
                        "test",
                        5})
              .accepted,
          "X/Y application command");
    check(std::fabs(std::get<double>(application.parameters()
                                         .get("performance.xy.y")
                                         ->current_value) -
                    0.8) < 0.0001,
          "X/Y parameter state");
    check(application.execute({"edit.undo.v1", {}, "test", 6}).accepted &&
              !timeline.snapshot().timeline.tracks[1].muted,
          "application undo action");
    check(application.execute({"edit.redo.v1"}).accepted &&
              timeline.snapshot().timeline.tracks[1].muted,
          "application redo action");
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
        producers.emplace_back([&application, producer] {
            for (int i = 0; i < 10; ++i)
                check(application.submit(
                          {"step.select.v1",
                           {{"step", std::int64_t{producer * 10 + i}}},
                           "thread",
                           static_cast<std::uint64_t>(i)}),
                      "concurrent command submission");
        });
    for (auto &thread : producers)
        thread.join();
    const auto results = application.dispatch_pending();
    check(results.size() == 40 &&
              std::is_sorted(results.begin(), results.end(),
                             [](const auto &a, const auto &b) {
                                 return a.sequence < b.sequence;
                             }),
          "deterministic concurrent command ordering");
    const auto state = application.snapshot();
    check(state.revision >= 44 &&
              state.timeline_revision == timeline.snapshot().revision &&
              !state.available_actions.empty(),
          "application state revision consistency");
    ControllerMapper full_mapper(full, actions, error);
    const auto feedback = full_mapper.feedback(state, "controller.input");
    check(!feedback.empty() &&
              feedback.front().state_revision == state.revision,
          "controller feedback generation");
    const auto filtered = full_mapper.feedback(state, "led.play");
    check(std::none_of(
              filtered.begin(), filtered.end(),
              [](const auto &item) { return item.destination == "led.play"; }),
          "feedback loop suppression");
    ApplicationService bounded(engine, timeline, samples, 2);
    check(bounded.submit({"navigation.confirm.v1"}) &&
              bounded.submit({"navigation.cancel.v1"}) &&
              !bounded.submit({"navigation.focus-next.v1"}) &&
              bounded.rejected_commands() == 1,
          "application queue backpressure");
    samples.shutdown();
    check(std::any_of(feedback.begin(), feedback.end(),
                      [](const auto &item) {
                          return item.destination == "parameter.display.0" &&
                                 std::holds_alternative<double>(item.value);
                      }),
          "parameter value feedback");
}

void test_physical_controller_protocol() {
    using namespace neurogenic;
    ControllerPacket source{ControllerMessageType::encoder, 42, {3, 0, 7, 0}};
    const auto encoded = encode_controller_packet(source);
    check(!encoded.empty() && encoded.size() <= controller_maximum_frame,
          "controller packet encoding");
    ControllerFrameDecoder decoder;
    decoder.push(std::span<const std::uint8_t>(encoded.data(), 5));
    check(!decoder.pop(), "partial controller frame accepted");
    decoder.push(
        std::span<const std::uint8_t>(encoded.data() + 5, encoded.size() - 5));
    const auto decoded = decoder.pop();
    check(decoded && decoded->type == source.type &&
              decoded->sequence == source.sequence &&
              decoded->payload == source.payload,
          "controller framing round-trip");
    auto corrupt = encoded;
    corrupt[10] ^= 0x40;
    decoder.push(corrupt);
    check(!decoder.pop() && decoder.invalid_frames() > 0,
          "controller CRC rejection");

    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{1, "controller", false, {3840, {}}}};
    TimelineService timeline(
        [&](const Timeline &value, std::string &message) {
            return engine.queue_timeline(value, message);
        },
        initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 32);
    ControllerHost host(application, 64);
    SimulatedController simulator(host);
    check(simulator.negotiate("test-controller") &&
              host.snapshot().connection == ControllerConnection::connected,
          "controller protocol negotiation");
    check(host.snapshot().protocol_minor == 2 &&
              (host.snapshot().capabilities & controller_inventory_068) &&
              !(host.snapshot().capabilities & controller_analog),
          "controller 1.2 inventory capabilities");
    const auto dedicated_profile = full_performance_profile();
    check(std::none_of(dedicated_profile.capabilities.controls.begin(),
                       dedicated_profile.capabilities.controls.end(),
                       [](const auto &control) {
                           return control.type == ControlType::fader ||
                                  control.type == ControlType::crossfader ||
                                  control.id == "data.push";
                       }),
          "dedicated controller has no analog or encoder-push assumptions");
    check(!simulator.analog(0, 32768),
          "unavailable dedicated-controller analog input rejected");
    std::string protocol_error;
    check(!host.receive(
              {ControllerMessageType::capabilities, 2, {0x3f, 0, 0, 0}},
              protocol_error),
          "controller capability inventory cannot be expanded by a profile");
    check(simulator.negotiate("test-controller"),
          "controller renegotiation after incompatible capabilities");
    check(simulator.button(0, true), "controller grid event");
    const auto grid_results = application.dispatch_pending();
    check(!grid_results.empty() && grid_results.front().accepted,
          "controller grid command acceptance");
    check(!timeline.snapshot().timeline.tracks[0].pattern.events.empty(),
          "controller semantic step dispatch");
    check(application.execute({"pad.mode-select.v1", {{"mode", std::string("pad")}}})
              .accepted,
          "controller-independent mode action");
    check(application.snapshot().pad_mode == PadMode::pad,
          "controller mode switching");
    host.publish(application.snapshot());
    const auto initial_feedback = simulator.drain_feedback();
    check(host.set_analog_calibration(1, {1000, 64000, 256}, protocol_error) &&
              !host.set_analog_calibration(7, {0, 0, 0}, protocol_error),
          "controller analog calibration validation");
    check(simulator.encoder(0, 3) && host.snapshot().encoders[0] == 3 &&
              host.snapshot().analog[0] == 0,
          "controller raw encoder diagnostics without analog controls");
    host.publish(application.snapshot());
    const auto feedback = initial_feedback;
    check(std::any_of(feedback.begin(), feedback.end(),
                      [](const auto &packet) {
                          return packet.type == ControllerMessageType::led;
                      }) &&
              host.snapshot().feedback_revision ==
                  application.snapshot().revision,
          "controller LED/context state synchronization");
    check(std::any_of(feedback.begin(), feedback.end(),
                      [](const auto &packet) {
                          return packet.type ==
                                     ControllerMessageType::context &&
                                 packet.payload.size() > 20;
                      }),
          "controller 1.1 extended contextual feedback");
    host.publish(application.snapshot());
    check(simulator.drain_feedback().empty(),
          "controller redundant feedback suppression");
    const auto sequencer_context =
        build_controller_context(application.snapshot());
    check(sequencer_context[0].parameter_id == "transport.tempo" &&
              sequencer_context[0].active &&
              sequencer_context[0].maximum > sequencer_context[0].minimum,
          "controller explicit contextual parameter mapping");
    const auto sequencer_page_context = resolve_controller_context(
        application.snapshot(), "sequencer", {});
    check(sequencer_page_context[8].label == "PAGE-" &&
              sequencer_page_context[8].action_id ==
                  "step.page-previous.v1" &&
              sequencer_page_context[9].label == "PAGE+" &&
              sequencer_page_context[9].action_id == "step.page-next.v1" &&
              sequencer_page_context[10].action_id ==
                  "step.trigger-reset.v1" &&
              sequencer_page_context[14].action_id ==
                  "pattern.previous.v1" &&
              sequencer_page_context[15].action_id == "pattern.next.v1",
          "controller sequencer paging and edit context");
    check(simulator.button(63, true),
          "controller shift press");
    (void)application.dispatch_pending();
    check(!application.snapshot().modifiers.shift,
          "controller Shift remains session-local");
    std::array<float, 1> controller_left{}, controller_right{};
    engine.process(controller_left, controller_right);
    ModifierState local_shift;
    local_shift.shift = true;
    const auto shifted_context = resolve_controller_context(
        application.snapshot(), "sequencer", local_shift);
    check(shifted_context[8].label == "UNDO" &&
              shifted_context[9].label == "REDO",
          "controller shift contextual layer");
    const auto tempo_before = std::get<double>(
        application.parameters().get("transport.tempo")->current_value);
    check(simulator.encoder(0, 1), "controller fine encoder input");
    const auto fine_results = application.dispatch_pending();
    check(!fine_results.empty() && fine_results.back().accepted,
          fine_results.empty() ? "controller fine command missing"
                               : fine_results.back().error.c_str());
    engine.process(controller_left, controller_right);
    const auto tempo_fine = std::get<double>(
        application.parameters().get("transport.tempo")->current_value);
    check(tempo_fine > tempo_before && tempo_fine - tempo_before < 1.0,
          "controller Shift fine adjustment");
    check(simulator.button(63, false),
          "controller shift release");
    (void)application.dispatch_pending();
    check(simulator.encoder(0, 4), "controller accelerated encoder input");
    (void)application.dispatch_pending();
    const auto tempo_coarse = std::get<double>(
        application.parameters().get("transport.tempo")->current_value);
    check(tempo_coarse - tempo_fine > 0.4, "controller coarse acceleration");
    for (std::uint16_t id = 36; id <= 69; ++id)
        check(simulator.button(id, true) && simulator.button(id, false),
              "controller Task 068 inventory event");
    (void)application.dispatch_pending();
    check(simulator.button(50, true) && simulator.button(50, false) &&
              simulator.button(999, true) && simulator.button(999, false),
          "unassigned and unknown controller buttons are harmless");
    host.set_brightness_limit(32);
    host.publish(application.snapshot());
    const auto dim_feedback = simulator.drain_feedback();
    check(std::all_of(dim_feedback.begin(), dim_feedback.end(),
                      [](const auto &packet) {
                          return packet.type != ControllerMessageType::led ||
                                 (!packet.payload.empty() &&
                                  packet.payload.back() <= 32);
                      }),
          "controller RGB brightness limit");
    host.disconnected("test");
    check(host.snapshot().connection == ControllerConnection::disconnected,
          "controller disconnect lifecycle");
    check(simulator.negotiate("test-controller"),
          "controller reconnect negotiation");
    host.publish(application.snapshot());
    const auto reconnect_feedback = simulator.drain_feedback();
    check(std::count_if(reconnect_feedback.begin(), reconnect_feedback.end(),
                        [](const auto &p) {
                            return p.type == ControllerMessageType::led;
                        }) >= 16,
          "controller reconnect full feedback refresh");

    ControllerHost incompatible(application);
    incompatible.connected();
    check(!incompatible.receive(
              {ControllerMessageType::hello_reply,
               1,
               {static_cast<std::uint8_t>(controller_protocol_major + 1), 0, 3,
                1, 'b', 'a', 'd', 'x'}},
              protocol_error) &&
              incompatible.snapshot().connection ==
                  ControllerConnection::incompatible,
          "controller major-version mismatch");
    ControllerHost wrong_identity(application, 128, "expected-controller");
    SimulatedController wrong_simulator(wrong_identity);
    check(!wrong_simulator.negotiate("different-controller") &&
              wrong_identity.snapshot().connection ==
                  ControllerConnection::incompatible,
          "controller identity mismatch");
    ControllerHost legacy(application);
    SimulatedController legacy_simulator(legacy);
    check(legacy_simulator.negotiate("legacy", 0),
          "controller protocol 1.0 minor compatibility");
    legacy.publish(application.snapshot());
    const auto legacy_feedback = legacy_simulator.drain_feedback();
    check(std::any_of(legacy_feedback.begin(), legacy_feedback.end(),
                      [](const auto &p) {
                          return p.type == ControllerMessageType::context &&
                                 p.payload.size() <= 40;
                      }),
          "legacy contextual feedback payload");
    ControllerHost version_11(application);
    SimulatedController simulator_11(version_11);
    check(simulator_11.negotiate("version-1.1", 1) &&
              version_11.snapshot().connection ==
                  ControllerConnection::connected,
          "controller protocol 1.1 minor compatibility");

    ControllerHost bounded(application, 1);
    SimulatedController bounded_simulator(bounded);
    check(bounded_simulator.negotiate("bounded"),
          "bounded controller negotiation");
    bounded.publish(application.snapshot());
    check(bounded.snapshot().dropped_output > 0,
          "controller bounded feedback queue overflow");
    for (std::size_t pass = 0; pass < 256 &&
         bounded.snapshot().feedback_revision != application.snapshot().revision;
         ++pass) {
        (void)bounded_simulator.drain_feedback();
        bounded.publish(application.snapshot());
    }
    (void)bounded_simulator.drain_feedback();
    bounded.publish(application.snapshot());
    check(bounded.snapshot().feedback_revision == application.snapshot().revision,
          "controller transport backpressure eventually fully resynchronizes");
    check(controller_protocol_json(host.snapshot())
                      .find("neurogenic.controller-protocol.v1") !=
                  std::string::npos &&
              controller_protocol_json(host.snapshot()).find("\"pickup\"") !=
                  std::string::npos &&
              controller_protocol_json(host.snapshot()).find("\"context\"") !=
                  std::string::npos,
          "controller diagnostic JSON");
    GuiModel gui(application);
    const auto &frame = gui.frame(1920, 1200);
    check(std::any_of(frame.commands.begin(), frame.commands.end(),
                      [](const auto &command) {
                          return command.type == DrawCommand::Type::text &&
                                 command.text == "PAGE-";
                      }),
          "GUI/controller contextual label consistency");
    check(gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200),
          "GUI controller-context navigation");
    const auto sample_state = application.snapshot();
    const auto sample_context = build_controller_context(sample_state);
    const auto &sample_frame = gui.frame(1920, 1200);
    check(sample_state.controller_context == "samples" &&
              sample_context[0].parameter_id == "sample.pitch" &&
              std::any_of(
                  sample_frame.commands.begin(), sample_frame.commands.end(),
                  [&](const auto &command) {
                      return command.type == DrawCommand::Type::text &&
                             command.text.find(sample_context[0].label) !=
                                 std::string::npos;
                  }),
          "GUI and physical controller shared context model");
}

void test_controller_agnostic_sessions() {
    using namespace neurogenic;
    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{1, "controller sessions", false, {3840, {}}}};
    TimelineService timeline(
        [&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 64);
    std::string error;

    auto descriptor = rp2040_controller_descriptor();
    check(validate_device_descriptor(descriptor, error),
          "RP2040 device descriptor validation");
    check(descriptor.controls.size() == 67 &&
              std::none_of(descriptor.controls.begin(), descriptor.controls.end(),
                           [](const auto &control) {
                               return control.type == ControlType::fader ||
                                      control.type == ControlType::crossfader ||
                                      control.id == "data.push";
                           }),
          "RP2040 descriptor inventory");
    auto profile = full_performance_profile();
    auto session_descriptor = descriptor;
    ControllerSession first(application, session_descriptor, profile, "first", 64);
    ControllerSession second(application, session_descriptor, profile, "second", 64);
    check(first.valid() && second.valid(), "controller sessions valid");
    check(first.input({"page.4", 1, 0, true, 1}, error) &&
              first.state().context == "mixer" &&
              second.state().context == "sequencer",
          "controller contexts are session-local");
    check(first.input({"performance.9", 1, 0, true, 2}, error) &&
              first.state().modifiers.shift && !second.state().modifiers.shift,
          "controller modifiers are session-local");
    check(first.input({"edit.0", 1, 0, true, 3}, error),
          "explicit unassigned control is harmless");
    check(!first.input({"missing", 1, 0, true, 4}, error) && !error.empty(),
          "unknown controller control rejected");

    profile.bindings.front().invert = true;
    profile.bindings.front().fine_scale = 0.025;
    profile.bindings.front().acceleration = 4.0;
    const auto serialized = serialize_controller_profile(profile);
    ControllerProfile reparsed;
    check(parse_controller_profile(serialized, reparsed, error) &&
              validate_profile(reparsed, application.actions(), error) &&
              reparsed.profile_id == profile.profile_id &&
              reparsed.bindings.front().invert &&
              std::fabs(reparsed.bindings.front().fine_scale - 0.025) < 0.0001 &&
              std::fabs(reparsed.bindings.front().acceleration - 4.0) < 0.0001 &&
              std::any_of(reparsed.bindings.begin(), reparsed.bindings.end(),
                          [](const auto &binding) {
                              return binding.control_id == "performance.0" &&
                                     binding.action_id == "transport.play.v1";
                          }),
          "controller profile deterministic round-trip");
    ControllerProfile malformed;
    check(!parse_controller_profile(
              "controller|bad|Bad\ncontrol|x|encoder|X\n"
              "bind|x|semantic|context-encoder|index=oops\n",
              malformed, error),
          "malformed controller profile rejected without exception");
    auto missing_arguments = generic_midi_profile();
    missing_arguments.bindings.push_back(
        {"button.1", "midi.clock-source-set.v1"});
    check(!validate_profile(missing_arguments, application.actions(), error) &&
              error.find("bound arguments") != std::string::npos,
          "controller action mappings reject missing required arguments");
    missing_arguments.bindings.back().arguments["source"] =
        std::string("external");
    check(validate_profile(missing_arguments, application.actions(), error),
          "controller action mappings accept validated bound arguments");

    auto midi_profile = generic_midi_profile();
    midi_profile.profile_id = "generic-midi-test";
    ControllerDeviceDescriptor midi_descriptor;
    midi_descriptor.descriptor_id = "test.midi.v1";
    midi_descriptor.device_id = "test-midi";
    midi_descriptor.display_name = "Test MIDI surface";
    midi_descriptor.transport = ControllerTransport::midi;
    midi_descriptor.controls = midi_profile.capabilities.controls;
    ControllerSession midi_session(application, midi_descriptor, midi_profile,
                                   "midi-session", 32);
    check(midi_session.valid(), "generic MIDI controller session");
    MidiControlSurfaceAdapter midi(midi_session);
    check(midi.add({2, 0, 20, MidiControlMode::cc_absolute, "knob.1"}, error) &&
              midi.add({2, 0, 21,
                        MidiControlMode::cc_relative_binary_offset,
                        "encoder.1"}, error) &&
              midi.add({2, 0, 60, MidiControlMode::note_momentary,
                        "button.1"}, error),
          "generic MIDI bindings");
    check(midi.dispatch({0xb0, 20, 100, 0, 2, 3}, error),
          "MIDI absolute control dispatch");
    check(midi.dispatch({0xb0, 21, 65, 0, 2, 3}, error),
          "MIDI relative control dispatch");
    check(midi.dispatch({0x90, 60, 127, 0, 2, 3}, error),
          "MIDI momentary control dispatch");
    const auto results = application.dispatch_pending();
    check(results.size() == 3 &&
              std::all_of(results.begin(), results.end(),
                          [](const auto &result) { return result.accepted; }),
          "generic MIDI semantic actions accepted");
    check(!midi.dispatch({0xb0, 20, 100, 0, 9, 3}, error),
          "unmatched MIDI port remains musical-routing independent");
    check(midi.begin_learn("encoder.1",
                           MidiControlMode::cc_relative_twos_complement,
                           std::chrono::seconds(1), error) &&
              midi.dispatch({0xb1, 74, 1, 0, 3, 3}, error) &&
              midi.learn_status().state == MidiLearnState::learned &&
              midi.learn_status().learned->port == 3 &&
              midi.learn_status().learned->channel == 1,
          "MIDI learn captures explicit port/channel/message identity");
    check(midi.bindings().size() == 3 && midi.confirm_learn(error) &&
              midi.bindings().size() == 4,
          "MIDI learn changes mappings only after explicit confirmation");
    check(midi.begin_learn("button.1", MidiControlMode::note_momentary,
                           std::chrono::seconds(1), error),
          "MIDI learn restart");
    midi.cancel_learn();
    check(midi.learn_status().state == MidiLearnState::cancelled &&
              !midi.dispatch({0x90, 61, 127, 0, 3, 3}, error),
          "cancelled MIDI learn preserves musical input");
    check(midi.begin_learn("button.1", MidiControlMode::note_momentary,
                           std::chrono::milliseconds(1), error),
          "MIDI learn timeout arm");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    midi.update_learn_timeout();
    check(midi.learn_status().state == MidiLearnState::timed_out,
          "MIDI learn timeout leaves profile unchanged");

    MidiControllerProfile stored{midi_profile, "Exact MIDI Port",
                                 midi.bindings()};
    const auto profile_path = std::filesystem::temp_directory_path() /
                              "neurogenic-controller-task070.ngc";
    MidiControllerProfile restored;
    check(save_midi_controller_profile(profile_path, stored, error) &&
              load_midi_controller_profile(profile_path, restored, error) &&
              restored.port_selector == stored.port_selector &&
              restored.midi == stored.midi &&
              serialize_midi_controller_profile(restored) ==
                  serialize_midi_controller_profile(stored),
          "MIDI controller profile persistence");
    std::filesystem::remove(profile_path);

    const auto legacy_modes =
        serialize_controller_profile(midi_profile) +
        "midi-device|Exact MIDI Port\n"
        "midi|1|0|1|absolute|knob.1\n"
        "midi|1|0|2|relative-twos|encoder.1\n"
        "midi|1|0|3|relative-offset|encoder.1\n";
    MidiControllerProfile legacy_mode_profile;
    check(parse_midi_controller_profile(legacy_modes, legacy_mode_profile,
                                        error) &&
              legacy_mode_profile.midi.size() == 3 &&
              serialize_midi_controller_profile(legacy_mode_profile).find(
                  "cc-relative-2c") != std::string::npos,
          "legacy MIDI mode aliases parse and serialize canonically");
    check(!parse_midi_controller_profile(
              serialize_controller_profile(midi_profile) +
                  "midi|1|0|1|mystery|knob.1\n",
              legacy_mode_profile, error) &&
              error.find("expected note") != std::string::npos,
          "unknown MIDI mode reports accepted values");
    check(!parse_midi_controller_profile(
              serialize_controller_profile(midi_profile) +
                  "midi|1|0|1|cc-absolute|knob.1|surprise\n",
              legacy_mode_profile, error) &&
              error.find("option") != std::string::npos,
          "unknown MIDI mapping option is rejected actionably");
    check(!parse_midi_controller_profile(
              serialize_controller_profile(midi_profile) +
                  "midi|9|0|1|cc-absolute|knob.1\n",
              legacy_mode_profile, error),
          "out-of-range logical MIDI port is rejected");
    for (const auto &name : {"minimal.ngc", "generic-midi.ngc",
                             "full-performance.ngc",
                             "generic-midi-surface.ngc"}) {
        std::ifstream example(std::filesystem::path(NEUROGENIC_SOURCE_DIR) /
                              "examples/controllers" / name);
        std::ostringstream contents;
        contents << example.rdbuf();
        MidiControllerProfile parsed_example;
        check(example && parse_midi_controller_profile(
                             contents.str(), parsed_example, error) &&
                  validate_profile(parsed_example.controller,
                                   application.actions(), error),
              "shipped controller example parses and validates");
    }

    first.observe(application.snapshot(), true);
    std::size_t feedback_count{};
    while (first.next_feedback()) ++feedback_count;
    check(feedback_count > 0, "capability-aware feedback generated");
    const auto unchanged_revision = application.snapshot().revision;
    first.set_context("samples");
    first.observe(application.snapshot());
    bool saw_local_context{};
    while (auto value = first.next_feedback())
        saw_local_context |= value->kind == ControllerFeedbackKind::context;
    check(saw_local_context && application.snapshot().revision == unchanged_revision,
          "controller-local context invalidates feedback independently");
    ControllerSession bounded(application, session_descriptor, profile,
                              "bounded-session", 1);
    bounded.observe(application.snapshot(), true);
    check(bounded.state().dropped_feedback > 0 &&
              bounded.state().resync_required,
          "controller feedback overflow requests resynchronization");
    for (std::size_t pass = 0; pass < 128 && bounded.needs_feedback(); ++pass) {
        (void)bounded.next_feedback();
        bounded.observe(application.snapshot());
    }
    while (bounded.next_feedback()) {}
    bounded.observe(application.snapshot());
    check(!bounded.needs_feedback(),
          "controller feedback overflow eventually fully resynchronizes");

    auto invalid_profile = profile;
    invalid_profile.capabilities.controls.push_back(
        {"invented.fader", "Invented", ControlType::fader, {}, 0, 1, true});
    ControllerSession invalid(application, descriptor, invalid_profile,
                              "invalid-profile", 16);
    check(!invalid.valid(),
          "mapping profile cannot add controls absent from device inventory");

    ControllerCoordinator coordinator(application);
    auto feedback_profile = midi_profile;
    auto feedback_control = std::find_if(
        feedback_profile.capabilities.controls.begin(),
        feedback_profile.capabilities.controls.end(),
        [](const auto &control) { return control.id == "button.1"; });
    feedback_control->feedback = {FeedbackType::single_led};
    auto feedback_binding = std::find_if(
        feedback_profile.bindings.begin(), feedback_profile.bindings.end(),
        [](const auto &binding) { return binding.control_id == "button.1"; });
    feedback_binding->feedback_id = "button.1";
    MidiControllerProfile coordinated{feedback_profile, "Exact MIDI Port", {}};
    coordinated.midi.push_back(
        {4, 0, 22, MidiControlMode::cc_relative_binary_offset, "encoder.1"});
    coordinated.midi.push_back(
        {4, 0, 60, MidiControlMode::note_momentary, "button.1", true});
    MidiMapper coordinated_mapper;
    check(coordinator.add_midi("midi-one", coordinated, coordinated_mapper,
                               error) &&
              coordinator.states().size() == 1,
          "running coordinator owns an independent MIDI controller session");
    coordinator.poll(application.snapshot());
    check(coordinator.dispatch_midi_feedback(
              [](const MidiMessage &) { return false; }) == 0,
          "failed MIDI feedback send retains pending feedback");
    check(coordinator.dispatch_midi_feedback(
              [](const MidiMessage &) { return true; }) > 0,
          "MIDI feedback converges after transport send recovery");
    bool reentered{};
    coordinator.reconnect_midi();
    coordinator.poll(application.snapshot());
    check(coordinator.dispatch_midi_feedback([&](const MidiMessage &) {
              reentered = !coordinator.states().empty();
              return true;
          }) > 0 && reentered,
          "feedback sender can reenter coordinator without global-lock deadlock");
    coordinator.reconnect_midi();
    check(coordinator.states().front().session.resync_required,
          "MIDI controller reconnect requests a full feedback refresh");
    coordinator.poll(application.snapshot());
    check(coordinator.dispatch_midi_feedback(
              [](const MidiMessage &) { return true; }) > 0,
          "MIDI reconnect regenerates authoritative feedback without state change");
    coordinator.disconnect_midi();
    coordinator.reconnect_midi();
    coordinator.poll(application.snapshot());
    check(coordinator.dispatch_midi_feedback(
              [](const MidiMessage &) { return true; }) > 0,
          "disconnect during pending feedback converges after reconnect");
    coordinator.shutdown();

    ControllerCoordinator shutdown_during_send(application);
    MidiMapper shutdown_mapper;
    check(shutdown_during_send.add_midi("shutdown-send", coordinated,
                                        shutdown_mapper, error),
          "feedback shutdown endpoint configured");
    shutdown_during_send.poll(application.snapshot());
    bool shutdown_reentered{};
    (void)shutdown_during_send.dispatch_midi_feedback(
        [&](const MidiMessage &) {
            shutdown_reentered = true;
            shutdown_during_send.shutdown();
            return false;
        });
    check(shutdown_reentered && shutdown_during_send.states().empty(),
          "shutdown during failed external feedback is reentrant and deterministic");

    std::size_t raw_after_endpoint_destruction{};
    MidiMapper lifetime_mapper;
    lifetime_mapper.set_raw_handler(
        [&](const MidiMessage &) { ++raw_after_endpoint_destruction; });
    {
        ControllerCoordinator lifetime_coordinator(application);
        check(lifetime_coordinator.add_midi("temporary", coordinated,
                                            lifetime_mapper, error),
              "temporary MIDI surface registration");
    }
    check(lifetime_mapper.receive({0xb0, 99, 1, 0, 4, 3}) &&
              lifetime_mapper.dispatch_one() &&
              raw_after_endpoint_destruction == 1,
          "destroyed MIDI adapter is unregistered before further dispatch");
    {
        ControllerCoordinator failed_coordinator(application);
        auto invalid = coordinated;
        invalid.midi.push_back(
            {4, 0, 61, MidiControlMode::note_momentary, "missing.control"});
        check(!failed_coordinator.add_midi("invalid", invalid, lifetime_mapper,
                                           error),
              "partial MIDI endpoint initialization fails safely");
    }
    check(lifetime_mapper.receive({0xb0, 98, 1, 0, 4, 3}) &&
              lifetime_mapper.dispatch_one() &&
              raw_after_endpoint_destruction == 2,
          "failed MIDI endpoint leaves no mapper registration");
    {
        ControllerCoordinator reverse_lifetime(application);
        {
            MidiMapper shorter_lived_mapper;
            check(reverse_lifetime.add_midi("reverse-lifetime", coordinated,
                                            shorter_lived_mapper, error),
                  "MIDI endpoint accepts explicit mapper lifetime token");
        }
        reverse_lifetime.shutdown();
    }
}

void test_interactive_step_sequencer() {
    using namespace neurogenic;
    std::string error;
    bool publish = true;
    std::size_t publications{};
    Timeline initial;
    initial.tracks = {
        {10,
         "drums",
         false,
         {4000,
          {{7, 100, MusicalEventType::note, 50, 0.5F, 90, 0, 0, 0},
           {8, 240, MusicalEventType::parameter, 60, 1, 0, 99, 0.25F, 0}}}},
        {20,
         "other",
         false,
         {1920, {{9, 0, MusicalEventType::note, 40, 1, 120, 0, 0, 0}}}}};
    AudioEngine engine;
    TimelineService timeline(
        [&](const Timeline &value, std::string &message) {
            ++publications;
            if (!publish) {
                message = "publication rejected";
                return false;
            }
            return engine.queue_timeline(value, message);
        },
        initial);
    SampleManager samples(engine);
    ApplicationService app(engine, timeline, samples);
    auto state = app.snapshot();
    check(state.step_resolution == StepResolution::sixteenth &&
              state.step_ticks == 240 && state.tracks[0].target_note == 36 &&
              state.tracks[1].target_note == 37,
          "default resolution and per-track targets");
    const std::array<std::pair<std::string_view, MusicalTick>, 8> resolutions{
        {{"1/4", 960},
         {"1/8", 480},
         {"1/16", 240},
         {"1/32", 120},
         {"1/4T", 640},
         {"1/8T", 320},
         {"1/16T", 160},
         {"1/32T", 80}}};
    for (const auto &[name, ticks] : resolutions) {
        const auto parsed = parse_step_resolution(name);
        check(parsed && step_resolution_ticks(*parsed) == ticks &&
                  name == step_resolution_name(*parsed),
              "step resolution conversion");
    }
    const auto before_revision = timeline.snapshot().revision;
    check(
        app.execute({"step.select.v1", {{"step", std::int64_t{5}}}}).accepted &&
            timeline.snapshot().revision == before_revision,
        "selection modified timeline");
    check(app.execute({"step.toggle.v1",
                       {{"step", std::int64_t{4}},
                        {"velocity", 0.7},
                        {"duration_ticks", std::int64_t{180}}}})
              .accepted,
          "step insertion");
    auto edited = timeline.snapshot();
    const auto inserted = std::find_if(
        edited.timeline.tracks[0].pattern.events.begin(),
        edited.timeline.tracks[0].pattern.events.end(), [](const auto &event) {
            return event.tick == 960 && event.pitch == 36;
        });
    check(inserted != edited.timeline.tracks[0].pattern.events.end() &&
              inserted->velocity == 0.7F && inserted->duration == 180,
          "step note values");
    check(edited.timeline.tracks[0].pattern.events[0].id == 7 &&
              edited.timeline.tracks[1].pattern.events[0].id == 9,
          "unrelated stable event IDs");
    check(app.execute({"step.toggle.v1", {{"step", std::int64_t{4}}}}).accepted,
          "step removal");
    edited = timeline.snapshot();
    check(std::none_of(edited.timeline.tracks[0].pattern.events.begin(),
                       edited.timeline.tracks[0].pattern.events.end(),
                       [](const auto &event) {
                           return event.tick == 960 && event.pitch == 36;
                       }),
          "repeated toggle removal");
    check(
        app.execute({"track.note-target-set.v1", {{"note", std::int64_t{42}}}})
                .accepted &&
            app.snapshot().tracks[0].target_note == 42,
        "note target selection");
    check(app.execute({"step.resolution-set.v1",
                       {{"resolution", std::string("1/8T")}}})
                  .accepted &&
              app.snapshot().step_ticks == 320,
          "triplet resolution");
    check(
        app.execute({"step.toggle.v1", {{"step", std::int64_t{12}}}}).accepted,
        "partial final grid position");
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{13}}}}).accepted,
        "out-of-pattern step accepted");
    check(app.execute({"step.resolution-set.v1",
                       {{"resolution", std::string("1/32")}}})
              .accepted,
          "32nd resolution");
    check(app.execute({"step.page-next.v1"}).accepted &&
              app.snapshot().step_page == 1,
          "step page navigation");
    check(app.execute({"track.select.v1", {{"track_id", std::int64_t{20}}}})
                  .accepted &&
              app.snapshot().tracks[1].target_note == 37,
          "independent track target");
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{16}}}}).accepted,
        "independent track length bound");
    publish = false;
    const auto failed = timeline.snapshot();
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}}).accepted,
        "publication failure accepted");
    check(timeline.snapshot().revision == failed.revision &&
              timeline.snapshot().timeline.tracks[1].pattern.events.size() ==
                  failed.timeline.tracks[1].pattern.events.size(),
          "publication failure changed timeline");
    publish = true;
    {
        std::array<float, 37> left{}, right{};
        for (int i = 0; i < 8; ++i)
            engine.process(left, right);
    }
    engine.play();
    for (int step = 0; step < 8; ++step) {
        check(app.execute({"step.toggle.v1", {{"step", std::int64_t{step}}}})
                  .accepted,
              "rapid live step edit");
        std::array<float, 37> left{}, right{};
        engine.process(left, right);
    }
    check(publications >= 11 && engine.playing(),
          "live editing interrupted transport");
    ActionRegistry actions;
    ControllerMapper minimal(minimal_controller_profile(), actions, error);
    auto enter = minimal.resolve({"enter", 1, 0, true, 1}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "tracks",
          "encoder workflow track focus");
    auto turn = minimal.resolve({"data", -1, 0, true, 2}, {}, error);
    check(turn.action &&
              std::get<double>(turn.action->arguments.at("steps")) == -1 &&
              app.execute(*turn.action).accepted,
          "relative navigation mapping");
    enter = minimal.resolve({"enter", 1, 0, true, 3}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "steps",
          "encoder workflow step focus");
    turn = minimal.resolve({"data", 1, 0, true, 4}, {}, error);
    check(turn.action && app.execute(*turn.action).accepted &&
              app.snapshot().selected_step == 1,
          "encoder-only step navigation");
    enter = minimal.resolve({"enter", 1, 0, true, 5}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "step-properties" &&
              app.snapshot().selected_event,
          "encoder-only property entry");
    turn = minimal.resolve({"data", 1, 0, true, 6}, {}, error);
    check(turn.action && app.execute(*turn.action).accepted,
          "encoder-only property adjustment");
    auto back = minimal.resolve({"back", 1, 0, true, 7}, {}, error);
    check(back.action && app.execute(*back.action).accepted &&
              app.snapshot().focused_element == "steps",
          "property editor Back");
    check(app.execute(*back.action).accepted &&
              app.snapshot().focused_element == "tracks",
          "encoder workflow Back");
    samples.shutdown();
}

void test_undo_redo_and_step_properties() {
    using namespace neurogenic;
    std::string error;
    bool publish = true;
    std::size_t publications{};
    Timeline initial;
    initial.tracks = {
        {1,
         "drums",
         false,
         {1920,
          {{1, 0, MusicalEventType::note, 36, 0.5F, 120, 0, 0, 0},
           {2, 20, MusicalEventType::note, 40, 0.75F, 240, 0, 0, 0},
           {3, 0, MusicalEventType::parameter, 0, 0, 0, 7, 0.25F, 0}}}}};
    AudioEngine engine;
    TimelineService timeline(
        [&](const Timeline &, std::string &message) {
            ++publications;
            if (!publish) {
                message = "publication rejected";
                return false;
            }
            return true;
        },
        initial, {}, 32);
    SampleManager samples(engine);
    ApplicationService app(engine, timeline, samples);
    check(!app.snapshot().can_undo && !app.snapshot().can_redo &&
              app.snapshot().history_capacity == 32,
          "initial history state");
    check(app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}}).accepted,
          "step insertion for history");
    check(app.execute({"edit.undo.v1"}).accepted, "undo step insertion");
    const auto after_insert_undo = timeline.snapshot();
    check(std::none_of(
              after_insert_undo.timeline.tracks[0].pattern.events.begin(),
              after_insert_undo.timeline.tracks[0].pattern.events.end(),
              [](const MusicalEvent &event) {
                  return event.type == MusicalEventType::note &&
                         event.tick == 240 && event.pitch == 36;
              }),
          "undo step insertion state");
    check(app.execute({"edit.redo.v1"}).accepted, "redo step insertion");
    check(
        app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}}).accepted &&
            app.execute({"edit.undo.v1"}).accepted,
        "undo step removal");
    check(app.execute({"edit.redo.v1"}).accepted, "redo step removal");
    check(app.execute(
                 {"step.event-select.v1",
                  {{"step", std::int64_t{0}}, {"event_id", std::int64_t{2}}}})
              .accepted,
          "specific event selection");
    auto state = app.snapshot();
    check(state.selected_event && state.selected_event->event_id == 2 &&
              state.selected_event->events_at_step == 2 &&
              state.selected_event->index_at_step == 1,
          "multiple note selection metadata");
    check(app.execute({"step.velocity-set.v1", {{"velocity", 0.25}}}).accepted,
          "velocity edit");
    auto changed = timeline.snapshot();
    check(changed.timeline.tracks[0].pattern.events[1].id == 2 &&
              changed.timeline.tracks[0].pattern.events[1].velocity == 0.25F &&
              changed.timeline.tracks[0].pattern.events[0].id == 1 &&
              changed.timeline.tracks[0].pattern.events[2].type ==
                  MusicalEventType::parameter,
          "velocity edit identity/isolation");
    check(app.execute({"step.duration-set.v1",
                       {{"duration_ticks", std::int64_t{600}}}})
              .accepted,
          "duration edit");
    check(app.execute({"step.pitch-set.v1", {{"pitch", std::int64_t{42}}}})
              .accepted,
          "pitch edit");
    check(app.execute({"step.timing-offset-set.v1",
                       {{"offset_ticks", std::int64_t{30}}}})
              .accepted,
          "timing offset edit");
    state = app.snapshot();
    check(state.selected_event && state.selected_event->event_id == 2 &&
              state.selected_event->duration == 600 &&
              state.selected_event->pitch == 42 &&
              state.selected_event->timing_offset == 30,
          "step properties not reflected");
    check(!app.execute({"step.timing-offset-set.v1",
                        {{"offset_ticks", std::int64_t{240}}}})
               .accepted,
          "invalid timing offset accepted");
    check(app.execute({"step.property-reset.v1",
                       {{"property", std::string("timing")}}})
                  .accepted &&
              app.snapshot().selected_event->timing_offset == 0,
          "timing reset");
    const auto before_group = timeline.snapshot();
    const auto depth_before = timeline.history_status().undo_depth;
    check(app.execute({"edit.group-begin.v1"}).accepted, "group begin");
    check(app.execute({"step.velocity-set.v1", {{"velocity", 0.9}}}).accepted &&
              app.execute({"step.duration-set.v1",
                           {{"duration_ticks", std::int64_t{300}}}})
                  .accepted,
          "grouped property edits");
    check(timeline.snapshot().revision == before_group.revision,
          "group leaked partial edits");
    check(app.execute({"edit.group-commit.v1"}).accepted &&
              timeline.history_status().undo_depth == depth_before + 1,
          "group did not create one history entry");
    check(app.execute({"edit.undo.v1"}).accepted, "group undo");
    changed = timeline.snapshot();
    check(changed.timeline.tracks[0].pattern.events[1].velocity ==
                  before_group.timeline.tracks[0].pattern.events[1].velocity &&
              changed.timeline.tracks[0].pattern.events[1].duration ==
                  before_group.timeline.tracks[0].pattern.events[1].duration,
          "group undo state");
    check(
        app.execute({"edit.redo.v1"}).accepted &&
            timeline.snapshot().timeline.tracks[0].pattern.events[1].velocity ==
                0.9F,
        "group redo");
    check(app.execute({"edit.undo.v1"}).accepted &&
              app.execute({"step.event-select.v1",
                           {{"step", std::int64_t{0}},
                            {"event_id", std::int64_t{2}}}})
                  .accepted &&
              app.execute({"step.pitch-set.v1", {{"pitch", std::int64_t{44}}}})
                  .accepted &&
              !app.snapshot().can_redo,
          "new edit did not invalidate redo");
    const auto before_failure = timeline.snapshot();
    const auto history_before = timeline.history_status();
    publish = false;
    check(!app.execute({"step.velocity-set.v1", {{"velocity", 0.1}}}).accepted,
          "failed publication accepted");
    check(
        timeline.snapshot().revision == before_failure.revision &&
            timeline.history_status().undo_depth == history_before.undo_depth &&
            timeline.snapshot().timeline.tracks[0].pattern.events[1].velocity ==
                before_failure.timeline.tracks[0].pattern.events[1].velocity,
        "failed publication changed state/history");
    publish = true;
    auto stale = timeline.edit();
    check(app.execute({"step.duration-set.v1",
                       {{"duration_ticks", std::int64_t{480}}}})
              .accepted,
          "conflict winner");
    check(stale.set_track_name(1, "stale", error) &&
              !timeline.commit(std::move(stale),
                               TimelineCommitPolicy::next_audio_callback,
                               error) &&
              error.find("conflict") != std::string::npos,
          "revision conflict accepted");
    engine.play();
    check(app.execute({"edit.undo.v1"}).accepted && engine.playing(),
          "undo interrupted playback");
    std::array<float, 37> left{}, right{};
    engine.process(left, right);
    check(publications > 0, "history publication not exercised");
    samples.shutdown();

    TimelineService bounded(
        [](const Timeline &, std::string &) { return true; }, initial, {}, 2);
    for (int i = 0; i < 4; ++i) {
        auto edit = bounded.edit();
        check(edit.set_track_name(1, "v" + std::to_string(i), error) &&
                  bounded.commit(std::move(edit),
                                 TimelineCommitPolicy::next_audio_callback,
                                 error),
              "bounded history commit");
    }
    check(bounded.history_status().undo_depth == 2 &&
              bounded.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              bounded.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              !bounded.undo(TimelineCommitPolicy::next_audio_callback, error),
          "history capacity exhaustion");
}

void test_gui_model() {
    using namespace neurogenic;
    AudioEngine engine;
    Timeline initial;
    initial.tracks = {
        {1,
         "kick",
         false,
         {3840, {{1, 0, MusicalEventType::note, 36, 1, 120, 0, 0, 0}}}},
        {2, "hat", true, {1920, {}}}};
    TimelineService timeline(
        [](const Timeline &, std::string &) { return true; }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    GuiModel gui(application);
    const auto small = gui.layout(1024, 600), compact = gui.layout(1024, 800),
               large = gui.layout(1920, 1200);
    check(small.scale == 1 && small.content.w > 0 && small.grid.size() == 16,
          "1024x600 GUI layout");
    check(compact.scale == 1 && compact.breakpoint == GuiBreakpoint::compact &&
              compact.content.h > small.content.h,
          "1024x800 compact GUI layout");
    check(large.scale == 1.5F && large.breakpoint == GuiBreakpoint::wide &&
              large.inspector_visible &&
              large.parameters.x + large.parameters.w <= 1920,
          "1920x1200 automatic high-DPI layout");
    for (const auto scale : {1.0F, 1.5F, 2.0F}) {
        GuiModel scaled(application, GuiSettings{scale, false});
        const auto layout = scaled.layout(1920, 1200);
        check(layout.scale == scale && layout.grid.size() == 16 &&
                  layout.content.x + layout.content.w <= 1920 &&
                  (!layout.inspector_visible ||
                   layout.parameters.x + layout.parameters.w <= 1920),
              "reference-display manual scale layout");
    }
    const auto &first = gui.frame(1024, 600);
    check(first.state_revision == application.snapshot().revision &&
              !first.commands.empty(),
          "GUI state synchronization");
    const auto drawable = window_to_drawable(
        DisplayMetrics{960, 600, 1920, 1200, 2.0F, 2.0F}, 240.0F, 150.0F);
    check(drawable.x == 480.0F && drawable.y == 300.0F,
          "high-DPI window-to-drawable conversion");
    const auto first_content_width = first.layout.content.w;
    const auto &resized = gui.frame(1600, 900);
    check(resized.viewport_width == 1600 &&
              resized.layout.content.w > first_content_width,
          "GUI resize invalidation and expansion");
    check(gui.input({GuiInput::Type::key_down, GuiKey::scale_up}, 1600, 900) &&
              gui.settings().ui_scale && *gui.settings().ui_scale > 1.0F &&
              gui.frame(1600, 900).layout.scale == *gui.settings().ui_scale,
          "runtime scale adjustment and invalidation");
    std::string settings_error;
    check(!gui.set_ui_scale(3.0F, settings_error) && !settings_error.empty(),
          "invalid runtime scale accepted");
    check(
        gui.input({GuiInput::Type::key_down, GuiKey::scale_reset}, 1600, 900) &&
            !gui.settings().ui_scale,
        "runtime scale reset");
    for (const auto viewport :
         {std::pair{1920, 1200}, std::pair{1024, 800}, std::pair{1024, 600}}) {
        GuiModel hit_model(application);
        const auto hit_layout =
            hit_model.layout(viewport.first, viewport.second);
        const auto revision = timeline.snapshot().revision;
        GuiInput hit{GuiInput::Type::pointer_down};
        hit.x = hit_layout.grid[7].x + hit_layout.grid[7].w * 0.5F;
        hit.y = hit_layout.grid[7].y + hit_layout.grid[7].h * 0.5F;
        check(hit_model.input(hit, viewport.first, viewport.second) &&
                  timeline.snapshot().revision == revision + 1,
              "responsive sequencer hit testing");
    }
    check(gui.input({GuiInput::Type::key_down, GuiKey::space}, 1024, 600),
          "keyboard transport input");
    (void)application.dispatch_pending();
    check(application.snapshot().playing, "GUI semantic transport command");
    check(gui.input({GuiInput::Type::key_down, GuiKey::number, 5}, 1024, 600),
          "number step selection");
    (void)application.dispatch_pending();
    check(application.snapshot().selected_step == 4, "GUI step selection");
    GuiInput instrument_click;
    instrument_click.type = GuiInput::Type::pointer_down;
    instrument_click.x = small.instrument.x + 2;
    instrument_click.y = small.instrument.y + 2;
    check(gui.input(instrument_click, 1024, 600) &&
              timeline.snapshot().timeline.tracks[0].instrument_id ==
                  subtractive_instrument_id,
          "GUI semantic instrument selection");
    GuiInput parameters_tab;
    parameters_tab.type = GuiInput::Type::pointer_down;
    parameters_tab.x = small.tabs[2].x + 2;
    parameters_tab.y = small.tabs[2].y + 2;
    check(gui.input(parameters_tab, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::shift}, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1024, 600) &&
              gui.input({GuiInput::Type::key_up, GuiKey::shift}, 1024, 600) &&
              !application.snapshot().selected_step_locks.empty(),
          "GUI parameter lock creation");
    parameters_tab.x = small.tabs[0].x + 2;
    check(gui.input(parameters_tab, 1024, 600),
          "GUI return to sequencer after lock edit");
    const auto before_pointer = timeline.snapshot().revision;
    GuiInput step_click;
    step_click.type = GuiInput::Type::pointer_down;
    step_click.x = small.grid[1].x + 2;
    step_click.y = small.grid[1].y + 2;
    check(gui.input(step_click, 1024, 600) &&
              timeline.snapshot().revision == before_pointer + 1,
          "pointer step toggle");
    check(gui.input({GuiInput::Type::key_down, GuiKey::right}, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1024, 600),
          "keyboard step navigation and toggle");
    check(gui.input({GuiInput::Type::key_down, GuiKey::shift}, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::number, 1}, 1024,
                        600) &&
              gui.input({GuiInput::Type::key_up, GuiKey::shift}, 1024, 600) &&
              application.snapshot().selected_event,
          "GUI Select+step event selection");
    GuiInput property_click;
    property_click.type = GuiInput::Type::pointer_down;
    property_click.x = small.parameters.x + 20;
    property_click.y = small.parameters.y + 104;
    check(gui.input(property_click, 1024, 600) &&
              application.snapshot().focused_element == "step-properties",
          "GUI property inspector selection");
    const auto velocity_before =
        application.snapshot().selected_event->velocity;
    check(gui.input({GuiInput::Type::key_down, GuiKey::down}, 1024, 600) &&
              application.snapshot().selected_event->velocity < velocity_before,
          "GUI property adjustment");
    GuiInput undo_click;
    undo_click.type = GuiInput::Type::pointer_down;
    undo_click.x = small.parameters.x + 20;
    undo_click.y = small.parameters.y + 55;
    check(gui.input(undo_click, 1024, 600) && application.snapshot().can_redo,
          "GUI undo interaction");
    GuiInput click;
    click.type = GuiInput::Type::pointer_down;
    click.x = small.tabs[3].x + 2;
    click.y = small.tabs[3].y + 2;
    check(gui.input(click, 1024, 600) && gui.view() == GuiView::xy,
          "mouse view selection");
    GuiInput touch;
    touch.type = GuiInput::Type::touch_down;
    touch.x = small.content.x + 240;
    touch.y = small.content.y + 240;
    touch.timestamp_ms = 20;
    check(gui.input(touch, 1024, 600), "touch-equivalent X/Y input");
    (void)application.dispatch_pending();
    check(std::get<double>(
              application.parameters().get("performance.xy.x")->current_value) >
              0,
          "X/Y application integration");
    std::array<float, 64> performance_left{}, performance_right{};
    engine.process(performance_left, performance_right);
    GuiInput performance_click;
    performance_click.type = GuiInput::Type::pointer_down;
    performance_click.x = small.tabs[4].x + 2;
    performance_click.y = small.tabs[4].y + 2;
    check(gui.input(performance_click, 1024, 600) &&
              gui.view() == GuiView::performance,
          "GUI performance view selection");
    const auto &performance_frame = gui.frame(1024, 600);
    check(std::any_of(performance_frame.commands.begin(),
                      performance_frame.commands.end(),
                      [](const auto &item) {
                          return item.text.find("PERFORMANCE MONITOR") !=
                                 std::string::npos;
                      }) &&
              application.snapshot().performance.audio.callback_count > 0,
          "GUI performance state synchronization");
    const auto settings_path = std::filesystem::temp_directory_path() /
                               "neurogenic-gui-settings-test.conf";
    {
        GuiModel persistent(application, GuiSettings{1.7F, true},
                            settings_path);
        check(persistent.save_settings(settings_error), "GUI settings save");
    }
    const auto loaded = GuiModel::load_settings(settings_path, settings_error);
    check(loaded.ui_scale && std::fabs(*loaded.ui_scale - 1.7F) < 0.001F &&
              loaded.fullscreen,
          "GUI settings persistence");
    {
        std::ofstream invalid(settings_path);
        invalid << "ui_scale 9\nfullscreen x\n";
    }
    const auto recovered =
        GuiModel::load_settings(settings_path, settings_error);
    check(!recovered.ui_scale && !recovered.fullscreen &&
              !settings_error.empty(),
          "invalid GUI settings recovery");
    std::filesystem::remove(settings_path);
    check(
        gui.input({GuiInput::Type::key_down, GuiKey::fullscreen}, 1024, 600) &&
            gui.take_fullscreen_toggle(),
        "runtime fullscreen transition request");
    std::string error;
    const auto folder =
        std::filesystem::temp_directory_path() / "neurogenic-gui-browser";
    std::filesystem::create_directories(folder);
    write_pcm16_wav(folder / "sample.wav", 48000, 1, {0, 0});
    {
        std::ofstream ignored(folder / "ignored.txt");
        ignored << "x";
    }
    const auto entries = gui.browse(folder, error);
    check(error.empty() && entries.size() == 1 &&
              entries[0].path().extension() == ".wav",
          "sample browser filtering");
    std::filesystem::remove_all(folder);
    samples.shutdown();
}

void test_parameter_locks_and_automation() {
    using namespace neurogenic;
    const auto cutoff = resolve_parameter_destination("synth.cutoff");
    const auto delay_mix = resolve_parameter_destination("effect.delay.mix");
    check(cutoff && cutoff->target == ParameterTarget::subtractive_synth &&
              delay_mix && delay_mix->target == ParameterTarget::stereo_delay &&
              !resolve_parameter_destination("unknown.parameter"),
          "stable parameter destination resolution");

    Timeline timeline;
    Track track;
    track.id = 1;
    track.name = "synth";
    track.instrument_id = std::string(subtractive_instrument_id);
    track.pattern.length = 960;
    timeline.tracks.push_back(track);
    auto transaction = TimelineTransaction{};
    TimelineService editing(
        [](const Timeline &, std::string &) { return true; }, timeline);
    transaction = editing.edit();
    std::string error;
    check(transaction.add_note(1, 1, 0, 60, 1, 240, error) &&
              transaction.add_parameter_destination(1, 2, 0, "synth.cutoff",
                                                    1200, error) &&
              transaction.add_parameter_destination(1, 3, 0, "effect.delay.mix",
                                                    0.4F, error),
          "parameter automation event creation");
    PlaybackPlan plan;
    check(compile_timeline(transaction.timeline(), plan, error) &&
              plan.events[0].type == MusicalEventType::parameter &&
              plan.events[1].type == MusicalEventType::parameter &&
              plan.events[2].type == MusicalEventType::note_on,
          "parameter locks ordered before simultaneous notes");
    auto invalid = transaction.timeline();
    invalid.tracks[0].pattern.events[1].parameter_destination = "unknown";
    check(!compile_timeline(invalid, plan, error),
          "unknown parameter destination accepted");
    check(editing.commit(std::move(transaction),
                         TimelineCommitPolicy::next_audio_callback, error),
          "automation timeline commit");

    TimelineScheduler scheduler(48000);
    check(compile_timeline(editing.snapshot().timeline, plan, error),
          "automation compilation");
    scheduler.set_plan(&plan, 0);
    std::array<TimelineScheduledEvent, 16> events{};
    const auto first = scheduler.schedule(0, 48001, events);
    check(first >= 7 && events[0].frame == 0 && events[3].frame == 6000 &&
              events[4].frame == 24000,
          "parameter automation loop playback");

    Timeline delay_timeline;
    Track sample_track;
    sample_track.id = 1;
    sample_track.pattern.length = 960;
    sample_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1, 500});
    MusicalEvent delayed_mix;
    delayed_mix.id = 2;
    delayed_mix.tick = 4;
    delayed_mix.type = MusicalEventType::parameter;
    delayed_mix.value = 1;
    delayed_mix.parameter_destination = "effect.delay.mix";
    sample_track.pattern.events.push_back(delayed_mix);
    delay_timeline.tracks.push_back(std::move(sample_track));
    AudioEngine delay_engine;
    delay_engine.set_sample({48000, 1, std::vector<float>(1024, 0.25F)});
    check(delay_engine.queue_timeline(delay_timeline, error),
          "delay automation publication");
    std::array<float, 256> delay_left{}, delay_right{};
    delay_engine.process(delay_left, delay_right);
    check(std::abs(delay_left[50] - 0.2F) < 0.001F &&
              delay_left[150] < delay_left[50],
          "delay automation sample offset");

    AudioEngine engine;
    TimelineService service(
        [&](const Timeline &value, std::string &publish_error) {
            return engine.queue_timeline(value, publish_error);
        },
        timeline);
    SampleManager samples(engine);
    ApplicationService app(engine, service, samples);
    auto lock = [&](std::string id, double value) {
        return app.execute(
            {"step.lock-set.v1",
             {{"parameter_id", std::move(id)}, {"value", value}}});
    };
    check(lock("synth.cutoff", 2000).accepted &&
              lock("effect.delay.mix", 0.5).accepted,
          "multiple step parameter locks");
    std::array<float, 64> lock_left{}, lock_right{};
    engine.process(lock_left, lock_right);
    auto state = app.snapshot();
    check(state.selected_step_locks.size() == 2,
          "observable selected-step locks");
    const auto cutoff_event = state.selected_step_locks[1].event_id;
    check(lock("synth.cutoff", 3000).accepted &&
              app.snapshot().selected_step_locks[1].event_id == cutoff_event,
          "parameter lock edit preserves stable event ID");
    engine.process(lock_left, lock_right);
    check(
        app.execute({"step.lock-select.v1",
                     {{"parameter_id", std::string("synth.cutoff")}}})
                .accepted &&
            app.execute({"navigation.adjust.v1", {{"steps", 1.0}}}).accepted &&
            app.snapshot().selected_step_locks[1].value > 3000,
        "minimal-controller parameter lock adjustment");
    engine.process(lock_left, lock_right);
    const auto undo = app.execute({"edit.undo.v1"});
    engine.process(lock_left, lock_right);
    check(undo.accepted && app.execute({"edit.redo.v1"}).accepted,
          "parameter lock undo/redo");
    engine.process(lock_left, lock_right);
    check(app.execute({"step.lock-remove.v1",
                       {{"parameter_id", std::string("effect.delay.mix")}}})
                  .accepted &&
              app.snapshot().selected_step_locks.size() == 1 &&
              app.execute({"step.locks-clear.v1"}).accepted &&
              app.snapshot().selected_step_locks.empty(),
          "parameter lock removal and clear");
    check(!lock("unknown.parameter", 0.5).accepted,
          "unknown application lock accepted");
    samples.shutdown();

    TimelineService conflicts(
        [](const Timeline &, std::string &) { return true; }, timeline);
    auto first_edit = conflicts.edit();
    auto stale_edit = conflicts.edit();
    check(first_edit.add_parameter_destination(1, 10, 0, "synth.cutoff", 900,
                                               error) &&
              stale_edit.add_parameter_destination(1, 11, 0, "synth.cutoff",
                                                   1000, error) &&
              conflicts.commit(std::move(first_edit),
                               TimelineCommitPolicy::next_audio_callback,
                               error) &&
              !conflicts.commit(std::move(stale_edit),
                                TimelineCommitPolicy::next_audio_callback,
                                error),
          "parameter lock revision conflict");

    AudioEngine rejected_engine;
    TimelineService rejected_service(
        [](const Timeline &, std::string &publish_error) {
            publish_error = "publication rejected";
            return false;
        },
        timeline);
    SampleManager rejected_samples(rejected_engine);
    ApplicationService rejected_app(rejected_engine, rejected_service,
                                    rejected_samples);
    const auto history_before = rejected_service.history_status();
    check(!rejected_app
                  .execute({"step.lock-set.v1",
                            {{"parameter_id", std::string("synth.cutoff")},
                             {"value", 2000.0}}})
                  .accepted &&
              rejected_service.history_status().undo_depth ==
                  history_before.undo_depth &&
              rejected_app.snapshot().selected_step_locks.empty(),
          "failed lock publication changed state or history");
    rejected_samples.shutdown();
}

void test_probability_conditions_and_ratchets() {
    using namespace neurogenic;
    auto make = [](TriggerCondition condition = TriggerCondition::always,
                   float probability = 1.0F, std::uint8_t ratchets = 1) {
        Timeline value;
        value.generation_seed = 12345;
        Track track;
        track.id = 7;
        track.pattern.length = 960;
        MusicalEvent event{11, 0, MusicalEventType::note, 36, 1, 960};
        event.trigger_condition = condition;
        event.trigger_probability = probability;
        event.ratchet_count = ratchets;
        track.pattern.events.push_back(event);
        value.tracks.push_back(track);
        return value;
    };
    auto note_ons = [](const Timeline &timeline, bool fill = false,
                       std::uint32_t frames = 120001) {
        PlaybackPlan plan;
        std::string error;
        check(compile_timeline(timeline, plan, error), error.c_str());
        TimelineScheduler scheduler(48000);
        scheduler.set_plan(&plan, 0);
        scheduler.set_fill(fill);
        std::array<TimelineScheduledEvent, 128> events{};
        const auto count = scheduler.schedule(0, frames, events);
        std::vector<TimelineScheduledEvent> result;
        for (std::size_t i = 0; i < count; ++i)
            if (events[i].type == MusicalEventType::note_on)
                result.push_back(events[i]);
        return result;
    };
    check(note_ons(make(TriggerCondition::always, 0)).empty(),
          "zero probability fired");
    check(note_ons(make()).size() == 6, "full probability did not fire");
    check(note_ons(make(TriggerCondition::first_loop)).size() == 1 &&
              note_ons(make(TriggerCondition::not_first_loop)).size() == 5,
          "first-loop trigger conditions");
    auto every = make(TriggerCondition::every_nth_loop);
    every.tracks[0].pattern.events[0].condition_n = 2;
    every.tracks[0].pattern.events[0].condition_m = 2;
    check(note_ons(every).size() == 3, "every-N trigger condition");
    auto n_of_m = make(TriggerCondition::n_of_m);
    n_of_m.tracks[0].pattern.events[0].condition_n = 2;
    n_of_m.tracks[0].pattern.events[0].condition_m = 3;
    check(note_ons(n_of_m).size() == 4, "N-of-M trigger condition");
    check(note_ons(make(TriggerCondition::fill_active), false).empty() &&
              note_ons(make(TriggerCondition::fill_active), true).size() == 6,
          "Fill condition sampling");
    const auto ratchets =
        note_ons(make(TriggerCondition::always, 1, 4), false, 24000);
    check(ratchets.size() == 4 && ratchets[0].frame == 0 &&
              ratchets[1].frame == 6000 && ratchets[2].frame == 12000 &&
              ratchets[3].frame == 18000 && ratchets[0].occurrence == 0 &&
              ratchets[3].occurrence == 3,
          "ratchet timing and unique ownership");
    const auto random_a = note_ons(make(TriggerCondition::always, 0.5F));
    const auto random_b = note_ons(make(TriggerCondition::always, 0.5F));
    check(random_a.size() == random_b.size() &&
              std::equal(random_a.begin(), random_a.end(), random_b.begin(),
                         [](const auto &a, const auto &b) {
                             return a.frame == b.frame;
                         }),
          "probability seed reproducibility");
    auto with_unrelated_track = make(TriggerCondition::always, 0.5F);
    Track unrelated;
    unrelated.id = 99;
    unrelated.pattern.length = 480;
    unrelated.pattern.events.push_back(
        {91, 0, MusicalEventType::note, 40, 1, 120});
    with_unrelated_track.tracks.push_back(std::move(unrelated));
    const auto combined = note_ons(with_unrelated_track);
    std::vector<std::uint32_t> original_frames;
    for (const auto &event : combined)
        if (event.track == 7)
            original_frames.push_back(event.frame);
    check(original_frames.size() == random_a.size() &&
              std::equal(original_frames.begin(), original_frames.end(),
                         random_a.begin(),
                         [](auto frame, const auto &event) {
                             return frame == event.frame;
                         }),
          "probability changed with unrelated track activity");

    auto previous = make();
    previous.tracks[0].pattern.events[0].trigger_probability = 0;
    MusicalEvent second{12, 240, MusicalEventType::note, 38, 1, 120};
    second.trigger_condition = TriggerCondition::previous_not_fired;
    previous.tracks[0].pattern.events.push_back(second);
    const auto previous_events = note_ons(previous, false, 24000);
    check(previous_events.size() == 1 && previous_events[0].id == 12,
          "previous-trigger condition");
    auto lock_interaction = make(TriggerCondition::always, 0);
    MusicalEvent parameter;
    parameter.id = 13;
    parameter.tick = 0;
    parameter.type = MusicalEventType::parameter;
    parameter.parameter_destination = "effect.delay.mix";
    parameter.value = 0.5F;
    lock_interaction.tracks[0].pattern.events.push_back(parameter);
    PlaybackPlan lock_plan;
    std::string lock_error;
    check(compile_timeline(lock_interaction, lock_plan, lock_error),
          lock_error.c_str());
    TimelineScheduler lock_scheduler(48000);
    lock_scheduler.set_plan(&lock_plan, 0);
    std::array<TimelineScheduledEvent, 8> lock_events{};
    const auto lock_count = lock_scheduler.schedule(0, 1, lock_events);
    check(lock_count == 1 && lock_events[0].type == MusicalEventType::parameter,
          "conditional note suppressed independent parameter automation");

    AudioEngine engine;
    auto editable = make();
    TimelineService service(
        [&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        },
        editable);
    SampleManager samples(engine);
    ApplicationService app(engine, service, samples);
    check(app.execute({"step.event-select.v1", {{"step", std::int64_t{0}}}})
                  .accepted &&
              app.execute({"step.probability-set.v1", {{"probability", 0.25}}})
                  .accepted &&
              app.execute({"step.condition-set.v1",
                           {{"condition", std::string("n-of-m")},
                            {"n", std::int64_t{2}},
                            {"m", std::int64_t{5}}}})
                  .accepted &&
              app.execute({"step.ratchet-set.v1", {{"count", std::int64_t{3}}}})
                  .accepted,
          "semantic trigger editing actions");
    auto state = app.snapshot();
    check(state.selected_event &&
              std::abs(state.selected_event->trigger_probability - 0.25F) <
                  0.001F &&
              state.selected_event->condition_n == 2 &&
              state.selected_event->condition_m == 5 &&
              state.selected_event->ratchet_count == 3,
          "observable trigger metadata");
    std::array<float, 1> fill_left{}, fill_right{};
    engine.process(fill_left, fill_right);
    check(app.execute({"edit.undo.v1"}).accepted, "trigger Undo action");
    check(
        service.snapshot().timeline.tracks[0].pattern.events[0].ratchet_count ==
            1,
        "trigger Undo state");
    check(app.execute({"performance.fill-set.v1", {{"active", true}}}).accepted,
          "Fill action");
    check(app.snapshot().fill_active, "observable Fill state");
    engine.process(fill_left, fill_right);
    samples.shutdown();

    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-trigger-roundtrip.ngt";
    auto serialized = make(TriggerCondition::fill_inactive, 0.75F, 4);
    save_timeline(serialized, path);
    const auto loaded = load_timeline(path);
    std::filesystem::remove(path);
    const auto &loaded_event = loaded.tracks[0].pattern.events[0];
    check(loaded.generation_seed == serialized.generation_seed &&
              loaded_event.trigger_condition ==
                  TriggerCondition::fill_inactive &&
              loaded_event.ratchet_count == 4 &&
              std::abs(loaded_event.trigger_probability - 0.75F) < 0.001F,
          "trigger serialization round trip");
}

void test_mixer_and_routing() {
    using namespace neurogenic;
    std::string error;
    MixerConfiguration configuration;
    configuration.revision = 1;
    configuration.track_count = 2;
    configuration.tracks[0].track = 11;
    configuration.tracks[0].gain = 0.5F;
    configuration.tracks[0].pan = -1.0F;
    configuration.tracks[0].aux_send[0] = 0.5F;
    configuration.tracks[1].track = 22;
    configuration.tracks[1].pan = 1.0F;
    Mixer mixer;
    check(mixer.publish(configuration, error), "publish mixer configuration");
    mixer.begin_block(4);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        check(mixer.add_track(11, frame, 1, 1), "route first track");
        check(mixer.add_track(22, frame, 1, 1), "route second track");
    }
    std::array<float, 4> left{}, right{};
    mixer.process(left, right);
    mixer.apply_master_gain(left, right);
    mixer.route_master_output(left, right);
    near(left[0], 0.75F, "gain pan and auxiliary left summing");
    near(right[0], 1.0F, "gain pan and auxiliary right summing");
    const auto metered = mixer.state();
    check(metered.tracks[0].left > 0 && metered.tracks[1].right > 0 &&
              metered.master.left > 0,
          "track and master metering");

    configuration.revision++;
    configuration.tracks[0].mute = true;
    configuration.tracks[0].aux_pre_fader[0] = true;
    configuration.tracks[1].solo = true;
    check(mixer.publish(configuration, error), "publish mute solo mixer");
    mixer.begin_block(1);
    check(mixer.add_track(11, 0, 1, 1) && mixer.add_track(22, 0, 1, 1),
          "route muted and solo channels");
    left.fill(0);
    right.fill(0);
    mixer.process({left.data(), 1}, {right.data(), 1});
    mixer.apply_master_gain({left.data(), 1}, {right.data(), 1});
    mixer.route_master_output({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.5F, "solo and pre-fader left semantics");
    near(right[0], 1.0F, "solo and pre-fader right semantics");

    configuration.revision++;
    configuration.tracks[0].mute = false;
    configuration.tracks[0].solo = false;
    configuration.tracks[1].solo = false;
    configuration.tracks[0].aux_send = {};
    configuration.tracks[0].output_pair = 2;
    configuration.input_count = 1;
    configuration.inputs[0].physical_channel = 5;
    configuration.inputs[0].gain = 0.5F;
    check(mixer.publish(configuration, error), "publish six-channel routing");
    mixer.begin_block(1);
    check(mixer.add_track(11, 0, 1, 1), "route direct-output track");
    mixer.add_input(5, 0, 1);
    std::array<float, 6> physical{};
    left.fill(0);
    right.fill(0);
    mixer.process({left.data(), 1}, {right.data(), 1}, physical, 6);
    mixer.apply_master_gain({left.data(), 1}, {right.data(), 1});
    mixer.route_master_output({left.data(), 1}, {right.data(), 1}, physical, 6);
    check(std::fabs(physical[2] - 0.5F) < 0.0001F &&
              std::fabs(physical[3]) < 0.0001F && physical[0] > 0 &&
              physical[1] > 0,
          "six-channel output mapping and mono input conversion");
    std::array<float, 1> mono_output{};
    mixer.route_master_output({left.data(), 1}, {right.data(), 1}, mono_output,
                              1);
    check(mono_output[0] > 0, "stereo master to mono output conversion");
    auto invalid = configuration;
    invalid.tracks[0].output_pair = 5;
    check(!validate_mixer_configuration(invalid, 6, 6, error),
          "invalid output channel rejection");
    invalid = configuration;
    invalid.tracks[1].track = invalid.tracks[0].track;
    check(!validate_mixer_configuration(invalid, 6, 6, error),
          "duplicate routing rejection");
    check(!validate_mixer_configuration(configuration, 2, 2, error),
          "device capability change rejects unavailable routes");
    Mixer saturated;
    for (std::uint64_t revision = 1; revision <= 4; ++revision) {
        auto update = configuration;
        update.revision = revision;
        check(saturated.publish(update, error), "bounded mixer publication");
    }
    auto overflow = configuration;
    overflow.revision = 5;
    check(!saturated.publish(overflow, error) &&
              saturated.state().rejected_publications == 1,
          "mixer publication overflow is observable");

    MixerConfiguration grouped;
    grouped.revision = 1;
    grouped.track_count = 2;
    grouped.tracks[0].track = 11;
    grouped.tracks[0].destination_group = 100;
    grouped.tracks[1].track = 22;
    grouped.tracks[1].destination_group = 200;
    grouped.group_count = 2;
    grouped.groups[0].id = 100;
    std::copy_n("Drums", 5, grouped.groups[0].name.begin());
    grouped.groups[0].gain = 0.5F;
    grouped.groups[0].aux_send[0] = 0.5F;
    grouped.groups[1].id = 200;
    std::copy_n("Music", 5, grouped.groups[1].name.begin());
    grouped.groups[1].destination_group = 100;
    grouped.effects.instance_count = 1;
    grouped.effects.instances[0].id = 81;
    grouped.effects.instances[0].type = EffectProcessorType::gain;
    grouped.effects.instances[0].parameters[0] = {1, 0.5F};
    grouped.effects.instances[0].parameter_count = 1;
    grouped.effects.groups[0] = {{81}, 1, false};
    auto group_mixer = std::make_unique<Mixer>();
    check(group_mixer->publish(grouped, error), "publish group routing graph");
    group_mixer->begin_block(1);
    check(group_mixer->add_track(11, 0, 1, 1) &&
              group_mixer->add_track(22, 0, 1, 1),
          "route tracks into independent groups");
    std::array<float, 1> group_left{}, group_right{};
    group_mixer->process(group_left, group_right);
    near(group_left[0], 0.75F,
         "nested group serial FX, gain, and aux return order");
    near(group_right[0], 0.75F, "nested group stereo summing");
    near(group_mixer->group_pre_left(0)[0], 2.0F,
         "group pre-effects tap includes child group");
    near(group_mixer->group_post_effect_left(0)[0], 1.0F,
         "group post-effects tap");
    near(group_mixer->group_left(0)[0], 0.5F, "group post-fader tap");
    check(group_mixer->state().groups[0].left > 0,
          "group metering is observable");
    auto solo_group = grouped;
    solo_group.revision++;
    solo_group.groups[1].solo = true;
    check(group_mixer->publish(solo_group, error), "publish group solo");
    group_mixer->begin_block(1);
    check(group_mixer->add_track(11, 0, 1, 1) &&
              group_mixer->add_track(22, 0, 1, 1),
          "route group solo sources");
    group_left.fill(0);
    group_right.fill(0);
    group_mixer->process(group_left, group_right);
    near(group_left[0], 0.375F,
         "group solo keeps source subtree and required ancestor");
    check(group_mixer->set_modulation(
              {ParameterTarget::mixer_group,
               static_cast<std::uint32_t>(MixerGroupModulationParameter::gain),
               100},
              0.25F),
          "group gain modulation destination");
    auto cyclic = grouped;
    cyclic.groups[0].destination_group = 200;
    check(!validate_mixer_configuration(cyclic, 6, 6, error) &&
              error.find("cyclic") != std::string::npos,
          "cyclic group routing rejection");
    auto missing_group = grouped;
    missing_group.tracks[0].destination_group = 999;
    check(!validate_mixer_configuration(missing_group, 6, 6, error),
          "missing group destination rejection");
    RecordingService group_recorder(48000, 4);
    RecordingConfiguration group_recording;
    group_recording.source = RecordingSource::group_post_fader;
    group_recording.source_id = 100;
    group_recording.destination = SampleSlotId{19};
    group_recording.fixed_frames = 1;
    check(group_recorder.arm(group_recording, error) &&
              group_recorder.start(0, error),
          "arm group recording tap");
    group_recorder.capture_internal(RecordingSource::group_post_fader,
                                    group_mixer->group_left(0),
                                    group_mixer->group_right(0), 0, 100);
    check(group_recorder.status().state == RecordingState::completed &&
              std::fabs(group_recorder.completed_sample().interleaved[0] -
                        0.25F) < 0.0001F,
          "group post-fader recording source");
    auto render_grouped = [&](std::size_t partition) {
        auto rendered_mixer = std::make_unique<Mixer>();
        std::string render_error;
        check(rendered_mixer->publish(grouped, render_error),
              "partitioned group publish");
        std::array<float, 8> rendered{};
        for (std::size_t begin = 0; begin < rendered.size();
             begin += partition) {
            const auto count = std::min(partition, rendered.size() - begin);
            rendered_mixer->begin_block(count);
            for (std::size_t frame = 0; frame < count; ++frame) {
                check(rendered_mixer->add_track(11, frame, 1, 1),
                      "partitioned group track");
                check(rendered_mixer->add_track(22, frame, 1, 1),
                      "partitioned child group track");
            }
            std::span<float> output{rendered.data() + begin, count};
            std::array<float, 8> right_output{};
            rendered_mixer->process(output,
                                    {right_output.data() + begin, count});
        }
        return rendered;
    };
    const auto whole_group = render_grouped(8);
    const auto split_group = render_grouped(3);
    for (std::size_t frame = 0; frame < whole_group.size(); ++frame)
        near(whole_group[frame], split_group[frame],
             "group callback partition independence");
    Mixer group_publications;
    for (std::uint64_t revision = 1; revision <= 4; ++revision) {
        auto update = grouped;
        update.revision = revision;
        update.groups[0].pan = revision % 2 ? -0.25F : 0.25F;
        check(group_publications.publish(update, error),
              "rapid group routing publication");
    }
    auto rejected_group_update = grouped;
    rejected_group_update.revision = 5;
    check(!group_publications.publish(rejected_group_update, error) &&
              group_publications.state().rejected_publications == 1,
          "bounded rapid group publication rejection");

    MixerConfiguration fx_configuration;
    fx_configuration.revision = 1;
    fx_configuration.track_count = 1;
    fx_configuration.tracks[0].track = 11;
    auto &fx = fx_configuration.external_fx[0];
    fx.id = 1;
    fx.source = ExternalFxSource::track;
    fx.source_id = 11;
    fx.send_left = 2;
    fx.send_right = 3;
    fx.return_left = 0;
    fx.return_right = 1;
    fx.send_gain = 0.5F;
    fx.return_gain = 0.5F;
    fx.wet = 0.5F;
    fx.enabled = true;
    Mixer fx_mixer;
    check(fx_mixer.publish(fx_configuration, error),
          "publish external FX routing");
    fx_mixer.begin_block(4);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        check(fx_mixer.add_track(11, frame, 1, 1), "route FX source");
        fx_mixer.add_input(0, frame, 0.5F);
        fx_mixer.add_input(1, frame, 0.25F);
    }
    std::array<float, 4> fx_left{}, fx_right{};
    std::array<float, 24> fx_physical{};
    fx_mixer.process(fx_left, fx_right, fx_physical, 6);
    near(fx_physical[2], 0.5F, "external FX left send gain");
    near(fx_physical[3], 0.5F, "external FX right send gain");
    near(fx_left[0], 0.625F, "external FX linear wet/dry left");
    near(fx_right[0], 0.5625F, "external FX linear wet/dry right");
    near(fx_mixer.external_return_left(0)[0], 0.25F,
         "external FX return recording tap");
    check(fx_mixer.state().external_send_available[0] &&
              fx_mixer.state().external_return_available[0],
          "external FX availability state");
    RecordingService fx_recorder(48000, 8);
    RecordingConfiguration fx_recording;
    fx_recording.source = RecordingSource::external_fx_return;
    fx_recording.source_id = 0;
    fx_recording.destination = SampleSlotId{18};
    fx_recording.fixed_frames = 4;
    check(fx_recorder.arm(fx_recording, error) && fx_recorder.start(0, error),
          "arm external return recording tap");
    fx_recorder.capture_internal(RecordingSource::external_fx_return,
                                 fx_mixer.external_return_left(0),
                                 fx_mixer.external_return_right(0), 0, 0);
    check(fx_recorder.status().state == RecordingState::completed &&
              fx_recorder.completed_sample().interleaved[0] == 0.25F,
          "external return recording source");

    auto mono = fx_configuration;
    mono.revision++;
    mono.external_fx[0].mono_send = true;
    mono.external_fx[0].mono_return = true;
    mono.external_fx[0].wet = 1;
    check(fx_mixer.publish(mono, error), "publish mono external FX loop");
    fx_mixer.begin_block(1);
    check(fx_mixer.add_track(11, 0, 1, 0.5F), "route mono FX source");
    fx_mixer.add_input(0, 0, 0.4F);
    fx_left.fill(0);
    fx_right.fill(0);
    fx_physical.fill(0);
    fx_mixer.process({fx_left.data(), 1}, {fx_right.data(), 1}, fx_physical, 6);
    near(fx_physical[2], 0.375F, "mono send fold-down");
    near(fx_left[0], 0.2F, "mono return left duplication");
    near(fx_right[0], 0.2F, "mono return right duplication");

    auto latency = fx_configuration;
    latency.revision++;
    latency.external_fx[0].wet = 0;
    latency.external_fx[0].latency_frames = 2;
    check(fx_mixer.publish(latency, error), "publish FX latency compensation");
    fx_mixer.begin_block(4);
    check(fx_mixer.add_track(11, 0, 1, 1), "route compensation impulse");
    fx_left.fill(0);
    fx_right.fill(0);
    fx_physical.fill(0);
    fx_mixer.process(fx_left, fx_right, fx_physical, 6);
    near(fx_left[0], 0, "compensated dry is delayed");
    near(fx_left[2], 1, "manual compensation frame offset");
    auto feedback = fx_configuration;
    feedback.external_fx[0].send_left = 0;
    feedback.external_fx[0].send_right = 1;
    check(!validate_mixer_configuration(feedback, 6, 6, error),
          "external send cannot conflict with master output");
    check(!validate_mixer_configuration(fx_configuration, 2, 2, error),
          "missing physical FX channels rejected");

    AudioEngine engine;
    engine.set_sample(constant_sample(256));
    Timeline timeline;
    Track track;
    track.id = 77;
    track.pattern.length = ticks_per_quarter;
    track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1.0F, 240});
    timeline.tracks.push_back(track);
    MixerConfiguration engine_mixer;
    engine_mixer.revision = 1;
    engine_mixer.track_count = 1;
    engine_mixer.tracks[0].track = 77;
    check(engine.publish_mixer(engine_mixer, error) &&
              engine.queue_timeline(timeline, error),
          "publish timeline track routing");
    RecordingService recorder(48000, 64);
    RecordingConfiguration recording;
    recording.source = RecordingSource::track_channel;
    recording.source_id = 77;
    recording.destination = SampleSlotId{9};
    recording.fixed_frames = 32;
    check(recorder.arm(recording, error) && recorder.start(0, error),
          "arm track recording tap");
    engine.set_recording_service(&recorder);
    std::array<float, 64> rendered_left{}, rendered_right{};
    engine.process(rendered_left, rendered_right);
    engine.set_recording_service(nullptr);
    check(recorder.status().state == RecordingState::completed &&
              recorder.completed_sample().interleaved[0] != 0,
          "post-fader track recording tap");

    Project project;
    project.name = "Mixer";
    project.timeline = timeline;
    project.mixer = grouped;
    project.modulation.modulator_count = 1;
    project.modulation.modulators[0].id = 501;
    project.modulation.modulators[0].type = ModulatorType::lfo;
    project.modulation.modulators[0].owner_kind = ModulatorOwnerKind::group;
    project.modulation.modulators[0].owner_id = 100;
    project.modulation.route_count = 1;
    project.modulation.routes[0].id = 502;
    project.modulation.routes[0].source_instance = 501;
    project.modulation.routes[0].source_output = 1;
    project.modulation.routes[0].destination = {
        ParameterTarget::mixer_group,
        static_cast<std::uint32_t>(MixerGroupModulationParameter::gain), 100};
    project.modulation.routes[0].minimum = 0;
    project.modulation.routes[0].maximum = 2;
    project.modulation.routes[0].base_value = 0.5F;
    project.modulation.routes[0].depth = 0.25F;
    project.modulation.routes[0].destination_owner_explicit = true;
    project.modulation.routes[0].destination_owner_kind =
        ModulatorOwnerKind::group;
    project.modulation.routes[0].destination_owner_id = 100;
    project.routing_scenes.push_back({41, "Performance", fx_configuration});
    project.active_routing_scene = 41;
    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-mixer-project";
    std::filesystem::remove_all(path);
    save_project(project, path);
    const auto restored = load_project(path);
    check(restored.mixer.track_count == 2 && restored.mixer.group_count == 2 &&
              restored.mixer.groups[0].id == 100 &&
              restored.mixer.groups[1].destination_group == 100 &&
              restored.mixer.tracks[0].destination_group == 100 &&
              restored.mixer.effects.groups[0].instances[0] == 81 &&
              restored.modulation.modulators[0].owner_kind ==
                  ModulatorOwnerKind::group &&
              restored.modulation.routes[0].destination.instance == 100 &&
              restored.routing_scenes.size() == 1 &&
              restored.routing_scenes[0].id == 41 &&
              restored.routing_scenes[0].mixer.external_fx[0].enabled &&
              restored.active_routing_scene == 41,
          "mixer project persistence");
    std::filesystem::remove_all(path);

    AudioEngine app_engine;
    SampleManager samples(app_engine);
    TimelineService service(
        [&](const Timeline &value, std::string &publish_error) {
            return app_engine.queue_timeline(value, publish_error);
        },
        timeline);
    ApplicationService application(app_engine, service, samples);
    check(application
                  .execute({"effect.create.v1",
                            {{"id", std::int64_t{901}}, {"type", "gain"}}})
                  .accepted &&
              application
                  .execute({"effect.insert.v1",
                            {{"id", std::int64_t{901}},
                             {"chain", "track"},
                             {"index", std::int64_t{0}},
                             {"position", std::int64_t{0}}}})
                  .accepted &&
              application
                  .execute({"effect.parameter-set.v1",
                            {{"id", std::int64_t{901}},
                             {"parameter", std::int64_t{1}},
                             {"value", 1.5}}})
                  .accepted,
          "controller-independent effect chain actions");
    app_engine.process(rendered_left, rendered_right);
    check(application.execute({"mixer.gain-set.v1", {{"value", 0.25}}})
                  .accepted &&
              application
                  .execute({"mixer.aux-send-set.v1",
                            {{"aux", std::int64_t{1}},
                             {"value", 0.4},
                             {"pre_fader", true}}})
                  .accepted &&
              application
                  .execute({"mixer.input-set.v1",
                            {{"index", std::int64_t{0}},
                             {"physical_channel", std::int64_t{0}},
                             {"gain", 0.75},
                             {"pan", 0.0},
                             {"route_to_master", true},
                             {"aux_1", 0.2}}})
                  .accepted,
          "controller-independent mixer actions");
    app_engine.process(rendered_left, rendered_right);
    auto group_result =
        application.execute({"mixer.group-create.v1", {{"name", "Drums"}}});
    check(group_result.accepted, group_result.error.c_str());
    group_result = application.execute(
        {"mixer.track-destination.v1",
         {{"track_id", std::int64_t{77}}, {"group_id", std::int64_t{1}}}});
    check(group_result.accepted, group_result.error.c_str());
    group_result = application.execute(
        {"mixer.group-levels.v1",
         {{"id", std::int64_t{1}}, {"gain", 0.75}, {"pan", -0.1}}});
    check(group_result.accepted, group_result.error.c_str());
    app_engine.process(rendered_left, rendered_right);
    group_result = application.execute(
        {"effect.create.v1", {{"id", std::int64_t{902}}, {"type", "gain"}}});
    check(group_result.accepted, group_result.error.c_str());
    group_result = application.execute({"effect.insert.v1",
                                        {{"id", std::int64_t{902}},
                                         {"chain", "group"},
                                         {"index", std::int64_t{1}},
                                         {"position", std::int64_t{0}}}});
    check(group_result.accepted, group_result.error.c_str());
    check(application
              .execute({"modulator.create.v1",
                        {{"type", std::int64_t{1}},
                         {"owner",
                          static_cast<std::int64_t>(ModulatorOwnerKind::group)},
                         {"owner_id", std::int64_t{1}},
                         {"name", "Group LFO"}}})
              .accepted,
          "create group-owned modulator");
    const auto group_modulator_id =
        application.snapshot().modulation.modulators[0].id;
    check(application
              .execute({"modulator.route-create.v1",
                        {{"source_id",
                          static_cast<std::int64_t>(group_modulator_id)},
                         {"destination", "mixer.group.pan"},
                         {"depth", 0.25}}})
              .accepted,
          "group-owned modulation actions");
    app_engine.process(rendered_left, rendered_right);
    const auto app_state = application.snapshot();
    check(app_state.mixer.configuration.effects.instance_count == 2,
          "observable effect instance count");
    check(app_state.mixer.configuration.effects.instances[0].id == 901 &&
              app_state.mixer.configuration.effects.tracks[0].instances[0] ==
                  901,
          "observable track effect state");
    check(std::fabs(app_state.mixer.configuration.tracks[0].gain - 0.25F) <
                  0.0001F &&
              std::fabs(app_state.mixer.configuration.tracks[0].aux_send[1] -
                        0.4F) < 0.0001F &&
              app_state.mixer.configuration.input_count == 1 &&
              std::fabs(app_state.mixer.configuration.inputs[0].aux_send[0] -
                        0.2F) < 0.0001F,
          "observable mixer channel state");
    check(app_state.mixer.configuration.group_count == 1 &&
              app_state.mixer.configuration.groups[0].id == 1 &&
              app_state.mixer.configuration.tracks[0].destination_group == 1 &&
              app_state.mixer.configuration.effects.groups[0].instances[0] ==
                  902,
          "observable group state");
    check(app_state.modulation.modulators[0].owner_kind ==
              ModulatorOwnerKind::group,
          "observable group modulation state");
    check(!application
               .execute({"mixer.group-delete.v1", {{"id", std::int64_t{1}}}})
               .accepted,
          "referenced group deletion rejection");
    check(application
                  .execute({"mixer.fx-loop-set.v1",
                            {{"index", std::int64_t{0}},
                             {"source", "master"},
                             {"source_id", std::int64_t{0}},
                             {"send_left", std::int64_t{2}},
                             {"send_right", std::int64_t{3}},
                             {"return_left", std::int64_t{0}},
                             {"return_right", std::int64_t{1}},
                             {"enabled", true}}})
                  .accepted &&
              application.execute({"mixer.scene-save.v1", {{"name", "Live"}}})
                  .accepted &&
              application
                  .execute({"mixer.fx-loop-levels.v1",
                            {{"index", std::int64_t{0}},
                             {"send_gain", 0.5},
                             {"return_gain", 0.75},
                             {"wet", 0.8}}})
                  .accepted,
          "external FX and routing scene actions");
    const auto scene_state = application.snapshot();
    check(scene_state.routing_scenes.size() == 1 &&
              scene_state.mixer.configuration.external_fx[0].wet == 0.8F &&
              application
                  .execute({"mixer.scene-recall.v1",
                            {{"id", static_cast<std::int64_t>(
                                        scene_state.routing_scenes[0].id)}}})
                  .accepted,
          "routing scene recall action");
    app_engine.process(rendered_left, rendered_right);
    check(std::fabs(
              application.snapshot().mixer.configuration.external_fx[0].wet -
              0.5F) < 0.0001F,
          "scene recall atomically restores mixer snapshot");
    const auto scene_id = scene_state.routing_scenes[0].id;
    check(application
                  .execute({"mixer.scene-rename.v1",
                            {{"id", static_cast<std::int64_t>(scene_id)},
                             {"name", "Live renamed"}}})
                  .accepted &&
              application
                  .execute({"mixer.scene-delete.v1",
                            {{"id", static_cast<std::int64_t>(scene_id)}}})
                  .accepted &&
              application.snapshot().routing_scenes.empty(),
          "routing scene rename and delete actions");
    GuiModel gui(application);
    for (int i = 0; i < 7; ++i)
        check(gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200),
              "navigate to mixer view");
    check(gui.view() == GuiView::mixer &&
              gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1920, 1200),
          "headless mixer GUI navigation");
    check(gui.input({GuiInput::Type::key_down, GuiKey::right}, 1024, 600) &&
              !gui.frame(1024, 600).commands.empty(),
          "compact encoder mixer adjustment");
    check(
        gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200) &&
            gui.view() == GuiView::routing &&
            gui.input({GuiInput::Type::key_down, GuiKey::right}, 1920, 1200) &&
            !gui.frame(1024, 600).commands.empty(),
        "headless external routing GUI interaction");
    check(gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1920, 1200) &&
              gui.view() == GuiView::modulation &&
              !gui.frame(1920, 1200).commands.empty() &&
              gui.input({GuiInput::Type::key_down, GuiKey::tab}, 1024, 600) &&
              gui.view() == GuiView::midi &&
              gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1024, 600) &&
              application.snapshot().midi.clock_source ==
                  MidiClockSource::external &&
              application
                  .execute({"midi.clock-forward-set.v1", {{"enabled", true}}})
                  .accepted &&
              application.snapshot().midi.forward_external_clock &&
              !gui.frame(1024, 600).commands.empty(),
          "responsive modulation GUI navigation");
}

void test_lua_and_offline() {
    using namespace neurogenic;
    bool submitted = false;
    Timeline lua_published;
    TimelineService timeline([&](const Timeline &value, std::string &) {
        lua_published = value;
        return true;
    });
    ScriptRuntime lua(
        [&](Pattern pattern) {
            submitted = pattern.steps[0].enabled && pattern.steps[0].note == 64;
            return true;
        },
        [] { return 123ULL; }, &timeline);
    std::string error;
#ifdef NEUROGENIC_HAS_LUA
    check(lua.execute("local "
                      "p=neurogenic.v1.pattern.new();p:set_step(1,true,64,0.75)"
                      ";neurogenic.v1.pattern.queue(p)",
                      error) &&
              submitted,
          "Lua execution");
    check(!lua.execute("this is not lua", error) && !error.empty(),
          "invalid Lua accepted");
    check(!lua.execute(
              "local p=neurogenic.v1.pattern.new();p:set_step(1,true,200,1)",
              error),
          "invalid Lua pattern accepted");
    const auto script_path =
        std::filesystem::temp_directory_path() / "neurogenic-script-test.lua";
    {
        std::ofstream script(script_path);
        script << "local p=neurogenic.v1.pattern.new(); "
                  "p:set_step(1,true,64,1); neurogenic.v1.pattern.queue(p)\n";
    }
    submitted = false;
    check(lua.execute_file(script_path, error) && submitted,
          "Lua file execution");
    std::filesystem::remove(script_path);
    check(
        lua.execute("local p=neurogenic.v1.timeline.current(); local "
                    "a=p:create_track(1,'kick',3840); "
                    "a:add_note{tick=0,pitch=36,velocity=1,duration=120}; "
                    "local b=p:create_track(2,'hat',720); "
                    "b:generate_euclidean(6,3,0,42,0.7,60); p:set_tempo(110); "
                    "p:set_time_signature(4,4); p:set_seed(123); p:commit()",
                    error),
        error.c_str());
    auto timeline_state = timeline.snapshot();
    check(timeline_state.revision == 2 &&
              timeline_state.generation_seed == 123 &&
              timeline_state.timeline.tracks.size() == 2 &&
              timeline_state.timeline.tracks[1].pattern.length == 720,
          "Lua multitrack generation");
    check(lua.execute(
              "local p=neurogenic.v1.timeline.current(); local t=p:track(1); "
              "t:add_note{tick=480,pitch=42,velocity=.6,duration=120,"
              "probability=.4,condition='every-n',condition_n=2,"
              "condition_m=2,ratchets=3}; p:commit()",
              error),
          error.c_str());
    timeline_state = timeline.snapshot();
    const auto &lua_trigger =
        timeline_state.timeline.tracks[0].pattern.events.back();
    check(std::abs(lua_trigger.trigger_probability - 0.4F) < 0.001F &&
              lua_trigger.trigger_condition ==
                  TriggerCondition::every_nth_loop &&
              lua_trigger.ratchet_count == 3,
          "Lua trigger property adapter");
    const auto preserved = timeline_state.timeline.tracks[1].pattern.events;
    check(
        lua.execute("local p=neurogenic.v1.timeline.current(); local "
                    "t=p:track(1); t:transpose(2); t:quantize(120); p:commit()",
                    error),
        error.c_str());
    timeline_state = timeline.snapshot();
    check(timeline_state.timeline.tracks[0].pattern.events[0].pitch == 38 &&
              timeline_state.timeline.tracks[1].pattern.events.size() ==
                  preserved.size() &&
              timeline_state.timeline.tracks[1].pattern.events[0].pitch ==
                  preserved[0].pitch,
          "Lua mutation did not preserve unchanged track");
    check(
        lua.execute(
            "local p=neurogenic.v1.timeline.current(); local t=p:track(1); "
            "t:add_parameter{tick=0,destination='effect.delay.mix',value=0.3}; "
            "p:commit()",
            error),
        error.c_str());
    timeline_state = timeline.snapshot();
    check(std::any_of(timeline_state.timeline.tracks[0].pattern.events.begin(),
                      timeline_state.timeline.tracks[0].pattern.events.end(),
                      [](const MusicalEvent &event) {
                          return event.parameter_destination ==
                                 "effect.delay.mix";
                      }),
          "Lua parameter destination adapter");
    const auto before_error = timeline.snapshot();
    check(!lua.execute("local p=neurogenic.v1.timeline.current(); "
                       "p:track(1):set_name('must rollback'); p:commit(); "
                       "error('after commit')",
                       error),
          "Lua error after staged commit accepted");
    check(timeline.snapshot().revision == before_error.revision &&
              timeline.snapshot().timeline.tracks[0].name ==
                  before_error.timeline.tracks[0].name,
          "Lua error failed to roll back transaction");
    check(!lua.execute(
              "local p=neurogenic.v1.timeline.current(); "
              "p:track(1):add_note{tick=-1,pitch=36,duration=10}; p:commit()",
              error),
          "invalid Lua timeline edit accepted");
    auto conflicting = timeline.edit();
    check(conflicting.set_track_name(1, "external", error) &&
              timeline.commit(std::move(conflicting),
                              TimelineCommitPolicy::next_audio_callback, error),
          "external edit before Lua conflict test");
    check(!lua.execute("local first=neurogenic.v1.timeline.current(); local "
                       "second=neurogenic.v1.timeline.current(); "
                       "first:track(1):set_name('first'); first:commit(); "
                       "second:track(1):set_name('stale'); second:commit()",
                       error),
          "multiple Lua commits accepted");
    check(timeline.snapshot().timeline.tracks[0].name == "external",
          "failed Lua execution published a partial edit");
    check(!lua.execute("while true do end", error) &&
              error.find("instruction limit") != std::string::npos,
          "Lua instruction limit");
    check(
        lua.execute(
            "local a=neurogenic.random(99); local b=neurogenic.random(99); for "
            "i=1,100 do assert(a:integer(1,100000)==b:integer(1,100000)) end",
            error),
        "Lua deterministic random stream");
    auto run_examples = [](const std::vector<std::string> &scripts) {
        TimelineService example_service(
            [](const Timeline &, std::string &) { return true; });
        ScriptRuntime runtime([](Pattern) { return true; }, [] { return 0ULL; },
                              &example_service);
        std::string example_error;
        for (const auto &script : scripts)
            check(runtime.execute_file(
                      std::filesystem::path(NEUROGENIC_SOURCE_DIR) /
                          "examples" / script,
                      example_error),
                  example_error.c_str());
        return example_service.snapshot();
    };
    const auto drum_example =
        run_examples({"timeline_drums.lua", "timeline_mutate.lua"});
    check(drum_example.timeline.tracks.size() == 4 &&
              drum_example.revision == 3,
          "drum and mutation examples");
    const auto poly_example = run_examples({"timeline_polyrhythm.lua"});
    check(poly_example.timeline.tracks.size() == 3 &&
              poly_example.timeline.tracks[0].pattern.length !=
                  poly_example.timeline.tracks[1].pattern.length,
          "polyrhythm example");
    const auto variation_example = run_examples({"timeline_variation.lua"});
    check(variation_example.generation_seed == 20261119 &&
              variation_example.timeline.tracks.size() == 1,
          "variation example");
#else
    check(!lua.execute("return 1", error) && !error.empty(),
          "Lua unexpectedly available");
#endif
    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-offline-test.wav";
    AudioEngine offline;
    offline.set_sample(constant_sample(16));
    offline.trigger(60, 1);
    offline.render_wav(path, 64);
    std::ifstream file(path, std::ios::binary);
    std::array<char, 4> riff{};
    file.read(riff.data(), 4);
    check(std::string(riff.data(), 4) == "RIFF", "offline WAV");
    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        test_appliance_lifecycle();
        test_hardware_diagnostics();
        test_performance_monitoring();
        test_midi_routing_and_clock();
        test_modulation_engine();
        test_performance_macros();
        test_precise_macro_morphing();
        test_polyphonic_sample_modulation();
        test_native_synthesis_and_dsp();
        test_native_effect_chains();
        test_queue_and_scheduler();
        test_polyphony_and_events();
        test_sample_slots();
        test_live_sample_management_and_resampling();
        test_sample_slicing();
        test_expressive_sample_playback();
        test_realtime_time_stretch();
        test_timeline_and_tracks();
        test_pattern_clip_arrangement();
        test_live_arrangement_recording();
        test_unified_project_persistence();
        test_audio_recording_and_resampling();
        test_mixer_and_routing();
        test_transport_integration_and_timeline_io();
        test_midi_control_dispatch();
        test_timeline_editing_and_generation();
        test_application_control_and_controllers();
        test_physical_controller_protocol();
        test_controller_agnostic_sessions();
        test_interactive_step_sequencer();
        test_undo_redo_and_step_properties();
        test_parameter_locks_and_automation();
        test_probability_conditions_and_ratchets();
        test_gui_model();
        test_lua_and_offline();
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
