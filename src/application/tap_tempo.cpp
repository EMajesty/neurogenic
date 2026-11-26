#include "neurogenic/application/tap_tempo.hpp"
#include <algorithm>
#include <cmath>

namespace neurogenic {
std::optional<double> TapTempoEstimator::observe(std::uint64_t now,
                                                  double minimum_bpm,
                                                  double maximum_bpm) noexcept {
    if (!now || minimum_bpm <= 0 || maximum_bpm <= minimum_bpm)
        return std::nullopt;
    if (!last_tap_ || now <= last_tap_ || now - last_tap_ > inactivity_ns_) {
        reset();
        last_tap_ = now;
        return std::nullopt;
    }
    const auto interval = now - last_tap_;
    last_tap_ = now;
    const auto fastest = static_cast<std::uint64_t>(60'000'000'000.0 /
                                                    maximum_bpm);
    const auto slowest = static_cast<std::uint64_t>(60'000'000'000.0 /
                                                    minimum_bpm);
    if (interval < fastest || interval > slowest) {
        count_ = 0;
        return std::nullopt;
    }
    if (count_ < maximum_intervals)
        intervals_[count_++] = interval;
    else {
        std::move(intervals_.begin() + 1, intervals_.end(), intervals_.begin());
        intervals_.back() = interval;
    }
    if (count_ < 2)
        return std::nullopt;
    auto sorted = intervals_;
    for (std::size_t i = 1; i < count_; ++i) {
        const auto value = sorted[i];
        auto position = i;
        while (position && sorted[position - 1] > value) {
            sorted[position] = sorted[position - 1];
            --position;
        }
        sorted[position] = value;
    }
    const auto median = sorted[count_ / 2];
    long double sum{};
    std::size_t accepted{};
    for (std::size_t i = 0; i < count_; ++i) {
        const auto value = intervals_[i];
        // A single anomalous interval cannot drag the estimate by more than
        // 20%; repeated new intervals establish the new tempo naturally.
        if (value * 5 < median * 4 || value * 4 > median * 5)
            continue;
        sum += value;
        ++accepted;
    }
    if (accepted < 2)
        return std::nullopt;
    return std::clamp(60'000'000'000.0 / static_cast<double>(sum / accepted),
                      minimum_bpm, maximum_bpm);
}
void TapTempoEstimator::reset() noexcept {
    count_ = 0;
    last_tap_ = 0;
}
} // namespace neurogenic
