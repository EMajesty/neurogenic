#pragma once
#include "neurogenic/audio/mixer.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/dsp/macro_morph.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/dsp/purest_gain.hpp"
#include "neurogenic/dsp/time_stretch.hpp"
#include "neurogenic/dsp/voice_modulation.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/sequencer/scheduler.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
namespace neurogenic {
class RecordingService;
enum class SamplePlaybackMode : std::uint8_t { one_shot, gate, toggle };
enum class RegionFadeCurve : std::uint8_t { linear, equal_power };
struct SampleRegionPlayback {
    std::uint64_t fade_in_frames{}, fade_out_frames{};
    std::uint64_t loop_start_frame{}, loop_end_frame{};
    std::uint32_t loop_crossfade_frames{};
    RegionFadeCurve fade_curve{RegionFadeCurve::linear};
    bool loop_enabled{};
};
struct SamplePlaybackSettings {
    SamplePlaybackMode mode{SamplePlaybackMode::one_shot};
    std::uint8_t choke_group{};
    bool choke_release{true};
    float semitones{}, fine_cents{}, gain{1.0F};
    std::int16_t root_note{-1};
    bool reverse{};
    StretchMode stretch_mode{StretchMode::off};
    StretchQuality stretch_quality{StretchQuality::balanced};
    float stretch_ratio{1.0F};
    float source_beats{}, source_bpm{};
    float attack_seconds{}, decay_seconds{}, sustain{1.0F};
    float release_seconds{0.005F};
    friend bool operator==(const SamplePlaybackSettings &,
                           const SamplePlaybackSettings &) = default;
};
inline constexpr std::size_t maximum_preset_samples = 16;
inline constexpr std::size_t maximum_preset_synth_parameters = 64;
struct PresetSampleActivation {
    SampleSlotId slot{invalid_sample_slot};
    std::shared_ptr<const Sample> sample;
};
struct PresetSynthParameterActivation {
    InstrumentInstanceId instance{default_synth_instance_id};
    SynthParameter parameter{SynthParameter::gain};
    float value{};
};
struct PresetPadActivation {
    SampleSlotId slot{invalid_sample_slot};
    std::uint64_t start_frame{}, end_frame{};
    float gain{1.0F};
    SamplePlaybackSettings playback;
    SampleRegionPlayback region;
    bool assigned{};
};
struct PadMappingActivation {
    SampleSlotId slot{invalid_sample_slot};
    std::uint64_t publication{}, start_frame{}, end_frame{};
    float gain{1.0F};
    SamplePlaybackSettings playback;
    SampleRegionPlayback region;
    bool assigned{};
};
struct PresetActivationRequest {
    MixerConfiguration mixer;
    ModulationConfiguration modulation;
    std::array<PresetSampleActivation, maximum_preset_samples> samples{};
    std::size_t sample_count{};
    std::array<PresetSynthParameterActivation,
               maximum_preset_synth_parameters>
        synth_parameters{};
    std::size_t synth_parameter_count{};
    std::array<PresetPadActivation, 16> pads{};
    bool replace_pad_mappings{};
    bool release_existing_voices{};
};
class AudioEngine {
  public:
    struct TimelineActivationEvent {
        std::uint64_t frame{};
    };
    static constexpr std::size_t default_voice_capacity = 32;
    static constexpr std::size_t default_note_bank_size = 16;
    static constexpr std::size_t default_sample_version_capacity = 64;
    explicit AudioEngine(
        std::uint32_t rate = 48000,
        std::size_t voice_capacity = default_voice_capacity,
        std::size_t event_staging_capacity = 512,
        std::size_t sample_version_capacity = default_sample_version_capacity);
    ~AudioEngine();
    void
    set_sample(Sample sample); // compatibility: slot 0, mapped from every note
    void set_sample_slot(SampleSlotId id, Sample sample);
    [[nodiscard]] bool
    publish_sample_slot(SampleSlotId id, Sample sample,
                        std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    publish_sample_slot(SampleSlotId id, std::shared_ptr<const Sample> sample,
                        std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    unload_sample_slot(SampleSlotId id,
                       std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    sample_update_applied(std::uint64_t publication) const noexcept {
        return publication != 0 &&
               applied_sample_update_sequence_.load(
                   std::memory_order_acquire) >= publication;
    }
    void reclaim_sample_versions() noexcept;
    [[nodiscard]] bool prepare_gapless_handoff(
        SampleSlotId, std::shared_ptr<Sample>, TrackId, EventId,
        std::uint64_t loop_frames, std::string &error) noexcept;
    void cancel_gapless_handoff() noexcept;
    [[nodiscard]] bool gapless_handoff_prepared() const noexcept;
    [[nodiscard]] bool gapless_handoff_completed() const noexcept;
    [[nodiscard]] std::uint64_t gapless_first_playback_frame() const noexcept;
    [[nodiscard]] bool map_note_to_slot(std::uint8_t note,
                                        SampleSlotId id) noexcept;
    [[nodiscard]] bool has_sample_slot(SampleSlotId id) const noexcept;
    [[nodiscard]] std::size_t sample_slot_count() const noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return rate_; }
    void trigger(std::uint8_t note, float velocity,
                 std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool trigger_pad(std::uint8_t note, float velocity,
                                   bool note_on = true,
                                   float pitch_offset = 0.0F,
                                   std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool trigger_slice(SampleSlotId, std::uint64_t publication,
                                     std::uint64_t start_frame,
                                     std::uint64_t end_frame, float slice_gain,
                                     float velocity,
                                     std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool trigger_region(
        SampleSlotId, std::uint64_t publication, std::uint64_t start_frame,
        std::uint64_t end_frame, float gain, SamplePlaybackSettings,
        SampleRegionPlayback, float velocity,
        std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool stop_slice_previews() noexcept;
    [[nodiscard]] bool map_note_to_slice(std::uint8_t note, SampleSlotId,
                                         std::uint64_t publication,
                                         std::uint64_t start_frame,
                                         std::uint64_t end_frame,
                                         float gain = 1.0F,
                                         SamplePlaybackSettings = {}) noexcept;
    [[nodiscard]] bool map_note_to_region(
        std::uint8_t note, SampleSlotId, std::uint64_t publication,
        std::uint64_t start_frame, std::uint64_t end_frame, float gain,
        SamplePlaybackSettings, SampleRegionPlayback) noexcept;
    [[nodiscard]] bool clear_note_slice(std::uint8_t note) noexcept;
    [[nodiscard]] bool publish_pad_mappings(
        const std::array<PadMappingActivation, 16> &,
        std::uint16_t changed_mask) noexcept;
    [[nodiscard]] bool configure_note_playback(std::uint8_t note,
                                               SamplePlaybackSettings) noexcept;
    void trigger_synth(
        std::uint8_t note, float velocity, bool note_on = true,
        InstrumentInstanceId instance = default_synth_instance_id) noexcept;
    [[nodiscard]] bool set_synth_parameter(
        SynthParameter, float,
        InstrumentInstanceId instance = default_synth_instance_id) noexcept;
    [[nodiscard]] bool set_delay_parameter(DelayParameter, float) noexcept;
    [[nodiscard]] bool set_delay_bypass(bool) noexcept;
    [[nodiscard]] bool
    publish_modulation(const ModulationConfiguration &configuration) noexcept;
    [[nodiscard]] bool set_modulation_input(ModulationSourceId, float) noexcept;
    [[nodiscard]] bool set_modulator_value(ModulatorInstanceId, float) noexcept;
    [[nodiscard]] bool schedule_macro_morph(const MacroMorphCommand &) noexcept;
    [[nodiscard]] bool stop_macro_morph(ModulatorInstanceId) noexcept;
    [[nodiscard]] std::array<MacroMorphRuntime, MacroMorphProcessor::capacity>
    macro_morphs() const noexcept;
    [[nodiscard]] std::uint64_t dropped_macro_morph_commands() const noexcept {
        return dropped_macro_morph_commands_.load(std::memory_order_acquire);
    }
    [[nodiscard]] ModulationSnapshot modulation_snapshot() const noexcept {
        return modulation_.snapshot();
    }
    [[nodiscard]] bool publish_mixer(const MixerConfiguration &configuration,
                                     std::string &error) noexcept {
        return mixer_.publish(configuration, error);
    }
    [[nodiscard]] bool publish_preset_activation(
        const PresetActivationRequest &, std::uint64_t &sequence,
        std::array<std::uint64_t, maximum_preset_samples> &sample_publications,
        std::string &error) noexcept;
    [[nodiscard]] bool preset_activation_applied(
        std::uint64_t sequence) const noexcept {
        return sequence != 0 &&
               applied_preset_activation_.load(std::memory_order_acquire) >=
                   sequence;
    }
    [[nodiscard]] bool cancel_preset_activation(std::uint64_t) noexcept;
    [[nodiscard]] bool preset_activation_cancelled(
        std::uint64_t sequence) const noexcept {
        return sequence != 0 &&
               cancelled_preset_activation_.load(std::memory_order_acquire) >=
                   sequence;
    }
    void reclaim_preset_activations() noexcept;
    [[nodiscard]] bool finalize_preset_activation(
        std::uint64_t sequence) noexcept;
    [[nodiscard]] MixerState mixer_state() const noexcept {
        return mixer_.state();
    }
    bool queue_pattern(const Pattern &pattern) noexcept;
    bool queue_timeline(const Timeline &, std::string &error);
    bool queue_timeline_at(const Timeline &, std::uint64_t activation_frame,
                           VoiceTransitionPolicy, std::string &error,
                           std::span<const TrackId> affected_tracks = {});
    [[nodiscard]] std::uint64_t
    last_timeline_activation_frame() const noexcept {
        return last_timeline_activation_frame_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t timeline_activation_count() const noexcept {
        return timeline_activation_count_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool
    pop_timeline_activation(TimelineActivationEvent &event) noexcept {
        return timeline_activations_.pop(event);
    }
    [[nodiscard]] std::uint64_t dropped_timeline_activations() const noexcept {
        return dropped_timeline_activations_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t last_timeline_lateness_frames() const noexcept {
        return last_timeline_lateness_frames_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t song_end_count() const noexcept {
        return song_end_count_.load(std::memory_order_acquire);
    }
    void play() noexcept;
    void stop() noexcept;
    void restart() noexcept;
    [[nodiscard]] bool seek_frame(std::uint64_t frame) noexcept;
    void midi_panic() noexcept;
    [[nodiscard]] bool set_fill(bool active) noexcept;
    void process(std::span<float> left, std::span<float> right) noexcept;
    void process_duplex(std::span<float> left, std::span<float> right,
                        std::span<const float> interleaved_input,
                        std::uint16_t input_channels) noexcept;
    void process_io(std::span<float> interleaved_output,
                    std::uint16_t output_channels,
                    std::span<const float> interleaved_input = {},
                    std::uint16_t input_channels = 0) noexcept;
    void set_recording_service(RecordingService *service) noexcept {
        recording_.store(service, std::memory_order_release);
    }
    void set_midi_output_scheduler(MidiOutputScheduler *scheduler) noexcept {
        midi_output_.store(scheduler, std::memory_order_release);
    }
    [[nodiscard]] bool publish_midi_clock_output(
        const MidiClockOutputConfiguration &configuration) noexcept {
        return midi_clock_configurations_.push(configuration);
    }
    [[nodiscard]] bool publish_midi_routing(
        const MidiRoutingConfiguration &configuration) noexcept {
        return midi_routing_configurations_.push(configuration);
    }
    [[nodiscard]] bool receive_midi(const MidiPacket &packet) noexcept {
        if (midi_inputs_.push(packet))
            return true;
        dropped_midi_inputs_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    [[nodiscard]] std::uint64_t dropped_midi_inputs() const noexcept {
        return dropped_midi_inputs_.load(std::memory_order_acquire);
    }
    [[nodiscard]] ExternalClockState external_clock_state() const noexcept {
        ExternalClockState state;
        state.running = external_clock_running_.load(std::memory_order_acquire);
        state.available =
            external_clock_available_.load(std::memory_order_acquire);
        state.dropout = external_clock_dropout_.load(std::memory_order_acquire);
        state.tempo_bpm = external_clock_tempo_.load(std::memory_order_acquire);
        state.pulses = external_clock_pulses_.load(std::memory_order_acquire);
        state.last_timestamp_ns =
            external_clock_last_timestamp_.load(std::memory_order_acquire);
        state.sync_status = static_cast<MidiSyncStatus>(
            external_clock_status_.load(std::memory_order_acquire));
        state.phase_error_pulses =
            external_clock_phase_error_.load(std::memory_order_acquire);
        state.song_position =
            external_song_position_.load(std::memory_order_acquire);
        return state;
    }
    void capture_external_input(std::span<const float> interleaved,
                                std::uint16_t channels) noexcept;
    void recording_device_stopped() noexcept;
    [[nodiscard]] std::uint64_t callback_frame_position() const noexcept {
        return callback_frame_.load(std::memory_order_acquire);
    }
    void render_wav(const std::filesystem::path &, std::uint64_t frames);
    [[nodiscard]] std::uint64_t frame_position() const noexcept {
        return published_frame_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool playing() const noexcept {
        return published_playing_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool fill_active() const noexcept {
        return published_fill_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t dropped_triggers() const noexcept {
        return dropped_triggers_.load();
    }
    [[nodiscard]] std::uint64_t dropped_patterns() const noexcept {
        return dropped_patterns_.load();
    }
    [[nodiscard]] std::uint64_t stolen_voices() const noexcept {
        return stolen_voices_.load();
    }
    [[nodiscard]] std::uint64_t missing_slot_triggers() const noexcept {
        return missing_slot_triggers_.load();
    }
    [[nodiscard]] std::uint64_t dropped_sample_updates() const noexcept {
        return dropped_sample_updates_.load();
    }
    [[nodiscard]] std::uint64_t reclaimed_sample_versions() const noexcept {
        return reclaimed_sample_versions_.load();
    }
    [[nodiscard]] std::uint64_t dropped_timelines() const noexcept {
        return dropped_timelines_.load();
    }
    [[nodiscard]] std::uint64_t dropped_timeline_events() const noexcept {
        return dropped_timeline_events_.load();
    }
    [[nodiscard]] std::uint64_t applied_timeline_parameters() const noexcept {
        return applied_timeline_parameters_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float last_timeline_parameter_value() const noexcept {
        return last_timeline_parameter_value_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t timeline_event_overruns() const noexcept {
        return dropped_timeline_events_.load();
    }
    [[nodiscard]] std::uint64_t dropped_legacy_events() const noexcept {
        return dropped_legacy_events_.load();
    }
    [[nodiscard]] std::uint64_t dropped_transport_commands() const noexcept {
        return dropped_transport_commands_.load();
    }
    [[nodiscard]] std::uint64_t reclaimed_timeline_plans() const noexcept {
        return reclaimed_timeline_plans_.load();
    }
    [[nodiscard]] std::size_t voice_capacity() const noexcept {
        return voices_.size();
    }
    [[nodiscard]] std::size_t active_voice_count() const noexcept {
        return callback_monitor_.active_voices();
    }
    [[nodiscard]] AudioCallbackStatistics callback_statistics() const noexcept {
        return callback_monitor_.snapshot();
    }
    void set_performance_monitoring(bool enabled) noexcept {
        callback_monitor_.set_enabled(enabled);
    }
    [[nodiscard]] std::uint32_t buffer_frames() const noexcept {
        return buffer_frames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] InstrumentState synth_state() const noexcept {
        InstrumentState total{};
        for (const auto &synth : synths_) {
            const auto state = synth->state();
            total.active_voices += state.active_voices;
            total.maximum_voices += state.maximum_voices;
            total.stolen_voices += state.stolen_voices;
        }
        return total;
    }
    [[nodiscard]] InstrumentState
    synth_instance_state(InstrumentInstanceId instance) const noexcept {
        return instance > 0 && instance <= synths_.size()
                   ? synths_[instance - 1]->state()
                   : InstrumentState{};
    }
    [[nodiscard]] std::uint64_t dropped_dsp_commands() const noexcept {
        return dropped_dsp_commands_.load();
    }
    [[nodiscard]] std::uint64_t instrument_nanoseconds() const noexcept {
        return instrument_nanoseconds_.load();
    }
    [[nodiscard]] std::uint64_t effect_nanoseconds() const noexcept {
        return effect_nanoseconds_.load();
    }

  private:
    struct Trigger {
        std::uint8_t note{};
        float velocity{};
        std::uint32_t frame_offset{};
        std::uint64_t order{};
        SampleSlotId slot{invalid_sample_slot};
        std::uint64_t publication{}, start_frame{}, end_frame{};
        float slice_gain{1.0F};
        SamplePlaybackSettings playback;
        SampleRegionPlayback region;
        float pitch_offset{};
        bool sliced{}, region_trigger{}, stop_previews{}, note_on{true},
            pad_event{};
    };
    struct SynthTrigger {
        std::uint8_t note{};
        float velocity{};
        bool note_on{};
        InstrumentInstanceId instance{default_synth_instance_id};
    };
    struct DspCommand {
        enum class Target : std::uint8_t {
            synth,
            delay,
            delay_bypass,
            modulation_input,
            modulator_value,
            macro_morph
        };
        Target target{};
        std::uint32_t parameter{};
        float value{};
        InstrumentInstanceId instance{};
        MacroMorphCommand morph;
    };
    struct Voice {
        const Sample *sample{};
        std::size_t sample_version{};
        double position{}, increment{1.0}, base_increment{1.0};
        TimeStretchConfiguration stretch_configuration;
        TimeStretchState stretch_state;
        float stretch_ratio_target{1.0F}, stretch_explicit_ratio{1.0F},
            stretch_source_beats{}, stretch_source_bpm{};
        std::uint64_t start_frame{}, end_frame{};
        std::uint64_t clip_elapsed_frames{}, clip_total_frames{},
            clip_fade_in_frames{}, clip_fade_out_frames{},
            crossfade_in_frames{}, crossfade_out_offset_frames{},
            crossfade_out_frames{};
        SampleRegionPlayback region;
        float gain{}, base_gain{}, base_pan{}, modulation_gain{1.0F}, modulation_pan{},
            modulation_pitch{}, envelope_level{1.0F};
        SampleVoiceModulationState voice_modulation;
        enum class EnvelopeStage : std::uint8_t {
            attack,
            decay,
            sustain,
            release
        } envelope_stage{EnvelopeStage::sustain};
        float attack_step{}, decay_step{}, release_step{}, release_samples{1},
            sustain{1.0F};
        std::uint64_t age{};
        TrackId track{};
        EventId event{};
        std::uint64_t occurrence{};
        std::uint8_t pitch{};
        std::uint8_t trigger_note{}, choke_group{};
        SamplePlaybackMode mode{SamplePlaybackMode::one_shot};
        bool choke_release{}, reverse{}, stretched{}, timeline_owned{},
            preview{}, audio_clip{}, active{};
        AudioClipFadeCurve clip_fade_curve{AudioClipFadeCurve::linear};
        AudioClipFadeCurve crossfade_in_curve{AudioClipFadeCurve::linear};
        AudioClipFadeCurve crossfade_out_curve{AudioClipFadeCurve::linear};
    };
    enum class SampleVersionState : std::uint8_t {
        free,
        queued,
        active,
        retiring,
        retired
    };
    struct SampleVersion {
        std::atomic<SampleVersionState> state{SampleVersionState::free};
        std::atomic<std::uint32_t> id{invalid_sample_slot.value};
        std::shared_ptr<const Sample> sample;
        std::atomic<std::uint64_t> publication{};
    };
    struct SliceTarget {
        SampleSlotId slot{invalid_sample_slot};
        std::uint64_t publication{}, start_frame{}, end_frame{};
        float gain{1.0F};
        float pan{};
        std::uint64_t source_offset_frames{};
        std::uint64_t clip_total_frames{}, clip_fade_in_frames{},
            clip_fade_out_frames{}, crossfade_in_frames{},
            crossfade_out_offset_frames{}, crossfade_out_frames{};
        SamplePlaybackSettings playback;
        SampleRegionPlayback region;
        AudioClipFadeCurve clip_fade_curve{AudioClipFadeCurve::linear};
        AudioClipFadeCurve crossfade_in_curve{AudioClipFadeCurve::linear};
        AudioClipFadeCurve crossfade_out_curve{AudioClipFadeCurve::linear};
        bool audio_clip{}, enabled{};
    };
    struct SliceMapCommand {
        enum class Type : std::uint8_t { map, clear, settings };
        Type type{Type::map};
        std::uint8_t note{};
        SliceTarget target;
    };
    struct PadMappingBatch {
        std::array<SliceTarget, 16> targets{};
        std::uint16_t changed_mask{};
    };
    struct SampleUpdate {
        enum class Type : std::uint8_t { install, unload };
        Type type{};
        SampleSlotId id{};
        std::size_t version{};
        std::uint64_t sequence{};
    };
    enum class PresetActivationState : std::uint8_t {
        free,
        queued,
        cancelled,
        applied
    };
    struct PreparedPresetActivation {
        std::atomic<PresetActivationState> state{PresetActivationState::free};
        std::uint64_t sequence{};
        MixerConfiguration mixer;
        ModulationConfiguration modulation;
        std::array<std::size_t, maximum_preset_samples> sample_versions{};
        std::array<std::uint64_t, maximum_preset_samples> sample_publications{};
        std::size_t sample_count{};
        std::array<PresetSynthParameterActivation,
                   maximum_preset_synth_parameters>
            synth_parameters{};
        std::size_t synth_parameter_count{};
        std::array<PresetPadActivation, 16> pads{};
        bool replace_pad_mappings{}, release_existing_voices{};
    };
    class SampleUpdateQueue {
      public:
        explicit SampleUpdateQueue(std::size_t capacity)
            : items_(std::make_unique<SampleUpdate[]>(capacity)),
              capacity_(capacity) {}
        bool push(const SampleUpdate &item) noexcept {
            const auto head = head_.load(std::memory_order_relaxed);
            const auto next = (head + 1) % capacity_;
            if (next == tail_.load(std::memory_order_acquire))
                return false;
            items_[head] = item;
            head_.store(next, std::memory_order_release);
            return true;
        }
        bool pop(SampleUpdate &item) noexcept {
            const auto tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire))
                return false;
            item = items_[tail];
            tail_.store((tail + 1) % capacity_, std::memory_order_release);
            return true;
        }

      private:
        std::unique_ptr<SampleUpdate[]> items_;
        std::size_t capacity_{};
        std::atomic<std::size_t> head_{}, tail_{};
    };
    enum class PlanState : std::uint8_t { free, queued, active, retired };
    struct PlanSlot {
        std::atomic<PlanState> state{PlanState::free};
        std::unique_ptr<PlaybackPlan> plan;
    };
    struct TimelinePublication {
        static constexpr std::size_t maximum_affected_tracks =
            maximum_mixer_tracks;
        std::uint8_t slot{};
        std::uint64_t activation_frame{};
        VoiceTransitionPolicy voice_policy{VoiceTransitionPolicy::preserve};
        std::array<TrackId, maximum_affected_tracks> affected_tracks{};
        std::uint16_t affected_track_count{};
    };
    enum class TransportCommand : std::uint8_t {
        play,
        stop,
        restart,
        midi_panic,
        fill_on,
        fill_off
    };
    [[nodiscard]] std::size_t
    find_active_sample(SampleSlotId id) const noexcept;
    void start_voice(std::uint8_t note, float velocity, TrackId track = 0,
                     EventId event = 0, std::uint64_t occurrence = 0,
                     bool timeline_owned = false,
                     const SliceTarget *slice = nullptr, bool preview = false,
                     float pitch_offset = 0.0F) noexcept;
    void activate_gapless_handoff(std::uint64_t) noexcept;
    void stop_timeline_voice(TrackId, EventId, std::uint64_t occurrence,
                             bool immediate = false) noexcept;
    void release_note(std::uint8_t, TrackId = 0, EventId = 0,
                      std::uint64_t occurrence = 0,
                      bool timeline_owned = false) noexcept;
    void release_voice(Voice &, bool immediate = false) noexcept;
    void stop_voices(bool timeline_only = false) noexcept;
    void activate_timeline(const TimelinePublication &, std::uint64_t) noexcept;
    void cleanup_timeline_voices(VoiceTransitionPolicy, std::uint64_t,
                                 std::span<const TrackId> = {}) noexcept;
    void reclaim_retired_plans() noexcept;
    void reclaim_sample_versions_unlocked() noexcept;
    void activate_preset(std::size_t) noexcept;
    std::uint32_t rate_;
    Scheduler scheduler_;
    TimelineScheduler timeline_scheduler_;
    PurestGain effect_;
    SpscQueue<Trigger, 65> triggers_;
    SpscQueue<SynthTrigger, 65> synth_triggers_;
    SpscQueue<DspCommand, 129> dsp_commands_;
    MacroMorphProcessor macro_morphs_;
    std::array<std::atomic<std::uint64_t>, MacroMorphProcessor::capacity>
        published_morph_ids_{}, published_morph_instances_{},
        published_morph_start_frames_{}, published_morph_end_frames_{};
    std::array<std::atomic<std::uint32_t>, MacroMorphProcessor::capacity>
        published_morph_start_values_{}, published_morph_target_values_{},
        published_morph_effective_values_{}, published_morph_progress_{},
        published_morph_flags_{};
    std::atomic<std::uint64_t> dropped_macro_morph_commands_{};
    SpscQueue<ModulationConfiguration, 5> modulation_configurations_;
    SpscQueue<Pattern, 9> patterns_;
    static constexpr std::size_t timeline_plan_slot_count = 4;
    SpscQueue<TimelinePublication, 4> timelines_;
    SpscQueue<TimelineActivationEvent, 64> timeline_activations_;
    std::array<PlanSlot, timeline_plan_slot_count> timeline_plans_{};
    SpscQueue<TransportCommand, 16> transport_commands_;
    SpscQueue<std::uint64_t, 16> transport_seeks_;
    SpscQueue<MidiClockOutputConfiguration, 5> midi_clock_configurations_;
    SpscQueue<MidiRoutingConfiguration, 5> midi_routing_configurations_;
    SpscQueue<MidiPacket, 257> midi_inputs_;
    SpscQueue<SliceMapCommand, 65> slice_map_commands_;
    SpscQueue<PadMappingBatch, 5> pad_mapping_batches_;
    std::atomic<std::uint64_t> dropped_triggers_{}, dropped_patterns_{},
        dropped_timelines_{}, dropped_timeline_events_{},
        dropped_legacy_events_{}, dropped_transport_commands_{},
        reclaimed_timeline_plans_{}, published_frame_{}, next_order_{},
        stolen_voices_{}, missing_slot_triggers_{}, dropped_sample_updates_{},
        reclaimed_sample_versions_{};
    std::atomic<std::uint64_t> last_timeline_activation_frame_{},
        last_timeline_lateness_frames_{}, song_end_count_{};
    std::atomic<std::uint64_t> timeline_activation_count_{};
    std::atomic<std::uint64_t> dropped_timeline_activations_{};
    std::atomic<std::uint64_t> applied_timeline_parameters_{};
    std::atomic<float> last_timeline_parameter_value_{};
    std::atomic<std::uint64_t> applied_sample_update_sequence_{};
    std::atomic<std::uint64_t> dropped_dsp_commands_{};
    std::uint64_t next_sample_update_sequence_{};
    std::mutex sample_update_producer_mutex_;
    SampleUpdateQueue sample_updates_;
    static constexpr std::size_t preset_activation_capacity = 4;
    SpscQueue<std::uint8_t, preset_activation_capacity + 1>
        preset_activations_;
    std::unique_ptr<PreparedPresetActivation[]> preset_activation_slots_;
    std::atomic<std::uint64_t> applied_preset_activation_{};
    std::atomic<std::uint64_t> cancelled_preset_activation_{};
    std::uint64_t next_preset_activation_{};
    std::unique_ptr<SampleVersion[]> sample_versions_;
    std::size_t sample_version_capacity_{};
    struct GaplessHandoff {
        // 0 idle, 1 prepared, 2 callback-activated. A single atomic phase
        // makes cancellation and boundary activation mutually exclusive.
        std::atomic<std::uint8_t> phase{};
        std::atomic<std::uint64_t> first_frame{};
        std::size_t sample_version{std::numeric_limits<std::size_t>::max()};
        SampleSlotId slot{invalid_sample_slot};
        TrackId track{};
        EventId clip{};
        std::uint64_t loop_frames{};
    } gapless_handoff_;
    std::atomic<std::size_t> reserved_handoff_voices_{};
    std::atomic<std::size_t> active_voice_count_{};
    bool split_processing_{};
    std::array<SampleSlotId, 128> note_slots_{};
    std::array<SliceTarget, 128> note_slices_{};
    std::vector<Voice> voices_;
    std::vector<ScheduledEvent> legacy_events_;
    std::vector<TimelineScheduledEvent> timeline_events_;
    int active_timeline_slot_{-1};
    TimelinePublication pending_timeline_{};
    bool has_pending_timeline_{};
    std::uint64_t active_timeline_end_frame_{};
    std::array<Trigger, 64> pending_triggers_{};
    std::size_t pending_trigger_count_{};
    std::uint64_t next_voice_age_{};
    std::uint64_t transport_frame_{};
    float project_tempo_bpm_{120.0F};
    bool transport_playing_{true};
    std::atomic<bool> processing_started_{};
    std::atomic<bool> published_playing_{true};
    std::atomic<bool> published_fill_{};
    CallbackMonitor callback_monitor_;
    std::atomic<std::uint32_t> buffer_frames_{};
    std::atomic<std::uint64_t> instrument_nanoseconds_{}, effect_nanoseconds_{};
    std::atomic<RecordingService *> recording_{};
    std::atomic<MidiOutputScheduler *> midi_output_{};
    MidiClockGenerator midi_clock_;
    MidiClockFollower midi_clock_follower_;
    MidiRouter midi_router_;
    std::array<std::array<std::array<bool, 128>, 16>, maximum_midi_ports>
        timeline_midi_notes_{};
    using MidiNoteTrackOwners =
        std::array<std::array<std::array<TrackId, 128>, 16>,
                   maximum_midi_ports>;
    std::unique_ptr<MidiNoteTrackOwners> timeline_midi_note_tracks_{
        std::make_unique<MidiNoteTrackOwners>()};
    MidiClockOutputConfiguration midi_clock_configuration_{};
    bool accept_external_clock_{};
    bool external_clock_lost_{};
    std::uint32_t active_timeline_tempo_us_{500000};
    std::atomic<bool> external_clock_running_{}, external_clock_available_{},
        external_clock_dropout_{};
    std::atomic<double> external_clock_tempo_{};
    std::atomic<std::uint64_t> external_clock_pulses_{};
    std::atomic<std::uint64_t> external_clock_last_timestamp_{};
    std::atomic<std::uint64_t> dropped_midi_inputs_{};
    std::atomic<std::uint8_t> external_clock_status_{};
    std::atomic<double> external_clock_phase_error_{};
    std::atomic<std::uint16_t> external_song_position_{};
    std::atomic<std::uint64_t> callback_frame_{};
    std::array<std::unique_ptr<SubtractiveSynth>, maximum_instrument_instances>
        synths_{};
    std::array<bool, maximum_instrument_instances> synth_active_{};
    StereoDelay delay_;
    ModulationEngine modulation_;
    Mixer mixer_;
    const float *duplex_input_{};
    std::size_t duplex_input_samples_{};
    std::uint16_t duplex_input_channels_{};
    float *physical_output_{};
    std::size_t physical_output_samples_{};
    std::uint8_t physical_output_channels_{};
};
} // namespace neurogenic
