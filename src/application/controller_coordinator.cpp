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
    mutable std::mutex operation_mutex, state_mutex;
    SerialEndpoint(ApplicationService &application, std::string endpoint_id,
                   std::filesystem::path configured_path,
                   std::string expected_device, std::size_t capacity)
        : id(std::move(endpoint_id)), path(std::move(configured_path)),
          expected(std::move(expected_device)),
          host(application, capacity, expected), transport(capacity) {}
    void set_error(std::string value) {
        std::lock_guard lock(state_mutex); error = std::move(value);
    }
    std::string get_error() const {
        std::lock_guard lock(state_mutex); return error;
    }
};
struct ControllerCoordinator::MidiEndpoint {
    std::string id, error;
    MidiControllerProfile profile;
    ControllerDeviceDescriptor descriptor;
    ControllerSession session;
    std::shared_ptr<MidiControlSurfaceAdapter> adapter;
    MidiMapper *mapper{};
    std::weak_ptr<void> mapper_lifetime;
    mutable std::mutex state_mutex;
    MidiEndpoint(ApplicationService &application, std::string endpoint_id,
                 MidiControllerProfile configured,
                 ControllerDeviceDescriptor device)
        : id(std::move(endpoint_id)), profile(std::move(configured)),
          descriptor(std::move(device)),
          session(application, descriptor, profile.controller, id, 128),
          adapter(std::make_shared<MidiControlSurfaceAdapter>(session)) {}
    ~MidiEndpoint() {
        if (mapper && !mapper_lifetime.expired())
            mapper->remove_control_surface(adapter);
    }
    void set_error(std::string value) {
        std::lock_guard lock(state_mutex); error = std::move(value);
    }
    std::string get_error() const {
        std::lock_guard lock(state_mutex); return error;
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
    if (endpoint_id.empty() || path.empty()) {
        error = "serial controller endpoint identity/path is empty or duplicate";
        return false;
    }
    auto endpoint = std::make_shared<SerialEndpoint>(
        *application_, std::move(endpoint_id), std::move(path),
        std::move(expected_device), queue_capacity);
    if (endpoint->transport.open(endpoint->path, endpoint->error))
        endpoint->host.connected();
    else
        endpoint->retry = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
    error = endpoint->error;
    bool duplicate{};
    {
        const std::lock_guard lock(mutex_);
        duplicate = std::any_of(serial_.begin(), serial_.end(), [&](const auto &value) {
                return value->id == endpoint->id || value->path == endpoint->path;
            });
        if (!duplicate) serial_.push_back(endpoint);
    }
    if (duplicate) {
        endpoint->transport.close();
        error = "serial controller endpoint identity/path is empty or duplicate";
        return false;
    }
    return true;
}
bool ControllerCoordinator::add_midi(std::string endpoint_id,
                                     MidiControllerProfile profile,
                                     MidiMapper &mapper, std::string &error) {
    if (endpoint_id.empty()) {
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
    auto endpoint = std::make_shared<MidiEndpoint>(
        *application_, std::move(endpoint_id), std::move(profile),
        std::move(descriptor));
    if (!endpoint->session.valid()) {
        error = endpoint->session.state().error;
        return false;
    }
    for (const auto &binding : endpoint->profile.midi)
        if (!endpoint->adapter->add(binding, error)) return false;
    if (!mapper.add_control_surface(endpoint->adapter)) {
        error = "MIDI controller adapter is already registered";
        return false;
    }
    endpoint->mapper = &mapper;
    endpoint->mapper_lifetime = mapper.lifetime_token();
    bool duplicate{};
    {
        const std::lock_guard lock(mutex_);
        duplicate = std::any_of(midi_.begin(), midi_.end(), [&](const auto &value) {
                return value->id == endpoint->id;
            });
        if (!duplicate) midi_.push_back(endpoint);
    }
    if (duplicate) {
        mapper.remove_control_surface(endpoint->adapter);
        error = "MIDI controller endpoint identity is empty or duplicate";
        return false;
    }
    error.clear();
    return true;
}
void ControllerCoordinator::poll(const ApplicationState &state) {
    std::vector<std::shared_ptr<SerialEndpoint>> serial;
    std::vector<std::shared_ptr<MidiEndpoint>> midi;
    {
        const std::lock_guard lock(mutex_);
        serial = serial_;
        midi = midi_;
    }
    const auto now = std::chrono::steady_clock::now();
    for (auto &endpoint : serial) {
        const std::lock_guard operation(endpoint->operation_mutex);
        auto endpoint_error = endpoint->get_error();
        if (!endpoint->transport.connected() && now >= endpoint->retry) {
            if (endpoint->transport.open(endpoint->path, endpoint_error))
                endpoint->host.connected();
            else {
                endpoint->host.disconnected(endpoint_error);
                endpoint->retry = now + std::chrono::seconds(1);
            }
        }
        if (endpoint->transport.connected()) {
            endpoint->host.publish(state);
            if (!endpoint->transport.poll(endpoint->host, endpoint_error)) {
                endpoint->host.disconnected(endpoint_error);
                endpoint->transport.close();
                endpoint->retry = now + std::chrono::seconds(1);
            }
        }
        endpoint->set_error(std::move(endpoint_error));
    }
    for (auto &endpoint : midi) {
        endpoint->adapter->update_learn_timeout();
        endpoint->adapter->observe(state);
    }
}
std::size_t ControllerCoordinator::dispatch_midi_feedback(
    const std::function<bool(const MidiMessage &)> &sender) {
    std::vector<std::shared_ptr<MidiEndpoint>> endpoints;
    {
        const std::lock_guard lock(mutex_);
        endpoints = midi_;
    }
    std::size_t sent{};
    for (auto &endpoint : endpoints)
        while (auto message = endpoint->adapter->next_feedback()) {
            if (!sender(*message)) {
                endpoint->adapter->requeue_feedback(*message);
                endpoint->set_error("MIDI feedback send failed");
                break;
            }
            ++sent;
        }
    return sent;
}
void ControllerCoordinator::reconnect_midi() noexcept {
    std::vector<std::shared_ptr<MidiEndpoint>> endpoints;
    { const std::lock_guard lock(mutex_); endpoints = midi_; }
    for (auto &endpoint : endpoints) endpoint->adapter->reconnect();
}
void ControllerCoordinator::disconnect_midi() noexcept {
    std::vector<std::shared_ptr<MidiEndpoint>> endpoints;
    { const std::lock_guard lock(mutex_); endpoints = midi_; }
    for (auto &endpoint : endpoints) endpoint->adapter->disconnect();
}
std::vector<ControllerEndpointState> ControllerCoordinator::states() const {
    const std::lock_guard lock(mutex_);
    std::vector<ControllerEndpointState> result;
    for (const auto &endpoint : serial_) {
        const auto state = endpoint->host.snapshot();
        result.push_back({endpoint->id, "usb-cdc", endpoint->path.string(),
                          endpoint->expected, endpoint->get_error(),
                          state.connection == ControllerConnection::connected,
                          state.reconnects, state.received, state.sent,
                          state.dropped_input + state.dropped_output,
                          endpoint->host.session_snapshot()});
    }
    for (const auto &endpoint : midi_) {
        const auto state = endpoint->adapter->session_state();
        result.push_back({endpoint->id, "midi", endpoint->profile.port_selector,
                          endpoint->descriptor.device_id, endpoint->get_error(), true,
                          0, state.received, 0,
                          state.rejected + state.dropped_feedback, state});
    }
    return result;
}
std::optional<MidiControllerProfile>
ControllerCoordinator::midi_profile(std::string_view id) const {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(midi_.begin(), midi_.end(),
                                    [&](const auto &value) { return value->id == id; });
    if (found == midi_.end()) return {};
    auto result = (*found)->profile;
    result.midi = (*found)->adapter->bindings();
    return result;
}
std::optional<MidiLearnStatus>
ControllerCoordinator::midi_learn_status(std::string_view id) const {
    std::shared_ptr<MidiEndpoint> endpoint;
    {
        const std::lock_guard lock(mutex_);
        const auto found = std::find_if(midi_.begin(), midi_.end(),
            [&](const auto &value) { return value->id == id; });
        if (found == midi_.end()) return {};
        endpoint = *found;
    }
    return endpoint->adapter->learn_status();
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
        updated.midi = (*found)->adapter->bindings();
    }
    return save_midi_controller_profile(path, updated, error);
}
bool ControllerCoordinator::begin_midi_learn(
    std::string_view id, std::string control_id, MidiControlMode mode,
    std::chrono::milliseconds timeout, std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    { const std::lock_guard lock(mutex_);
      const auto found = std::find_if(midi_.begin(), midi_.end(),
          [&](const auto &value) { return value->id == id; });
      if (found != midi_.end()) endpoint = *found; }
    if (!endpoint) {
        error = "unknown MIDI controller endpoint";
        return false;
    }
    return endpoint->adapter->begin_learn(std::move(control_id), mode, timeout,
                                          error);
}
bool ControllerCoordinator::cancel_midi_learn(std::string_view id,
                                               std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    { const std::lock_guard lock(mutex_);
      const auto found = std::find_if(midi_.begin(), midi_.end(),
          [&](const auto &value) { return value->id == id; });
      if (found != midi_.end()) endpoint = *found; }
    if (!endpoint) {
        error = "unknown MIDI controller endpoint";
        return false;
    }
    endpoint->adapter->cancel_learn();
    error.clear();
    return true;
}
bool ControllerCoordinator::confirm_midi_learn(std::string_view id,
                                                std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    { const std::lock_guard lock(mutex_);
      const auto found = std::find_if(midi_.begin(), midi_.end(),
          [&](const auto &value) { return value->id == id; });
      if (found != midi_.end()) endpoint = *found; }
    if (!endpoint) { error = "unknown MIDI controller endpoint"; return false; }
    return endpoint->adapter->confirm_learn(error);
}
bool ControllerCoordinator::set_midi_binding(std::string_view id,
                                              std::size_t index,
                                              MidiControlBinding binding,
                                              std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    { const std::lock_guard lock(mutex_);
      const auto found = std::find_if(midi_.begin(), midi_.end(),
          [&](const auto &value) { return value->id == id; });
      if (found != midi_.end()) endpoint = *found; }
    if (!endpoint) { error = "unknown MIDI controller endpoint"; return false; }
    const auto count = endpoint->adapter->bindings().size();
    const auto accepted = index == count
        ? endpoint->adapter->add(std::move(binding), error)
        : endpoint->adapter->replace(index, std::move(binding), error);
    if (accepted) endpoint->session.reconnect();
    return accepted;
}
bool ControllerCoordinator::remove_midi_binding(std::string_view id,
                                                 std::size_t index,
                                                 std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    { const std::lock_guard lock(mutex_);
      const auto found = std::find_if(midi_.begin(), midi_.end(),
          [&](const auto &value) { return value->id == id; });
      if (found != midi_.end()) endpoint = *found; }
    if (!endpoint) { error = "unknown MIDI controller endpoint"; return false; }
    const auto accepted = endpoint->adapter->remove(index, error);
    if (accepted) endpoint->session.reconnect();
    return accepted;
}

bool ControllerCoordinator::set_controller_binding(std::string_view id,
                                                   std::size_t index,
                                                   ControlBinding binding,
                                                   std::string &error) {
    std::shared_ptr<MidiEndpoint> endpoint;
    {
        const std::lock_guard lock(mutex_);
        const auto found = std::find_if(midi_.begin(), midi_.end(),
            [&](const auto &value) { return value->id == id; });
        if (found != midi_.end()) endpoint = *found;
    }
    if (!endpoint) {
        error = "unknown MIDI controller endpoint";
        return false;
    }
    if (!endpoint->session.set_binding(index, std::move(binding), error))
        return false;
    endpoint->profile.controller = endpoint->session.profile();
    return true;
}
void ControllerCoordinator::shutdown() noexcept {
    std::vector<std::shared_ptr<SerialEndpoint>> serial;
    std::vector<std::shared_ptr<MidiEndpoint>> midi;
    {
        const std::lock_guard lock(mutex_);
        serial.swap(serial_);
        midi.swap(midi_);
    }
    for (auto &endpoint : serial) {
        const std::lock_guard operation(endpoint->operation_mutex);
        endpoint->host.disconnected("application shutdown");
        endpoint->transport.close();
    }
    for (auto &endpoint : midi) {
        endpoint->adapter->disconnect();
        if (endpoint->mapper && !endpoint->mapper_lifetime.expired()) {
            endpoint->mapper->remove_control_surface(endpoint->adapter);
        }
        endpoint->mapper = nullptr;
        endpoint->mapper_lifetime.reset();
    }
}
} // namespace neurogenic
