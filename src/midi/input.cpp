#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/routing.hpp"
#ifdef NEUROGENIC_HAS_LIBREMIDI
#include <libremidi/libremidi.hpp>
#endif
#ifdef NEUROGENIC_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>

namespace neurogenic {
namespace {
#ifdef NEUROGENIC_HAS_LIBREMIDI
bool alsa_compiled() {
#if defined(LIBREMIDI_ALSA)
    return true;
#else
    return false;
#endif
}
#endif
} // namespace
struct MidiInput::Impl {
    MidiMapper &mapper;
#ifdef NEUROGENIC_HAS_LIBREMIDI
#ifdef __linux__
    libremidi::observer observer{
        libremidi::observer_configuration{.track_any = true},
        libremidi::observer_configuration_for(libremidi::API::ALSA_SEQ)};
#else
    libremidi::observer observer;
#endif
    std::array<std::unique_ptr<libremidi::midi_in>, maximum_midi_ports> inputs;
    std::array<std::string, maximum_midi_ports> configured_names;
#endif
    explicit Impl(MidiMapper &value) : mapper(value) {}
};
MidiInput::MidiInput(MidiMapper &mapper)
    : impl_(std::make_unique<Impl>(mapper)) {}
MidiInput::~MidiInput() { close(); }
std::string MidiInput::backend() const {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    const auto api = impl_->observer.get_current_api();
    if (api == libremidi::API::DUMMY) {
        if (!alsa_compiled())
            return "not compiled (ALSA sequencer backend missing)";
#ifdef NEUROGENIC_HAS_ALSA
        return "ALSA sequencer compiled and libasound " +
               std::string(snd_asoundlib_version()) +
               " linked, but unavailable";
#else
        return "ALSA sequencer compiled, but runtime library unavailable";
#endif
    }
    return std::string(libremidi::get_api_display_name(api));
#else
    return "not compiled (configure with NEUROGENIC_FETCH_DEPENDENCIES=ON)";
#endif
}
std::vector<std::string> MidiInput::inputs() const {
    std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_LIBREMIDI
    for (const auto &port : impl_->observer.get_input_ports())
        result.push_back(port.display_name.empty() ? port.port_name
                                                   : port.display_name);
#endif
    return result;
}
bool MidiInput::open(const std::string &selector, std::string &error) {
    return connect(1, selector, error);
}
bool MidiInput::connect(std::uint32_t logical, const std::string &selector,
                        std::string &error) {
    return connect_impl(logical, selector, false, error);
}
bool MidiInput::connect_exact(std::uint32_t logical,
                              const std::string &selector,
                              std::string &error) {
    return connect_impl(logical, selector, true, error);
}
bool MidiInput::connect_impl(std::uint32_t logical, const std::string &selector,
                             bool exact, std::string &error) {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    if (!logical || logical > maximum_midi_ports) {
        error = "invalid logical MIDI input port";
        return false;
    }
    impl_->inputs[logical - 1].reset();
    impl_->configured_names[logical - 1].clear();
    try {
        if (impl_->observer.get_current_api() == libremidi::API::DUMMY) {
            error = alsa_compiled()
                        ? "ALSA sequencer backend was compiled, but is "
                          "unavailable (is /dev/snd/seq accessible?)"
                        : "libremidi was compiled without the ALSA sequencer "
                          "backend";
            return false;
        }
        const auto ports = impl_->observer.get_input_ports();
        auto selected = ports.end();
        const bool numeric = !exact &&
            !selector.empty() &&
            std::all_of(selector.begin(), selector.end(),
                        [](unsigned char c) { return c >= '0' && c <= '9'; });
        if (numeric) {
            const auto index = std::stoul(selector);
            if (index < ports.size())
                selected = ports.begin() + index;
        } else
            selected =
                std::find_if(ports.begin(), ports.end(), [&](const auto &port) {
                    const auto name = port.display_name.empty()
                                          ? port.port_name
                                          : port.display_name;
                    return selector.empty() ||
                           (exact ? name == selector
                                  : port.port_name.find(selector) !=
                                            std::string::npos ||
                                        port.display_name.find(selector) !=
                                            std::string::npos);
                });
        if (selected == ports.end()) {
            error = ports.empty()
                        ? "ALSA sequencer backend is active, but no MIDI "
                          "inputs were discovered"
                        : exact ? "ALSA sequencer backend found no exact MIDI "
                                  "input match"
                                : "ALSA sequencer backend found no matching "
                                  "MIDI input";
            return false;
        }
        libremidi::input_configuration configuration;
        configuration.on_message = [this, logical](
                                       const libremidi::message &message) {
            if (message.empty())
                return;
            const auto size = static_cast<std::uint8_t>(
                std::min<std::size_t>(message.size(), 3));
            impl_->mapper.receive(
                {message[0], size > 1 ? message[1] : std::uint8_t{},
                 size > 2 ? message[2] : std::uint8_t{},
                 static_cast<std::uint64_t>(
                     std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()),
                 logical, size});
        };
        auto input = std::make_unique<libremidi::midi_in>(
            configuration,
            libremidi::midi_in_configuration_for(impl_->observer));
        auto status = input->open_port(*selected);
        if (status != stdx::error{})
            status.throw_exception();
        impl_->inputs[logical - 1] = std::move(input);
        // Preserve the backend port identity for reconnect; display names are
        // not guaranteed unique and must not cause substitution.
        impl_->configured_names[logical - 1] = selected->port_name;
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error = "MIDI device was discovered but could not be opened: " +
                std::string(exception.what());
        impl_->inputs[logical - 1].reset();
        return false;
    }
#else
    (void)logical;
    (void)selector;
    error = "libremidi support was not built";
    return false;
#endif
}
void MidiInput::disconnect(std::uint32_t logical) noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    if (logical && logical <= maximum_midi_ports) {
        impl_->inputs[logical - 1].reset();
        impl_->configured_names[logical - 1].clear();
    }
#else
    (void)logical;
#endif
}
std::size_t MidiInput::reconnect_missing(std::string &error) noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    std::size_t connected{};
    const auto available = impl_->observer.get_input_ports();
    for (std::size_t i = 0; i < impl_->inputs.size(); ++i) {
        if (impl_->inputs[i] && !impl_->inputs[i]->is_port_connected())
            impl_->inputs[i].reset();
        if (impl_->inputs[i] || impl_->configured_names[i].empty())
            continue;
        const auto found = std::find_if(available.begin(), available.end(),
                                        [&](const auto &port) {
            return port.port_name == impl_->configured_names[i];
        });
        if (found == available.end())
            continue;
        try {
            libremidi::input_configuration configuration;
            configuration.on_message = [this, logical = i + 1](
                                           const libremidi::message &message) {
                if (message.empty())
                    return;
                const auto size = static_cast<std::uint8_t>(
                    std::min<std::size_t>(message.size(), 3));
                impl_->mapper.receive(
                    {message[0], size > 1 ? message[1] : std::uint8_t{},
                     size > 2 ? message[2] : std::uint8_t{},
                     static_cast<std::uint64_t>(
                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count()),
                     static_cast<std::uint32_t>(logical), size});
            };
            auto input = std::make_unique<libremidi::midi_in>(
                configuration,
                libremidi::midi_in_configuration_for(impl_->observer));
            auto status = input->open_port(*found);
            if (status != stdx::error{})
                status.throw_exception();
            impl_->inputs[i] = std::move(input);
            ++connected;
        } catch (const std::exception &exception) {
            error = "MIDI input reconnect failed: " +
                    std::string(exception.what());
        }
    }
    if (connected)
        error.clear();
    return connected;
#else
    error = "libremidi support was not built";
    return 0;
#endif
}
std::array<bool, 8> MidiInput::connection_state() const noexcept {
    std::array<bool, 8> result{};
#ifdef NEUROGENIC_HAS_LIBREMIDI
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = impl_->inputs[i] && impl_->inputs[i]->is_port_connected();
#endif
    return result;
}
void MidiInput::close() noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    for (auto &input : impl_->inputs)
        input.reset();
#endif
}
} // namespace neurogenic
