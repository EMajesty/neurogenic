#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neurogenic {

enum class StartupStage : std::uint8_t {
    idle,
    configuration,
    project_selection,
    project_restoration,
    audio,
    midi,
    controllers,
    gui,
    ready,
    shutting_down,
    stopped
};
[[nodiscard]] const char *startup_stage_name(StartupStage) noexcept;

struct LifecycleIssue {
    StartupStage stage{};
    std::string message;
    bool recoverable{};
};
struct LifecycleSnapshot {
    std::uint64_t revision{};
    StartupStage stage{StartupStage::idle};
    float progress{};
    bool ready{}, degraded{}, shutdown_cancelled{};
    std::vector<LifecycleIssue> issues;
};

enum class UnsavedShutdownChoice : std::uint8_t { save, discard, cancel };

class ApplianceLifecycle {
  public:
    void transition(StartupStage, float progress = 0.0F);
    void issue(StartupStage, std::string message, bool recoverable);
    void ready();
    [[nodiscard]] bool request_shutdown(bool dirty, UnsavedShutdownChoice,
                                        std::string &reason);
    void stopped();
    [[nodiscard]] LifecycleSnapshot snapshot() const { return state_; }

  private:
    LifecycleSnapshot state_;
};

struct AppliancePreferences {
    static constexpr std::uint32_t version = 1;
    bool reopen_last_project{};
    std::filesystem::path last_project;
    std::string audio_output, audio_input;
    std::uint16_t audio_input_channels{2}, audio_output_channels{2};
    std::uint32_t audio_buffer_frames{256};
    std::vector<std::string> midi_inputs, midi_outputs;
    bool fullscreen{};
    std::optional<float> ui_scale;
    bool operator==(const AppliancePreferences &) const = default;
};

[[nodiscard]] std::filesystem::path default_preferences_path();
[[nodiscard]] bool load_preferences(const std::filesystem::path &,
                                    AppliancePreferences &, std::string &error);
[[nodiscard]] bool save_preferences(const std::filesystem::path &,
                                    const AppliancePreferences &,
                                    std::string &error);
[[nodiscard]] std::filesystem::path select_startup_project(
    const AppliancePreferences &, const std::filesystem::path &explicit_project,
    std::string &warning);
[[nodiscard]] std::string lifecycle_json(const LifecycleSnapshot &);

} // namespace neurogenic
