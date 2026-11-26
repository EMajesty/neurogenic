#pragma once
#include "neurogenic/midi/mapping.hpp"

#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/assignment_picker.hpp"
#include "neurogenic/application/lifecycle.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include "neurogenic/application/recording_workflow.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neurogenic {

[[nodiscard]] std::string format_slint_musical_position(
    MusicalTick tick, std::uint16_t numerator,
    std::uint16_t denominator);
[[nodiscard]] std::string slint_recording_label(RecordingState state,
                                                bool playing);

struct SlintContextItem {
    std::string label{"Unavailable"}, value{"--"}, unit, parameter_id,
        action_id;
    std::map<std::string, ApplicationValue> arguments;
    bool available{};
    std::string bank_id, role;
    bool locked{};
};

struct SlintPadItem {
    std::string label, status;
    std::uint64_t slot_id{};
    bool active{}, queued{}, selected{}, available{};
};

struct SlintPageItem {
    std::string id, title;
    bool implemented{};
};

struct SlintSceneItem {
    std::string label;
    bool queued{};
};

struct SlintShellState {
    std::uint64_t revision{};
    bool playing{}, record_armed{}, recording{}, project_dirty{};
    bool metronome_enabled{}, count_in_active{}, tap_tempo_available{true};
    bool history_pending{}, can_undo{}, can_redo{};
    double tempo_bpm{120.0}, metronome_level{0.35};
    std::uint8_t count_in_bars{};
    std::string project_name{"Untitled"}, transport{"Stopped"},
        time_signature{"4/4"}, position{"1.1.1"},
        selected_track{"Unavailable"}, active_content{"Unavailable"},
        track_owner{"Arrangement"}, launch_quantization{"Bar"},
        page_id{"performance"}, destination_id{"performance"},
        page_title{"Performance"}, notification,
        undo_label, redo_label, focus_identity{"transport"},
        navigation_scope{"application"}, data_role{"Tempo"}, modal_identity;
    bool effective_shift{};
    bool modal_active{}, modal_confirm_focused{};
    std::uint64_t root_revision{1};
    std::vector<SlintPageItem> pages;
};

struct SlintContextState {
    std::string identity{"performance:primary"}, bank{"Primary"};
    std::array<SlintContextItem, 8> encoders;
    std::array<SlintContextItem, 8> functions;
    std::array<SlintPadItem, 16> pads;
};

struct SlintArgumentItem {
    std::string name, label, value, hint, type;
    bool required{}, selected{};
};

struct SlintPerformancePageState {
    std::string selected_track{"Unavailable"}, active_content{"Unavailable"},
        track_owner{"Arrangement"}, launch_quantization{"Bar"};
    std::vector<SlintSceneItem> scenes;
    std::array<SlintPadItem, 16> pads;
    std::vector<std::uint64_t> unassigned_slots;
    struct BankItem {
        std::uint64_t id{};
        std::string name;
        bool selected{};
    };
    std::vector<BankItem> banks;
    std::string bank_name{"No Performance bank"}, assignment_status;
    std::uint64_t selected_assignment{};
    struct AssignmentTargetItem {
        std::string kind, id, label, category;
        bool selected{}, available{true};
        std::vector<AssignmentArgumentField> arguments;
    };
    std::vector<AssignmentTargetItem> assignment_targets;
    struct PositionItem {
        std::uint32_t position{};
        std::string label, category;
        bool secondary{}, action{}, pad{}, assigned{}, selected{};
    };
    std::vector<PositionItem> assignment_positions;
    std::size_t selected_assignment_target{};
    std::uint32_t editor_position{};
    bool editor_secondary{}, editor_action{}, editor_pad{}, editor_visible{};
    std::string editor_label, editor_behavior{"one-shot"};
    std::vector<SlintArgumentItem> editor_arguments;
    std::size_t selected_editor_argument{};
};

struct SlintStepItem {
    std::string label, status{"Empty"};
    bool active{}, selected{}, playhead{}, available{};
};

struct SlintPatternItem {
    std::uint64_t id{};
    std::string name;
    bool selected{}, active{}, queued{};
};

struct SlintArrangementItem {
    std::uint64_t id{};
    std::string label, range, kind;
    std::uint64_t track{}, source{};
    MusicalTick start{}, duration{};
    bool selected{}, enabled{};
};

