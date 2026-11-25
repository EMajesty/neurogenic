#pragma once
#include "neurogenic/application/performance_macros.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/audio/live_loop.hpp"
#include "neurogenic/audio/sample_analysis.hpp"
#include "neurogenic/audio/sample_processing.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/project/preset.hpp"
#include "neurogenic/timeline/arrangement_recording.hpp"
#include "neurogenic/timeline/performance_launching.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neurogenic {
struct NormalizedXY {
    double x{}, y{};
    friend bool operator==(const NormalizedXY &,
                           const NormalizedXY &) = default;
};
using ApplicationValue = std::variant<std::monostate, bool, std::int64_t,
                                      double, std::string, NormalizedXY>;
enum class ValueType : std::uint8_t { none, boolean, integer, real, text, xy };
struct ArgumentSpec {
    std::string name;
    ValueType type{ValueType::none};
    bool required{true};
};
struct ActionDescriptor {
    std::string id, name, description, category;
    std::vector<ArgumentSpec> arguments;
    bool available{};
    std::string context, unavailable_reason;
};
class ActionRegistry {
  public:
    ActionRegistry();
    bool register_action(ActionDescriptor, std::string &error);
    [[nodiscard]] const ActionDescriptor *find(std::string_view) const noexcept;
    [[nodiscard]] const std::vector<ActionDescriptor> &
    actions() const noexcept {
        return actions_;
    }
    bool validate(std::string_view,
                  const std::map<std::string, ApplicationValue> &,
                  std::string &error) const;

  private:
    std::vector<ActionDescriptor> actions_;
};

enum class ParameterType : std::uint8_t { boolean, integer, real, enumeration };
struct ParameterDescriptor {
    std::string id, name, unit;
    ParameterType type{ParameterType::real};
    ApplicationValue minimum, maximum, default_value, current_value;
    double step{1};
    bool continuous{}, automatable{}, modulatable{};
    std::vector<std::string> choices;
    ApplicationValue effective_value{};
};
class ParameterRegistry {
  public:
    bool register_parameter(ParameterDescriptor, std::string &error);
    bool set(std::string_view, const ApplicationValue &, std::string &error);
    bool set_normalized(std::string_view, double, std::string &error);
    bool adjust(std::string_view, double steps, std::string &error);
    bool reset(std::string_view, std::string &error);
    [[nodiscard]] std::optional<ParameterDescriptor>
        get(std::string_view) const;
    [[nodiscard]] std::vector<ParameterDescriptor> parameters() const;

  private:
    bool valid_value(const ParameterDescriptor &, const ApplicationValue &,
                     std::string &) const;
    mutable std::mutex mutex_;
    std::map<std::string, ParameterDescriptor, std::less<>> values_;
};

