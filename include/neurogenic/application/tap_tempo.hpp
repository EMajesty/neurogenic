#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace neurogenic {
class TapTempoEstimator {
  public:
    static constexpr std::size_t maximum_intervals = 7;
    explicit TapTempoEstimator(std::uint64_t inactivity_ns = 2'500'000'000ULL)
        : inactivity_ns_(inactivity_ns) {}
    [[nodiscard]] std::optional<double> observe(std::uint64_t monotonic_ns,
                                                double minimum_bpm = 20.0,
                                                double maximum_bpm = 300.0) noexcept;
    void reset() noexcept;
  private:
    std::array<std::uint64_t, maximum_intervals> intervals_{};
    std::size_t count_{};
    std::uint64_t last_tap_{}, inactivity_ns_{};
};
} // namespace neurogenic