struct SlintTrackItem {
    std::uint64_t id{};
    std::string name, instrument;
    bool selected{}, muted{}, solo{}, record_armed{}, effectively_audible{};
};

struct SlintSequencerPageState {
    std::string selected_track{"Unavailable"}, selected_pattern{"Unavailable"},
        resolution{"1/16"}, pattern_length{"Unavailable"},
        selected_velocity{"--"}, selected_duration{"--"}, selected_pitch{"--"},
        selected_probability{"--"}, selected_condition{"--"},
        selected_ratchet{"--"}, selected_timing{"--"}, page_label{"Page 1/1"},
        lock_parameter{"Unavailable"}, lock_value{"--"};
    std::uint64_t step_page{}, step_page_count{1}, selected_pattern_id{},
        selected_clip_id{};
    std::string selected_clip_kind;
    std::size_t selected_lock_index{};
    bool arrangement_view{}, playhead_follow{}, has_selected_event{},
        can_page_previous{}, can_page_next{}, lock_available{};
    bool confirm_delete_track{}, confirm_delete_pattern{},
        confirm_delete_clip{};
    std::array<SlintStepItem, 16> steps;
    std::vector<SlintTrackItem> tracks;
    std::vector<SlintPatternItem> patterns;
    std::vector<std::string> lock_parameters;
    std::vector<SlintArrangementItem> arrangement;
    std::string loop_range{"Off"};
};

struct SlintWaveformPeak {
    float left_min{}, left_max{}, right_min{}, right_max{};
};

struct SlintSliceMarker {
    std::uint64_t id{}, start_frame{}, end_frame{};
    std::string label;
    bool selected{}, assigned{};
};

struct SlintRegionItem {
    std::uint64_t id{}, start_frame{}, end_frame{};
    std::string label, fade, loop;
    bool selected{};
};

struct SlintSamplePageState {
    std::uint32_t slot{};
    std::string name{"Empty slot"}, status{"Empty"}, metadata{"Unavailable"},
        analysis_status{"Idle"}, processing_status{"Idle"}, error;
    std::uint64_t active_revision{}, frames{}, region_start{}, region_end{};
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    double duration_seconds{};
    float zoom{1.0F}, offset{}, processing_progress{};
    bool ready{}, loading{}, missing{}, previewing{}, loop_enabled{};
    bool processing_can_cancel{}, processing_can_publish{},
        processing_audio_active{};
    std::uint64_t processing_job{};
    std::uint32_t processing_destination{};
    std::uint64_t fade_in{}, fade_out{}, loop_start{}, loop_end{};
    std::vector<SlintWaveformPeak> waveform;
    std::vector<SlintSliceMarker> slices;
    std::vector<SlintRegionItem> regions;
};

struct SlintRecordingWorkflowState {
    struct LoopItem {
        std::uint64_t id{};
        std::string label, state, mode;
        bool selected{}, audio_active{};
    };
    bool visible{};
    std::string mode{"External audio"}, phase{"Idle"},
        source{"external-stereo"}, destination{"Sample slot 0"},
        quantization{"immediate"}, error;
    std::uint32_t destination_slot{};
    std::uint64_t source_id{}, captured_frames{}, capacity_frames{},
        overflow_count{};
    std::size_t captured_events{};
    float progress{};
    bool can_configure{true}, can_arm{}, can_start{}, can_stop{}, can_cancel{},
        can_commit{}, can_overdub{}, can_replace{}, can_clear{}, can_save{},
        result_audio_active{}, result_pending{};
    std::uint64_t session_id{}, loop_iteration{};
    std::string pass{"capture"};
    std::vector<LoopItem> loops;
};

struct SlintSoundParameterItem {
    std::string id, name, value, unit, group;
    double minimum{}, maximum{}, normalized{};
    bool available{}, automatable{}, modulatable{};
};

struct SlintSoundModulationItem {
    std::uint64_t id{}, source_id{};
    std::string source, destination, amount;
    bool enabled{};
};

struct SlintSoundPageState {
    std::uint64_t track_id{};
    std::uint32_t instrument_instance{};
    std::string track{"Unavailable"}, instrument_id, instrument{"None"},
        subview{"instrument"}, preset_library, preset_id,
        preset_status{"Idle"}, error;
    bool synth{}, sample{}, instrument_available{};
    std::size_t selected_parameter{}, selected_modulation{};
    std::vector<std::string> groups;
    std::vector<SlintSoundParameterItem> parameters;
    std::vector<SlintSoundModulationItem> modulation;
    SamplePlaybackSettings sample_settings;
};