enum class PadMode : std::uint8_t { step, pad, levels, notes };
enum class StepResolution : std::uint8_t {
    quarter,
    eighth,
    sixteenth,
    thirty_second,
    quarter_triplet,
    eighth_triplet,
    sixteenth_triplet,
    thirty_second_triplet
};
enum class Modifier : std::uint8_t { shift, select };
struct ModifierState {
    bool shift{}, select{}, shift_latched{}, select_latched{};
};
struct TrackSummary {
    TrackId id{};
    std::string name;
    bool muted{};
    MusicalTick pattern_length{};
    std::uint8_t target_note{36};
    std::string instrument_id{sample_instrument_id};
    std::vector<MusicalEvent> events;
    InstrumentInstanceId instrument_instance_id{default_synth_instance_id};
};
struct SelectedEventState {
    TrackId track_id{};
    EventId event_id{};
    std::int64_t step{};
    MusicalTick tick{}, timing_offset{};
    std::uint8_t pitch{};
    float velocity{};
    MusicalTick duration{};
    std::size_t index_at_step{}, events_at_step{};
    float trigger_probability{1.0F};
    TriggerCondition trigger_condition{TriggerCondition::always};
    std::uint16_t condition_n{1}, condition_m{1};
    std::uint8_t ratchet_count{1};
};
struct StepParameterLockState {
    EventId event_id{};
    std::string parameter_id;
    float value{};
};
struct StepSelectionState {
    PatternId pattern_id{};
    TrackId track_id{};
    std::int64_t step{};
    friend bool operator==(const StepSelectionState &,
                           const StepSelectionState &) = default;
};
struct SliceState {
    SliceId id{};
    std::string name;
    std::uint64_t start_frame{}, end_frame{};
    float gain{1.0F};
    std::optional<std::uint8_t> root_note;
};
struct RegionState {
    RegionId id{};
    std::string name;
    std::uint64_t start_frame{}, end_frame{};
    RegionDirection direction{RegionDirection::forward};
    RegionFadeCurve fade_curve{RegionFadeCurve::linear};
    std::uint64_t fade_in_frames{}, fade_out_frames{};
    float gain{1.0F};
    bool loop_enabled{};
    std::uint64_t loop_start_frame{}, loop_end_frame{};
    std::uint32_t loop_crossfade_frames{};
    std::size_t usage{};
};
struct PatternState {
    PatternId id{};
    std::string name;
    MusicalTick length{};
    std::size_t track_count{};
    PatternFollowAction follow;
};
struct ClipState {
    ClipId id{};
    PatternId pattern{};
    MusicalTick start{}, duration{};
    bool loop{}, enabled{};
    int transpose{};
    float velocity_scale{1.0F};
};
struct AudioClipState {
    AudioClip clip;
    bool source_available{};
    std::string validation_error;
};
struct PerformanceLaunchState {
    static constexpr std::uint32_t api_version = 2;
    std::vector<LaunchSlot> slots;
    std::vector<PerformanceLaunchScene> scenes;
    std::vector<PreparedLaunch> pending;
    std::vector<TrackPlaybackOwnership> ownership;
    PerformancePlaybackMode mode{PerformancePlaybackMode::arrangement};
    VelocityPolicy velocity_policy{VelocityPolicy::native};
    float fixed_velocity{1.0F};
    LaunchDiagnostics diagnostics;
};
struct PerformanceBankState {
    static constexpr std::uint32_t api_version = 1;
    std::vector<PerformanceBank> banks;
    PerformanceBankId active_bank{};
    PerformanceAssignmentId selected_assignment{};
};
struct PatternChainEntryState {
    PatternId pattern{};
    std::uint16_t repeats{};
};
struct PresetApplicationState {
    static constexpr std::uint32_t api_version = 1;
    std::uint64_t revision{}, activation_sequence{};
    std::string status{"idle"}, preset_id, mode, error;
    TrackId target_track{};
    float progress{};
    std::vector<std::filesystem::path> missing_assets;
};
struct ApplicationState {
    std::uint64_t revision{};
    bool playing{};
    std::uint64_t transport_frame{};
    MusicalTick transport_tick{};
    double tempo_bpm{120};
    std::uint16_t time_signature_numerator{4}, time_signature_denominator{4};
    TrackId selected_track{};
    SampleSlotId selected_sample_slot{};
    SliceId selected_slice{};
    RegionId selected_region{};
    std::size_t selected_pad{};
    SamplePlaybackSettings selected_pad_settings;
    float effective_stretch_ratio{1.0F};
    std::uint32_t stretch_latency_frames{};
    std::string selected_pad_property{"gain"};
    std::array<SamplePlaybackSettings, 16> pad_settings{};
    std::uint64_t slice_revision{}, sample_frames{};
    std::uint32_t sample_rate{};
    std::uint16_t sample_channels{};
    std::vector<SliceState> slices;
    std::vector<RegionState> regions;
    std::array<std::optional<SliceId>, 16> slice_pad_assignments{};
    std::array<std::optional<RegionId>, 16> region_pad_assignments{};
    std::vector<WaveformPeak> waveform_peaks;
    SampleAnalysisSessionId selected_analysis_session{};
    std::vector<SampleAnalysisSnapshot> sample_analysis_sessions;
    SampleProcessingJobId selected_processing_job{};
    std::vector<SampleProcessingSnapshot> sample_processing_jobs;
    std::uint64_t selected_pattern{};
    TimelinePlaybackMode playback_mode{TimelinePlaybackMode::legacy};
    PatternId active_pattern{}, queued_pattern{}, next_pattern{};
    LaunchQuantization launch_quantization{LaunchQuantization::bar};
    MusicalTick song_start{}, song_end{}, arrangement_loop_start{},
        arrangement_loop_end{};
    bool arrangement_loop_enabled{};
    std::vector<PatternState> patterns;
    std::vector<ClipState> clips;
    std::vector<AudioClipState> audio_clips;
    std::vector<AudioClipCrossfade> audio_crossfades;
    PerformanceLaunchState performance_launching;
    PerformanceBankState performance_banks;
    PatternChainId pattern_chain_id{};
    std::vector<PatternChainEntryState> pattern_chain_entries;
    std::size_t pattern_chain_position{};
    bool pattern_chain_loop{}, pattern_chain_running{};
    VoiceTransitionPolicy voice_transition{VoiceTransitionPolicy::preserve};
    SongEndBehavior song_end_behavior{SongEndBehavior::loop};
    std::string arrangement_error;
    std::uint64_t next_launch_frame{}, last_launch_frame{},
        launch_lateness_frames{};
    std::int64_t selected_step{};
    std::uint64_t step_page{};
    StepResolution step_resolution{StepResolution::sixteenth};
    MusicalTick step_ticks{ticks_per_quarter / 4};
    float step_velocity{1.0F};
    MusicalTick step_duration{ticks_per_quarter / 4};
    std::optional<SelectedEventState> selected_event;
    std::string selected_step_property{"velocity"};
    std::string selected_lock_parameter{"synth.cutoff"};
    std::vector<StepParameterLockState> selected_step_locks;
    std::vector<StepSelectionState> selected_steps;
    bool edit_group_active{}, can_undo{}, can_redo{};
    bool history_pending{};
    std::string undo_label, redo_label;
    bool fill_active{}, fill_latched{};
    std::uint64_t generation_seed{};
    std::size_t undo_depth{}, redo_depth{}, history_capacity{};
    PadMode pad_mode{PadMode::step};
    std::string controller_context{"sequencer"};
    std::string parameter_page{"main"}, focused_element{"transport"};
    enum class InteractionRequest : std::uint8_t {
        none, confirm, cancel, save, focus_next, focus_previous,
        page_next, page_previous, first, last, activate, parent
    };
    struct InteractionModal {
        std::string identity, target_identity, previous_focus;
        std::uint64_t generation{};
        bool destructive{}, confirm_available{};
    };
    struct InteractionState {
        std::string destination{"performance"}, subview{"root"},
            focus_identity{"transport"}, navigation_scope{"application"},
            data_role{"Tempo"};
        InteractionModal modal;
        InteractionRequest request{InteractionRequest::none};
        std::uint64_t revision{1}, request_revision{};
    } interaction;
    ModifierState modifiers;
    std::vector<std::string> available_actions;
    std::vector<ParameterDescriptor> parameters;
    std::vector<TrackSummary> tracks;
    std::vector<SampleSlotStatus> sample_slots;
    std::uint64_t timeline_revision{};
    PerformanceSnapshot performance;
    std::string project_name{"Untitled"};
    std::filesystem::path project_path;
    bool project_dirty{}, project_loading{}, project_saving{};
    std::string project_error;
    std::vector<std::filesystem::path> missing_assets;
    PresetApplicationState preset_application;
    RecordingStatus recording;
    std::vector<LiveLoopSessionStatus> live_loops;
    MixerState mixer;
    std::size_t selected_mixer_channel{};
    MixerBusId selected_group{};
    std::size_t selected_external_fx_loop{};
    std::vector<RoutingScene> routing_scenes;
    std::uint64_t active_routing_scene{};
    ModulationConfiguration modulation;
    ModulationSnapshot modulation_runtime;
    ModulationGraphDiagnostics modulation_graph;
    ModulationRouteId selected_modulation_route{};
    MidiProjectConfiguration midi;
    std::uint64_t midi_dropped{}, midi_sent{};
    ExternalClockState external_midi_clock;
    ArrangementRecordingStatus arrangement_recording;
    std::uint32_t arrangement_recording_api_version{1};
    std::vector<CapturedPatternLaunch> captured_pattern_launches;
    std::vector<CapturedAutomationChange> captured_automation_changes;
    std::vector<CapturedPerformanceLaunch> captured_performance_launches;
    std::vector<MusicalEvent> arrangement_automation;
    MacroDiscoverySnapshot performance_macros;
    std::array<bool, maximum_midi_ports> midi_output_connected{};
    std::array<bool, maximum_midi_ports> midi_input_connected{};
};
struct ApplicationCommand {
    std::string action_id;
    std::map<std::string, ApplicationValue> arguments;
    std::string source_id;
    std::uint64_t source_sequence{};
};
struct CommandResult {
    bool accepted{};
    std::uint64_t sequence{}, state_revision{};
    std::string error;
};

