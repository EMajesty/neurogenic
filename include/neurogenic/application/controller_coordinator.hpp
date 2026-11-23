#pragma once
#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/midi/mapping.hpp"
#include <functional>
#include <memory>

namespace neurogenic {
struct ControllerEndpointState {
    std::string id, transport, configured_path, expected_device, error;
    bool connected{};
    std::uint64_t reconnects{}, received{}, sent{}, dropped{};
    ControllerSessionState session;
};
class ControllerCoordinator {
  public:
    explicit ControllerCoordinator(ApplicationService &application);
    ~ControllerCoordinator();
    bool add_serial(std::string id, std::filesystem::path path,
                    std::string expected_device, std::string &error,
                    std::size_t queue_capacity = 128);
    bool add_midi(std::string id, MidiControllerProfile, MidiMapper &,
                  std::string &error);
    void poll(const ApplicationState &);
    std::size_t dispatch_midi_feedback(
        const std::function<bool(const MidiMessage &)> &sender);
    void reconnect_midi() noexcept;
    void disconnect_midi() noexcept;
    [[nodiscard]] std::vector<ControllerEndpointState> states() const;
    [[nodiscard]] MidiControlSurfaceAdapter *midi_surface(std::string_view id);
    [[nodiscard]] const MidiControllerProfile *midi_profile(std::string_view id) const;
    bool save_midi_profile(std::string_view id, const std::filesystem::path &,
                           std::string &error) const;
    void shutdown() noexcept;
  private:
    struct SerialEndpoint;
    struct MidiEndpoint;
    ApplicationService *application_{};
    std::vector<std::unique_ptr<SerialEndpoint>> serial_;
    std::vector<std::unique_ptr<MidiEndpoint>> midi_;
};
} // namespace neurogenic