struct SlintMixerChannelItem {
    std::uint64_t id{};
    std::string name, type, route{"Master"};
    float gain{1.0F}, pan{}, meter_left{}, meter_right{};
    std::array<float, mixer_aux_count> aux_sends{};
    bool mute{}, solo{}, clip{}, selected{}, available{true};
};

struct SlintMixerFxItem {
    std::size_t index{};
    std::uint32_t latency_frames{};
    std::string name, status, routing;
    float send_gain{}, return_gain{}, wet{}, meter_left{}, meter_right{};
    bool enabled{}, bypass{}, return_mute{}, send_available{}, return_available{};
};

struct SlintMixerPageState {
    std::string bank_label{"Channels 1-8"}, error;
    std::size_t bank{}, selected_channel{};
    std::uint64_t selected_id{};
    std::vector<SlintMixerChannelItem> channels;
    std::vector<SlintMixerFxItem> external_fx;
    std::array<std::string, 3> output_pairs{"1/2", "3/4", "5/6"};
};

struct SlintFxOwnerItem {
    std::string kind, name;
    std::int64_t index{};
    bool selected{};
};

struct SlintFxInstanceItem {
    std::uint32_t id{};
    std::string name, type;
    bool bypass{}, selected{};
};

struct SlintFxParameterItem {
    std::uint32_t id{};
    std::string stable_id, name, value, unit;
    double minimum{}, maximum{}, normalized{};
    bool automatable{}, modulatable{}, selected{};
};

struct SlintFxModulatorItem {
    std::uint32_t id{};
    std::string name, type, owner;
    bool enabled{}, selected{};
};

struct SlintFxModulatorParameterItem {
    std::string id, name, value, unit;
    double minimum{}, maximum{}, current{};
    bool discrete{}, selected{};
};

struct SlintFxModulatorTypeItem {
    std::int64_t id{};
    std::string name, category;
    bool selected{};
};

struct SlintFxDestinationItem {
    std::string id, name, range;
    double minimum{}, maximum{};
    bool selected{};
};

struct SlintFxRouteItem {
    std::uint32_t id{}, source_id{};
    std::string source, destination, depth;
    bool enabled{}, selected{}, valid{};
};

struct SlintFxMacroItem {
    std::uint64_t id{};
    std::string name, assignments;
    float value{};
    bool enabled{}, selected{};
};

struct SlintFxMacroAssignmentItem {
    std::uint64_t id{}, macro_id{};
    std::string destination, range, curve;
    float minimum{}, maximum{};
    bool enabled{}, selected{};
};

struct SlintFxSceneItem {
    std::uint64_t id{};
    std::string name, summary;
    bool selected{};
};

struct SlintFxPageState {
    std::string subview{"chain"}, owner_kind{"track"}, owner_name{"Unavailable"},
        error;
    std::int64_t owner_index{};
    std::size_t selected_owner{}, selected_effect{}, selected_parameter{},
        selected_modulator_type{}, selected_modulator{},
        selected_modulator_parameter{}, selected_destination{}, selected_route{},
        selected_macro{}, selected_macro_assignment{}, selected_scene{};
    bool chain_bypass{};
    std::vector<SlintFxOwnerItem> owners;
    std::vector<SlintFxInstanceItem> effects;
    std::vector<SlintFxParameterItem> parameters;
    std::vector<SlintFxModulatorItem> modulators;
    std::vector<SlintFxModulatorTypeItem> modulator_types;
    std::vector<SlintFxModulatorParameterItem> modulator_parameters;
    std::vector<SlintFxDestinationItem> destinations;
    std::vector<SlintFxRouteItem> routes;
    std::vector<SlintFxMacroItem> macros;
    std::vector<SlintFxMacroAssignmentItem> macro_assignments;
    std::vector<SlintFxSceneItem> scenes;
};

enum class SlintBrowserEntryKind : std::uint8_t {
    directory, project, sample, instrument_preset, fx_preset, other
};

struct SlintBrowserEntry {
    std::string id, name, path, type, metadata;
    SlintBrowserEntryKind kind{SlintBrowserEntryKind::other};
    std::uintmax_t size{};
    std::int64_t modified{};
    bool directory{}, compatible{}, selected{};
};

