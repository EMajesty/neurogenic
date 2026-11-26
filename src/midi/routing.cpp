#include "neurogenic/midi/routing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace neurogenic {
namespace {
bool accepts(const MidiRoute &route, std::uint8_t status) noexcept {
    const auto type = status & 0xF0U;
    if (status == 0xF8)
        return route.clock;
    if (status >= 0xF9)
        return route.transport;
    if (type == 0x80 || type == 0x90)
        return route.notes;
    if (type == 0xB0)
        return route.control_change;
    if (type == 0xC0)
        return route.program_change;
    if (type == 0xE0)
        return route.pitch_bend;
    return false;
}
} // namespace
bool validate_midi_routes(std::span<const MidiRoute> routes,
                          std::string &error) noexcept {
    if (routes.size() > maximum_midi_routes) {
        error = "MIDI route capacity exceeded";
        return false;
    }
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const auto &route = routes[i];
        if (!route.id || !route.input_port ||
            route.input_port > maximum_midi_ports || !route.output_port ||
            route.output_port > maximum_midi_ports ||
            route.input_channel < -1 || route.input_channel > 15 ||
            route.output_channel < -1 || route.output_channel > 15) {
            error = "invalid MIDI route";
            return false;
        }
        if (route.input_port == route.output_port &&
            (route.input_channel < 0 || route.output_channel < 0 ||
             route.input_channel == route.output_channel)) {
            error = "MIDI routing loop rejected";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (routes[j].id == route.id) {
                error = "duplicate MIDI route ID";
                return false;
            }
    }
    error.clear();
    return true;
}
bool MidiOutputScheduler::enqueue(const MidiPacket &packet) noexcept {
    const auto status = packet.bytes[0];
    const auto critical = status >= 0xF8 || status == 0xF2 ||
                          (status & 0xF0U) == 0x80 ||
                          ((status & 0xF0U) == 0xB0 && packet.size >= 2 &&
                           (packet.bytes[1] == 120 || packet.bytes[1] == 123));
    const auto safety = (status & 0xF0U) == 0xB0 && packet.size >= 2 &&
                        packet.bytes[1] == 64;
    if (!packet.port || !packet.size || packet.size > packet.bytes.size() ||
        !((critical || safety) ? priority_queue_.push(packet)
                               : queue_.push(packet))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    queued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}
std::size_t MidiOutputScheduler::dispatch(Sender sender) {
    MidiPacket packet;
    std::size_t count{};
    while (priority_queue_.pop(packet)) {
        queued_.fetch_sub(1, std::memory_order_relaxed);
        if (sender(packet)) {
            sent_.fetch_add(1, std::memory_order_relaxed);
            ++count;
        } else
            dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    while (queue_.pop(packet)) {
        queued_.fetch_sub(1, std::memory_order_relaxed);
        if (sender(packet)) {
            sent_.fetch_add(1, std::memory_order_relaxed);
            ++count;
        } else
            dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    return count;
}
void MidiOutputScheduler::panic(MidiPortId port) noexcept {
    const auto first = port ? port : 1U,
               last = port ? port : static_cast<MidiPortId>(maximum_midi_ports);
    for (auto output = first; output <= last; ++output)
        for (std::uint8_t channel = 0; channel < 16; ++channel) {
            (void)enqueue({output,
                           {static_cast<std::uint8_t>(0xB0U | channel), 64, 0},
                           3});
            (void)enqueue({output,
                           {static_cast<std::uint8_t>(0xB0U | channel), 123, 0},
                           3});
            (void)enqueue({output,
                           {static_cast<std::uint8_t>(0xB0U | channel), 120, 0},
                           3});
        }
}
bool MidiRouter::configure(std::span<const MidiRoute> routes,
                           std::string &error) noexcept {
    if (!validate_midi_routes(routes, error))
        return false;
    std::copy(routes.begin(), routes.end(), routes_.begin());
    route_count_ = routes.size();
    return true;
}
void MidiRouter::configure_realtime(std::span<const MidiRoute> routes) noexcept {
    const auto count = std::min(routes.size(), routes_.size());
    std::copy_n(routes.begin(), count, routes_.begin());
    route_count_ = count;
}
bool MidiRouter::route(const MidiPacket &input,
                       MidiOutputScheduler &output) noexcept {
    bool forwarded{};
    for (std::size_t i = 0; i < route_count_; ++i) {
        const auto &route = routes_[i];
        const auto channel_message = input.bytes[0] < 0xF0;
        const auto channel = input.bytes[0] & 0x0F;
        if (!route.enabled || route.input_port != input.port ||
            !accepts(route, input.bytes[0]) ||
            (channel_message && route.input_channel >= 0 &&
             channel != route.input_channel))
            continue;
        auto packet = input;
        packet.port = route.output_port;
        if (channel_message && route.output_channel >= 0)
            packet.bytes[0] = static_cast<std::uint8_t>(
                (packet.bytes[0] & 0xF0U) | route.output_channel);
        forwarded |= output.enqueue(packet);
    }
    if (!forwarded)
        dropped_.fetch_add(1, std::memory_order_relaxed);
    return forwarded;
}
MidiClockGenerator::MidiClockGenerator(std::uint32_t rate,
                                       std::uint32_t tempo) noexcept
    : sample_rate_(std::max(1U, rate)) {
    set_tempo(tempo);
}
void MidiClockGenerator::set_tempo(std::uint32_t tempo) noexcept {
    tempo = std::clamp(tempo, 10000U, 60000000U);
    const auto old = denominator_;
    microseconds_per_quarter_ = tempo;
    denominator_ = static_cast<std::uint64_t>(sample_rate_) * tempo;
    if (old)
        phase_ = static_cast<std::uint64_t>(
            (static_cast<long double>(phase_) * denominator_) / old);
}
void MidiClockGenerator::transport(std::uint8_t status,
                                   MidiOutputScheduler &out,
                                   std::span<const MidiPortId> ports) noexcept {
    for (const auto port : ports)
        (void)out.enqueue({port, {status, 0, 0}, 1});
}
void MidiClockGenerator::start(MidiOutputScheduler &out,
                               std::span<const MidiPortId> ports) noexcept {
    phase_ = pulses_ = 0;
    running_ = true;
    transport(0xFA, out, ports);
}
void MidiClockGenerator::stop(MidiOutputScheduler &out,
                              std::span<const MidiPortId> ports) noexcept {
    running_ = false;
    transport(0xFC, out, ports);
}
void MidiClockGenerator::continue_playback(
    MidiOutputScheduler &out, std::span<const MidiPortId> ports) noexcept {
    running_ = true;
    transport(0xFB, out, ports);
}
void MidiClockGenerator::process(std::uint32_t frames, std::uint64_t absolute,
                                 MidiOutputScheduler &out,
                                 std::span<const MidiPortId> ports) noexcept {
    if (!running_)
        return;
    constexpr std::uint64_t pulse_numerator = 24'000'000ULL;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        phase_ += pulse_numerator;
        while (phase_ >= denominator_) {
            phase_ -= denominator_;
            ++pulses_;
            for (const auto port : ports)
                (void)out.enqueue({port, {0xF8, 0, 0}, 1, 0, absolute + frame});
        }
    }
}
void MidiClockReceiver::receive(const MidiPacket &packet) noexcept {
    const auto status = packet.bytes[0];
    if (status == 0xFA) {
        state_.running = true;
        state_.pulses = 0;
        state_.dropout = false;
    } else if (status == 0xFB) {
        state_.running = true;
        state_.dropout = false;
    } else if (status == 0xFC)
        state_.running = false;
    else if (status == 0xF8) {
        if (state_.last_timestamp_ns &&
            packet.timestamp_ns > state_.last_timestamp_ns) {
            const auto interval =
                packet.timestamp_ns - state_.last_timestamp_ns;
            if (interval >= 1'000'000 && interval <= 1'000'000'000) {
                intervals_[interval_cursor_++ % intervals_.size()] = interval;
                interval_count_ =
                    std::min(interval_count_ + 1, intervals_.size());
                auto sorted = intervals_;
                for (std::size_t i = 1; i < interval_count_; ++i) {
                    const auto value = sorted[i];
                    auto position = i;
                    while (position && sorted[position - 1] > value) {
                        sorted[position] = sorted[position - 1];
                        --position;
                    }
                    sorted[position] = value;
                }
                const auto median = sorted[interval_count_ / 2];
                const auto estimate = 60'000'000'000.0 / (median * 24.0);
                state_.tempo_bpm = state_.available
                                       ? state_.tempo_bpm * 0.8 + estimate * 0.2
                                       : estimate;
                state_.available = true;
                state_.dropout = false;
            }
        }
        state_.last_timestamp_ns = packet.timestamp_ns;
        ++state_.pulses;
    }
}
void MidiClockReceiver::update(std::uint64_t now) noexcept {
    if (state_.available && state_.last_timestamp_ns &&
        now > state_.last_timestamp_ns + 500'000'000ULL)
        state_.dropout = true;
}

MidiClockFollower::MidiClockFollower(std::uint32_t sample_rate) noexcept
    : sample_rate_(std::max(1U, sample_rate)) {
    constexpr std::uint64_t q32 = std::uint64_t{1} << 32;
    increment_q32_ = static_cast<std::uint64_t>(
        (static_cast<__uint128_t>(q32) * 48) / sample_rate_); // 120 BPM
}
void MidiClockFollower::reset() noexcept {
    phase_q32_ = next_pulse_ = frames_since_pulse_ = 0;
    interval_count_ = interval_cursor_ = 0;
    state_ = {};
    state_.sync_status = MidiSyncStatus::stopped;
    discontinuity_ = true;
}
bool MidiClockFollower::consume_discontinuity() noexcept {
    return std::exchange(discontinuity_, false);
}
void MidiClockFollower::clock(std::uint64_t timestamp) noexcept {
    constexpr std::uint64_t q32 = std::uint64_t{1} << 32;
    if (state_.last_timestamp_ns && timestamp > state_.last_timestamp_ns) {
        const auto interval = timestamp - state_.last_timestamp_ns;
        if (interval >= 1'000'000 && interval <= 1'000'000'000) {
            intervals_[interval_cursor_++ % intervals_.size()] = interval;
            interval_count_ = std::min(interval_count_ + 1, intervals_.size());
            auto sorted = intervals_;
            for (std::size_t i = 1; i < interval_count_; ++i) {
                const auto value = sorted[i];
                auto position = i;
                while (position && sorted[position - 1] > value) {
                    sorted[position] = sorted[position - 1];
                    --position;
                }
                sorted[position] = value;
            }
            const auto median = sorted[interval_count_ / 2];
            const auto measured = static_cast<std::uint64_t>(
                (static_cast<__uint128_t>(q32) * 1'000'000'000ULL) /
                (static_cast<__uint128_t>(median) * sample_rate_));
            increment_q32_ = interval_count_ < 3
                                 ? measured
                                 : (increment_q32_ * 7 + measured) / 8;
            state_.tempo_bpm =
                60'000'000'000.0 / (static_cast<double>(median) * 24.0);
            state_.available = true;
        }
    }
    const auto target = next_pulse_ > (std::numeric_limits<std::uint64_t>::max() >> 32)
                            ? std::numeric_limits<std::uint64_t>::max()
                            : next_pulse_ << 32;
    const auto error = target >= phase_q32_
                           ? static_cast<std::int64_t>(std::min<std::uint64_t>(
                                 target - phase_q32_,
                                 std::numeric_limits<std::int64_t>::max()))
                           : -static_cast<std::int64_t>(std::min<std::uint64_t>(
                                 phase_q32_ - target,
                                 std::numeric_limits<std::int64_t>::max()));
    state_.phase_error_pulses = static_cast<double>(error) / q32;
    const auto was_lost = state_.sync_status == MidiSyncStatus::lost;
    if (state_.sync_status == MidiSyncStatus::acquiring || was_lost ||
        std::fabs(state_.phase_error_pulses) > 2.0) {
        phase_q32_ = target;
        discontinuity_ |= was_lost;
    } else {
        const auto limit = static_cast<std::int64_t>(q32 / 8);
        const auto correction = std::clamp(error / 4, -limit, limit);
        if (correction >= 0)
            phase_q32_ += static_cast<std::uint64_t>(correction);
        else
            phase_q32_ -= std::min(phase_q32_,
                                   static_cast<std::uint64_t>(-correction));
    }
    ++next_pulse_;
    ++state_.pulses;
    state_.last_timestamp_ns = timestamp;
    state_.dropout = false;
    frames_since_pulse_ = 0;
    state_.sync_status = interval_count_ >= 6 ? MidiSyncStatus::locked
                                              : MidiSyncStatus::acquiring;
}
void MidiClockFollower::receive(const MidiPacket &packet) noexcept {
    if (!packet.size)
        return;
    switch (packet.bytes[0]) {
    case 0xF2:
        if (packet.size >= 3) {
            state_.song_position = static_cast<std::uint16_t>(
                packet.bytes[1] | ((packet.bytes[2] & 0x7FU) << 7));
            next_pulse_ = static_cast<std::uint64_t>(state_.song_position) * 6;
            phase_q32_ = next_pulse_ << 32;
            discontinuity_ = true;
        }
        break;
    case 0xFA:
        phase_q32_ = next_pulse_ = frames_since_pulse_ = 0;
        state_.song_position = 0;
        state_.pulses = 0;
        state_.running = true;
        state_.dropout = false;
        state_.sync_status = MidiSyncStatus::acquiring;
        discontinuity_ = true;
        break;
    case 0xFB:
        state_.running = true;
        state_.dropout = false;
        state_.sync_status = state_.available ? MidiSyncStatus::holdover
                                              : MidiSyncStatus::acquiring;
        discontinuity_ = true;
        break;
    case 0xFC:
        state_.running = false;
        state_.sync_status = MidiSyncStatus::stopped;
        discontinuity_ = true;
        break;
    case 0xF8:
        clock(packet.timestamp_ns);
        break;
    default:
        break;
    }
}
void MidiClockFollower::advance(std::uint32_t frames) noexcept {
    if (!state_.running)
        return;
    frames_since_pulse_ += frames;
    const auto holdover = sample_rate_ / 8;
    const auto lost = sample_rate_ / 2;
    if (frames_since_pulse_ > lost) {
        state_.sync_status = MidiSyncStatus::lost;
        state_.dropout = true;
        return;
    }
    if (frames_since_pulse_ > holdover && state_.available)
        state_.sync_status = MidiSyncStatus::holdover;
    const auto advance = static_cast<__uint128_t>(increment_q32_) * frames;
    phase_q32_ = advance >
                         std::numeric_limits<std::uint64_t>::max() - phase_q32_
                     ? std::numeric_limits<std::uint64_t>::max()
                     : phase_q32_ + static_cast<std::uint64_t>(advance);
}
ExternalClockState MidiClockFollower::state() const noexcept { return state_; }
} // namespace neurogenic