class ApplicationService {
  public:
    static constexpr std::size_t default_queue_capacity = 256;
    ApplicationService(AudioEngine &, TimelineService &, SampleManager &,
                       std::size_t queue_capacity = default_queue_capacity);
    ~ApplicationService();
    [[nodiscard]] const ActionRegistry &actions() const noexcept {
        return actions_;
    }
    [[nodiscard]] ParameterRegistry &parameters() noexcept {
        return parameters_;
    }
    [[nodiscard]] bool submit(ApplicationCommand) noexcept;
    [[nodiscard]] std::vector<CommandResult> dispatch_pending();
    [[nodiscard]] CommandResult execute(const ApplicationCommand &);
    [[nodiscard]] ApplicationState snapshot() const;
    // Immutable revision-cached presentation snapshot. Clients that only read
    // state should retain this handle for one refresh instead of requesting
    // multiple complete value copies. The audio callback never accesses or
    // constructs this object.
    [[nodiscard]] std::shared_ptr<const ApplicationState> snapshot_shared() const;
    [[nodiscard]] std::uint64_t rejected_commands() const noexcept;
    // Presentation-neutral interaction ownership. Frontends retain geometry
    // and toolkit focus, while controllers can address this stable contract.
    bool open_interaction_modal(std::string identity,
                                std::string target_identity,
                                bool destructive, std::string &error);
    bool close_interaction_modal(std::uint64_t generation,
                                 std::string &error);
    bool set_interaction_focus(std::string identity, std::string scope,
                               std::string data_role, std::string &error);
    bool set_interaction_destination(std::string destination,
                                     std::string subview,
                                     std::string &error);
    [[nodiscard]] SliceService &slices() noexcept { return slices_; }
    [[nodiscard]] SampleAnalysisService &sample_analysis() noexcept {
        return sample_analysis_;
    }
    [[nodiscard]] SampleProcessingService &sample_processing() noexcept {
        return sample_processing_;
    }
    [[nodiscard]] RecordingService &recording() noexcept { return recording_; }
    [[nodiscard]] LiveLoopService &live_loops() noexcept { return live_loops_; }
    [[nodiscard]] ArrangementRecordingService &
    arrangement_recording() noexcept {
        return arrangement_recording_;
    }
    [[nodiscard]] PerformanceMacroService &performance_macros() noexcept {
        return performance_macros_;
    }
    [[nodiscard]] MidiOutputScheduler &midi_output() noexcept {
        return midi_output_;
    }
    [[nodiscard]] bool receive_midi(const MidiPacket &packet) noexcept {
        return engine_.receive_midi(packet);
    }
    void set_midi_output_connections(
        const std::array<bool, maximum_midi_ports> &connections);
    void set_midi_input_connections(
        const std::array<bool, maximum_midi_ports> &connections);