struct SlintBrowserPageState {
    std::string category{"projects"}, location, filter, sort{"name"},
        destination{"Current project"}, preview{"No selection"}, error,
        confirmation_action, confirmation_message;
    std::size_t selected{}, page{}, page_count{1}, total_entries{}, selected_missing{};
    std::uint64_t request_id{};
    bool loading{}, confirmation_pending{}, truncated{}, can_previous_page{},
        can_next_page{};
    std::vector<std::string> categories, recent_locations;
    std::vector<std::string> missing_assets;
    std::vector<SlintBrowserEntry> entries;
};

struct SlintTextEntryState {
    std::string title, text, operation, error, context_library,
        context_preset_id, context_preset_type;
    TrackId context_track{};
    std::uint64_t context_id{};
    std::size_t cursor{};
    bool visible{}, uppercase{}, symbols{};
};

struct SlintSetupDeviceItem {
    std::string id, name, detail;
    bool selected{}, available{true};
};

struct SlintSetupControllerItem {
    std::string id, transport, device, profile, protocol, status, error,
        learn_status{"Idle"};
    bool connected{}, feedback{}, selected{};
    std::string learn_preview;
};
struct SlintSetupProfileItem {
    std::string id, name, descriptor, status, associations;
    bool built_in{}, available{}, active{}, selected{};
};
struct SlintSetupMidiRouteItem {
    std::uint32_t id{};
    std::uint32_t input_port{}, output_port{};
    std::int8_t input_channel{-1}, output_channel{-1};
    bool notes{}, control_change{}, clock{}, transport{}, enabled{}, selected{};
};
struct SlintSetupMappingItem {
    std::string endpoint_id, control_id, source, mode;
    std::size_t binding_index{};
    bool feedback{}, selected{};
    std::size_t controller_binding_index{};
    std::string target_kind, target_id, target_label;
    std::string context, feedback_id;
    bool pickup{}, invert{}, shift{}, select{};
    double fine_scale{0.1}, acceleration{2.0};
    std::map<std::string, ApplicationValue> arguments;
    std::uint32_t port{1};
    std::uint8_t channel{}, number{};
    MidiControlMode midi_mode{MidiControlMode::cc_absolute};
};
struct SlintSetupMappingTargetItem {
    std::string kind, id, label;
    bool selected{};
    std::vector<AssignmentArgumentField> arguments;
};

// Copy-only runtime data supplied by the appliance.  This deliberately keeps
// AudioDevice, MIDI adapters, and controller sessions outside the UI layer.
struct SlintSetupRuntimeSnapshot {
    std::uint64_t revision{};
    std::string audio_backend{"Unavailable"}, active_audio_input,
        active_audio_output, audio_status{"Unavailable"}, system{"Unknown"},
        version{"Unknown"};
    std::uint32_t active_sample_rate{48000}, active_buffer_size{};
    std::uint16_t active_input_channels{}, active_output_channels{};
    std::uint64_t xruns{};
    bool audio_running{}, xruns_available{};
    AppliancePreferences requested;
    std::vector<SlintSetupDeviceItem> audio_inputs, audio_outputs,
        midi_inputs, midi_outputs;
    std::vector<SlintSetupControllerItem> controllers;
    std::vector<SlintSetupProfileItem> profiles;
    std::vector<SlintSetupMappingItem> mappings;
    std::vector<std::string> lifecycle_warnings;
};

struct SlintSetupServices {
    std::function<SlintSetupRuntimeSnapshot()> snapshot;
    std::function<bool(const AppliancePreferences &, std::string &)> save;
    std::function<bool(std::string_view, std::string_view, std::string_view,
                       std::string &)> begin_midi_learn;
    std::function<bool(std::string_view, std::string &)> cancel_midi_learn;
    std::function<bool(std::string_view, std::string &)> confirm_midi_learn;
    std::function<bool(std::string_view, std::size_t, std::string &)>
        remove_midi_binding;
    std::function<bool(std::string_view, std::size_t, MidiControlBinding,
                       std::string &)> set_midi_binding;
    std::function<bool(std::string_view, std::size_t, ControlBinding,
                       std::string &)> set_controller_binding;
    std::function<bool(std::string_view, std::string_view, std::string &)>
        save_controller_profile;
    std::function<bool(std::string_view, std::string_view, std::string_view,
                       std::string_view, std::string &)> profile_action;
    std::function<bool()> request_exit;
};

