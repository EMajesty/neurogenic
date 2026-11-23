#pragma once

#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include <array>
#include <cstdint>
#include <functional>
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
    bool available{};
};

struct SlintPadItem {
    std::string label, status{"Unavailable"};
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
    double tempo_bpm{120.0};
    std::string project_name{"Untitled"}, transport{"Stopped"},
        time_signature{"4/4"}, position{"1.1.1"},
        selected_track{"Unavailable"}, active_content{"Unavailable"},
        track_owner{"Arrangement"}, launch_quantization{"Bar"},
        page_id{"performance"}, page_title{"Performance"}, notification;
    std::vector<SlintPageItem> pages;
};

struct SlintContextState {
    std::array<SlintContextItem, 8> encoders;
    std::array<SlintContextItem, 8> functions;
};

struct SlintPerformancePageState {
    std::string selected_track{"Unavailable"}, active_content{"Unavailable"},
        track_owner{"Arrangement"}, launch_quantization{"Bar"};
    std::vector<SlintSceneItem> scenes;
    std::array<SlintPadItem, 16> pads;
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
    bool selected{}, enabled{};
};

struct SlintSequencerPageState {
    std::string selected_track{"Unavailable"}, selected_pattern{"Unavailable"},
        resolution{"1/16"}, pattern_length{"Unavailable"},
        selected_velocity{"--"}, selected_duration{"--"};
    std::uint64_t step_page{}, selected_pattern_id{}, selected_clip_id{};
    bool arrangement_view{};
    std::array<SlintStepItem, 16> steps;
    std::vector<SlintPatternItem> patterns;
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
    float zoom{1.0F}, offset{};
    bool ready{}, loading{}, missing{}, previewing{}, loop_enabled{};
    std::uint64_t fade_in{}, fade_out{}, loop_start{}, loop_end{};
    std::vector<SlintWaveformPeak> waveform;
    std::vector<SlintSliceMarker> slices;
    std::vector<SlintRegionItem> regions;
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

struct SlintUiState {
    SlintShellState shell;
    SlintContextState context;
    SlintPerformancePageState performance;
    SlintSequencerPageState sequencer;
    SlintSamplePageState sample;
    SlintSoundPageState sound;
};

// Presentation-neutral bridge used by the Slint renderer and renderer-free
// tests. It owns no engine state: every value is copied from ApplicationState
// and every mutation goes through a semantic action or ParameterBindingService.
class SlintFrontendAdapter {
  public:
    explicit SlintFrontendAdapter(ApplicationService &application);
    [[nodiscard]] bool refresh();
    [[nodiscard]] const SlintUiState &state() const noexcept {
        return state_;
    }
    [[nodiscard]] bool invoke_action(std::string action_id);
    [[nodiscard]] bool turn_encoder(std::size_t index, double steps,
                                    bool fine = false);
    [[nodiscard]] bool invoke_function(std::size_t index);
    [[nodiscard]] bool press_pad(std::size_t index, bool pressed = true);
    [[nodiscard]] bool select_page(std::size_t index);
    [[nodiscard]] bool select_track(int direction);
    [[nodiscard]] bool launch_scene(std::size_t index);
    [[nodiscard]] bool return_to_arrangement();
    [[nodiscard]] bool set_launch_quantization(std::string value);
    [[nodiscard]] bool cycle_launch_quantization();
    [[nodiscard]] bool transport_record();
    [[nodiscard]] bool select_pattern(std::size_t index, bool launch = false);
    [[nodiscard]] bool create_pattern(bool duplicate = false);
    [[nodiscard]] bool select_step(std::size_t index);
    [[nodiscard]] bool set_step_velocity(double value);
    [[nodiscard]] bool set_step_duration(std::int64_t ticks);
    [[nodiscard]] bool cycle_step_resolution();
    [[nodiscard]] bool toggle_arrangement_view();
    [[nodiscard]] bool select_arrangement_clip(std::size_t index);
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
    void dismiss_notification() noexcept;
    [[nodiscard]] bool keyboard_action(std::string_view key, bool pressed);
    void shutdown() noexcept { active_ = false; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string &last_error() const noexcept {
        return last_error_;
    }

  private:
    bool execute(ApplicationCommand command);
    ApplicationService &application_;
    ParameterBindingService bindings_;
    SlintUiState state_;
    std::string last_error_;
    bool active_{true};
    std::uint64_t local_revision_{1}, published_local_revision_{};
};

struct SlintOptions {
    int width{1280}, height{800};
    bool fullscreen{};
    std::function<bool()> request_close;
    std::function<bool()> external_stop_requested;
};

[[nodiscard]] bool slint_available() noexcept;
int run_slint_frontend(ApplicationService &, const SlintOptions &,
                       std::string &error);

} // namespace neurogenic
