#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/application/discovery.hpp"
#include "neurogenic/application/parameter_binding.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Distribution {
    std::uint64_t median{}, p95{}, p99{}, maximum{};
};

Distribution distribution(std::vector<std::uint64_t> values) {
    std::sort(values.begin(), values.end());
    const auto at = [&](double q) {
        return values[std::min(values.size() - 1,
            static_cast<std::size_t>(q * static_cast<double>(values.size() - 1)))];
    };
    return {at(0.5), at(0.95), at(0.99), values.back()};
}
} // namespace

int main() {
    using namespace neurogenic;
    try {
        AudioEngine engine{48'000, 64};
        SampleManager samples{engine};
        Timeline source;
        for (TrackId id = 1; id <= 16; ++id) {
            source.tracks.push_back({id, "Benchmark Track " + std::to_string(id)});
            source.tracks.back().pattern.length = ticks_per_quarter * 4;
            for (std::uint64_t step = 0; step < 16; ++step)
                source.tracks.back().pattern.events.push_back(
                    {step + 1, static_cast<MusicalTick>(step * ticks_per_quarter / 4),
                     MusicalEventType::note, static_cast<std::uint8_t>(36 + id),
                     0.8F, ticks_per_quarter / 8});
        }
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, source);
        ApplicationService application(engine, timeline, samples);
        SlintFrontendAdapter adapter(application);
        constexpr std::size_t iterations = 256;
        std::vector<std::uint64_t> idle, full, incremental, snapshot_copy,
            snapshot_shared, discovery, parameter_resolution;
        idle.reserve(iterations);
        full.reserve(iterations);
        incremental.reserve(iterations);
        snapshot_copy.reserve(iterations);
        snapshot_shared.reserve(iterations);
        discovery.reserve(iterations);
        parameter_resolution.reserve(iterations);
        ParameterBindingService bindings(application);
        BindingReference tempo{BindingTargetKind::explicit_object, "master:main",
                               "mixer.master.gain", {},
                               application.snapshot_shared()->revision};
        for (std::size_t i = 0; i < iterations; ++i) {
            auto started = Clock::now();
            const auto value = application.snapshot();
            snapshot_copy.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
            if (value.tracks.empty()) throw std::runtime_error("empty snapshot");
            started = Clock::now();
            const auto handle = application.snapshot_shared();
            snapshot_shared.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
            if (!handle || handle->tracks.empty())
                throw std::runtime_error("empty shared snapshot");
            started = Clock::now();
            const auto descriptors = discover_application(application);
            discovery.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
            if (descriptors.actions.empty())
                throw std::runtime_error("empty discovery");
            BindingError binding_error;
            started = Clock::now();
            const auto resolved = bindings.resolve(tempo, binding_error);
            parameter_resolution.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
            if (!resolved) throw std::runtime_error("tempo binding unresolved");
        }
        const auto assignment_discovery_count =
            adapter.refresh_diagnostics().assignment_discovery_rebuilds;
        for (std::size_t i = 0; i < iterations; ++i) {
            const auto started = Clock::now();
            (void)adapter.refresh();
            idle.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
        }
        for (std::size_t i = 0; i < iterations; ++i) {
            const auto started = Clock::now();
            if (!adapter.select_page(i % 8) || !adapter.refresh())
                throw std::runtime_error("page selection failed");
            full.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
        }
        const auto play = application.execute({"transport.play.v1", {}, "benchmark", 0});
        if (!play.accepted)
            throw std::runtime_error(play.error);
        std::array<float, 64> left{}, right{};
        engine.process(left, right);
        (void)adapter.refresh();
        const auto full_count = adapter.refresh_diagnostics().full_rebuilds;
        for (std::size_t i = 0; i < iterations; ++i) {
            engine.process(left, right);
            const auto started = Clock::now();
            (void)adapter.refresh();
            incremental.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started).count()));
        }
        const auto diagnostics = adapter.refresh_diagnostics();
        if (diagnostics.assignment_discovery_rebuilds !=
            assignment_discovery_count)
            throw std::runtime_error(
                "page refresh repeated unchanged assignment discovery");
        if (diagnostics.telemetry_updates == 0)
            throw std::runtime_error("playback did not use telemetry publication");
        if (diagnostics.full_rebuilds != full_count)
            throw std::runtime_error("telemetry replaced structural page models");
        const auto idle_result = distribution(std::move(idle));
        const auto baseline = distribution(std::move(full));
        const auto optimized = distribution(std::move(incremental));
        const auto copy_result = distribution(std::move(snapshot_copy));
        const auto shared_result = distribution(std::move(snapshot_shared));
        const auto discovery_result = distribution(std::move(discovery));
        const auto resolution_result = distribution(std::move(parameter_resolution));
        std::cout << "neurogenic.slint-benchmark.v1 passed\n"
                  << "idle_ns median=" << idle_result.median
                  << " p95=" << idle_result.p95 << " p99=" << idle_result.p99
                  << " max=" << idle_result.maximum << '\n'
                  << "full_ns median=" << baseline.median << " p95=" << baseline.p95
                  << " p99=" << baseline.p99 << " max=" << baseline.maximum << '\n'
                  << "incremental_ns median=" << optimized.median
                  << " p95=" << optimized.p95 << " p99=" << optimized.p99
                  << " max=" << optimized.maximum << '\n'
                  << "snapshot_copy_ns median=" << copy_result.median
                  << " p95=" << copy_result.p95 << " p99=" << copy_result.p99
                  << " max=" << copy_result.maximum << '\n'
                  << "snapshot_shared_ns median=" << shared_result.median
                  << " p95=" << shared_result.p95 << " p99=" << shared_result.p99
                  << " max=" << shared_result.maximum << '\n'
                  << "discovery_ns median=" << discovery_result.median
                  << " p95=" << discovery_result.p95 << " p99=" << discovery_result.p99
                  << " max=" << discovery_result.maximum << '\n'
                  << "parameter_resolution_ns median=" << resolution_result.median
                  << " p95=" << resolution_result.p95 << " p99=" << resolution_result.p99
                  << " max=" << resolution_result.maximum << '\n'
                  << "full_rebuilds=" << diagnostics.full_rebuilds
                  << " telemetry_updates=" << diagnostics.telemetry_updates
                  << " skipped=" << diagnostics.skipped
                  << " assignment_discovery_rebuilds="
                  << diagnostics.assignment_discovery_rebuilds << '\n';
        samples.shutdown();
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "slint benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