struct SlintSetupPageState {
    std::string subview{"audio"}, audio_backend{"Unavailable"},
        audio_status{"Unavailable"}, active_audio_input{"Unavailable"},
        active_audio_output{"Unavailable"}, requested_audio_input,
        requested_audio_output, midi_clock{"Internal"}, system{"Unknown"},
        version{"Unknown"}, error, controller_learn_status{"Idle"},
        controller_learn_preview;
    std::uint32_t active_sample_rate{48000}, active_buffer_size{},
        requested_buffer_size{256};
    std::uint16_t active_input_channels{}, active_output_channels{},
        requested_input_channels{2}, requested_output_channels{2};
    std::uint64_t xruns{};
    double callback_load{}, callback_peak_load{};
    bool audio_running{}, xruns_available{}, restart_required{}, pending{},
        fullscreen{}, reopen_last_project{};
    float ui_scale{1.0F};
    std::string startup_project;
    std::size_t selected_controller{}, selected_profile{};
    std::size_t selected_mapping{};
    std::size_t selected_mapping_target{};
    std::size_t selected_midi_route{};
    std::vector<SlintSetupDeviceItem> audio_inputs, audio_outputs,
        midi_inputs, midi_outputs;
    std::vector<SlintSetupControllerItem> controllers;
    std::vector<SlintSetupProfileItem> profiles;
    std::vector<SlintSetupMappingItem> mappings;
    std::vector<SlintSetupMappingTargetItem> mapping_targets;
    std::vector<SlintSetupMidiRouteItem> midi_routes;
    std::vector<std::string> warnings;
    std::vector<SlintArgumentItem> mapping_arguments;
    std::size_t selected_mapping_argument{};
    bool conflict_visible{};
    std::string conflict_existing_source, conflict_existing_target,
        conflict_proposed_source, conflict_proposed_target, conflict_reason;
};

struct SlintUiState {
    SlintShellState shell;
    SlintContextState context;
    SlintPerformancePageState performance;
    SlintSequencerPageState sequencer;
    SlintSamplePageState sample;
    SlintSoundPageState sound;
    SlintMixerPageState mixer;
    SlintFxPageState fx;
    SlintBrowserPageState browser;
    SlintSetupPageState setup;
    SlintRecordingWorkflowState recording;
    SlintTextEntryState text_entry;
};

enum class SlintRefreshKind : std::uint8_t { none, full, telemetry };

struct SlintRefreshDiagnostics {
    std::uint64_t calls{}, full_rebuilds{}, telemetry_updates{}, skipped{};
    std::uint64_t snapshot_nanoseconds{}, construction_nanoseconds{},
        maximum_refresh_nanoseconds{}, assignment_discovery_rebuilds{};
    SlintRefreshKind last_kind{SlintRefreshKind::none};
};

struct SlintPublicationDiagnostics {
    std::uint64_t passes{}, model_creations{}, model_resets{}, model_row_updates{};
};

[[nodiscard]] SlintPublicationDiagnostics slint_publication_diagnostics() noexcept;
void reset_slint_publication_diagnostics() noexcept;