  private:
    struct AuthoringHistoryState {
        MixerConfiguration mixer;
        ModulationConfiguration modulation;
        MacroProject macros;
        std::optional<SliceProject> samples;
        std::map<InstrumentInstanceId, std::map<std::string, double>>
            synth_parameters;
    };
    struct ApplicationHistoryEntry {
        std::string label;
        std::optional<Timeline> timeline_before, timeline_after;
        std::optional<AuthoringHistoryState> authoring_before,
            authoring_after;
    };
    struct PendingHistoryRestore {
        ApplicationHistoryEntry entry;
        AuthoringHistoryState target;
        PresetActivationRequest activation;
        bool undo{};
        std::uint64_t activation_sequence{};
        std::array<std::uint64_t, maximum_preset_samples>
            sample_publications{};
    };
    struct PendingProjectRecovery {
        Project project;
        PresetActivationRequest activation;
        std::array<std::filesystem::path, maximum_preset_samples> sample_paths{};
        std::array<std::uint64_t, maximum_preset_samples> sample_publications{};
        std::uint64_t activation_sequence{};
    };
    struct PendingPresetApplication {
        SoundPreset preset;
        PresetImportMode mode{PresetImportMode::preview};
        TrackId target_track{};
        InstrumentInstanceId target_instrument{};
        PresetActivationRequest activation;
        std::array<std::filesystem::path, maximum_preset_samples> sample_paths{};
        std::array<std::uint64_t, maximum_preset_samples>
            sample_publications{};
        std::map<std::string, double> synth_parameters;
        std::uint64_t activation_sequence{};
        bool published{};
        std::unique_ptr<AuthoringHistoryState> history_before;
    };
    CommandResult execute_with_history(const ApplicationCommand &,
                                       std::uint64_t);
    CommandResult execute_locked(const ApplicationCommand &, std::uint64_t);
    [[nodiscard]] std::unique_ptr<AuthoringHistoryState>
        capture_authoring_state(bool include_samples = false) const;
    bool prepare_history_restore(const ApplicationHistoryEntry &, bool,
                                 std::string &);
    void push_history(ApplicationHistoryEntry) const;
    void clear_history();
    static constexpr std::size_t application_history_capacity = 32;
    bool publish_modulation_with_macros(const ModulationConfiguration &,
                                        std::string &);
    bool publish_performance_launches(const Timeline &,
                                      std::span<const PreparedLaunch>,
                                      std::span<const PreparedLaunch>,
                                      std::string &) const;
    void synchronize_locked() const;
    void changed_locked();
    AudioEngine &engine_;
    TimelineService &timeline_;
    SampleManager &samples_;
    mutable SliceService slices_;
    mutable SampleAnalysisService sample_analysis_;
    mutable SampleProcessingService sample_processing_;
    mutable RecordingService recording_;
    mutable LiveLoopService live_loops_;
    mutable ArrangementRecordingService arrangement_recording_;
    mutable PerformanceLauncher performance_launcher_;
    mutable std::uint64_t performance_launcher_revision_{};
    mutable PerformanceMacroService performance_macros_;
    RecordingConfiguration recording_configuration_;
    std::uint64_t recording_fixed_bars_{};
    mutable MixerConfiguration mixer_configuration_;
    mutable ModulationConfiguration modulation_configuration_;
    MidiOutputScheduler midi_output_;
    MidiRouter midi_router_;
    ModulationRouteId next_modulation_route_id_{1};
    ModulatorInstanceId next_modulator_instance_id_{1};
    std::uint64_t next_macro_morph_id_{1};
    mutable std::map<InstrumentInstanceId, std::map<std::string, double>>
        synth_instance_parameters_;
    ActionRegistry actions_;
    ParameterRegistry parameters_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    mutable ApplicationState state_;
    std::map<TrackId, std::uint8_t> track_targets_;
    std::deque<std::pair<std::uint64_t, ApplicationCommand>> queue_;
    std::uint64_t next_sequence_{1}, rejected_{};
    std::optional<TimelineTransaction> edit_group_;
    mutable std::optional<EventId> selected_event_id_;
    mutable std::optional<MusicalTick> queued_launch_tick_;
    mutable std::optional<std::uint64_t> queued_launch_frame_;
    mutable std::uint64_t observed_song_end_count_{};
    mutable std::uint64_t observed_timeline_activation_count_{};
    mutable std::uint64_t observed_timeline_activation_drops_{};
    mutable PatternId pending_record_pattern_{};
    mutable bool pending_record_chain_{};
    mutable std::optional<std::uint64_t> pending_record_frame_;
    MusicalTick automation_thinning_ticks_{ticks_per_quarter / 64};
    bool fill_momentary_{};
    mutable std::optional<PendingPresetApplication> pending_preset_;
    mutable std::optional<PendingHistoryRestore> pending_history_restore_;
    mutable std::optional<PendingProjectRecovery> pending_project_recovery_;
    mutable std::deque<ApplicationHistoryEntry> history_undo_, history_redo_;
    mutable std::shared_ptr<const ApplicationState> published_snapshot_;
    mutable Project project_;
    mutable SystemMonitor system_monitor_;
};

