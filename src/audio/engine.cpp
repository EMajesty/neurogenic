#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
constexpr MusicalTick repeat_ticks(NoteRepeatDivision division) noexcept {
    switch (division) {
    case NoteRepeatDivision::quarter: return ticks_per_quarter;
    case NoteRepeatDivision::eighth: return ticks_per_quarter / 2;
    case NoteRepeatDivision::sixteenth: return ticks_per_quarter / 4;
    case NoteRepeatDivision::thirty_second: return ticks_per_quarter / 8;
    case NoteRepeatDivision::eighth_triplet: return ticks_per_quarter / 3;
    case NoteRepeatDivision::sixteenth_triplet: return ticks_per_quarter / 6;
    case NoteRepeatDivision::thirty_second_triplet: return ticks_per_quarter / 12;
    }
    return ticks_per_quarter / 4;
}
MusicalTick repeat_tick_at_frame(std::uint64_t frame, std::uint32_t rate,
                                 std::uint32_t tempo_us) noexcept {
    const auto numerator = static_cast<__uint128_t>(frame) * 1'000'000U *
                           ticks_per_quarter;
    return static_cast<MusicalTick>(numerator /
        (static_cast<__uint128_t>(rate) * tempo_us));
}
std::uint64_t repeat_frame_at_tick(MusicalTick tick, std::uint32_t rate,
                                   std::uint32_t tempo_us) noexcept {
    if (tick <= 0) return 0;
    const auto numerator = static_cast<__uint128_t>(tick) * rate * tempo_us;
    const auto denominator =
        static_cast<__uint128_t>(1'000'000U) * ticks_per_quarter;
    return static_cast<std::uint64_t>((numerator + denominator - 1) /
                                      denominator);
}
constexpr TrackId repeat_track_owner(std::uint64_t owner) noexcept {
    return (TrackId{1} << 63U) | (owner & ~(TrackId{1} << 63U));
}
}
AudioEngine::AudioEngine(std::uint32_t rate, std::size_t voice_capacity,
                         std::size_t event_staging_capacity,
                         std::size_t sample_version_capacity)
    : rate_(rate), scheduler_(rate, 120.0), timeline_scheduler_(rate),
      sample_updates_(sample_version_capacity + 1),
      preset_activation_slots_(
          std::make_unique<PreparedPresetActivation[]>(
              preset_activation_capacity)),
      sample_versions_(
          std::make_unique<SampleVersion[]>(sample_version_capacity)),
      sample_version_capacity_(sample_version_capacity),
      voices_(voice_capacity), legacy_events_(event_staging_capacity),
      timeline_events_(event_staging_capacity), midi_clock_(rate),
      midi_clock_follower_(rate), delay_(rate), modulation_(rate),
      mixer_(rate) {
    if (rate == 0 || voice_capacity == 0 || event_staging_capacity == 0 ||
        sample_version_capacity == 0)
        throw std::invalid_argument(
            "sample rate and all bounded capacities must be positive");
    for (auto &synth : synths_)
        synth = std::make_unique<SubtractiveSynth>(rate, voice_capacity);
    note_slots_.fill(invalid_sample_slot);
    for (auto &target : note_slices_) {
        target.playback.mode = SamplePlaybackMode::gate;
        target.playback.release_seconds = 0;
    }
    for (std::size_t i = 0; i < default_note_bank_size; ++i)
        note_slots_[36 + i] = SampleSlotId{static_cast<std::uint32_t>(i)};
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::set_sample(Sample sample) {
    set_sample_slot(SampleSlotId{0}, std::move(sample));
    note_slots_.fill(SampleSlotId{0});
}

std::size_t AudioEngine::find_active_sample(SampleSlotId id) const noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                id.value &&
            sample_versions_[i].state.load(std::memory_order_acquire) ==
                SampleVersionState::active)
            return i;
    return std::numeric_limits<std::size_t>::max();
}

bool AudioEngine::has_sample_slot(SampleSlotId id) const noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i) {
        const auto state =
            sample_versions_[i].state.load(std::memory_order_acquire);
        if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                id.value &&
            (state == SampleVersionState::queued ||
             state == SampleVersionState::active))
            return true;
    }
    return false;
}

std::size_t AudioEngine::sample_slot_count() const noexcept {
    std::size_t count{};
    for (std::size_t i = 0; i < sample_version_capacity_; ++i) {
        const auto state =
            sample_versions_[i].state.load(std::memory_order_acquire);
        if (state != SampleVersionState::queued &&
            state != SampleVersionState::active)
            continue;
        const auto id = sample_versions_[i].id.load(std::memory_order_acquire);
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j) {
            const auto prior =
                sample_versions_[j].state.load(std::memory_order_acquire);
            if (sample_versions_[j].id.load(std::memory_order_acquire) == id &&
                (prior == SampleVersionState::queued ||
                 prior == SampleVersionState::active)) {
                seen = true;
                break;
            }
        }
        if (!seen)
            ++count;
    }
    return count;
}

void AudioEngine::set_sample_slot(SampleSlotId id, Sample sample) {
    if (id == invalid_sample_slot)
        throw std::invalid_argument("invalid sample slot identifier");
    if (sample.sample_rate != rate_)
        throw std::invalid_argument(
            "prepared sample rate does not match engine rate; use "
            "load_wav(path, engine.sample_rate()) or SampleManager");
    if ((sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.empty() ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument(
            "sample must contain non-empty mono or stereo interleaved audio");
    if (!publish_sample_slot(id, std::move(sample)))
        throw std::runtime_error("sample update capacity is exhausted");
}

void AudioEngine::reclaim_sample_versions_unlocked() noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::retired) {
            sample_versions_[i].sample.reset();
            sample_versions_[i].id.store(invalid_sample_slot.value,
                                         std::memory_order_release);
            sample_versions_[i].publication.store(0, std::memory_order_release);
            sample_versions_[i].state.store(SampleVersionState::free,
                                            std::memory_order_release);
            reclaimed_sample_versions_.fetch_add(1, std::memory_order_relaxed);
        }
}
void AudioEngine::reclaim_sample_versions() noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    reclaim_sample_versions_unlocked();
}

bool AudioEngine::prepare_gapless_handoff(
    SampleSlotId slot, std::shared_ptr<Sample> sample, TrackId track,
    EventId clip, std::uint64_t loop_frames, std::string &error) noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    if (gapless_handoff_.phase.load(std::memory_order_acquire) != 0 || !sample ||
        slot == invalid_sample_slot || !track || !clip || loop_frames == 0 ||
        sample->sample_rate != rate_ || sample->frames() != loop_frames) {
        error = "invalid or already prepared gapless handoff";
        return false;
    }
    reclaim_sample_versions_unlocked();
    std::size_t version = sample_version_capacity_;
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::free) { version = i; break; }
    if (version == sample_version_capacity_) {
        error = "sample-version capacity exhausted";
        return false;
    }
    const auto reserved = reserved_handoff_voices_.fetch_add(
        1, std::memory_order_acq_rel);
    if (reserved >= voices_.size() ||
        active_voice_count_.load(std::memory_order_acquire) + reserved >=
            voices_.size()) {
        reserved_handoff_voices_.fetch_sub(1, std::memory_order_acq_rel);
        error = "sample-voice capacity exhausted";
        return false;
    }
    sample_versions_[version].sample = std::move(sample);
    sample_versions_[version].id.store(slot.value, std::memory_order_release);
    sample_versions_[version].publication.store(++next_sample_update_sequence_,
                                                std::memory_order_release);
    sample_versions_[version].state.store(SampleVersionState::queued,
                                          std::memory_order_release);
    gapless_handoff_.sample_version = version;
    gapless_handoff_.slot = slot;
    gapless_handoff_.track = track;
    gapless_handoff_.clip = clip;
    gapless_handoff_.loop_frames = loop_frames;
    gapless_handoff_.first_frame.store(0, std::memory_order_relaxed);
    gapless_handoff_.phase.store(1, std::memory_order_release);
    error.clear();
    return true;
}
void AudioEngine::cancel_gapless_handoff() noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    std::uint8_t prepared = 1;
    if (gapless_handoff_.phase.compare_exchange_strong(
            prepared, 0, std::memory_order_acq_rel)) {
        const auto version = gapless_handoff_.sample_version;
        if (version < sample_version_capacity_)
            sample_versions_[version].state.store(SampleVersionState::retired,
                                                  std::memory_order_release);
        reserved_handoff_voices_.fetch_sub(1, std::memory_order_acq_rel);
    } else if (prepared == 2)
        gapless_handoff_.phase.store(0, std::memory_order_release);
}
bool AudioEngine::gapless_handoff_prepared() const noexcept {
    return gapless_handoff_.phase.load(std::memory_order_acquire) == 1;
}
bool AudioEngine::gapless_handoff_completed() const noexcept {
    return gapless_handoff_.phase.load(std::memory_order_acquire) == 2;
}
std::uint64_t AudioEngine::gapless_first_playback_frame() const noexcept {
    return gapless_handoff_.first_frame.load(std::memory_order_acquire);
}

bool AudioEngine::publish_sample_slot(SampleSlotId id, Sample sample,
                                      std::uint64_t *publication) noexcept {
    try {
        return publish_sample_slot(
            id, std::make_shared<const Sample>(std::move(sample)), publication);
    } catch (...) {
        return false;
    }
}