// Presentation-neutral bridge used by the Slint renderer and renderer-free
// tests. It owns no engine state: every value is copied from ApplicationState
// and every mutation goes through a semantic action or ParameterBindingService.
class SlintFrontendAdapter {
  public:
    explicit SlintFrontendAdapter(ApplicationService &application,
                                  SlintSetupServices setup = {});
    ~SlintFrontendAdapter();
    [[nodiscard]] bool refresh();
    [[nodiscard]] const SlintUiState &state() const noexcept {
        return state_;
    }
    [[nodiscard]] const SlintRefreshDiagnostics &refresh_diagnostics() const noexcept {
        return refresh_diagnostics_;
    }
    [[nodiscard]] bool invoke_action(std::string action_id);
    [[nodiscard]] bool turn_encoder(std::size_t index, double steps,
                                    bool fine = false);
    [[nodiscard]] bool invoke_function(std::size_t index);
    [[nodiscard]] bool change_context_bank(int direction);
    [[nodiscard]] bool press_pad(std::size_t index, bool pressed = true);
    [[nodiscard]] bool assign_performance_pad(std::size_t position,
                                              std::uint64_t slot_id);
    [[nodiscard]] bool clear_performance_pad(std::size_t position);
    [[nodiscard]] bool manage_performance_bank(std::string operation);
    [[nodiscard]] bool select_performance_bank(std::size_t index);
    [[nodiscard]] bool assign_performance_parameter(
        std::size_t position, bool secondary, std::string parameter_id,
        std::string label = {});
    [[nodiscard]] bool open_performance_assignment_editor(
        std::size_t position, bool secondary, bool action);
    [[nodiscard]] bool select_performance_assignment_target(std::size_t index);
    [[nodiscard]] bool select_performance_assignment_argument(std::size_t index);
    [[nodiscard]] bool set_performance_assignment_argument(std::size_t index,
                                                           std::string value);
    [[nodiscard]] bool confirm_performance_assignment(
        std::string label, std::string behavior,
        std::array<std::string, 4> argument_names = {},
        std::array<std::string, 4> argument_values = {});
    [[nodiscard]] bool clear_performance_assignment_editor();
    [[nodiscard]] bool select_performance_pad_assignment(std::size_t position);
    void cancel_performance_assignment_editor() noexcept;
    [[nodiscard]] bool select_page(std::size_t index);
    [[nodiscard]] bool select_reference_page(std::size_t index);
    [[nodiscard]] bool set_reference_shift(bool effective);
    [[nodiscard]] bool select_track(int direction);
    [[nodiscard]] bool launch_scene(std::size_t index);
    [[nodiscard]] bool return_to_arrangement();
    [[nodiscard]] bool set_launch_quantization(std::string value);
    [[nodiscard]] bool cycle_launch_quantization();
    [[nodiscard]] bool transport_record();
    [[nodiscard]] bool recording_action(std::string action);
    [[nodiscard]] bool set_recording_mode(std::string mode);
    [[nodiscard]] bool set_recording_source(std::string source,
                                            std::uint64_t source_id = 0);
    [[nodiscard]] bool set_recording_destination(std::uint32_t slot);
    [[nodiscard]] bool set_recording_quantization(std::string quantization);
    [[nodiscard]] bool select_live_loop(std::size_t index);
    void close_recording() noexcept;
    [[nodiscard]] bool select_pattern(std::size_t index, bool launch = false);
    [[nodiscard]] bool create_pattern(bool duplicate = false);
    [[nodiscard]] bool select_step(std::size_t index);
    [[nodiscard]] bool set_step_velocity(double value);
    [[nodiscard]] bool set_step_duration(std::int64_t ticks);
    [[nodiscard]] bool change_step_page(int direction);
    [[nodiscard]] bool set_playhead_follow(bool enabled);
    [[nodiscard]] bool edit_step_property(std::string property, int direction);
    [[nodiscard]] bool cycle_step_condition();
    [[nodiscard]] bool cycle_lock_parameter(int direction);
    [[nodiscard]] bool edit_selected_lock(std::string operation, int direction = 0);
    [[nodiscard]] bool manage_pattern(std::string operation);
    [[nodiscard]] bool select_track_index(std::size_t index);
    [[nodiscard]] bool manage_track(std::string operation);
    [[nodiscard]] bool cycle_step_resolution();
    [[nodiscard]] bool toggle_arrangement_view();
    [[nodiscard]] bool select_arrangement_clip(std::size_t index);
    [[nodiscard]] bool manage_arrangement(std::string operation);
    [[nodiscard]] bool select_sample_slot(int direction);
    [[nodiscard]] bool load_sample(std::string path);
    [[nodiscard]] bool select_slice(std::size_t index);
    [[nodiscard]] bool create_slice(std::uint64_t frame);
    [[nodiscard]] bool move_selected_slice(std::uint64_t start,
                                           std::uint64_t end);
    [[nodiscard]] bool delete_selected_slice();
    [[nodiscard]] bool assign_selected_slice(std::size_t pad);
    [[nodiscard]] bool preview_sample(std::string_view target);
    [[nodiscard]] bool stop_preview();
    [[nodiscard]] bool analyze_transients();
    [[nodiscard]] bool apply_analysis(bool accept);
    [[nodiscard]] bool select_region(std::size_t index);
    [[nodiscard]] bool set_region_bounds(std::uint64_t start,
                                         std::uint64_t end);
    [[nodiscard]] bool toggle_region_loop();
    [[nodiscard]] bool set_region_fades(std::uint64_t fade_in,
                                        std::uint64_t fade_out);
    [[nodiscard]] bool start_processing(std::string operation);
    [[nodiscard]] bool processing_action(std::string action,
                                         std::string value = {});
    [[nodiscard]] bool set_waveform_view(float zoom, float offset);
    [[nodiscard]] bool select_sound_subview(std::string subview);
    [[nodiscard]] bool set_track_instrument(std::string instrument_id);
    [[nodiscard]] bool adjust_sound_parameter(std::size_t index, double steps);
    [[nodiscard]] bool select_sound_parameter(std::size_t index);
    [[nodiscard]] bool cycle_sample_playback_mode();
    [[nodiscard]] bool adjust_sample_instrument(std::string property,
                                                double steps);
    [[nodiscard]] bool select_sound_modulation(std::size_t index);
    [[nodiscard]] bool edit_sound_modulation(std::string operation);
    [[nodiscard]] bool preset_action(std::string operation,
                                     std::string library,
                                     std::string preset_id);
    [[nodiscard]] bool select_mixer_channel(std::size_t index);
    [[nodiscard]] bool bank_mixer(int direction);
    [[nodiscard]] bool adjust_mixer(std::string operation, double steps = 1.0);
    [[nodiscard]] bool toggle_mixer(std::string operation);
    [[nodiscard]] bool cycle_mixer_route();
    [[nodiscard]] bool cycle_mixer_group();
    [[nodiscard]] bool select_external_fx(std::size_t index);
    [[nodiscard]] bool adjust_external_fx(std::string operation,
                                          double steps = 1.0);
    [[nodiscard]] bool toggle_external_fx(std::string operation);
    [[nodiscard]] bool select_fx_subview(std::string subview);
    [[nodiscard]] bool select_fx_owner(std::size_t index);
    [[nodiscard]] bool select_effect(std::size_t index);
    [[nodiscard]] bool add_effect(std::string type);
    [[nodiscard]] bool edit_effect(std::string operation);
    [[nodiscard]] bool select_effect_parameter(std::size_t index);
    [[nodiscard]] bool adjust_effect_parameter(double steps);
    [[nodiscard]] bool select_fx_modulator(std::size_t index);
    [[nodiscard]] bool select_fx_modulator_type(std::size_t index);
    [[nodiscard]] bool manage_fx_modulator(std::string operation);
    [[nodiscard]] bool select_fx_modulator_parameter(std::size_t index);
    [[nodiscard]] bool adjust_fx_modulator_parameter(double steps);
    [[nodiscard]] bool select_fx_destination(std::size_t index);
    [[nodiscard]] bool select_fx_route(std::size_t index);
    [[nodiscard]] bool edit_fx_route(std::string operation);
    [[nodiscard]] bool create_fx_route();
    [[nodiscard]] bool select_fx_macro(std::size_t index);
    [[nodiscard]] bool adjust_fx_macro(double steps);
    [[nodiscard]] bool manage_fx_macro(std::string operation);
    [[nodiscard]] bool select_fx_macro_assignment(std::size_t index);
    [[nodiscard]] bool manage_fx_macro_assignment(std::string operation);
    [[nodiscard]] bool select_fx_scene(std::size_t index);
    [[nodiscard]] bool manage_fx_scene(std::string operation);
    [[nodiscard]] bool select_browser_category(std::string category);
    [[nodiscard]] bool navigate_browser(std::string path);
    [[nodiscard]] bool browser_parent();
    [[nodiscard]] bool select_browser_entry(std::size_t index);
    [[nodiscard]] bool set_browser_filter(std::string filter);
    [[nodiscard]] bool cycle_browser_sort();
    [[nodiscard]] bool change_browser_page(int direction);
    [[nodiscard]] bool refresh_browser();
    [[nodiscard]] bool activate_browser_entry();
    [[nodiscard]] bool browser_project_action(std::string action,
                                              std::string path = {});
    [[nodiscard]] bool browser_preset_action(std::string action);
    [[nodiscard]] bool confirm_browser_action(bool accept);
    [[nodiscard]] bool select_missing_asset(std::size_t index);
    [[nodiscard]] bool relink_missing_asset(bool map_directory);
    [[nodiscard]] bool request_text_entry(std::string operation,
                                          std::string current = {});
    [[nodiscard]] bool text_entry_insert(std::string text);
    [[nodiscard]] bool text_entry_action(std::string action);
    [[nodiscard]] bool select_setup_subview(std::string subview);
    [[nodiscard]] bool select_setup_device(std::string kind,
                                           std::size_t index);
    [[nodiscard]] bool adjust_setup_value(std::string property, int direction);
    [[nodiscard]] bool toggle_setup_preference(std::string property);
    [[nodiscard]] bool setup_preferences_action(std::string action);
    [[nodiscard]] bool select_setup_controller(std::size_t index);
    [[nodiscard]] bool select_setup_profile(std::size_t index);
    [[nodiscard]] bool manage_setup_profile(std::string action,
                                            std::string value = {},
                                            std::string path = {});
    [[nodiscard]] bool select_setup_midi_route(std::size_t index);
    [[nodiscard]] bool edit_setup_midi_route(std::string operation);
    [[nodiscard]] bool setup_midi_learn(std::string operation,
                                        std::string control_id = {},
                                        std::string mode = {});
    [[nodiscard]] bool select_setup_mapping(std::size_t index);
    [[nodiscard]] bool remove_setup_mapping();
    [[nodiscard]] bool author_setup_midi_mapping(
        std::uint32_t port, std::uint8_t channel, std::uint8_t number,
        std::string mode, std::string control_id, bool feedback,
        bool replace_conflict);
    [[nodiscard]] bool confirm_setup_mapping_conflict();
    void cancel_setup_mapping_conflict() noexcept;
    [[nodiscard]] bool edit_setup_mapping(std::string target_kind,
                                          std::string target_id,
                                          bool pickup, bool invert,
                                          double fine_scale,
                                          double acceleration);
    [[nodiscard]] bool configure_setup_mapping_scope(
        std::string context, bool shift, bool select,
        std::string feedback_id);
    [[nodiscard]] bool select_setup_mapping_target(std::size_t index);
    [[nodiscard]] bool select_setup_mapping_argument(std::size_t index);
    [[nodiscard]] bool set_setup_mapping_argument(std::size_t index,
                                                   std::string value);
    [[nodiscard]] bool apply_setup_mapping_target();
    [[nodiscard]] bool apply_setup_mapping_target(
        std::array<std::string, 4> argument_values);
    [[nodiscard]] bool save_setup_controller_profile(std::string path);
    [[nodiscard]] bool request_application_exit();
    void dismiss_notification() noexcept;
    [[nodiscard]] bool keyboard_action(std::string_view key, bool pressed);
    void shutdown() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string &last_error() const noexcept {
        return last_error_;
    }

