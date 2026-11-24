#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    using namespace neurogenic;
    try {
        if (argc != 4)
            throw std::runtime_error("expected width height screenshot-path");
        AudioEngine engine{48'000, 64};
        SampleManager samples{engine};
        Timeline timeline_data;
        timeline_data.tracks.push_back({1, "Rendered Track"});
        timeline_data.tracks.front().pattern.length =
            ticks_per_quarter * 4 + ticks_per_quarter / 4;
        timeline_data.tracks.front().pattern.events.push_back(
            {1, ticks_per_quarter * 4, MusicalEventType::note, 60, 0.8F,
             ticks_per_quarter / 4});
        PatternDefinition rendered_pattern;
        rendered_pattern.id = 70;
        rendered_pattern.name = "Rendered Pattern";
        rendered_pattern.length = ticks_per_quarter * 4;
        rendered_pattern.tracks.push_back(
            {1, timeline_data.tracks.front().pattern});
        timeline_data.arrangement.patterns.push_back(rendered_pattern);
        timeline_data.arrangement.launch_slots.push_back(
            {77, 1, LaunchSourceType::pattern, 70, LaunchBehavior::loop,
             LaunchQuantization::bar});
        timeline_data.arrangement.launch_slots.back().logical_position = 0;
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, timeline_data);
        ApplicationService application(engine, timeline, samples);
        SlintOptions options;
        options.width = std::stoi(argv[1]);
        options.height = std::stoi(argv[2]);
        options.interaction_test = true;
        options.screenshot_path = argv[3];
        std::string error;
        if (run_slint_frontend(application, options, error) != 0)
            throw std::runtime_error(error);
        if (!std::filesystem::exists(options.screenshot_path) ||
            std::filesystem::file_size(options.screenshot_path) < 1024)
            throw std::runtime_error("render artifact is empty");
        const auto publication = slint_publication_diagnostics();
        if (publication.model_creations == 0 ||
            publication.model_row_updates == 0)
            throw std::runtime_error("generated models were not incrementally published");
        std::cout << "neurogenic.slint-rendered-tests.v1 passed "
                  << options.width << 'x' << options.height
                  << " model_creations=" << publication.model_creations
                  << " model_resets=" << publication.model_resets
                  << " model_row_updates=" << publication.model_row_updates << '\n';
        samples.shutdown();
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "slint rendered test failed: " << exception.what() << '\n';
        return 1;
    }
}