bool AudioEngine::publish_sample_slot(SampleSlotId id,
                                      std::shared_ptr<const Sample> sample,
                                      std::uint64_t *publication) noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    if (publication)
        *publication = 0;
    if (id == invalid_sample_slot || !sample || sample->sample_rate != rate_ ||
        (sample->channels != 1 && sample->channels != 2) ||
        sample->interleaved.empty() ||
        sample->interleaved.size() % sample->channels != 0)
        return false;
    reclaim_sample_versions_unlocked();
    std::size_t version = sample_version_capacity_;
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::free) {
            version = i;
            break;
        }
    if (version == sample_version_capacity_) {
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sample_versions_[version].sample = std::move(sample);
    sample_versions_[version].id.store(id.value, std::memory_order_release);
    sample_versions_[version].state.store(SampleVersionState::queued,
                                          std::memory_order_release);
    if (next_sample_update_sequence_ ==
            std::numeric_limits<std::uint64_t>::max() ||
        !sample_updates_.push({SampleUpdate::Type::install, id, version,
                               next_sample_update_sequence_ + 1})) {
        sample_versions_[version].state.store(SampleVersionState::free,
                                              std::memory_order_release);
        sample_versions_[version].sample.reset();
        sample_versions_[version].id.store(invalid_sample_slot.value,
                                           std::memory_order_release);
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++next_sample_update_sequence_;
    sample_versions_[version].publication.store(next_sample_update_sequence_,
                                                std::memory_order_release);
    if (publication)
        *publication = next_sample_update_sequence_;
    return true;
}

bool AudioEngine::unload_sample_slot(SampleSlotId id,
                                     std::uint64_t *publication) noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    if (publication)
        *publication = 0;
    if (id == invalid_sample_slot ||
        next_sample_update_sequence_ ==
            std::numeric_limits<std::uint64_t>::max() ||
        !sample_updates_.push({SampleUpdate::Type::unload, id, 0,
                               next_sample_update_sequence_ + 1})) {
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++next_sample_update_sequence_;
    if (publication)
        *publication = next_sample_update_sequence_;
    return true;
}

bool AudioEngine::map_note_to_slot(std::uint8_t note,
                                   SampleSlotId id) noexcept {
    if (processing_started_.load(std::memory_order_acquire) ||
        !has_sample_slot(id))
        return false;
    note_slots_[note] = id;
    return true;
}

void AudioEngine::trigger(std::uint8_t note, float velocity,
                          std::uint32_t frame_offset) noexcept {
    Trigger event{};
    event.note = note;
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    if (!triggers_.push(event))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::trigger_pad(std::uint8_t note, float velocity, bool note_on,
                              float pitch_offset,
                              std::uint32_t frame_offset) noexcept {
    if (!std::isfinite(velocity) || velocity < 0 || velocity > 1 ||
        !std::isfinite(pitch_offset) || pitch_offset < -96 || pitch_offset > 96)
        return false;
    Trigger event{};
    event.note = note;
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.note_on = note_on;
    event.pad_event = true;
    event.pitch_offset = pitch_offset;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::trigger_slice(SampleSlotId slot, std::uint64_t publication,
                                std::uint64_t start_frame,
                                std::uint64_t end_frame, float slice_gain,
                                float velocity,
                                std::uint32_t frame_offset) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(slice_gain) ||
        slice_gain < 0 || !std::isfinite(velocity) || velocity < 0)
        return false;
    Trigger event{};
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.slot = slot;
    event.publication = publication;
    event.start_frame = start_frame;
    event.end_frame = end_frame;
    event.slice_gain = slice_gain;
    event.sliced = true;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::trigger_region(
    SampleSlotId slot, std::uint64_t publication, std::uint64_t start_frame,
    std::uint64_t end_frame, float gain, SamplePlaybackSettings playback,
    SampleRegionPlayback region, float velocity,
    std::uint32_t frame_offset) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(gain) || gain < 0 ||
        !std::isfinite(velocity) || velocity < 0 ||
        region.fade_in_frames > end_frame - start_frame ||
        region.fade_out_frames > end_frame - start_frame ||
        (region.loop_enabled &&
         (region.loop_start_frame < start_frame ||
          region.loop_start_frame >= region.loop_end_frame ||
          region.loop_end_frame > end_frame ||
          region.loop_crossfade_frames >
              region.loop_end_frame - region.loop_start_frame)))
        return false;
    Trigger event{};
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.slot = slot;
    event.publication = publication;
    event.start_frame = start_frame;
    event.end_frame = end_frame;
    event.slice_gain = gain;
    event.playback = playback;
    event.region = region;
    event.sliced = true;
    event.region_trigger = true;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::stop_slice_previews() noexcept {
    Trigger event{};
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.stop_previews = true;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::map_note_to_slice(std::uint8_t note, SampleSlotId slot,
                                    std::uint64_t publication,
                                    std::uint64_t start_frame,
                                    std::uint64_t end_frame, float gain,
                                    SamplePlaybackSettings playback) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(gain) || gain < 0)
        return false;
    playback.gain *= gain;
    SliceTarget target{};
    target.slot = slot;
    target.publication = publication;
    target.start_frame = start_frame;
    target.end_frame = end_frame;
    target.playback = playback;
    target.enabled = true;
    return slice_map_commands_.push({SliceMapCommand::Type::map, note, target});
}
bool AudioEngine::map_note_to_region(
    std::uint8_t note, SampleSlotId slot, std::uint64_t publication,
    std::uint64_t start_frame, std::uint64_t end_frame, float gain,
    SamplePlaybackSettings playback, SampleRegionPlayback region) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(gain) || gain < 0 ||
        region.fade_in_frames > end_frame - start_frame ||
        region.fade_out_frames > end_frame - start_frame ||
        (region.loop_enabled &&
         (region.loop_start_frame < start_frame ||
          region.loop_start_frame >= region.loop_end_frame ||
          region.loop_end_frame > end_frame ||
          region.loop_crossfade_frames >
              region.loop_end_frame - region.loop_start_frame)))
        return false;
    playback.gain *= gain;
    SliceTarget target{};
    target.slot = slot;
    target.publication = publication;
    target.start_frame = start_frame;
    target.end_frame = end_frame;
    target.playback = playback;
    target.region = region;
    target.enabled = true;
    return slice_map_commands_.push({SliceMapCommand::Type::map, note, target});
}
bool AudioEngine::clear_note_slice(std::uint8_t note) noexcept {
    return slice_map_commands_.push({SliceMapCommand::Type::clear, note, {}});
}
bool AudioEngine::publish_pad_mappings(
    const std::array<PadMappingActivation, 16> &mappings,
    std::uint16_t changed_mask) noexcept {
    PadMappingBatch batch;
    batch.changed_mask = changed_mask;
    for (std::size_t pad = 0; pad < mappings.size(); ++pad) {
        if ((changed_mask & (std::uint16_t{1} << pad)) == 0) continue;
        const auto &source = mappings[pad];
        auto &target = batch.targets[pad];
        if (!source.assigned) continue;
        if (source.slot == invalid_sample_slot || source.publication == 0 ||
            source.start_frame >= source.end_frame ||
            !std::isfinite(source.gain) || source.gain < 0)
            return false;
        target.slot = source.slot;
        target.publication = source.publication;
        target.start_frame = source.start_frame;
        target.end_frame = source.end_frame;
        target.playback = source.playback;
        target.playback.gain *= source.gain;
        target.region = source.region;
        target.enabled = true;
    }
    return changed_mask == 0 || pad_mapping_batches_.push(batch);
}
bool AudioEngine::configure_note_playback(
    std::uint8_t note, SamplePlaybackSettings settings) noexcept {
    SliceTarget target{};
    target.playback = settings;
    return slice_map_commands_.push(
        {SliceMapCommand::Type::settings, note, target});
}
void AudioEngine::trigger_synth(std::uint8_t note, float velocity, bool note_on,
                                InstrumentInstanceId instance) noexcept {
    if (instance == 0 || instance > synths_.size() ||
        !synth_triggers_.push({note, velocity, note_on, instance}))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::begin_note_repeat(const NoteRepeatTarget &target) noexcept {
    if (target.owner == 0 || target.generation == 0 || target.note > 127 ||
        !std::isfinite(target.velocity) || target.velocity < 0.0F ||
        target.velocity > 1.0F ||
        (target.kind == NoteRepeatTargetKind::synth &&
         (target.instrument == 0 || target.instrument > synths_.size())) ||
        (target.kind == NoteRepeatTargetKind::external_midi &&
         (target.midi_port == 0 || target.midi_port > maximum_midi_ports ||
          target.midi_channel > 15)))
        return false;
    if (!note_repeat_commands_.push(
            {NoteRepeatCommand::Type::begin, target})) {
        dropped_note_repeat_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::end_note_repeat(std::uint64_t owner,
                                  std::uint64_t generation) noexcept {
    if (owner == 0 || generation == 0) return false;
    NoteRepeatTarget target;
    target.owner = owner;
    target.generation = generation;
    if (!note_repeat_commands_.push({NoteRepeatCommand::Type::end, target})) {
        dropped_note_repeat_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::set_note_repeat_division(
    NoteRepeatDivision division) noexcept {
    NoteRepeatTarget target;
    target.division = division;
    if (!note_repeat_commands_.push(
            {NoteRepeatCommand::Type::update_division, target})) {
        dropped_note_repeat_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
void AudioEngine::cancel_note_repeats() noexcept {
    if (!note_repeat_commands_.push({NoteRepeatCommand::Type::cancel_all, {}}))
        dropped_note_repeat_commands_.fetch_add(1, std::memory_order_relaxed);
}
NoteRepeatState AudioEngine::note_repeat_state() const noexcept {
    return {published_note_repeat_count_.load(std::memory_order_acquire),
            published_note_repeat_owner_mask_.load(std::memory_order_acquire),
            note_repeat_triggers_.load(std::memory_order_acquire),
            note_repeat_cancellations_.load(std::memory_order_acquire),
            dropped_note_repeat_commands_.load(std::memory_order_acquire)};
}
bool AudioEngine::set_synth_parameter(SynthParameter parameter, float value,
                                      InstrumentInstanceId instance) noexcept {
    if (instance == 0 || instance > synths_.size())
        return false;
    if (!dsp_commands_.push({DspCommand::Target::synth,
                             static_cast<std::uint32_t>(parameter), value,
                             instance, {}})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::set_delay_parameter(DelayParameter parameter,
                                      float value) noexcept {
    if (!dsp_commands_.push({DspCommand::Target::delay,
                             static_cast<std::uint32_t>(parameter), value, 0,
                             {}})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::set_delay_bypass(bool value) noexcept {
    if (!dsp_commands_.push(
            {DspCommand::Target::delay_bypass, 0, value ? 1.0F : 0.0F, 0,
             {}})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::publish_modulation(
    const ModulationConfiguration &configuration) noexcept {
    auto prepared = configuration;
    ModulationGraphDiagnostics diagnostics;
    try {
        if (!prepare_modulation_graph(prepared, diagnostics))
            return false;
    } catch (...) {
        return false;
    }
    if (!modulation_configurations_.push(prepared)) {
        dropped_dsp_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
void AudioEngine::reclaim_preset_activations() noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    for (std::size_t i = 0; i < preset_activation_capacity; ++i) {
        auto &slot = preset_activation_slots_[i];
        if (slot.state.load(std::memory_order_acquire) ==
            PresetActivationState::applied)
            slot.state.store(PresetActivationState::free,
                             std::memory_order_release);
    }
    reclaim_sample_versions_unlocked();
}

bool AudioEngine::publish_preset_activation(
    const PresetActivationRequest &request, std::uint64_t &sequence,
    std::array<std::uint64_t, maximum_preset_samples> &sample_publications,
    std::string &error) noexcept {
    sequence = 0;
    sample_publications.fill(0);
    if (request.sample_count > request.samples.size() ||
        request.synth_parameter_count > request.synth_parameters.size()) {
        error = "preset activation capacity exceeded";
        return false;
    }
    auto mixer = request.mixer;
    if (!mixer_.prepare(mixer, error)) return false;
    auto modulation = request.modulation;
    ModulationGraphDiagnostics diagnostics;
    if (!prepare_modulation_graph(modulation, diagnostics)) {
        error = diagnostics.message;
        return false;
    }
    for (std::size_t i = 0; i < request.synth_parameter_count; ++i) {
        const auto &parameter = request.synth_parameters[i];
        if (parameter.instance == 0 || parameter.instance > synths_.size() ||
            !std::isfinite(parameter.value) ||
            parameter.parameter < SynthParameter::waveform ||
            parameter.parameter > SynthParameter::pitch) {
            error = "invalid preset synth parameter";
            return false;
        }
    }
    for (std::size_t i = 0; i < request.sample_count; ++i) {
        const auto &sample = request.samples[i];
        if (sample.slot == invalid_sample_slot || !sample.sample ||
            sample.sample->sample_rate != rate_ ||
            (sample.sample->channels != 1 && sample.sample->channels != 2) ||
            sample.sample->interleaved.empty() ||
            sample.sample->interleaved.size() % sample.sample->channels != 0) {
            error = "invalid prepared preset sample";
            return false;
        }
    }
    for (const auto &pad : request.pads) {
        if (!pad.assigned) continue;
        const auto length = pad.end_frame > pad.start_frame
                                ? pad.end_frame - pad.start_frame
                                : 0;
        if (pad.slot == invalid_sample_slot || length == 0 ||
            !std::isfinite(pad.gain) || pad.gain < 0 ||
            pad.region.fade_in_frames > length ||
            pad.region.fade_out_frames > length ||
            (pad.region.loop_enabled &&
             (pad.region.loop_start_frame < pad.start_frame ||
              pad.region.loop_start_frame >= pad.region.loop_end_frame ||
              pad.region.loop_end_frame > pad.end_frame ||
              pad.region.loop_crossfade_frames >
                  pad.region.loop_end_frame -
                      pad.region.loop_start_frame))) {
            error = "invalid prepared preset region";
            return false;
        }
    }
    std::lock_guard lock(sample_update_producer_mutex_);
    reclaim_sample_versions_unlocked();
    std::size_t activation = preset_activation_capacity;
    for (std::size_t i = 0; i < preset_activation_capacity; ++i)
        if (preset_activation_slots_[i].state.load(std::memory_order_acquire) ==
            PresetActivationState::free) {
            activation = i;
            break;
        }
    if (activation == preset_activation_capacity) {
        error = "preset activation queue is full";
        return false;
    }
    std::array<std::size_t, maximum_preset_samples> versions{};
    std::size_t reserved{};
    for (std::size_t i = 0; i < sample_version_capacity_ &&
                            reserved < request.sample_count;
         ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::free)
            versions[reserved++] = i;
    if (reserved != request.sample_count) {
        error = "sample version capacity is exhausted";
        return false;
    }
    if (next_preset_activation_ == std::numeric_limits<std::uint64_t>::max() ||
        next_sample_update_sequence_ >
            std::numeric_limits<std::uint64_t>::max() - request.sample_count) {
        error = "preset activation sequence is exhausted";
        return false;
    }
    auto &slot = preset_activation_slots_[activation];
    slot.sequence = ++next_preset_activation_;
    slot.mixer = mixer;
    slot.modulation = modulation;
    slot.sample_count = request.sample_count;
    slot.synth_parameters = request.synth_parameters;
    slot.synth_parameter_count = request.synth_parameter_count;
    slot.pads = request.pads;
    slot.replace_pad_mappings = request.replace_pad_mappings;
    slot.release_existing_voices = request.release_existing_voices;
    for (std::size_t i = 0; i < request.sample_count; ++i) {
        const auto version = versions[i];
        const auto publication = ++next_sample_update_sequence_;
        slot.sample_versions[i] = version;
        slot.sample_publications[i] = publication;
        sample_publications[i] = publication;
        sample_versions_[version].sample = request.samples[i].sample;
        sample_versions_[version].id.store(request.samples[i].slot.value,
                                           std::memory_order_release);
        sample_versions_[version].publication.store(publication,
                                                    std::memory_order_release);
        sample_versions_[version].state.store(SampleVersionState::queued,
                                              std::memory_order_release);
    }
    slot.state.store(PresetActivationState::queued, std::memory_order_release);
    if (!preset_activations_.push(static_cast<std::uint8_t>(activation))) {
        slot.state.store(PresetActivationState::free, std::memory_order_release);
        for (std::size_t i = 0; i < request.sample_count; ++i) {
            auto &version = sample_versions_[versions[i]];
            version.sample.reset();
            version.id.store(invalid_sample_slot.value,
                             std::memory_order_release);
            version.publication.store(0, std::memory_order_release);
            version.state.store(SampleVersionState::free,
                                std::memory_order_release);
        }
        error = "preset activation queue is full";
        return false;
    }
    sequence = slot.sequence;
    error.clear();
    return true;
}

bool AudioEngine::finalize_preset_activation(
    std::uint64_t sequence) noexcept {
    if (!preset_activation_applied(sequence)) return false;
    for (std::size_t i = 0; i < preset_activation_capacity; ++i) {
        auto &slot = preset_activation_slots_[i];
        if (slot.sequence == sequence &&
            slot.state.load(std::memory_order_acquire) ==
                PresetActivationState::applied) {
            mixer_.set_control_configuration(slot.mixer);
            reclaim_preset_activations();
            return true;
        }
    }
    return false;
}

bool AudioEngine::cancel_preset_activation(std::uint64_t sequence) noexcept {
    if (!sequence) return false;
    for (std::size_t i = 0; i < preset_activation_capacity; ++i) {
        auto &slot = preset_activation_slots_[i];
        if (slot.sequence != sequence) continue;
        auto expected = PresetActivationState::queued;
        return slot.state.compare_exchange_strong(
            expected, PresetActivationState::cancelled,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }
    return false;
}

void AudioEngine::activate_preset(std::size_t index) noexcept {
    if (index >= preset_activation_capacity) return;
    auto &activation = preset_activation_slots_[index];
    const auto state = activation.state.load(std::memory_order_acquire);
    if (state == PresetActivationState::cancelled) {
        for (std::size_t sample = 0; sample < activation.sample_count; ++sample)
            sample_versions_[activation.sample_versions[sample]].state.store(
                SampleVersionState::retired, std::memory_order_release);
        cancelled_preset_activation_.store(activation.sequence,
                                            std::memory_order_release);
        activation.state.store(PresetActivationState::applied,
                               std::memory_order_release);
        return;
    }
    if (state != PresetActivationState::queued)
        return;
    if (activation.release_existing_voices) stop_voices(false);
    for (std::size_t sample = 0; sample < activation.sample_count; ++sample) {
        const auto version_index = activation.sample_versions[sample];
        const auto id = sample_versions_[version_index].id.load(
            std::memory_order_acquire);
        for (std::size_t i = 0; i < sample_version_capacity_; ++i)
            if (i != version_index &&
                sample_versions_[i].id.load(std::memory_order_acquire) == id &&
                sample_versions_[i].state.load(std::memory_order_acquire) ==
                    SampleVersionState::active)
                sample_versions_[i].state.store(SampleVersionState::retiring,
                                                std::memory_order_release);
        sample_versions_[version_index].state.store(SampleVersionState::active,
                                                    std::memory_order_release);
    }
    mixer_.activate_prepared(activation.mixer);
    (void)modulation_.configure(activation.modulation);
    mixer_.clear_modulation();
    for (std::size_t instance = 0; instance < synths_.size(); ++instance)
        (void)synths_[instance]->configure_voice_modulation(
            activation.modulation, static_cast<std::uint32_t>(instance + 1));
    for (std::size_t i = 0; i < activation.synth_parameter_count; ++i) {
        const auto &parameter = activation.synth_parameters[i];
        (void)synths_[parameter.instance - 1]->set_parameter(
            static_cast<std::uint32_t>(parameter.parameter), parameter.value);
    }
    if (activation.replace_pad_mappings)
        for (std::size_t pad = 0; pad < activation.pads.size(); ++pad) {
            const auto note = static_cast<std::uint8_t>(36 + pad);
            const auto &source = activation.pads[pad];
            if (!source.assigned) {
                note_slices_[note].enabled = false;
                note_slices_[note].playback = source.playback;
                continue;
            }
            SliceTarget target;
            target.slot = source.slot;
            target.start_frame = source.start_frame;
            target.end_frame = source.end_frame;
            target.gain = source.gain;
            target.playback = source.playback;
            target.region = source.region;
            target.enabled = true;
            const auto active = find_active_sample(source.slot);
            if (active < sample_version_capacity_)
                target.publication = sample_versions_[active].publication.load(
                    std::memory_order_acquire);
            note_slices_[note] = target;
        }
    if (activation.sample_count)
        applied_sample_update_sequence_.store(
            activation.sample_publications[activation.sample_count - 1],
            std::memory_order_release);
    applied_preset_activation_.store(activation.sequence,
                                     std::memory_order_release);
    activation.state.store(PresetActivationState::applied,
                           std::memory_order_release);
}
bool AudioEngine::set_modulation_input(ModulationSourceId source,
                                       float value) noexcept {
    if (!dsp_commands_.push({DspCommand::Target::modulation_input,
                             static_cast<std::uint32_t>(source), value, 0,
                             {}})) {
        dropped_dsp_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::set_modulator_value(ModulatorInstanceId instance,
                                      float value) noexcept {
    if (!dsp_commands_.push(
            {DspCommand::Target::modulator_value, instance, value, 0, {}})) {
        dropped_dsp_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::schedule_macro_morph(
    const MacroMorphCommand &command) noexcept {
    DspCommand dsp;
    dsp.target = DspCommand::Target::macro_morph;
    dsp.morph = command;
    if (!dsp_commands_.push(dsp)) {
        dropped_macro_morph_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::stop_macro_morph(ModulatorInstanceId instance) noexcept {
    MacroMorphCommand command;
    command.type = MacroMorphCommandType::stop;
    command.instance = instance;
    return schedule_macro_morph(command);
}
std::array<MacroMorphRuntime, MacroMorphProcessor::capacity>
AudioEngine::macro_morphs() const noexcept {
    std::array<MacroMorphRuntime, MacroMorphProcessor::capacity> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto &morph = result[i];
        morph.morph_id = published_morph_ids_[i].load(std::memory_order_acquire);
        morph.instance = static_cast<ModulatorInstanceId>(
            published_morph_instances_[i].load(std::memory_order_acquire));
        morph.start_frame =
            published_morph_start_frames_[i].load(std::memory_order_acquire);
        morph.end_frame =
            published_morph_end_frames_[i].load(std::memory_order_acquire);
        morph.start_value = std::bit_cast<float>(
            published_morph_start_values_[i].load(std::memory_order_acquire));
        morph.target_value = std::bit_cast<float>(
            published_morph_target_values_[i].load(std::memory_order_acquire));
        morph.effective_value = std::bit_cast<float>(
            published_morph_effective_values_[i].load(std::memory_order_acquire));
        morph.progress = std::bit_cast<float>(
            published_morph_progress_[i].load(std::memory_order_acquire));
        const auto flags =
            published_morph_flags_[i].load(std::memory_order_acquire);
        morph.active = flags & 1U;
        morph.scheduled = flags & 2U;
        morph.interrupted = flags & 4U;
        morph.completed = flags & 8U;
        morph.duration_unit = static_cast<MacroMorphDurationUnit>(
            (flags >> 4U) & 1U);
        morph.curve = static_cast<MacroMorphCurve>((flags >> 5U) & 3U);
    }
    return result;
}

bool AudioEngine::queue_pattern(const Pattern &pattern) noexcept {
    if (!valid_pattern(pattern))
        return false;
    if (!patterns_.push(pattern)) {
        dropped_patterns_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool AudioEngine::queue_timeline(const Timeline &timeline, std::string &error) {
    return queue_timeline_at(timeline, 0, VoiceTransitionPolicy::stop_outgoing,
                             error);
}

bool AudioEngine::queue_timeline_at(const Timeline &timeline,
                                    std::uint64_t activation_frame,
                                    VoiceTransitionPolicy voice_policy,
                                    std::string &error,
                                    std::span<const TrackId> affected_tracks) {
    if (affected_tracks.size() >
        TimelinePublication::maximum_affected_tracks) {
        error = "timeline transition track capacity exceeded";
        return false;
    }
    reclaim_retired_plans();
    std::size_t slot = timeline_plans_.size();
    for (std::size_t i = 0; i < timeline_plans_.size(); ++i)
        if (timeline_plans_[i].state.load(std::memory_order_acquire) ==
            PlanState::free) {
            slot = i;
            break;
        }
    if (slot == timeline_plans_.size()) {
        dropped_timelines_.fetch_add(1, std::memory_order_relaxed);
        error = "timeline plan slots are full";
        return false;
    }
    auto plan = std::make_unique<PlaybackPlan>();
    if (!compile_timeline(timeline, *plan, error))
        return false;
    if (plan->peak_audio_clip_voices > voices_.size()) {
        error = "audio clip overlap requires " +
                std::to_string(plan->peak_audio_clip_voices) +
                " voices; configured sample voice capacity is " +
                std::to_string(voices_.size());
        return false;
    }
    timeline_plans_[slot].plan = std::move(plan);
    timeline_plans_[slot].state.store(PlanState::queued,
                                      std::memory_order_release);
    TimelinePublication publication;
    publication.slot = static_cast<std::uint8_t>(slot);
    publication.activation_frame = activation_frame;
    publication.voice_policy = voice_policy;
    publication.affected_track_count =
        static_cast<std::uint16_t>(affected_tracks.size());
    std::copy(affected_tracks.begin(), affected_tracks.end(),
              publication.affected_tracks.begin());
    if (!timelines_.push(publication)) {
        timeline_plans_[slot].state.store(PlanState::free,
                                          std::memory_order_release);
        timeline_plans_[slot].plan.reset();
        dropped_timelines_.fetch_add(1, std::memory_order_relaxed);
        error = "timeline queue is full";
        return false;
    }
    error.clear();
    return true;
}

void AudioEngine::reclaim_retired_plans() noexcept {
    for (auto &slot : timeline_plans_)
        if (slot.state.load(std::memory_order_acquire) == PlanState::retired) {
            slot.plan.reset();
            slot.state.store(PlanState::free, std::memory_order_release);
            reclaimed_timeline_plans_.fetch_add(1, std::memory_order_relaxed);
        }
}

void AudioEngine::cleanup_timeline_voices(VoiceTransitionPolicy policy,
                                          std::uint64_t actual_frame,
                                          std::span<const TrackId> tracks) noexcept {
    const auto affected = [&](TrackId track) {
        return tracks.empty() ||
               std::find(tracks.begin(), tracks.end(), track) != tracks.end();
    };
    if (policy != VoiceTransitionPolicy::preserve) {
        const auto immediate = policy == VoiceTransitionPolicy::stop_outgoing;
        for (auto &voice : voices_)
            if (voice.active && voice.timeline_owned && affected(voice.track))
                release_voice(voice, immediate);
        for (auto &synth : synths_)
            synth->release_timeline_voices(immediate, tracks);
    }
    // A replaced plan cannot later schedule its outstanding MIDI note-offs.
    // Clean those exact timeline-owned notes under every transition policy;
    // preserve applies to in-process audio voices, never to remote stuck notes.
    if (auto *midi = midi_output_.load(std::memory_order_acquire))
        for (std::size_t port = 0; port < timeline_midi_notes_.size(); ++port)
            for (std::size_t channel = 0;
                 channel < timeline_midi_notes_[port].size(); ++channel)
                for (std::size_t note = 0;
                     note < timeline_midi_notes_[port][channel].size(); ++note)
                    if (timeline_midi_notes_[port][channel][note] &&
                        affected((*timeline_midi_note_tracks_)[port][channel][note])) {
                        MidiPacket packet;
                        packet.port = static_cast<MidiPortId>(port + 1);
                        packet.frame = actual_frame;
                        packet.bytes = {
                            static_cast<std::uint8_t>(0x80U | channel),
                            static_cast<std::uint8_t>(note), 0};
                        packet.size = 3;
                        (void)midi->enqueue(packet);
                        timeline_midi_notes_[port][channel][note] = false;
                        (*timeline_midi_note_tracks_)[port][channel][note] = 0;
                    }
}

void AudioEngine::activate_timeline(const TimelinePublication &publication,
                                    std::uint64_t actual_frame) noexcept {
    cleanup_timeline_voices(
        publication.voice_policy, actual_frame,
        std::span(publication.affected_tracks.data(),
                  publication.affected_track_count));
    auto &next = timeline_plans_[publication.slot];
    auto *timeline = next.plan.get();
    timeline_scheduler_.set_plan(timeline, actual_frame);
    scheduler_.set_tempo(timeline->transport.microseconds_per_quarter);
    project_tempo_bpm_ = static_cast<float>(
        60000000.0 / timeline->transport.microseconds_per_quarter);
    midi_clock_.set_tempo(timeline->transport.microseconds_per_quarter);
    active_timeline_tempo_us_ = timeline->transport.microseconds_per_quarter;
    modulation_.set_tempo(project_tempo_bpm_);
    for (auto &synth : synths_)
        synth->set_tempo(project_tempo_bpm_);
    modulation_.clear_automation();
    next.state.store(PlanState::active, std::memory_order_release);
    if (active_timeline_slot_ >= 0)
        timeline_plans_[static_cast<std::size_t>(active_timeline_slot_)]
            .state.store(PlanState::retired, std::memory_order_release);
    active_timeline_slot_ = publication.slot;
    active_timeline_end_frame_ = 0;
    if (timeline->playback_length > 0 &&
        timeline->song_end_behavior != SongEndBehavior::loop) {
        const auto duration =
            timeline_scheduler_.frame_at_tick(timeline->playback_length);
        active_timeline_end_frame_ =
            duration > std::numeric_limits<std::uint64_t>::max() - actual_frame
                ? std::numeric_limits<std::uint64_t>::max()
                : actual_frame + duration;
    }
    last_timeline_activation_frame_.store(actual_frame,
                                          std::memory_order_release);
    timeline_activation_count_.fetch_add(1, std::memory_order_release);
    if (!timeline_activations_.push({actual_frame}))
        dropped_timeline_activations_.fetch_add(1, std::memory_order_relaxed);
    last_timeline_lateness_frames_.store(
        actual_frame > publication.activation_frame
            ? actual_frame - publication.activation_frame
            : 0,
        std::memory_order_release);
}
void AudioEngine::play() noexcept {
    if (!transport_commands_.push(TransportCommand::play))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::stop() noexcept {
    if (!transport_commands_.push(TransportCommand::stop))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::restart() noexcept {
    if (!transport_commands_.push(TransportCommand::restart))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::seek_frame(std::uint64_t frame) noexcept {
    if (!transport_seeks_.push(frame)) {
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
void AudioEngine::midi_panic() noexcept {
    if (!transport_commands_.push(TransportCommand::midi_panic))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::panic() noexcept {
    // Safety delivery is level-triggered rather than dependent on queue space.
    // Repeated requests coalesce and are acknowledged by the next callback.
    panic_requested_.store(true, std::memory_order_release);
}
bool AudioEngine::set_fill(bool active) noexcept {
    if (!transport_commands_.push(active ? TransportCommand::fill_on
                                         : TransportCommand::fill_off)) {
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::publish_metronome(
    const MetronomeConfiguration &configuration) noexcept {
    if (!std::isfinite(configuration.level) || configuration.level < 0.0F ||
        configuration.level > 1.0F ||
        configuration.microseconds_per_quarter == 0 ||
        configuration.meter_numerator == 0 ||
        configuration.meter_denominator == 0)
        return false;
    MetronomeCommand command;
    command.type = MetronomeCommand::Type::configure;
    command.configuration = configuration;
    return metronome_commands_.push(command);
}
bool AudioEngine::begin_count_in(std::uint64_t start_frame,
                                 std::uint64_t end_frame) noexcept {
    if (end_frame <= start_frame)
        return false;
    MetronomeCommand command;
    command.type = MetronomeCommand::Type::begin_count_in;
    command.count_in_start = start_frame;
    command.count_in_end = end_frame;
    return metronome_commands_.push(command);
}
void AudioEngine::cancel_count_in() noexcept {
    MetronomeCommand command;
    command.type = MetronomeCommand::Type::cancel_count_in;
    (void)metronome_commands_.push(command);
}
MetronomeState AudioEngine::metronome_state() const noexcept {
    MetronomeState result;
    result.configuration.enabled =
        published_metronome_enabled_.load(std::memory_order_acquire);
    result.configuration.level =
        published_metronome_level_.load(std::memory_order_acquire);
    result.count_in_active =
        published_count_in_active_.load(std::memory_order_acquire);
    result.beat_active =
        published_metronome_beat_.load(std::memory_order_acquire);
    result.downbeat =
        published_metronome_downbeat_.load(std::memory_order_acquire);
    result.click_count =
        published_metronome_click_count_.load(std::memory_order_acquire);
    result.count_in_start_frame =
        published_count_in_start_.load(std::memory_order_acquire);
    result.count_in_end_frame =
        published_count_in_end_.load(std::memory_order_acquire);
    return result;
}

void AudioEngine::start_voice(std::uint8_t note, float velocity, TrackId track,
                              EventId event, std::uint64_t occurrence,
                              bool timeline_owned, const SliceTarget *slice,
                              bool preview, float pitch_offset) noexcept {
    const auto slot = slice && slice->enabled ? slice->slot : note_slots_[note];
    const auto sample_index = find_active_sample(slot);
    if (sample_index == std::numeric_limits<std::size_t>::max()) {
        missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto *sample = sample_versions_[sample_index].sample.get();
    const auto sample_frames = sample->frames();
    if (slice && slice->enabled &&
        ((slice->publication != 0 &&
          sample_versions_[sample_index].publication.load(
              std::memory_order_acquire) != slice->publication) ||
         slice->start_frame >= slice->end_frame ||
         slice->end_frame > sample_frames)) {
        missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto settings = slice ? slice->playback : SamplePlaybackSettings{};
    if (settings.mode == SamplePlaybackMode::toggle)
        for (auto &voice : voices_)
            if (voice.active && voice.trigger_note == note &&
                voice.mode == SamplePlaybackMode::toggle) {
                if (voice.envelope_stage == Voice::EnvelopeStage::release) {
                    voice.active = false;
                    continue;
                }
                release_voice(voice);
                return;
            }
    if (settings.choke_group != 0)
        for (auto &voice : voices_)
            if (voice.active && voice.choke_group == settings.choke_group)
                release_voice(voice, !settings.choke_release);
    auto selected = voices_.size();
    std::size_t free_voices{};
    for (const auto &candidate : voices_) free_voices += !candidate.active;
    const auto reserved =
        reserved_handoff_voices_.load(std::memory_order_acquire);
    if (reserved && free_voices <= reserved) {
        dropped_timeline_events_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (!voices_[i].active) {
            selected = i;
            break;
        }
    if (selected == voices_.size()) {
        // Arrangement clips have an explicit bounded polyphony contract. They
        // never evict pad/pattern voices (or one another) nondeterministically.
        if (slice && slice->audio_clip) {
            dropped_timeline_events_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        selected = 0;
        for (std::size_t i = 1; i < voices_.size(); ++i)
            if (voices_[i].age < voices_[selected].age)
                selected = i;
        stolen_voices_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto start = slice && slice->enabled ? slice->start_frame : 0;
    const auto end = slice && slice->enabled ? slice->end_frame : sample_frames;
    auto &voice = voices_[selected];
    voice = {};
    voice.sample = sample;
    voice.sample_version = sample_index;
    voice.start_frame = start;
    voice.end_frame = end;
    voice.region = slice ? slice->region : SampleRegionPlayback{};
    voice.audio_clip = slice && slice->audio_clip;
    if (voice.audio_clip) {
        voice.clip_elapsed_frames = slice->source_offset_frames;
        voice.clip_total_frames = slice->clip_total_frames;
        voice.clip_fade_in_frames = slice->clip_fade_in_frames;
        voice.clip_fade_out_frames = slice->clip_fade_out_frames;
        voice.crossfade_in_frames = slice->crossfade_in_frames;
        voice.crossfade_out_offset_frames = slice->crossfade_out_offset_frames;
        voice.crossfade_out_frames = slice->crossfade_out_frames;
        voice.clip_fade_curve = slice->clip_fade_curve;
        voice.crossfade_in_curve = slice->crossfade_in_curve;
        voice.crossfade_out_curve = slice->crossfade_out_curve;
    }
    voice.reverse = settings.reverse;
    voice.position = settings.reverse ? static_cast<double>(end - 1)
                                      : static_cast<double>(start);
    voice.increment = std::exp2(
        (settings.semitones + settings.fine_cents / 100.0F + pitch_offset) /
        12.0F);
    voice.base_increment = voice.increment;
    const auto time_ratio = std::clamp(
        RealtimeTimeStretcher::resolve_time_ratio(
            settings.stretch_ratio, settings.source_beats, settings.source_bpm,
            end - start, rate_, project_tempo_bpm_),
        0.25F, 4.0F);
    voice.stretch_configuration = {settings.stretch_mode,
                                   settings.stretch_quality, time_ratio,
                                   static_cast<float>(voice.increment), rate_};
    voice.stretch_configuration.loop_enabled = voice.region.loop_enabled;
    voice.stretch_configuration.reverse = settings.reverse;
    voice.stretch_configuration.loop_start_frame =
        voice.region.loop_start_frame;
    voice.stretch_configuration.loop_end_frame = voice.region.loop_end_frame;
    voice.stretch_configuration.loop_crossfade_frames =
        voice.region.loop_crossfade_frames;
    voice.stretch_explicit_ratio = settings.stretch_ratio;
    voice.stretch_source_beats = settings.source_beats;
    voice.stretch_source_bpm = settings.source_bpm;
    voice.stretch_ratio_target = time_ratio;
    voice.stretched = settings.stretch_mode == StretchMode::realtime &&
                      RealtimeTimeStretcher::valid(voice.stretch_configuration);
    if (voice.stretched) {
        RealtimeTimeStretcher::reset(voice.stretch_state,
                                     voice.stretch_configuration, start, end,
                                     slice ? slice->source_offset_frames : 0);
    } else if (slice && slice->source_offset_frames) {
        const auto distance = static_cast<double>(slice->source_offset_frames) *
                              voice.increment;
        if (voice.region.loop_enabled) {
            const auto loop_start = static_cast<double>(
                voice.region.loop_start_frame);
            const auto loop_length = static_cast<double>(
                voice.region.loop_end_frame - voice.region.loop_start_frame);
            const auto phase = std::fmod(distance, loop_length);
            voice.position = voice.reverse
                                 ? static_cast<double>(
                                       voice.region.loop_end_frame - 1) - phase
                                 : loop_start + phase;
        } else {
            voice.position = voice.reverse
                                 ? std::max(static_cast<double>(start),
                                            static_cast<double>(end - 1) -
                                                distance)
                                 : std::min(static_cast<double>(end - 1),
                                            static_cast<double>(start) +
                                                distance);
        }
    }
    voice.gain = velocity * settings.gain * (slice ? slice->gain : 1.0F);
    voice.base_gain = voice.gain;
    voice.modulation_gain = 1.0F;
    voice.base_pan = slice ? slice->pan : 0.0F;
    voice.sustain = settings.sustain;
    if (settings.attack_seconds > 0) {
        voice.envelope_level = 0;
        voice.envelope_stage = Voice::EnvelopeStage::attack;
        voice.attack_step =
            1.0F / std::max(1.0F, settings.attack_seconds * rate_);
    } else if (settings.decay_seconds > 0) {
        voice.envelope_level = 1;
        voice.envelope_stage = Voice::EnvelopeStage::decay;
    } else {
        voice.envelope_level = settings.sustain;
        voice.envelope_stage = Voice::EnvelopeStage::sustain;
    }
    voice.decay_step = (1.0F - settings.sustain) /
                       std::max(1.0F, settings.decay_seconds * rate_);
    voice.release_samples =
        std::max(1.0F, settings.release_seconds * static_cast<float>(rate_));
    voice.release_step = 1.0F / voice.release_samples;
    voice.age = next_voice_age_++;
    voice.track = track;
    voice.event = event;
    voice.occurrence = occurrence;
    voice.pitch = note;
    voice.trigger_note = note;
    voice.choke_group = settings.choke_group;
    voice.mode = settings.mode;
    voice.choke_release = settings.choke_release;
    voice.timeline_owned = timeline_owned;
    voice.preview = preview;
    reset_sample_voice_modulation(voice.voice_modulation,
                                  modulation_.configuration(), track, velocity,
                                  occurrence ^ voice.age);
    voice.active = true;
}

void AudioEngine::activate_gapless_handoff(std::uint64_t frame) noexcept {
    auto *recorder = recording_.load(std::memory_order_acquire);
    if (!recorder ||
        gapless_handoff_.phase.load(std::memory_order_acquire) != 1 ||
        recorder->state() != RecordingState::completed ||
        frame < recorder->requested_stop_frame())
        return;
    std::uint8_t prepared = 1;
    if (!gapless_handoff_.phase.compare_exchange_strong(
            prepared, 2, std::memory_order_acq_rel))
        return;
    const auto version = gapless_handoff_.sample_version;
    if (version >= sample_version_capacity_) return;
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (i != version &&
            sample_versions_[i].id.load(std::memory_order_acquire) ==
                gapless_handoff_.slot.value &&
            sample_versions_[i].state.load(std::memory_order_acquire) ==
                SampleVersionState::active)
            sample_versions_[i].state.store(SampleVersionState::retiring,
                                            std::memory_order_release);
    sample_versions_[version].state.store(SampleVersionState::active,
                                          std::memory_order_release);
    reserved_handoff_voices_.fetch_sub(1, std::memory_order_acq_rel);
    SliceTarget target;
    target.slot = gapless_handoff_.slot;
    target.publication = sample_versions_[version].publication.load(
        std::memory_order_acquire);
    target.start_frame = 0;
    target.end_frame = gapless_handoff_.loop_frames;
    target.clip_total_frames = std::numeric_limits<std::uint64_t>::max();
    target.audio_clip = true;
    target.region.loop_enabled = true;
    target.region.loop_end_frame = gapless_handoff_.loop_frames;
    target.enabled = true;
    start_voice(0, 1.0F, gapless_handoff_.track, gapless_handoff_.clip, 0,
                true, &target);
    gapless_handoff_.first_frame.store(frame, std::memory_order_release);
}

void AudioEngine::release_voice(Voice &voice, bool immediate) noexcept {
    release_sample_voice_modulation(voice.voice_modulation);
    if (immediate || voice.release_step >= 1.0F) {
        voice.active = false;
        return;
    }
    voice.release_step = voice.envelope_level / voice.release_samples;
    voice.envelope_stage = Voice::EnvelopeStage::release;
}

void AudioEngine::release_note(std::uint8_t note, TrackId track, EventId event,
                               std::uint64_t occurrence,
                               bool timeline_owned) noexcept {
    for (auto &voice : voices_)
        if (voice.active && voice.trigger_note == note &&
            (!timeline_owned ||
             (voice.timeline_owned && voice.track == track &&
              voice.event == event && voice.occurrence == occurrence))) {
            release_sample_voice_modulation(voice.voice_modulation);
            if (voice.mode == SamplePlaybackMode::gate)
                release_voice(voice);
            if (timeline_owned)
                return;
        }
}

void AudioEngine::stop_timeline_voice(TrackId track, EventId event,
                                      std::uint64_t occurrence,
                                      bool immediate) noexcept {
    for (auto &voice : voices_)
        if (voice.active && voice.timeline_owned && voice.track == track &&
            voice.event == event && voice.occurrence == occurrence) {
            if (immediate)
                release_voice(voice, true);
            else
                release_note(voice.trigger_note, track, event, occurrence, true);
            return;
        }
}

void AudioEngine::stop_voices(bool timeline_only) noexcept {
    for (auto &voice : voices_)
        if (!timeline_only || voice.timeline_owned)
            voice.active = false;
    for (auto &synth : synths_)
        if (timeline_only)
            synth->release_timeline_voices(true);
        else
            synth->reset();
    synth_active_.fill(false);
}

void AudioEngine::process(std::span<float> left,
                          std::span<float> right) noexcept {
    const auto callback_frame = callback_frame_.load(std::memory_order_relaxed);
    activate_gapless_handoff(callback_frame);
    if (!split_processing_) {
        if (auto *recorder = recording_.load(std::memory_order_acquire)) {
            const auto frames = std::min(left.size(), right.size());
            const auto boundary = recorder->next_stop_boundary(callback_frame,
                                                                frames);
            if (boundary) {
                const auto first = static_cast<std::size_t>(boundary - callback_frame);
                split_processing_ = true;
                process(left.first(first), right.first(first));
                activate_gapless_handoff(boundary);
                if (first < frames)
                    process(left.subspan(first, frames - first),
                            right.subspan(first, frames - first));
                split_processing_ = false;
                return;
            }
        }
    }
    const auto monitoring = callback_monitor_.enabled();
    const auto callback_begin = monitoring
                                    ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
    processing_started_.store(true, std::memory_order_release);
    const auto frames = std::min(left.size(), right.size());
    MetronomeCommand metronome_command;
    while (metronome_commands_.pop(metronome_command)) {
        if (metronome_command.type == MetronomeCommand::Type::configure) {
            metronome_configuration_ = metronome_command.configuration;
            metronome_next_beat_ = 0;
        } else if (metronome_command.type ==
                   MetronomeCommand::Type::begin_count_in) {
            metronome_count_in_start_ = metronome_command.count_in_start;
            metronome_count_in_end_ = metronome_command.count_in_end;
            metronome_next_beat_ = 0;
        } else {
            metronome_count_in_start_ = metronome_count_in_end_ = 0;
            metronome_voice_active_ = false;
        }
    }
    std::fill(left.begin(), left.end(), 0);
    std::fill(right.begin(), right.end(), 0);
    mixer_.apply_pending_publications();
    if (panic_requested_.exchange(false, std::memory_order_acq_rel)) {
        transport_playing_ = false;
        cleanup_timeline_voices(VoiceTransitionPolicy::stop_outgoing,
                                callback_frame);
        stop_voices();
        modulation_.clear_automation();
        timeline_scheduler_.set_fill(false);
        published_fill_.store(false, std::memory_order_release);
        metronome_count_in_start_ = metronome_count_in_end_ = 0;
        metronome_voice_active_ = false;
        delay_.reset();
        mixer_.reset_effect_state();
        Trigger discarded;
        while (triggers_.pop(discarded)) {}
        pending_trigger_count_ = 0;
        for (auto &repeat : note_repeats_) repeat = {};
        NoteRepeatCommand discarded_repeat;
        while (note_repeat_commands_.pop(discarded_repeat)) {}
        published_note_repeat_count_.store(0, std::memory_order_release);
        published_note_repeat_owner_mask_.store(0, std::memory_order_release);
        note_repeat_cancellations_.fetch_add(1, std::memory_order_relaxed);
        if (auto *output = midi_output_.load(std::memory_order_acquire))
            output->panic();
        panic_acknowledgements_.fetch_add(1, std::memory_order_release);
    }
    ModulationConfiguration modulation_configuration;
    while (modulation_configurations_.pop(modulation_configuration)) {
        if (modulation_.configure(modulation_configuration)) {
            mixer_.clear_modulation();
            for (std::size_t instance = 0; instance < synths_.size();
                 ++instance)
                (void)synths_[instance]->configure_voice_modulation(
                    modulation_configuration,
                    static_cast<std::uint32_t>(instance + 1));
        }
    }
    std::uint8_t preset_activation{};
    while (preset_activations_.pop(preset_activation))
        activate_preset(preset_activation);
    // Normal publications were drained before coordinated preset activation.
    // Do not drain again here: a concurrently arriving mixer publication must
    // wait for the next callback rather than splitting the activated snapshot.
    mixer_.begin_prepared_block(frames);
    if (duplex_input_ && duplex_input_channels_) {
        const auto input_frames =
            std::min(frames, duplex_input_samples_ / duplex_input_channels_);
        for (std::size_t frame = 0; frame < input_frames; ++frame)
            for (std::uint16_t channel = 0; channel < duplex_input_channels_ &&
                                            channel < maximum_mixer_inputs;
                 ++channel)
                mixer_.add_input(
                    channel, frame,
                    duplex_input_[frame * duplex_input_channels_ + channel]);
    }

    SampleUpdate sample_update;
    while (sample_updates_.pop(sample_update)) {
        for (std::size_t i = 0; i < sample_version_capacity_; ++i)
            if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                    sample_update.id.value &&
                sample_versions_[i].state.load(std::memory_order_acquire) ==
                    SampleVersionState::active)
                sample_versions_[i].state.store(SampleVersionState::retiring,
                                                std::memory_order_release);
        if (sample_update.type == SampleUpdate::Type::install)
            sample_versions_[sample_update.version].state.store(
                SampleVersionState::active, std::memory_order_release);
        applied_sample_update_sequence_.store(sample_update.sequence,
                                              std::memory_order_release);
    }
    SliceMapCommand slice_map;
    while (slice_map_commands_.pop(slice_map)) {
        if (slice_map.type == SliceMapCommand::Type::map)
            note_slices_[slice_map.note] = slice_map.target;
        else if (slice_map.type == SliceMapCommand::Type::clear)
            note_slices_[slice_map.note].enabled = false;
        else
            note_slices_[slice_map.note].playback = slice_map.target.playback;
    }
    PadMappingBatch mapping_batch;
    while (pad_mapping_batches_.pop(mapping_batch))
        for (std::size_t pad = 0; pad < mapping_batch.targets.size(); ++pad)
            if (mapping_batch.changed_mask & (std::uint16_t{1} << pad))
                note_slices_[36 + pad] = mapping_batch.targets[pad];

    Pattern pattern;
    MidiClockOutputConfiguration midi_clock_configuration;
    while (midi_clock_configurations_.pop(midi_clock_configuration))
        midi_clock_configuration_ = midi_clock_configuration;
    DspCommand dsp;
    while (dsp_commands_.pop(dsp)) {
        if (dsp.target == DspCommand::Target::synth)
            synths_[dsp.instance - 1]->set_parameter(dsp.parameter, dsp.value);
        else if (dsp.target == DspCommand::Target::delay)
            delay_.set_parameter(dsp.parameter, dsp.value);
        else if (dsp.target == DspCommand::Target::delay_bypass)
            delay_.set_bypass(dsp.value >= 0.5F);
        else if (dsp.target == DspCommand::Target::modulation_input)
            modulation_.set_input(
                static_cast<ModulationSourceId>(dsp.parameter), dsp.value);
        else if (dsp.target == DspCommand::Target::modulator_value) {
            macro_morphs_.override_value(dsp.parameter, dsp.value);
            modulation_.set_modulator_value(dsp.parameter, dsp.value);
        } else if (!macro_morphs_.command(
                       dsp.morph, rate_,
                       60'000'000.0 / active_timeline_tempo_us_))
            dropped_macro_morph_commands_.fetch_add(1,
                                                    std::memory_order_relaxed);
    }
    SynthTrigger synth_trigger;
    while (synth_triggers_.pop(synth_trigger)) {
        if (synth_trigger.note_on)
            synths_[synth_trigger.instance - 1]->set_modulation_frame(
                callback_frame);
        if (synth_trigger.note_on)
            synths_[synth_trigger.instance - 1]->note_on(
                synth_trigger.note, synth_trigger.velocity);
        else
            synths_[synth_trigger.instance - 1]->note_off(synth_trigger.note);
        if (synth_trigger.note_on)
            synth_active_[synth_trigger.instance - 1] = true;
        if (synth_trigger.note_on)
            modulation_.note_on(0, synth_trigger.velocity);
        else
            modulation_.note_off(0);
    }
    while (patterns_.pop(pattern))
        scheduler_.request_pattern(pattern);
    TimelinePublication publication;
    while (timelines_.pop(publication)) {
        if (has_pending_timeline_)
            timeline_plans_[pending_timeline_.slot].state.store(
                PlanState::retired, std::memory_order_release);
        pending_timeline_ = publication;
        has_pending_timeline_ = true;
    }
    if (has_pending_timeline_ &&
        (pending_timeline_.activation_frame == 0 ||
         (!accept_external_clock_ &&
          pending_timeline_.activation_frame <= transport_frame_))) {
        activate_timeline(pending_timeline_, transport_frame_);
        has_pending_timeline_ = false;
    }
    MidiRoutingConfiguration midi_routing;
    while (midi_routing_configurations_.pop(midi_routing)) {
        midi_router_.configure_realtime(
            {midi_routing.routes.data(), midi_routing.route_count});
        accept_external_clock_ = midi_routing.accept_external_clock;
    }
    MidiPacket midi_input;
    while (midi_inputs_.pop(midi_input)) {
        midi_clock_follower_.receive(midi_input);
        if (auto *output = midi_output_.load(std::memory_order_acquire))
            (void)midi_router_.route(midi_input, *output);
        const auto realtime = midi_input.size ? midi_input.bytes[0] : 0;
        const auto forward_clock = realtime == 0xF2 || realtime == 0xF8 ||
                                   realtime == 0xFA || realtime == 0xFB ||
                                   realtime == 0xFC;
        if (accept_external_clock_ &&
            midi_clock_configuration_.forward_external && forward_clock) {
            if (auto *output = midi_output_.load(std::memory_order_acquire))
                for (std::size_t i = 0;
                     i < midi_clock_configuration_.port_count; ++i)
                    if (midi_clock_configuration_.ports[i] != midi_input.port) {
                        auto forwarded = midi_input;
                        forwarded.port = midi_clock_configuration_.ports[i];
                        (void)output->enqueue(forwarded);
                    }
        }
        if (accept_external_clock_ && midi_input.size &&
            midi_input.bytes[0] == 0xFA) {
            transport_frame_ = 0;
            transport_playing_ = true;
            stop_voices();
            modulation_.restart();
            if (auto *output = midi_output_.load(std::memory_order_acquire))
                output->panic();
        } else if (accept_external_clock_ && midi_input.size &&
                   midi_input.bytes[0] == 0xFB) {
            transport_playing_ = true;
        } else if (accept_external_clock_ && midi_input.size &&
                   midi_input.bytes[0] == 0xFC) {
            transport_playing_ = false;
            stop_voices();
            modulation_.clear_automation();
            if (auto *output = midi_output_.load(std::memory_order_acquire))
                output->panic();
        } else if (accept_external_clock_ && midi_input.size >= 3 &&
                   midi_input.bytes[0] == 0xF2) {
            stop_voices();
            modulation_.clear_automation();
            if (auto *output = midi_output_.load(std::memory_order_acquire))
                output->panic();
        }
    }
    const auto phase_to_frame = [&](std::uint64_t phase) noexcept {
        const auto numerator =
            static_cast<__uint128_t>(phase) * rate_ * active_timeline_tempo_us_;
        constexpr auto denominator =
            static_cast<__uint128_t>(24) * 1'000'000 * (std::uint64_t{1} << 32);
        const auto frame = numerator / denominator;
        return frame > std::numeric_limits<std::uint64_t>::max()
                   ? std::numeric_limits<std::uint64_t>::max()
                   : static_cast<std::uint64_t>(frame);
    };
    auto external_start_frame = transport_frame_;
    auto external_end_frame = transport_frame_;
    if (accept_external_clock_) {
        external_start_frame = phase_to_frame(midi_clock_follower_.phase_q32());
        midi_clock_follower_.advance(static_cast<std::uint32_t>(frames));
        external_end_frame = phase_to_frame(midi_clock_follower_.phase_q32());
    }
    const auto external_clock = midi_clock_follower_.state();
    if (accept_external_clock_ &&
        external_clock.sync_status == MidiSyncStatus::lost &&
        !external_clock_lost_) {
        external_clock_lost_ = true;
        transport_playing_ = false;
        stop_voices();
        modulation_.clear_automation();
    } else if (accept_external_clock_ && external_clock_lost_ &&
               external_clock.running &&
               external_clock.sync_status != MidiSyncStatus::lost) {
        external_clock_lost_ = false;
        transport_playing_ = true;
        stop_voices();
    }
    external_clock_running_.store(external_clock.running,
                                  std::memory_order_release);
    external_clock_available_.store(external_clock.available,
                                    std::memory_order_release);
    external_clock_dropout_.store(external_clock.dropout,
                                  std::memory_order_release);
    external_clock_tempo_.store(external_clock.tempo_bpm,
                                std::memory_order_release);
    external_clock_pulses_.store(external_clock.pulses,
                                 std::memory_order_release);
    external_clock_last_timestamp_.store(external_clock.last_timestamp_ns,
                                         std::memory_order_release);
    external_clock_status_.store(
        static_cast<std::uint8_t>(external_clock.sync_status),
        std::memory_order_release);
    external_clock_phase_error_.store(external_clock.phase_error_pulses,
                                      std::memory_order_release);
    external_song_position_.store(external_clock.song_position,
                                  std::memory_order_release);
    TransportCommand command;
    std::uint64_t seek_frame{};
    while (transport_seeks_.pop(seek_frame)) {
        transport_frame_ = seek_frame;
        stop_voices();
        modulation_.restart();
        if (auto *midi = midi_output_.load(std::memory_order_acquire))
            midi->panic();
    }
    while (transport_commands_.pop(command)) {
        if (command == TransportCommand::play) {
            if (!transport_playing_ && midi_clock_configuration_.enabled)
                if (auto *midi = midi_output_.load(std::memory_order_acquire))
                    midi_clock_.continue_playback(
                        *midi, {midi_clock_configuration_.ports.data(),
                                midi_clock_configuration_.port_count});
            transport_playing_ = true;
        } else if (command == TransportCommand::stop) {
            transport_playing_ = false;
            stop_voices();
            for (auto &repeat : note_repeats_) repeat = {};
            published_note_repeat_count_.store(0, std::memory_order_release);
            published_note_repeat_owner_mask_.store(0,
                                                     std::memory_order_release);
            modulation_.clear_automation();
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                if (midi_clock_configuration_.enabled)
                    midi_clock_.stop(*midi,
                                     {midi_clock_configuration_.ports.data(),
                                      midi_clock_configuration_.port_count});
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                midi->panic();
        } else if (command == TransportCommand::restart) {
            transport_frame_ = 0;
            transport_playing_ = true;
            stop_voices();
            modulation_.restart();
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                if (midi_clock_configuration_.enabled)
                    midi_clock_.start(*midi,
                                      {midi_clock_configuration_.ports.data(),
                                       midi_clock_configuration_.port_count});
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                midi->panic();
        } else if (command == TransportCommand::midi_panic) {
            if (auto *output = midi_output_.load(std::memory_order_acquire))
                output->panic();
        } else {
            const auto fill = command == TransportCommand::fill_on;
            timeline_scheduler_.set_fill(fill);
            published_fill_.store(fill, std::memory_order_release);
        }
    }
    auto release_repeat = [&](ActiveNoteRepeat &repeat) noexcept {
        if (!repeat.active) return;
        const auto owner = repeat_track_owner(repeat.target.owner);
        const auto identity = NoteIdentity{owner, repeat.target.generation,
                                           repeat.occurrence};
        if (repeat.gate_active) {
            if (repeat.target.kind == NoteRepeatTargetKind::sample)
                release_note(repeat.target.note, owner,
                             repeat.target.generation, repeat.occurrence, true);
            else if (repeat.target.kind == NoteRepeatTargetKind::synth)
                synths_[repeat.target.instrument - 1]->note_off(
                    repeat.target.note, identity);
            else if (auto *midi = midi_output_.load(std::memory_order_acquire)) {
                MidiPacket packet;
                packet.port = repeat.target.midi_port;
                packet.frame = callback_frame;
                packet.bytes = {
                    static_cast<std::uint8_t>(0x80 |
                        (repeat.target.midi_channel & 0x0F)),
                    repeat.target.note, 0};
                packet.size = 3;
                (void)midi->enqueue(packet);
            }
        }
        repeat = {};
        note_repeat_cancellations_.fetch_add(1, std::memory_order_relaxed);
    };
    NoteRepeatCommand repeat_command;
    while (note_repeat_commands_.pop(repeat_command)) {
        if (repeat_command.type == NoteRepeatCommand::Type::cancel_all) {
            for (auto &repeat : note_repeats_) release_repeat(repeat);
            continue;
        }
        if (repeat_command.type == NoteRepeatCommand::Type::end) {
            for (auto &repeat : note_repeats_)
                if (repeat.active &&
                    repeat.target.owner == repeat_command.target.owner &&
                    repeat.target.generation ==
                        repeat_command.target.generation)
                    release_repeat(repeat);
            continue;
        }
        if (repeat_command.type == NoteRepeatCommand::Type::update_division) {
            for (auto &repeat : note_repeats_) {
                if (!repeat.active) continue;
                auto target = repeat.target;
                release_repeat(repeat);
                target.division = repeat_command.target.division;
                repeat.target = target;
                repeat.active = transport_playing_ && !accept_external_clock_;
                const auto now = repeat_tick_at_frame(
                    transport_frame_, rate_, active_timeline_tempo_us_);
                const auto division = repeat_ticks(target.division);
                repeat.next_tick = static_cast<std::uint64_t>(
                    ((now + division - 1) / division) * division);
                if (repeat_frame_at_tick(
                        static_cast<MusicalTick>(repeat.next_tick), rate_,
                        active_timeline_tempo_us_) < transport_frame_)
                    repeat.next_tick += static_cast<std::uint64_t>(division);
            }
            continue;
        }
        auto *slot = static_cast<ActiveNoteRepeat *>(nullptr);
        for (auto &repeat : note_repeats_)
            if (repeat.active &&
                repeat.target.owner == repeat_command.target.owner &&
                repeat.target.generation == repeat_command.target.generation) {
                release_repeat(repeat);
                slot = &repeat;
                break;
            }
        if (!slot)
            for (auto &repeat : note_repeats_)
                if (!repeat.active) { slot = &repeat; break; }
        if (!slot) {
            dropped_note_repeat_commands_.fetch_add(1,
                                                     std::memory_order_relaxed);
            continue;
        }
        slot->target = repeat_command.target;
        slot->active = transport_playing_ && !accept_external_clock_;
        if (!slot->active) continue;
        const auto now = repeat_tick_at_frame(transport_frame_, rate_,
                                              active_timeline_tempo_us_);
        const auto division = repeat_ticks(slot->target.division);
        slot->next_tick = static_cast<std::uint64_t>(
            ((now + division - 1) / division) * division);
        if (repeat_frame_at_tick(static_cast<MusicalTick>(slot->next_tick),
                                 rate_, active_timeline_tempo_us_) <
            transport_frame_)
            slot->next_tick += static_cast<std::uint64_t>(division);
    }
    published_note_repeat_count_.store(
        static_cast<std::size_t>(std::count_if(
            note_repeats_.begin(), note_repeats_.end(),
            [](const auto &repeat) { return repeat.active; })),
        std::memory_order_release);
    std::uint16_t repeat_owner_mask{};
    for (const auto &repeat : note_repeats_)
        if (repeat.active && repeat.target.owner >= 1 &&
            repeat.target.owner <= 16)
            repeat_owner_mask |= static_cast<std::uint16_t>(
                std::uint16_t{1} << (repeat.target.owner - 1));
    published_note_repeat_owner_mask_.store(repeat_owner_mask,
                                             std::memory_order_release);
    const auto metronome_transport_start =
        accept_external_clock_ ? external_start_frame : transport_frame_;

    Trigger incoming;
    while (pending_trigger_count_ < pending_triggers_.size() &&
           triggers_.pop(incoming))
        pending_triggers_[pending_trigger_count_++] = incoming;
    if (transport_playing_ && midi_clock_configuration_.enabled)
        if (auto *midi = midi_output_.load(std::memory_order_acquire))
            midi_clock_.process(static_cast<std::uint32_t>(frames),
                                callback_frame, *midi,
                                {midi_clock_configuration_.ports.data(),
                                 midi_clock_configuration_.port_count});
    for (std::size_t i = 1; i < pending_trigger_count_; ++i) {
        auto value = pending_triggers_[i];
        auto j = i;
        while (j > 0 &&
               (pending_triggers_[j - 1].frame_offset > value.frame_offset ||
                (pending_triggers_[j - 1].frame_offset == value.frame_offset &&
                 pending_triggers_[j - 1].order > value.order))) {
            pending_triggers_[j] = pending_triggers_[j - 1];
            --j;
        }
        pending_triggers_[j] = value;
    }

    const auto scheduled_count =
        transport_playing_
            ? scheduler_.schedule(transport_frame_,
                                  static_cast<std::uint32_t>(frames),
                                  legacy_events_)
            : 0;
    std::size_t timeline_count{};
    std::uint32_t timeline_stop_offset = static_cast<std::uint32_t>(frames);
    bool timeline_stops_this_buffer{};
    if (transport_playing_) {
        const auto range_start =
            accept_external_clock_ ? external_start_frame : transport_frame_;
        if (has_pending_timeline_ &&
            pending_timeline_.activation_frame <= range_start) {
            activate_timeline(pending_timeline_, range_start);
            has_pending_timeline_ = false;
        }
        const auto raw_range_end = accept_external_clock_
                                       ? external_end_frame
                                       : transport_frame_ + frames;
        const auto range_end =
            active_timeline_end_frame_ > range_start &&
                    active_timeline_end_frame_ < raw_range_end
                ? active_timeline_end_frame_
                : raw_range_end;
        timeline_stops_this_buffer =
            active_timeline_end_frame_ != 0 &&
            active_timeline_end_frame_ <= raw_range_end;
        if (timeline_stops_this_buffer && raw_range_end > range_start)
            timeline_stop_offset = static_cast<std::uint32_t>(
                (static_cast<long double>(range_end - range_start) * frames) /
                static_cast<long double>(raw_range_end - range_start));
        const auto output_frames = timeline_stops_this_buffer
                                       ? timeline_stop_offset
                                       : static_cast<std::uint32_t>(frames);
        if (has_pending_timeline_ &&
            pending_timeline_.activation_frame > range_start &&
            pending_timeline_.activation_frame < range_end) {
            const auto boundary = pending_timeline_.activation_frame;
            const auto first_frames = static_cast<std::uint32_t>(
                (static_cast<long double>(boundary - range_start) *
                 output_frames) /
                static_cast<long double>(range_end - range_start));
            timeline_count = timeline_scheduler_.schedule_range(
                range_start, boundary, first_frames, timeline_events_);
            activate_timeline(pending_timeline_, boundary);
            has_pending_timeline_ = false;
            auto remainder =
                std::span(timeline_events_).subspan(timeline_count);
            const auto second = timeline_scheduler_.schedule_range(
                boundary, range_end, output_frames - first_frames, remainder);
            for (std::size_t i = 0; i < second; ++i)
                remainder[i].frame += first_frames;
            timeline_count += second;
        } else
            timeline_count = timeline_scheduler_.schedule_range(
                range_start, range_end, output_frames, timeline_events_);
    }
    dropped_timeline_events_.store(timeline_scheduler_.dropped_events(),
                                   std::memory_order_release);
    dropped_legacy_events_.store(scheduler_.dropped_events(),
                                 std::memory_order_release);
    std::size_t external_index{}, scheduled_index{}, timeline_index{};
    const auto instrument_begin = monitoring
                                      ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
    std::array<std::uint64_t, maximum_mixer_tracks> mixer_tracks{};
    std::array<float, maximum_mixer_tracks> synth_tracks{};
    const auto mixer_track_count =
        std::min(mixer_.active_track_count(), mixer_tracks.size());
    for (std::size_t i = 0; i < mixer_track_count; ++i)
        mixer_tracks[i] = mixer_.active_track_id(i);

    const auto playback_tempo = static_cast<float>(
        accept_external_clock_ && external_clock.tempo_bpm > 0
            ? external_clock.tempo_bpm
            : 60'000'000.0 / active_timeline_tempo_us_);
    for (auto &voice : voices_)
        if (voice.active && voice.stretched)
            voice.stretch_ratio_target = std::clamp(
                RealtimeTimeStretcher::resolve_time_ratio(
                    voice.stretch_explicit_ratio, voice.stretch_source_beats,
                    voice.stretch_source_bpm,
                    voice.end_frame - voice.start_frame, rate_, playback_tempo),
                0.25F, 4.0F);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (timeline_stops_this_buffer && frame == timeline_stop_offset) {
            cleanup_timeline_voices(VoiceTransitionPolicy::release_outgoing,
                                    active_timeline_end_frame_);
            transport_playing_ = false;
            song_end_count_.fetch_add(1, std::memory_order_relaxed);
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                if (midi_clock_configuration_.enabled)
                    midi_clock_.stop(*midi,
                                     {midi_clock_configuration_.ports.data(),
                                      midi_clock_configuration_.port_count});
        }
        const auto morph_range_start =
            accept_external_clock_ ? external_start_frame : transport_frame_;
        const auto morph_range_end = accept_external_clock_
                                         ? external_end_frame
                                         : transport_frame_ + frames;
        const auto morph_frame = morph_range_end >= morph_range_start
                                     ? morph_range_start +
                                           static_cast<std::uint64_t>(
                                               (static_cast<__uint128_t>(
                                                    morph_range_end -
                                                    morph_range_start) *
                                                frame) /
                                               std::max<std::size_t>(1, frames))
                                     : morph_range_start;
        const auto morph_tempo =
            accept_external_clock_ && external_clock.tempo_bpm > 0
                ? external_clock.tempo_bpm
                : 60'000'000.0 / active_timeline_tempo_us_;
        macro_morphs_.process(
            morph_frame, transport_playing_, rate_, morph_tempo,
            [&](ModulatorInstanceId instance, float value) noexcept {
                modulation_.set_modulator_value(instance, value);
            });
        modulation_.process_frame([&](ParameterDestination destination,
                                      float value) noexcept {
            if (destination.target == ParameterTarget::subtractive_synth) {
                for (std::size_t instance = 0; instance < synths_.size();
                     ++instance)
                    if (destination.instance == 0 ||
                        destination.instance == instance + 1)
                        (void)synths_[instance]->set_parameter(
                            destination.parameter, value);
            } else if (destination.target == ParameterTarget::stereo_delay)
                (void)delay_.set_parameter(destination.parameter, value);
            else if (destination.target == ParameterTarget::sample_voice) {
                for (auto &voice : voices_) {
                    if (!voice.active || (destination.instance != 0 &&
                                          voice.track != destination.instance))
                        continue;
                    switch (static_cast<SampleModulationParameter>(
                        destination.parameter)) {
                    case SampleModulationParameter::pitch:
                        voice.modulation_pitch = value;
                        voice.increment =
                            voice.base_increment * std::exp2(value / 12.0F);
                        voice.stretch_configuration.pitch_ratio =
                            static_cast<float>(voice.increment);
                        break;
                    case SampleModulationParameter::gain:
                        voice.modulation_gain = std::clamp(value, 0.0F, 2.0F);
                        break;
                    case SampleModulationParameter::pan:
                        voice.modulation_pan = std::clamp(value, -1.0F, 1.0F);
                        break;
                    }
                }
            } else if (destination.target == ParameterTarget::native_effect)
                (void)mixer_.set_effect_parameter(destination.instance,
                                                  destination.parameter, value);
            else
                (void)mixer_.set_modulation(destination, value);
        });
        mixer_.capture_modulation_frame(frame);
        const auto repeat_frame = transport_frame_ + frame;
        const auto repeat_tick = repeat_tick_at_frame(
            repeat_frame, rate_, active_timeline_tempo_us_);
        for (auto &repeat : note_repeats_) {
            if (!repeat.active) continue;
            const auto owner = repeat_track_owner(repeat.target.owner);
            if (repeat.gate_active && repeat_tick >= 0 &&
                static_cast<std::uint64_t>(repeat_tick) >=
                    repeat.gate_off_tick) {
                const NoteIdentity identity{owner, repeat.target.generation,
                                            repeat.occurrence};
                if (repeat.target.kind == NoteRepeatTargetKind::sample)
                    release_note(repeat.target.note, owner,
                                 repeat.target.generation, repeat.occurrence,
                                 true);
                else if (repeat.target.kind == NoteRepeatTargetKind::synth)
                    synths_[repeat.target.instrument - 1]->note_off(
                        repeat.target.note, identity);
                else if (auto *midi =
                             midi_output_.load(std::memory_order_acquire)) {
                    MidiPacket packet;
                    packet.port = repeat.target.midi_port;
                    packet.frame = callback_frame + frame;
                    packet.bytes = {
                        static_cast<std::uint8_t>(
                            0x80 | (repeat.target.midi_channel & 0x0F)),
                        repeat.target.note, 0};
                    packet.size = 3;
                    (void)midi->enqueue(packet);
                }
                repeat.gate_active = false;
            }
            const auto target_frame = repeat_frame_at_tick(
                static_cast<MusicalTick>(repeat.next_tick), rate_,
                active_timeline_tempo_us_);
            if (repeat_frame < target_frame) continue;
            ++repeat.occurrence;
            const NoteIdentity identity{owner, repeat.target.generation,
                                        repeat.occurrence};
            if (repeat.target.kind == NoteRepeatTargetKind::sample)
                start_voice(repeat.target.note, repeat.target.velocity, owner,
                            repeat.target.generation, repeat.occurrence, true,
                            &note_slices_[repeat.target.note]);
            else if (repeat.target.kind == NoteRepeatTargetKind::synth) {
                synths_[repeat.target.instrument - 1]->note_on(
                    repeat.target.note, repeat.target.velocity, identity);
                synth_active_[repeat.target.instrument - 1] = true;
            } else if (auto *midi =
                           midi_output_.load(std::memory_order_acquire)) {
                MidiPacket packet;
                packet.port = repeat.target.midi_port;
                packet.frame = callback_frame + frame;
                packet.bytes = {
                    static_cast<std::uint8_t>(
                        0x90 | (repeat.target.midi_channel & 0x0F)),
                    repeat.target.note,
                    static_cast<std::uint8_t>(repeat.target.velocity * 127.0F)};
                packet.size = 3;
                (void)midi->enqueue(packet);
            }
            const auto division = repeat_ticks(repeat.target.division);
            repeat.gate_off_tick = repeat.next_tick +
                                   static_cast<std::uint64_t>(division / 2);
            repeat.next_tick += static_cast<std::uint64_t>(division);
            repeat.gate_active = true;
            note_repeat_triggers_.fetch_add(1, std::memory_order_relaxed);
        }
        while (external_index < pending_trigger_count_ &&
               pending_triggers_[external_index].frame_offset == frame) {
            const auto &event = pending_triggers_[external_index++];
            if (event.stop_previews) {
                for (auto &voice : voices_)
                    if (voice.active && voice.preview)
                        voice.active = false;
                continue;
            }
            if (event.sliced) {
                SliceTarget target{};
                target.slot = event.slot;
                target.publication = event.publication;
                target.start_frame = event.start_frame;
                target.end_frame = event.end_frame;
                target.gain = event.slice_gain;
                if (event.region_trigger) {
                    target.playback = event.playback;
                    target.region = event.region;
                }
                target.enabled = true;
                start_voice(event.note, event.velocity, 0, 0, 0, false, &target,
                            true);
            } else {
                const auto *target = &note_slices_[event.note];
                if (event.pad_event && !event.note_on)
                    release_note(event.note);
                else
                    start_voice(event.note, event.velocity, 0, 0, 0, false,
                                target, false, event.pitch_offset);
            }
        }
        while (scheduled_index < scheduled_count &&
               legacy_events_[scheduled_index].frame == frame) {
            const auto &event = legacy_events_[scheduled_index++];
            start_voice(event.note, event.velocity, 0, 0, 0, false,
                        &note_slices_[event.note]);
        }
        while (timeline_index < timeline_count &&
               timeline_events_[timeline_index].frame == frame) {
            const auto &event = timeline_events_[timeline_index++];
            const auto audible = mixer_.track_effectively_audible(event.track);
            if (!audible &&
                (event.type == MusicalEventType::note_on ||
                 event.type == MusicalEventType::audio_clip_on ||
                 event.type == MusicalEventType::control_change ||
                 event.type == MusicalEventType::program_change ||
                 event.type == MusicalEventType::pitch_bend))
                continue;
            if (event.type == MusicalEventType::parameter) {
                last_timeline_parameter_value_.store(
                    event.value, std::memory_order_relaxed);
                applied_timeline_parameters_.fetch_add(
                    1, std::memory_order_relaxed);
                if (event.parameter_target == static_cast<std::uint32_t>(
                                                  ParameterTarget::performance_macro)) {
                    macro_morphs_.override_value(event.instrument_instance,
                                                 event.value);
                    modulation_.set_modulator_value(event.instrument_instance,
                                                    event.value);
                    continue;
                }
                modulation_.set_automation(
                    {static_cast<ParameterTarget>(event.parameter_target),
                     event.parameter_id,
                     (event.parameter_target ==
                          static_cast<std::uint32_t>(
                              ParameterTarget::subtractive_synth) ||
                      event.parameter_target ==
                          static_cast<std::uint32_t>(
                              ParameterTarget::native_effect))
                         ? event.instrument_instance
                         : 0},
                    event.value);
                if (event.parameter_target ==
                    static_cast<std::uint32_t>(
                        ParameterTarget::subtractive_synth))
                    synths_[event.instrument_instance - 1]->set_parameter(
                        event.parameter_id, event.value);
                else if (event.parameter_target ==
                         static_cast<std::uint32_t>(
                             ParameterTarget::native_effect))
                    (void)mixer_.set_effect_parameter(event.instrument_instance,
                                                      event.parameter_id,
                                                      event.value);
            } else if (event.type == MusicalEventType::audio_clip_on) {
                const auto &clip = event.audio_clip;
                // A prepared live-loop handoff owns this clip identity until
                // the ordinary arrangement voice is ready. Replace it at the
                // exact scheduler frame, never sum both voices.
                stop_timeline_voice(event.track, event.id, 0, true);
                SliceTarget target;
                target.slot = SampleSlotId{clip.sample_slot};
                target.publication = clip.sample_publication;
                target.start_frame = clip.source_start_frame;
                target.end_frame = clip.source_end_frame;
                target.gain = clip.gain;
                target.pan = clip.pan;
                target.source_offset_frames = event.audio_source_offset;
                target.clip_total_frames = event.clip_total_frames;
                target.clip_fade_in_frames = event.clip_fade_in_frames;
                target.clip_fade_out_frames = event.clip_fade_out_frames;
                target.crossfade_in_frames = event.crossfade_in_frames;
                target.crossfade_out_offset_frames =
                    event.crossfade_out_offset_frames;
                target.crossfade_out_frames = event.crossfade_out_frames;
                target.clip_fade_curve = clip.clip_fade_curve;
                target.crossfade_in_curve = clip.crossfade_in_curve;
                target.crossfade_out_curve = clip.crossfade_out_curve;
                target.audio_clip = true;
                target.playback.mode = SamplePlaybackMode::one_shot;
                target.playback.reverse =
                    clip.direction == AudioClipDirection::reverse;
                target.playback.semitones = clip.pitch_semitones;
                target.playback.stretch_mode =
                    clip.stretch_mode == AudioClipStretchMode::tempo_sync
                        ? StretchMode::realtime
                        : StretchMode::off;
                target.playback.source_bpm = clip.source_bpm;
                target.region.loop_enabled = clip.loop;
                target.region.loop_start_frame = clip.loop_start_frame;
                target.region.loop_end_frame = clip.loop_end_frame;
                target.region.fade_in_frames = clip.fade_in_frames;
                target.region.fade_out_frames = clip.fade_out_frames;
                target.region.loop_crossfade_frames =
                    clip.loop_crossfade_frames;
                target.region.fade_curve =
                    clip.fade_curve == AudioClipFadeCurve::equal_power
                        ? RegionFadeCurve::equal_power
                        : RegionFadeCurve::linear;
                target.enabled = true;
                start_voice(0, 1.0F, event.track, event.id, 0, true,
                            &target);
            } else if (event.type == MusicalEventType::audio_clip_off) {
                stop_timeline_voice(event.track, event.target_event_id, 0,
                                    true);
            } else if (event.instrument == subtractive_instrument_key) {
                const NoteIdentity owner{event.track, event.target_event_id,
                                         event.occurrence};
                if (event.type == MusicalEventType::note_on)
                    synths_[event.instrument_instance - 1]
                        ->set_modulation_frame(transport_frame_ + frame);
                if (event.type == MusicalEventType::note_on)
                    synths_[event.instrument_instance - 1]->note_on(
                        event.pitch, event.value, owner);
                else if (event.type == MusicalEventType::note_off)
                    synths_[event.instrument_instance - 1]->note_off(
                        event.pitch, owner);
                if (event.type == MusicalEventType::note_on)
                    synth_active_[event.instrument_instance - 1] = true;
                if (event.type == MusicalEventType::note_on)
                    modulation_.note_on(event.track, event.value);
                else if (event.type == MusicalEventType::note_off)
                    modulation_.note_off(event.track);
            } else if (event.instrument == sample_instrument_key) {
                if (event.type == MusicalEventType::note_on)
                    start_voice(event.pitch, event.value, event.track,
                                event.target_event_id, event.occurrence, true,
                                &note_slices_[event.pitch]);
                else if (event.type == MusicalEventType::note_off)
                    stop_timeline_voice(event.track, event.target_event_id,
                                        event.occurrence);
            } else if (event.instrument == external_midi_instrument_key) {
                if (auto *midi = midi_output_.load(std::memory_order_acquire)) {
                    MidiPacket packet;
                    packet.port = event.midi_output_port;
                    packet.frame = callback_frame + frame;
                    const auto channel = event.midi_channel & 0x0F;
                    if (event.type == MusicalEventType::note_on ||
                        event.type == MusicalEventType::note_off) {
                        packet.bytes = {
                            static_cast<std::uint8_t>(
                                (event.type == MusicalEventType::note_on
                                     ? 0x90
                                     : 0x80) |
                                channel),
                            event.pitch,
                            static_cast<std::uint8_t>(
                                std::clamp(event.value, 0.0F, 1.0F) * 127.0F)};
                        packet.size = 3;
                        if (event.midi_output_port > 0 &&
                            event.midi_output_port <= maximum_midi_ports)
                            timeline_midi_notes_[event.midi_output_port -
                                                 1][channel][event.pitch] =
                                event.type == MusicalEventType::note_on;
                        if (event.midi_output_port > 0 &&
                            event.midi_output_port <= maximum_midi_ports)
                            (*timeline_midi_note_tracks_)[event.midi_output_port -
                                                        1][channel][event.pitch] =
                                event.type == MusicalEventType::note_on
                                    ? event.track
                                    : 0;
                    } else if (event.type == MusicalEventType::control_change) {
                        packet.bytes = {
                            static_cast<std::uint8_t>(0xB0 | channel),
                            static_cast<std::uint8_t>(event.parameter_id),
                            static_cast<std::uint8_t>(
                                std::clamp(event.value, 0.0F, 1.0F) * 127.0F)};
                        packet.size = 3;
                    } else if (event.type == MusicalEventType::program_change) {
                        packet.bytes = {
                            static_cast<std::uint8_t>(0xC0 | channel),
                            static_cast<std::uint8_t>(event.parameter_id), 0};
                        packet.size = 2;
                    } else if (event.type == MusicalEventType::pitch_bend) {
                        const auto bend = static_cast<std::uint16_t>(std::clamp(
                            8192.0F + event.value * 8191.0F, 0.0F, 16383.0F));
                        packet.bytes = {
                            static_cast<std::uint8_t>(0xE0 | channel),
                            static_cast<std::uint8_t>(bend & 0x7F),
                            static_cast<std::uint8_t>((bend >> 7) & 0x7F)};
                        packet.size = 3;
                    }
                    if (packet.size)
                        (void)midi->enqueue(packet);
                }
            } else
                missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        }
        for (auto &voice : voices_) {
            if (!voice.active)
                continue;
            const auto voice_modulation = process_sample_voice_modulation(
                voice.voice_modulation, modulation_.configuration(), rate_,
                project_tempo_bpm_);
            const auto effective_pitch =
                voice.modulation_pitch + voice_modulation.pitch_semitones;
            voice.increment =
                voice.base_increment * std::exp2(effective_pitch / 12.0F);
            voice.stretch_configuration.pitch_ratio =
                static_cast<float>(voice.increment);
            const auto &sample = *voice.sample;
            if (voice.stretched) {
                const auto smoothing =
                    1.0F / std::max(1.0F, static_cast<float>(rate_) * 0.02F);
                const auto ratio = voice.stretch_configuration.time_ratio +
                                   (voice.stretch_ratio_target -
                                    voice.stretch_configuration.time_ratio) *
                                       smoothing;
                RealtimeTimeStretcher::set_time_ratio(
                    voice.stretch_state, voice.stretch_configuration, ratio);
            }
            const auto stretched_source =
                voice.stretched
                    ? RealtimeTimeStretcher::source_position(
                          voice.stretch_state, voice.stretch_configuration)
                    : 0.0;
            if ((voice.stretched && !voice.region.loop_enabled &&
                 ((!voice.reverse &&
                   stretched_source >= static_cast<double>(voice.end_frame)) ||
                  (voice.reverse && stretched_source <
                                        static_cast<double>(voice.start_frame)))) ||
                (!voice.stretched &&
                 (voice.position < static_cast<double>(voice.start_frame) ||
                  voice.position >= static_cast<double>(voice.end_frame)))) {
                voice.active = false;
                continue;
            }
            float sample_left{}, sample_right{};
            if (voice.stretched) {
                sample_left = RealtimeTimeStretcher::process_channel(
                    sample, voice.start_frame, voice.end_frame,
                    voice.stretch_configuration, voice.stretch_state, 0);
                sample_right = RealtimeTimeStretcher::process_channel(
                    sample, voice.start_frame, voice.end_frame,
                    voice.stretch_configuration, voice.stretch_state, 1);
            } else {
                const auto base = static_cast<std::uint64_t>(voice.position);
                const auto fraction = static_cast<float>(voice.position - base);
                const auto adjacent =
                    voice.reverse
                        ? (base > voice.start_frame ? base - 1 : base)
                        : (base + 1 < voice.end_frame ? base + 1 : base);
                const auto interpolate = [&](std::uint16_t channel) {
                    const auto actual = sample.channels > 1 ? channel : 0;
                    const auto first =
                        sample.interleaved[base * sample.channels + actual];
                    const auto second =
                        sample.interleaved[adjacent * sample.channels + actual];
                    return first + (second - first) * fraction;
                };
                sample_left = interpolate(0);
                sample_right = interpolate(1);
                if (voice.region.loop_enabled &&
                    voice.region.loop_crossfade_frames > 0) {
                    const auto loop_start = voice.region.loop_start_frame;
                    const auto loop_end = voice.region.loop_end_frame;
                    const auto crossfade = static_cast<double>(
                        voice.region.loop_crossfade_frames);
                    const auto in_crossfade =
                        voice.reverse
                            ? voice.position <
                                  static_cast<double>(loop_start) + crossfade
                            : voice.position >=
                                  static_cast<double>(loop_end) - crossfade;
                    if (in_crossfade) {
                        const auto amount = std::clamp(
                            voice.reverse
                                ? (static_cast<double>(loop_start) + crossfade -
                                   voice.position) /
                                      crossfade
                                : (voice.position -
                                   (static_cast<double>(loop_end) - crossfade)) /
                                      crossfade,
                            0.0, 1.0);
                        const auto alternate_position =
                            voice.reverse
                                ? static_cast<double>(loop_end) - crossfade +
                                      (voice.position - loop_start)
                                : static_cast<double>(loop_start) +
                                      (voice.position -
                                       (static_cast<double>(loop_end) -
                                        crossfade));
                        const auto alternate_base = static_cast<std::uint64_t>(
                            std::clamp(alternate_position,
                                       static_cast<double>(loop_start),
                                       static_cast<double>(loop_end - 1)));
                        const auto sample_at = [&](std::uint16_t channel) {
                            const auto actual = sample.channels > 1 ? channel : 0;
                            return sample.interleaved[alternate_base *
                                                          sample.channels +
                                                      actual];
                        };
                        sample_left = static_cast<float>(
                            sample_left * (1.0 - amount) + sample_at(0) * amount);
                        sample_right = static_cast<float>(sample_right *
                                                              (1.0 - amount) +
                                                          sample_at(1) * amount);
                    }
                }
            }
            const auto source_progress =
                voice.stretched
                    ? (voice.reverse
                           ? static_cast<double>(voice.end_frame - 1) -
                                 RealtimeTimeStretcher::playback_position(
                                     voice.stretch_state,
                                     voice.stretch_configuration,
                                     voice.start_frame, voice.end_frame)
                           : RealtimeTimeStretcher::playback_position(
                                 voice.stretch_state,
                                 voice.stretch_configuration, voice.start_frame,
                                 voice.end_frame) -
                                 static_cast<double>(voice.start_frame))
                    : (voice.reverse
                           ? static_cast<double>(voice.end_frame - 1) -
                                 voice.position
                           : voice.position -
                                 static_cast<double>(voice.start_frame));
            const auto source_remaining =
                static_cast<double>(voice.end_frame - voice.start_frame) -
                source_progress;
            auto region_level = 1.0F;
            const auto curve = [&](double normalized) {
                const auto value = static_cast<float>(
                    std::clamp(normalized, 0.0, 1.0));
                return voice.region.fade_curve == RegionFadeCurve::equal_power
                           ? std::sin(value * 1.5707963267948966F)
                           : value;
            };
            if (voice.region.fade_in_frames)
                region_level = std::min(
                    region_level,
                    voice.region.fade_in_frames == 1
                        ? (source_progress <= 0 ? 0.0F : 1.0F)
                        : curve(source_progress /
                                (voice.region.fade_in_frames - 1)));
            if (voice.region.fade_out_frames)
                region_level = std::min(
                    region_level,
                    voice.region.fade_out_frames == 1
                        ? (source_remaining <= 1 ? 0.0F : 1.0F)
                        : curve((source_remaining - 1) /
                                (voice.region.fade_out_frames - 1)));
            const auto clip_curve = [](double normalized,
                                       AudioClipFadeCurve curve, bool out) {
                const auto value = static_cast<float>(
                    std::clamp(normalized, 0.0, 1.0));
                if (curve == AudioClipFadeCurve::equal_power)
                    return out ? std::cos(value * 1.5707963267948966F)
                               : std::sin(value * 1.5707963267948966F);
                return out ? 1.0F - value : value;
            };
            auto clip_level = 1.0F;
            if (voice.audio_clip && voice.clip_fade_in_frames)
                clip_level *= clip_curve(
                    static_cast<double>(voice.clip_elapsed_frames) /
                        std::max<std::uint64_t>(1,
                            voice.clip_fade_in_frames - 1),
                    voice.clip_fade_curve, false);
            if (voice.audio_clip && voice.clip_fade_out_frames &&
                voice.clip_total_frames > voice.clip_fade_out_frames) {
                const auto start = voice.clip_total_frames -
                                   voice.clip_fade_out_frames;
                if (voice.clip_elapsed_frames >= start)
                    clip_level *= clip_curve(
                        static_cast<double>(voice.clip_elapsed_frames - start) /
                            std::max<std::uint64_t>(1,
                                voice.clip_fade_out_frames - 1),
                        voice.clip_fade_curve, true);
            }
            if (voice.audio_clip && voice.crossfade_in_frames)
                clip_level *= clip_curve(
                    static_cast<double>(voice.clip_elapsed_frames) /
                        std::max<std::uint64_t>(1,
                            voice.crossfade_in_frames - 1),
                    voice.crossfade_in_curve, false);
            if (voice.audio_clip && voice.crossfade_out_frames &&
                voice.clip_elapsed_frames >= voice.crossfade_out_offset_frames)
                clip_level *= clip_curve(
                    static_cast<double>(voice.clip_elapsed_frames -
                                        voice.crossfade_out_offset_frames) /
                        std::max<std::uint64_t>(1,
                            voice.crossfade_out_frames - 1),
                    voice.crossfade_out_curve, true);
            const auto level = voice.base_gain * voice.modulation_gain *
                               voice_modulation.gain * voice.envelope_level *
                               region_level * clip_level;
            const auto pan = std::clamp(voice.base_pan + voice.modulation_pan +
                                            voice_modulation.pan,
                                        -1.0F, 1.0F);
            sample_left *= pan > 0 ? 1.0F - pan : 1.0F;
            sample_right *= pan < 0 ? 1.0F + pan : 1.0F;
            const auto routed =
                voice.timeline_owned &&
                mixer_.add_track(voice.track, frame, sample_left * level,
                                 sample_right * level);
            if (!routed) {
                left[frame] += sample_left * level;
                right[frame] += sample_right * level;
            }
            if (voice.audio_clip)
                ++voice.clip_elapsed_frames;
            if (voice.envelope_stage == Voice::EnvelopeStage::attack) {
                voice.envelope_level += voice.attack_step;
                if (voice.envelope_level >= 1.0F) {
                    voice.envelope_level = 1.0F;
                    voice.envelope_stage = Voice::EnvelopeStage::decay;
                }
            } else if (voice.envelope_stage == Voice::EnvelopeStage::decay) {
                voice.envelope_level -= voice.decay_step;
                if (voice.envelope_level <= voice.sustain) {
                    voice.envelope_level = voice.sustain;
                    voice.envelope_stage = Voice::EnvelopeStage::sustain;
                }
            } else if (voice.envelope_stage == Voice::EnvelopeStage::release) {
                voice.envelope_level -= voice.release_step;
                if (voice.envelope_level <= 0) {
                    voice.envelope_level = 0;
                    voice.active = false;
                    continue;
                }
            }
            if (voice.stretched)
                RealtimeTimeStretcher::advance(voice.stretch_state);
            else
                voice.position +=
                    voice.reverse ? -voice.increment : voice.increment;
            if (!voice.stretched && voice.region.loop_enabled &&
                voice.envelope_stage != Voice::EnvelopeStage::release) {
                const auto loop_start =
                    static_cast<double>(voice.region.loop_start_frame);
                const auto loop_end =
                    static_cast<double>(voice.region.loop_end_frame);
                if (!voice.reverse && voice.position >= loop_end)
                    voice.position = loop_start +
                                     std::fmod(voice.position - loop_start,
                                               loop_end - loop_start);
                else if (voice.reverse && voice.position < loop_start) {
                    const auto distance = loop_start - voice.position;
                    voice.position = loop_end -
                                     std::fmod(distance, loop_end - loop_start);
                    if (voice.position >= loop_end) voice.position = loop_end - 1;
                }
            }
        }
        for (std::size_t synth_index = 0; synth_index < synths_.size();
             ++synth_index) {
            if (!synth_active_[synth_index])
                continue;
            auto &synth = synths_[synth_index];
            float synth_unrouted{};
            synth->render_sample_tracks(
                {mixer_tracks.data(), mixer_track_count},
                {synth_tracks.data(), mixer_track_count}, synth_unrouted);
            left[frame] += synth_unrouted;
            right[frame] += synth_unrouted;
            for (std::size_t i = 0; i < mixer_track_count; ++i)
                (void)mixer_.add_track(mixer_tracks[i], frame, synth_tracks[i],
                                       synth_tracks[i]);
            synth_active_[synth_index] = synth->active();
        }
    }

    const auto remaining = pending_trigger_count_ - external_index;
    if (monitoring)
        instrument_nanoseconds_.store(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - instrument_begin)
                    .count()),
            std::memory_order_relaxed);
    for (std::size_t i = 0; i < remaining; ++i) {
        pending_triggers_[i] = pending_triggers_[external_index + i];
        pending_triggers_[i].frame_offset -= static_cast<std::uint32_t>(frames);
    }
    pending_trigger_count_ = remaining;
    for (std::size_t version = 0; version < sample_version_capacity_; ++version)
        if (sample_versions_[version].state.load(std::memory_order_acquire) ==
            SampleVersionState::retiring) {
            bool referenced = false;
            for (const auto &voice : voices_)
                if (voice.active && voice.sample_version == version) {
                    referenced = true;
                    break;
                }
            if (!referenced)
                sample_versions_[version].state.store(
                    SampleVersionState::retired, std::memory_order_release);
        }
    if (timeline_stops_this_buffer) {
        if (transport_playing_) {
            cleanup_timeline_voices(VoiceTransitionPolicy::release_outgoing,
                                    active_timeline_end_frame_);
            transport_playing_ = false;
            song_end_count_.fetch_add(1, std::memory_order_relaxed);
            if (auto *midi = midi_output_.load(std::memory_order_acquire))
                if (midi_clock_configuration_.enabled)
                    midi_clock_.stop(*midi,
                                     {midi_clock_configuration_.ports.data(),
                                      midi_clock_configuration_.port_count});
        }
        transport_frame_ = active_timeline_end_frame_;
        active_timeline_end_frame_ = 0;
    } else if (transport_playing_ && accept_external_clock_) {
        transport_frame_ = external_end_frame;
    } else if (transport_playing_) {
        if (frames >
            std::numeric_limits<std::uint64_t>::max() - transport_frame_) {
            transport_frame_ = std::numeric_limits<std::uint64_t>::max();
            transport_playing_ = false;
            stop_voices();
        } else
            transport_frame_ += frames;
    }
    published_frame_.store(transport_frame_, std::memory_order_release);
    published_playing_.store(transport_playing_, std::memory_order_release);
    for (std::size_t i = 0; i < MacroMorphProcessor::capacity; ++i) {
        const auto &morph = macro_morphs_.morphs()[i];
        published_morph_ids_[i].store(morph.morph_id,
                                      std::memory_order_release);
        published_morph_instances_[i].store(morph.instance,
                                            std::memory_order_release);
        published_morph_start_frames_[i].store(morph.start_frame,
                                               std::memory_order_release);
        published_morph_end_frames_[i].store(morph.end_frame,
                                             std::memory_order_release);
        published_morph_start_values_[i].store(
            std::bit_cast<std::uint32_t>(morph.start_value),
            std::memory_order_release);
        published_morph_target_values_[i].store(
            std::bit_cast<std::uint32_t>(morph.target_value),
            std::memory_order_release);
        published_morph_effective_values_[i].store(
            std::bit_cast<std::uint32_t>(morph.effective_value),
            std::memory_order_release);
        published_morph_progress_[i].store(
            std::bit_cast<std::uint32_t>(morph.progress),
            std::memory_order_release);
        const auto flags = static_cast<std::uint32_t>(morph.active) |
                           (static_cast<std::uint32_t>(morph.scheduled) << 1U) |
                           (static_cast<std::uint32_t>(morph.interrupted) << 2U) |
                           (static_cast<std::uint32_t>(morph.completed) << 3U) |
                           (static_cast<std::uint32_t>(morph.duration_unit) << 4U) |
                           (static_cast<std::uint32_t>(morph.curve) << 5U);
        published_morph_flags_[i].store(flags, std::memory_order_release);
    }
    mixer_.process(left.first(frames), right.first(frames),
                   physical_output_ ? std::span<float>{physical_output_,
                                                       physical_output_samples_}
                                    : std::span<float>{},
                   physical_output_channels_);
    float follower_peak{};
    for (std::size_t frame = 0; frame < frames; ++frame)
        follower_peak =
            std::max(follower_peak,
                     std::max(std::fabs(left[frame]), std::fabs(right[frame])));
    modulation_.set_input(ModulationSourceId::envelope_follower,
                          std::min(follower_peak, 1.0F));
    const auto observe = [&](FollowerTapKind kind, std::uint64_t id,
                             std::span<const float> first,
                             std::span<const float> second = {}) {
        float peak{};
        for (const auto sample : first)
            peak = std::max(peak, std::fabs(sample));
        for (const auto sample : second)
            peak = std::max(peak, std::fabs(sample));
        modulation_.set_follower_input(kind, id, std::min(peak, 1.0F));
    };
    for (std::size_t track = 0; track < mixer_.active_track_count(); ++track)
        observe(FollowerTapKind::track, mixer_.active_track_id(track),
                mixer_.track_left(track), mixer_.track_right(track));
    for (std::size_t aux = 0; aux < mixer_aux_count; ++aux)
        observe(FollowerTapKind::auxiliary, aux, mixer_.aux_left(aux),
                mixer_.aux_right(aux));
    for (std::size_t group = 0; group < mixer_.active_group_count(); ++group)
        observe(FollowerTapKind::group, mixer_.active_group_id(group),
                mixer_.group_left(group), mixer_.group_right(group));
    for (std::size_t input = 0; input < mixer_.active_input_count(); ++input)
        observe(FollowerTapKind::physical_input, input, mixer_.input(input));
    if (auto *recorder = recording_.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < mixer_track_count; ++i)
            recorder->capture_internal(
                RecordingSource::track_channel, mixer_.track_left(i),
                mixer_.track_right(i), callback_frame, mixer_tracks[i]);
        for (std::size_t i = 0; i < mixer_aux_count; ++i)
            recorder->capture_internal(RecordingSource::auxiliary_bus,
                                       mixer_.aux_left(i), mixer_.aux_right(i),
                                       callback_frame, i);
        for (std::size_t i = 0; i < mixer_.active_group_count(); ++i) {
            const auto group_id = mixer_.active_group_id(i);
            recorder->capture_internal(
                RecordingSource::group_pre_effects, mixer_.group_pre_left(i),
                mixer_.group_pre_right(i), callback_frame, group_id);
            recorder->capture_internal(RecordingSource::group_post_effects,
                                       mixer_.group_post_effect_left(i),
                                       mixer_.group_post_effect_right(i),
                                       callback_frame, group_id);
            recorder->capture_internal(
                RecordingSource::group_post_fader, mixer_.group_left(i),
                mixer_.group_right(i), callback_frame, group_id);
        }
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
            recorder->capture_internal(RecordingSource::external_fx_return,
                                       mixer_.external_return_left(i),
                                       mixer_.external_return_right(i),
                                       callback_frame, i);
            recorder->capture_internal(
                RecordingSource::external_fx_mix, mixer_.external_mix_left(i),
                mixer_.external_mix_right(i), callback_frame, i);
        }
        recorder->capture_internal(RecordingSource::master_pre_effects,
                                   left.first(frames), right.first(frames),
                                   callback_frame);
    }
    mixer_.apply_master_gain(left.first(frames), right.first(frames));
    mixer_.process_master_effects(left.first(frames), right.first(frames));
    const auto effect_begin = monitoring
                                  ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    effect_.process(left.first(frames), right.first(frames));
    std::size_t delay_frame{};
    for (std::size_t i = 0; i < timeline_count; ++i) {
        const auto &event = timeline_events_[i];
        if (event.type != MusicalEventType::parameter ||
            event.parameter_target !=
                static_cast<std::uint32_t>(ParameterTarget::stereo_delay))
            continue;
        const auto event_frame = std::min<std::size_t>(event.frame, frames);
        if (event_frame > delay_frame)
            delay_.process(
                left.subspan(delay_frame, event_frame - delay_frame),
                right.subspan(delay_frame, event_frame - delay_frame));
        delay_.set_parameter(event.parameter_id, event.value);
        delay_frame = event_frame;
    }
    if (delay_frame < frames)
        delay_.process(left.subspan(delay_frame, frames - delay_frame),
                       right.subspan(delay_frame, frames - delay_frame));
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->capture_internal(RecordingSource::master_post_effects,
                                   left.first(frames), right.first(frames),
                                   callback_frame);
    // The click is a monitor signal, not musical project material. Rendering
    // after every recorder tap prevents it entering resampling, stems, loops,
    // or track recordings while the normal master route still reaches the
    // configured physical outputs.
    const auto count_in_active =
        metronome_count_in_end_ > metronome_count_in_start_ &&
        callback_frame < metronome_count_in_end_ &&
        callback_frame + frames > metronome_count_in_start_;
    const auto click_enabled =
        (metronome_configuration_.enabled && transport_playing_) ||
        count_in_active;
    const auto block_clock_start = count_in_active
                                       ? callback_frame -
                                             metronome_count_in_start_
                                       : metronome_transport_start;
    const auto numerator = static_cast<__uint128_t>(rate_) *
                           metronome_configuration_.microseconds_per_quarter *
                           4U;
    const auto denominator = static_cast<__uint128_t>(1'000'000U) *
                             std::max<std::uint16_t>(
                                 1, metronome_configuration_.meter_denominator);
    const auto boundary = [&](std::uint64_t beat) noexcept {
        const auto value = static_cast<__uint128_t>(beat) * numerator;
        return static_cast<std::uint64_t>((value + denominator - 1) /
                                          denominator);
    };
    auto beat = numerator ? static_cast<std::uint64_t>(
                                (static_cast<__uint128_t>(block_clock_start) *
                                 denominator) /
                                numerator)
                          : 0;
    while (boundary(beat) < block_clock_start)
        ++beat;
    bool clicked{}, downbeat{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto absolute = block_clock_start + frame;
        if (click_enabled && boundary(beat) == absolute &&
            (!count_in_active || callback_frame + frame >=
                                     metronome_count_in_start_)) {
            metronome_voice_frame_ = 0;
            metronome_voice_active_ = true;
            metronome_voice_downbeat_ =
                beat % std::max<std::uint16_t>(
                           1, metronome_configuration_.meter_numerator) ==
                0;
            clicked = true;
            downbeat = metronome_voice_downbeat_;
            published_metronome_click_count_.fetch_add(
                1, std::memory_order_relaxed);
            ++beat;
        }
        if (!metronome_voice_active_)
            continue;
        const auto duration = std::max<std::uint32_t>(1, rate_ / 80U);
        if (metronome_voice_frame_ >= duration) {
            metronome_voice_active_ = false;
            continue;
        }
        const auto frequency = metronome_voice_downbeat_ ? 1760.0 : 1320.0;
        const auto phase = 6.2831853071795864769 * frequency *
                           metronome_voice_frame_ / rate_;
        const auto envelope =
            1.0F - static_cast<float>(metronome_voice_frame_) / duration;
        const auto sample = static_cast<float>(std::sin(phase)) * envelope *
                            envelope *
                            std::clamp(metronome_configuration_.level, 0.0F,
                                       1.0F) *
                            (metronome_voice_downbeat_ ? 0.9F : 0.65F);
        left[frame] += sample;
        right[frame] += sample;
        ++metronome_voice_frame_;
    }
    published_metronome_enabled_.store(metronome_configuration_.enabled,
                                       std::memory_order_release);
    published_metronome_level_.store(metronome_configuration_.level,
                                     std::memory_order_release);
    published_count_in_active_.store(count_in_active,
                                     std::memory_order_release);
    published_metronome_beat_.store(clicked, std::memory_order_release);
    published_metronome_downbeat_.store(clicked && downbeat,
                                        std::memory_order_release);
    published_count_in_start_.store(metronome_count_in_start_,
                                    std::memory_order_release);
    published_count_in_end_.store(metronome_count_in_end_,
                                  std::memory_order_release);
    if (metronome_count_in_end_ &&
        callback_frame + frames >= metronome_count_in_end_)
        metronome_count_in_start_ = metronome_count_in_end_ = 0;
    mixer_.route_master_output(
        left.first(frames), right.first(frames),
        physical_output_
            ? std::span<float>{physical_output_, physical_output_samples_}
            : std::span<float>{},
        physical_output_channels_);
    if (monitoring)
        effect_nanoseconds_.store(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - effect_begin)
                    .count()),
            std::memory_order_relaxed);
    buffer_frames_.store(static_cast<std::uint32_t>(frames),
                         std::memory_order_relaxed);
    std::size_t active_voices{};
    for (const auto &voice : voices_)
        active_voices += voice.active ? 1U : 0U;
    active_voice_count_.store(active_voices, std::memory_order_release);
    if (monitoring) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - callback_begin)
                .count();
        callback_monitor_.record(
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
            static_cast<std::uint32_t>(frames), rate_, active_voices);
    }
    callback_frame_.store(callback_frame + frames, std::memory_order_release);
}

void AudioEngine::capture_external_input(std::span<const float> interleaved,
                                         std::uint16_t channels) noexcept {
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->capture_external(
            interleaved, channels,
            callback_frame_.load(std::memory_order_relaxed));
}
void AudioEngine::recording_device_stopped() noexcept {
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->device_stopped();
}

void AudioEngine::process_duplex(std::span<float> left, std::span<float> right,
                                 std::span<const float> input,
                                 std::uint16_t channels) noexcept {
    if (auto *recorder = recording_.load(std::memory_order_acquire)) {
        const auto frames = std::min({left.size(), right.size(),
                                      channels ? input.size() / channels : 0});
        const auto start = callback_frame_.load(std::memory_order_relaxed);
        const auto boundary = recorder->next_stop_boundary(start, frames);
        if (boundary && boundary > start && boundary < start + frames) {
            const auto first = static_cast<std::size_t>(boundary - start);
            process_duplex(left.first(first), right.first(first),
                           input.first(first * channels), channels);
            process_duplex(left.subspan(first, frames - first),
                           right.subspan(first, frames - first),
                           input.subspan(first * channels,
                                         (frames - first) * channels),
                           channels);
            return;
        }
    }
    capture_external_input(input, channels);
    duplex_input_ = input.data();
    duplex_input_samples_ = input.size();
    duplex_input_channels_ = channels;
    process(left, right);
    duplex_input_ = nullptr;
    duplex_input_samples_ = 0;
    duplex_input_channels_ = 0;
    auto *recorder = recording_.load(std::memory_order_acquire);
    if (!recorder || !recorder->monitoring_active() || channels == 0)
        return;
    const auto frames =
        std::min({left.size(), right.size(), input.size() / channels});
    for (std::size_t i = 0; i < frames; ++i) {
        const auto l = input[i * channels];
        const auto r = channels > 1 ? input[i * channels + 1] : l;
        left[i] += l;
        right[i] += r;
    }
}

void AudioEngine::process_io(std::span<float> output,
                             std::uint16_t output_channels,
                             std::span<const float> input,
                             std::uint16_t input_channels) noexcept {
    if (output_channels == 0)
        return;
    const auto frames = output.size() / output_channels;
    if (auto *recorder = recording_.load(std::memory_order_acquire)) {
        const auto start = callback_frame_.load(std::memory_order_relaxed);
        const auto boundary = recorder->next_stop_boundary(start, frames);
        if (boundary && boundary > start && boundary < start + frames) {
            const auto first = static_cast<std::size_t>(boundary - start);
            process_io(output.first(first * output_channels), output_channels,
                       input.first(std::min(input.size(),
                                            first * input_channels)),
                       input_channels);
            process_io(output.subspan(first * output_channels), output_channels,
                       input.subspan(std::min(input.size(),
                                              first * input_channels)),
                       input_channels);
            return;
        }
    }
    std::array<float, maximum_mixer_block> left{}, right{};
    const auto count = std::min(frames, left.size());
    std::fill(output.begin(), output.end(), 0.0F);
    physical_output_ = output.data();
    physical_output_samples_ = output.size();
    physical_output_channels_ = static_cast<std::uint8_t>(
        std::min<std::uint16_t>(output_channels, maximum_mixer_outputs));
    duplex_input_ = input.data();
    duplex_input_samples_ = input.size();
    duplex_input_channels_ = input_channels;
    capture_external_input(input, input_channels);
    process({left.data(), count}, {right.data(), count});
    if (auto *recorder = recording_.load(std::memory_order_acquire);
        recorder && recorder->monitoring_active() && input_channels) {
        const auto output_left = mixer_.active_master_left();
        const auto output_right = mixer_.active_master_right();
        if (output_left < output_channels && output_right < output_channels)
            for (std::size_t frame = 0;
                 frame < std::min(count, input.size() / input_channels);
                 ++frame) {
                output[frame * output_channels + output_left] +=
                    input[frame * input_channels];
                output[frame * output_channels + output_right] +=
                    input[frame * input_channels +
                          (input_channels > 1 ? 1 : 0)];
            }
    }
    physical_output_ = nullptr;
    physical_output_samples_ = 0;
    physical_output_channels_ = 0;
    duplex_input_ = nullptr;
    duplex_input_samples_ = 0;
    duplex_input_channels_ = 0;
}

void AudioEngine::render_wav(const std::filesystem::path &path,
                             std::uint64_t frames) {
    std::ofstream output(path, std::ios::binary);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<char *>(&value), 4);
    };
    output.write("RIFF", 4);
    write32(36 + frames * 4);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(1);
    write16(2);
    write32(rate_);
    write32(rate_ * 4);
    write16(4);
    write16(16);
    output.write("data", 4);
    write32(frames * 4);
    std::array<float, 256> left{}, right{};
    for (std::uint64_t done = 0; done < frames;) {
        const auto count = std::min<std::uint64_t>(left.size(), frames - done);
        process({left.data(), count}, {right.data(), count});
        for (std::size_t i = 0; i < count; ++i) {
            auto convert = [](float value) {
                return static_cast<std::int16_t>(
                    std::clamp(value, -1.0F, 1.0F) * 32767);
            };
            auto l = convert(left[i]), r = convert(right[i]);
            output.write(reinterpret_cast<char *>(&l), 2);
            output.write(reinterpret_cast<char *>(&r), 2);
        }
        done += count;
    }
}
} // namespace neurogenic
