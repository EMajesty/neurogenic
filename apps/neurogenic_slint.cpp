#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <iostream>

int main(int argc, char **argv) {
    using namespace neurogenic;
    SlintOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--fullscreen")
            options.fullscreen = true;
        else if (argument == "--width" && i + 1 < argc)
            options.width = std::stoi(argv[++i]);
        else if (argument == "--height" && i + 1 < argc)
            options.height = std::stoi(argv[++i]);
        else if (argument == "--help") {
            std::cout << "usage: neurogenic-slint [--fullscreen] [--width N] "
                         "[--height N]\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            return 2;
        }
    }
    if (options.width < 1024 || options.height < 600) {
        std::cerr << "Slint frontend minimum viewport is 1024x600\n";
        return 2;
    }
    AudioEngine engine;
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    });
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    std::string error;
    const auto result = run_slint_frontend(application, options, error);
    samples.shutdown();
    if (result != 0)
        std::cerr << error << '\n';
    return result;
}
