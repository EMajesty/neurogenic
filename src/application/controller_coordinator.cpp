#include "neurogenic/application/controller_coordinator.hpp"
#include <algorithm>
#include <chrono>

namespace neurogenic {
struct ControllerCoordinator::SerialEndpoint {
    std::string id;
    std::filesystem::path path;
    std::string expected, error;
    ControllerHost host;
    ControllerSerialTransport transport;
    std::chrono::steady_clock::time_point retry{};
    SerialEndpoint(ApplicationService &application, std::string endpoint_id,
                   std::filesystem::path configured_path,
                   std::string expected_device, std::size_t capacity)
        : id(std::move(endpoint_id)), path(std::move(configured_path)),
          expected(std::move(expected_device)),
          host(application, capacity, expected), transport(capacity) {}
};
struct ControllerCoordinator::MidiEndpoint {
    std::string id, error;
    MidiControllerProfile profile;
    ControllerDeviceDescriptor descriptor;
    ControllerSession session;
    MidiControlSurfaceAdapter adapter;
    MidiMapper *mapper{};
    std::weak_ptr<void> mapper_lifetime;
    MidiEndpoint(ApplicationService &application, std::string endpoint_id,
                 MidiControllerProfile configured,
                 ControllerDeviceDescriptor device)
        : id(std::move(endpoint_id)), profile(std::move(configured)),
          descriptor(std::move(device)),
          session(application, descriptor, profile.controller, id, 128),
          adapter(session) {}
    ~MidiEndpoint() {
        if (mapper && !mapper_lifetime.expired())
            mapper->remove_control_surface(adapter);
    }
};
ControllerCoordinator::ControllerCoordinator(ApplicationService &application)
    : application_(&application) {}
ControllerCoordinator::~ControllerCoordinator() { shutdown(); }
bool ControllerCoordinator::add_serial(std::string endpoint_id,
                                       std::filesystem::path path,
                                       std::string expected_device,
                                       std::string &error,
                                       std::size_t queue_capacity) {
    const std::lock_guard lock(mutex_);
    if (endpoint_id.empty() || path.empty() ||
        std::any_of(serial_.begin(), serial_.end(), [&](const auto &value) {
            return value->id == endpoint_id || value->path == path;
        })) {
        error = "serial controller endpoint identity/path is empty or duplicate";
        return false;
    }
    auto endpoint = std::make_unique<SerialEndpoint>(
        *application_, std::move(endpoint_id), std::move(path),
        std::move(expected_device), queue_capacity);
    if (endpoint->transport.open(endpoint->path, endpoint->error))
        endpoint->host.connected();
    else
        endpoint->retry = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
    error = endpoint->error;
    serial_.push_back(std::move(endpoint));
    return true;
}
bool ControllerCoordinator::add_midi(std::string endpoint_id,
                                     MidiControllerProfile profile,
                                     MidiMapper &mapper, std::string &error) {
    const std::lock_guard lock(mutex_);
    if (endpoint_id.empty() ||
        std::any_of(midi_.begin(), midi_.end(), [&](const auto &value) {
            return value->id == endpoint_id;
        })) {
        error = "MIDI controller endpoint identity is empty or duplicate";
        return false;
    }
    ControllerDeviceDescriptor descriptor;
    descriptor.descriptor_id = "midi.profile." + profile.controller.profile_id;
    descriptor.device_id = profile.controller.device_id.empty()
                               ? profile.controller.capabilities.controller_id
                               : profile.controller.device_id;
    descriptor.display_name = profile.controller.capabilities.name;
    descriptor.transport = ControllerTransport::midi;
    descriptor.controls = profile.controller.capabilities.controls;
    auto endpoint = std::make_unique<MidiEndpoint>(
        *application_, std::move(endpoint_id), std::move(profile),
        std::move(descriptor));
    if (!endpoint->session.valid()) {
        error = endpoint->session.state().error;
        return false;
    }
    for (const auto &binding : endpoint->profile.midi)
        if (!endpoint->adapter.add(binding, error)) return false;
    if (!mapper.add_control_surface(endpoint->adapter)) {
        error = "MIDI controller adapter is already registered";
        return false;
    }
    endpoint->mapper = &mapper;
    endpoint->mapper_lifetime = mapper.lifetime_token();
    midi_.push_back(std::move(endpoint));
    error.clear();
    return true;
}
void ControllerCoordinator::poll(const ApplicationState &state) {
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (auto &endpoint : serial_) {
        if (!endpoint->transport.connected() && now >= endpoint->retry) {
            if (endpoint->transport.open(endpoint->path, endpoint->error))
                endpoint->host.connected();
            else {
                endpoint->host.disconnected(endpoint->error);
                endpoint->retry = now + std::chrono::seconds(1);
            }
        }
        if (endpoint->transport.connected()) {
            endpoint->host.publish(state);
            if (!endpoint->transport.poll(endpoint->host, endpoint->error)) {
                endpoint->host.disconnected(endpoint->error);
                endpoint->transport.close();
                endpoint->retry = now + std::chrono::seconds(1);
            }
        }
    }
    for (auto &endpoint : midi_) {
        endpoint->adapter.update_learn_timeout();
        endpoint->adapter.observe(state);
    }
}
std::size_t ControllerCoordinator::dispatch_midi_feedback(
    const std::function<bool(const MidiMessage &)> &sender) {
    const std::lock_guard lock(mutex_);
    std::size_t sent{};
    for (auto &endpoint : midi_)
        while (auto message = endpoint->adapter.next_feedback()) {
            if (!sender(*message)) {
                endpoint->adapter.requeue_feedback(*message);
                endpoint->error = "MIDI feedback send failed";
                break;
            }
            ++sent;
        }
    return sent;
}
void ControllerCoordinator::reconnect_midi() noexcept {
    const std::lock_guard lock(mutex_);
    for (auto &endpoint : midi_) endpoint->adapter.reconnect();
}
void ControllerCoordinator::disconnect_midi() noexcept {
    const std::lock_guard lock(mutex_);
    for (auto &endpoint : midi_) endpoint->adapter.disconnect();
}
std::vector<ControllerEndpointState> ControllerCoordinator::states() const {
    const std::lock_guard lock(mutex_);
    std::vector<ControllerEndpointState> result;
    for (const auto &endpoint : serial_) {
        const auto state = endpoint->host.snapshot();
        result.push_back({endpoint->id, "usb-cdc", endpoint->path.string(),
                          endpoint->expected, endpoint->error,
                          state.connection == ControllerConnection::connected,
                          state.reconnects, state.received, state.sent,
                          state.dropped_input + state.dropped_output,
                          endpoint->host.session_snapshot()});
    }
    for (const auto &endpoint : midi_) {
        const auto state = endpoint->adapter.session_state();
        result.push_back({endpoint->id, "midi", endpoint->profile.port_selector,
                          endpoint->descriptor.device_id, endpoint->error, true,
                          0, state.received, 0,
                          state.rejected + state.dropped_feedback, state});
    }
    return result;
}
MidiControlSurfaceAdapter *ControllerCoordinator::midi_surface(std::string_view id) {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(midi_.begin(), midi_.end(),
                                    [&](const auto &value) { return value->id == id; });
    return found == midi_.end() ? nullptr : &(*found)->adapter;
}
const MidiControllerProfile *
ControllerCoordinator::midi_profile(std::string_view id) const {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(midi_.begin(), midi_.end(),
                                    [&](const auto &value) { return value->id == id; });
    return found == midi_.end() ? nullptr : &(*found)->profile;
}
bool ControllerCoordinator::save_midi_profile(
    std::string_view id, const std::filesystem::path &path,
    std::string &error) const {
    MidiControllerProfile updated;
    {
        const std::lock_guard lock(mutex_);
        const auto found = std::find_if(
            midi_.begin(), midi_.end(),
            [&](const auto &value) { return value->id == id; });
        if (found == midi_.end()) {
            error = "unknown MIDI controller endpoint";
            return false;
        }
        updated = (*found)->profile;
        updated.midi = (*found)->adapter.bindings();
    }
    return save_midi_controller_profile(path, updated, error);
}
bool ControllerCoordinator::begin_midi_learn(
    std::string_view id, std::string control_id, MidiControlMode mode,
    std::chrono::milliseconds timeout, std::string &error) {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(midi_.begin(), midi_.end(),
                                    [&](const auto &value) { return value->id == id; });
    if (found == midi_.end()) {
        error = "unknown MIDI controller endpoint";
        return false;
    }
    return (*found)->adapter.begin_learn(std::move(control_id), mode, timeout,
                                         error);
}
bool ControllerCoordinator::cancel_midi_learn(std::string_view id,
                                               std::string &error) {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(midi_.begin(), midi_.end(),
                                    [&](const auto &value) { return value->id == id; });
    if (found == midi_.end()) {
        error = "unknown MIDI controller endpoint";
        return false;
    }
    (*found)->adapter.cancel_learn();
    error.clear();
    return true;
}
void ControllerCoordinator::shutdown() noexcept {
    const std::lock_guard lock(mutex_);
    for (auto &endpoint : serial_) {
        endpoint->host.disconnected("application shutdown");
        endpoint->transport.close();
    }
    for (auto &endpoint : midi_) {
        endpoint->adapter.disconnect();
        if (endpoint->mapper && !endpoint->mapper_lifetime.expired()) {
            endpoint->mapper->remove_control_surface(endpoint->adapter);
        }
        endpoint->mapper = nullptr;
        endpoint->mapper_lifetime.reset();
    }
}
} // namespace neurogenic
