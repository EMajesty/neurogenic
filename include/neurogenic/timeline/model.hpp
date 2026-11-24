#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {
using MusicalTick = std::int64_t;
using EventId = std::uint64_t;
using TrackId = std::uint64_t;
using PatternId = std::uint64_t;
using ClipId = std::uint64_t;
using AudioClipId = std::uint64_t;
using AudioCrossfadeId = std::uint64_t;
using PatternChainId = std::uint64_t;
using LaunchSlotId = std::uint64_t;
using PerformanceSceneId = std::uint64_t;
using InstrumentInstanceId = std::uint32_t;
inline constexpr InstrumentInstanceId default_synth_instance_id = 1;
inline constexpr std::size_t maximum_instrument_instances = 16;
inline constexpr std::uint32_t ticks_per_quarter = 960;
constexpr std::uint32_t stable_id(std::string_view value) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : value) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}
inline constexpr std::string_view sample_instrument_id = "sample.main";
inline constexpr std::string_view subtractive_instrument_id =
    "synth.subtractive.v1";
inline constexpr std::string_view external_midi_instrument_id =
    "midi.external.v1";
inline constexpr auto sample_instrument_key = stable_id(sample_instrument_id),
                      subtractive_instrument_key =
                          stable_id(subtractive_instrument_id),
                      external_midi_instrument_key =
                          stable_id(external_midi_instrument_id);