  private:
    bool execute(ApplicationCommand command);
    bool process_interaction_request(const ApplicationState &);
    bool open_modal(std::string identity, std::string target, bool destructive);
    ApplicationService &application_;
    ParameterBindingService bindings_;
    RecordingWorkflowCoordinator recording_workflow_;
    SlintSetupServices setup_services_;
    AppliancePreferences staged_preferences_;
    std::uint64_t setup_runtime_revision_{};
    SlintUiState state_;
    SlintRefreshDiagnostics refresh_diagnostics_;
    std::uint64_t structural_fingerprint_{};
    std::uint64_t context_pad_fingerprint_{};
    std::unordered_map<std::uint64_t, std::array<SlintPadItem, 16>>
        context_pad_cache_;
    std::string last_error_;
    bool active_{true};
    std::uint64_t local_revision_{1}, published_local_revision_{};
    std::size_t context_bank_{};
    std::uint64_t consumed_interaction_request_{};
    std::vector<SlintPerformancePageState::AssignmentTargetItem>
        cached_performance_parameters_, cached_performance_actions_;
    std::vector<SlintSetupMappingTargetItem> cached_setup_targets_;
    std::uint64_t assignment_discovery_fingerprint_{};
    struct PendingMappingConflict {
        std::uint32_t port{};
        std::uint8_t channel{}, number{};
        std::string mode, control_id;
        bool feedback{};
    };
    std::optional<PendingMappingConflict> pending_mapping_conflict_;
    std::vector<std::string> browser_recent_locations_;
    class BrowserWorker;
    std::unique_ptr<BrowserWorker> browser_worker_;
};

struct SlintOptions {
    int width{1280}, height{800};
    bool fullscreen{};
    std::function<bool()> request_close;
    std::function<bool()> external_stop_requested;
    SlintSetupServices setup;
    // Generated-component CI hook. The appliance CLI never enables this.
    bool interaction_test{};
    bool interaction_test_backend{true};
    std::string screenshot_path;
};

[[nodiscard]] bool slint_available() noexcept;
int run_slint_frontend(ApplicationService &, const SlintOptions &,
                       std::string &error);

} // namespace neurogenic