struct TextEntryState {
    std::string text;
    std::size_t cursor{};
    char selected{'A'};
    bool active{}, committed{}, discarded{};
};
class TextEntryModel {
  public:
    [[nodiscard]] static constexpr std::string_view characters() noexcept {
        return " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
               "-_.";
    }
    void begin(std::string initial = {});
    void direct(std::string_view);
    void select_relative(int);
    void select(char);
    void insert();
    void erase_or_cancel();
    void move_cursor(int);
    void commit();
    void discard();
    [[nodiscard]] const TextEntryState &state() const noexcept {
        return state_;
    }

  private:
    TextEntryState state_;
};

struct XYBehavior {
    bool spring_to_center{}, latching{}, relative{}, invert_x{}, invert_y{};
    double scale_x{1}, scale_y{1};
};
class XYControl {
  public:
    explicit XYControl(XYBehavior behavior = {}) : behavior_(behavior) {}
    [[nodiscard]] NormalizedXY apply(double x, double y);
    void release();
    [[nodiscard]] NormalizedXY value() const noexcept { return value_; }

  private:
    XYBehavior behavior_;
    NormalizedXY value_{0.5, 0.5};
};
[[nodiscard]] const char *pad_mode_name(PadMode) noexcept;
[[nodiscard]] const char *step_resolution_name(StepResolution) noexcept;
[[nodiscard]] MusicalTick step_resolution_ticks(StepResolution) noexcept;
[[nodiscard]] std::optional<StepResolution>
    parse_step_resolution(std::string_view) noexcept;
} // namespace neurogenic