enum class MusicalEventType : std::uint8_t {
    note,
    note_on,
    note_off,
    parameter,
    control_change,
    program_change,
    pitch_bend,
    audio_clip_on,
    audio_clip_off
};
enum class TriggerCondition : std::uint8_t {
    always,
    first_loop,
    not_first_loop,
    every_nth_loop,
    n_of_m,
    previous_fired,
    previous_not_fired,
    fill_active,
    fill_inactive
};
struct MusicalEvent {
    EventId id{};
    MusicalTick tick{};
    MusicalEventType type{MusicalEventType::note};
    std::uint8_t pitch{60};
    float velocity{1.0F};
    MusicalTick duration{};
    std::uint32_t parameter_id{};
    float value{};
    EventId target_event_id{};
    std::string parameter_destination;
    float trigger_probability{1.0F};
    TriggerCondition trigger_condition{TriggerCondition::always};
    std::uint16_t condition_n{1}, condition_m{1};
    std::uint8_t ratchet_count{1};
};
struct MusicalPattern {
    MusicalTick length{ticks_per_quarter * 4};
    std::vector<MusicalEvent> events;
};
struct Track {
    TrackId id{};
    std::string name;
    bool muted{};
    MusicalPattern pattern;
    std::string instrument_id{sample_instrument_id};
    InstrumentInstanceId instrument_instance_id{default_synth_instance_id};
    std::uint32_t midi_output_port{1};
    std::uint8_t midi_channel{};
};
struct TransportMetadata {
    std::uint32_t microseconds_per_quarter{500000};
    std::uint16_t time_signature_numerator{4};
    std::uint16_t time_signature_denominator{4};
};
enum class TimelinePlaybackMode : std::uint8_t { legacy, live_pattern, song };
enum class LaunchQuantization : std::uint8_t {
    immediate,
    quarter,
    bar,
    two_bars,
    pattern_end,
    four_bars,
    eight_bars,
    subdivision
};
enum class LaunchSourceType : std::uint8_t { pattern, audio_clip, live_loop };
enum class LaunchBehavior : std::uint8_t {
    one_shot, toggle, gate, loop, retrigger, legato
};
enum class PerformancePlaybackMode : std::uint8_t {
    arrangement, session, hybrid
};
enum class VelocityPolicy : std::uint8_t { fixed, native, full_level };
enum class SlotFollowAction : std::uint8_t {
    none, stop, repeat, next, previous, specified, random_eligible
};
struct LaunchSlot {
    LaunchSlotId id{};
    TrackId track{};
    LaunchSourceType source_type{LaunchSourceType::pattern};
    std::uint64_t source_id{};
    LaunchBehavior behavior{LaunchBehavior::loop};
    LaunchQuantization quantization{LaunchQuantization::bar};
    MusicalTick subdivision_ticks{ticks_per_quarter / 4};
    SlotFollowAction follow{SlotFollowAction::none};
    LaunchSlotId follow_target{};
    std::uint16_t follow_repetitions{1};
    std::uint64_t random_seed{1};
    bool quantization_override{};
    // Optional presentation-neutral logical surface position. This is not a
    // protocol control ID or physical panel coordinate. Profiles and UIs may
    // expose any supported number of positions; -1 means unassigned.
    std::int32_t logical_position{-1};
};
struct PerformanceSceneEntry {
    TrackId track{};
    LaunchSlotId slot{};
};
struct PerformanceLaunchScene {
    PerformanceSceneId id{};
    std::string name;
    std::vector<PerformanceSceneEntry> entries;
    LaunchQuantization quantization{LaunchQuantization::bar};
    MusicalTick subdivision_ticks{ticks_per_quarter / 4};
};
enum class FollowActionType : std::uint8_t {
    continue_current,
    next,
    previous,
    specific,
    random_eligible,
    stop
};
enum class VoiceTransitionPolicy : std::uint8_t {
    preserve,
    release_outgoing,
    stop_outgoing
};
enum class SongEndBehavior : std::uint8_t { loop, stop, return_to_live };
struct PatternFollowAction {
    FollowActionType type{FollowActionType::next};
    std::uint16_t after_repetitions{1};
    float probability{1.0F};
    PatternId target{};
};
struct PatternTrack {
    TrackId track{};
    MusicalPattern pattern;
};
struct PatternDefinition {
    PatternId id{};
    std::string name;
    MusicalTick length{ticks_per_quarter * 4};
    std::uint16_t time_signature_numerator{4};
    std::uint16_t time_signature_denominator{4};
    std::vector<PatternTrack> tracks;
    PatternFollowAction follow;
};
struct PatternChainEntry {
    PatternId pattern{};
    std::uint16_t repeats{1};
};
struct ArrangementClip {
    ClipId id{};
    PatternId pattern{};
    MusicalTick start{};
    MusicalTick duration{ticks_per_quarter * 4};
    bool loop{true};
    std::int8_t transpose{};
    float velocity_scale{1.0F};
    bool enabled{true};
};
enum class AudioClipDirection : std::uint8_t { forward, reverse };
enum class AudioClipStretchMode : std::uint8_t { native, tempo_sync };
enum class AudioClipFadeCurve : std::uint8_t { linear, equal_power };
struct AudioClip {
    AudioClipId id{};
    TrackId track{};
    std::uint32_t sample_slot{};
    std::uint64_t sample_publication{}; // Runtime binding; zero selects active.
    std::string asset_reference;
    std::uint64_t region_id{};
    MusicalTick start{};
    MusicalTick duration{ticks_per_quarter * 4};
    std::uint64_t source_start_frame{}, source_end_frame{};
    std::uint64_t loop_start_frame{}, loop_end_frame{};
    std::uint64_t fade_in_frames{}, fade_out_frames{};
    // Arrangement-domain fades. Unlike the source-frame region fades above,
    // these follow the clip's musical time through stretch, reverse and loops.
    MusicalTick clip_fade_in{}, clip_fade_out{};
    std::uint32_t loop_crossfade_frames{};
    float gain{1.0F}, pan{}, source_bpm{}, pitch_semitones{};
    AudioClipDirection direction{AudioClipDirection::forward};
    AudioClipStretchMode stretch_mode{AudioClipStretchMode::native};
    AudioClipFadeCurve fade_curve{AudioClipFadeCurve::linear};
    AudioClipFadeCurve clip_fade_curve{AudioClipFadeCurve::linear};
    bool loop{}, enabled{true};
};
struct AudioClipCrossfade {
    AudioCrossfadeId id{};
    AudioClipId outgoing_clip{}, incoming_clip{};
    MusicalTick duration{};
    AudioClipFadeCurve curve{AudioClipFadeCurve::equal_power};
    bool enabled{true};
};
struct SongArrangement {
    TimelinePlaybackMode playback_mode{TimelinePlaybackMode::legacy};
    std::vector<PatternDefinition> patterns;
    std::vector<ArrangementClip> clips;
    std::vector<AudioClip> audio_clips;
    std::vector<AudioClipCrossfade> audio_crossfades;
    std::vector<LaunchSlot> launch_slots;
    std::vector<PerformanceLaunchScene> performance_scenes;
    PerformancePlaybackMode performance_mode{PerformancePlaybackMode::arrangement};
    VelocityPolicy velocity_policy{VelocityPolicy::native};
    float fixed_velocity{1.0F};
    PatternId active_pattern{}, queued_pattern{};
    LaunchQuantization launch_quantization{LaunchQuantization::bar};
    std::vector<PatternId> pattern_chain;
    std::size_t pattern_chain_position{};
    PatternChainId pattern_chain_id{1};
    std::vector<PatternChainEntry> chain_entries;
    bool chain_loop{}, chain_running{};
    VoiceTransitionPolicy voice_transition{VoiceTransitionPolicy::preserve};
    SongEndBehavior song_end_behavior{SongEndBehavior::loop};
    PatternId return_live_pattern{};
    MusicalTick song_start{}, song_end{};
    bool loop_enabled{};
    MusicalTick loop_start{}, loop_end{};
    // Absolute song-position parameter events. These are materialized into the
    // existing timeline event stream; base parameter values remain unchanged.
    std::vector<MusicalEvent> automation;
};
struct Timeline {
    TransportMetadata transport;
    std::vector<Track> tracks;
    std::uint64_t generation_seed{};
    SongArrangement arrangement;
};
} // namespace neurogenic
