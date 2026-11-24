#include "neurogenic/midi/output.hpp"
#ifdef NEUROGENIC_HAS_LIBREMIDI
#include <libremidi/libremidi.hpp>
#endif
#ifdef NEUROGENIC_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
#include <algorithm>
#include <array>

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
struct MidiOutput::Impl {
#ifdef NEUROGENIC_HAS_LIBREMIDI
#ifdef __linux__
    libremidi::observer observer{
        libremidi::observer_configuration{.track_any = true},
        libremidi::observer_configuration_for(libremidi::API::ALSA_SEQ)};
#else
    libremidi::observer observer;
#endif
    std::array<std::unique_ptr<libremidi::midi_out>, maximum_midi_ports> ports;
    std::array<std::string, maximum_midi_ports> configured_names;
#endif
};
MidiOutput::MidiOutput() : impl_(std::make_unique<Impl>()) {}
MidiOutput::~MidiOutput() = default;
std::string MidiOutput::backend() const {
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
std::vector<MidiPortInfo> MidiOutput::outputs() const {
    std::vector<MidiPortInfo> result;
#ifdef NEUROGENIC_HAS_LIBREMIDI
    const auto ports = impl_->observer.get_output_ports();
    for (std::size_t i = 0; i < std::min(ports.size(), maximum_midi_ports); ++i)
        result.push_back(
            {static_cast<MidiPortId>(i + 1),
             ports[i].display_name.empty() ? ports[i].port_name
                                           : ports[i].display_name,
             impl_->ports[i] && impl_->ports[i]->is_port_connected()});
#endif
    return result;
}
bool MidiOutput::connect(MidiPortId logical, const std::string &selector,
                         std::string &error) {
    return connect_impl(logical, selector, false, error);
}
bool MidiOutput::connect_exact(MidiPortId logical, const std::string &selector,
                               std::string &error) {
    return connect_impl(logical, selector, true, error);
}
bool MidiOutput::connect_impl(MidiPortId logical, const std::string &selector,
                              bool exact, std::string &error) {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    if (!logical || logical > maximum_midi_ports) {
        error = "invalid logical MIDI output port";
        return false;
    }
    impl_->ports[logical - 1].reset();
    impl_->configured_names[logical - 1].clear();
    if (impl_->observer.get_current_api() == libremidi::API::DUMMY) {
        error =
            alsa_compiled()
                ? "ALSA sequencer backend was compiled, but is unavailable "
                  "(is /dev/snd/seq accessible?)"
                : "libremidi was compiled without the ALSA sequencer backend";
        return false;
    }
    const auto available = impl_->observer.get_output_ports();
    auto selected = available.end();
    const bool numeric =
        !exact && !selector.empty() &&
        std::all_of(selector.begin(), selector.end(),
                    [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (numeric) {
        const auto index = std::stoul(selector);
        if (index < available.size())
            selected = available.begin() + index;
    } else
        selected = std::find_if(
            available.begin(), available.end(), [&](const auto &port) {
                const auto name = port.display_name.empty() ? port.port_name
                                                            : port.display_name;
                return selector.empty() ||
                       (exact ? name == selector
                              : port.port_name.find(selector) !=
                                        std::string::npos ||
                                    port.display_name.find(selector) !=
                                        std::string::npos);
            });
    if (selected == available.end()) {
        error = available.empty() ? "no MIDI outputs discovered"
                : exact           ? "no exact MIDI output match"
                                  : "no matching MIDI output";
        return false;
    }
    try {
        libremidi::output_configuration configuration;
        auto output = std::make_unique<libremidi::midi_out>(
            configuration,
            libremidi::midi_out_configuration_for(impl_->observer));
        auto status = output->open_port(*selected);
        if (status != stdx::error{})
            status.throw_exception();
        impl_->ports[logical - 1] = std::move(output);
        impl_->configured_names[logical - 1] = selected->port_name;
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error =
            "MIDI output could not be opened: " + std::string(exception.what());
        return false;
    }
#else
    (void)logical;
    (void)selector;
    error = "libremidi support was not built";
    return false;
#endif
}
void MidiOutput::disconnect(MidiPortId logical) noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    if (logical && logical <= maximum_midi_ports) {
        impl_->ports[logical - 1].reset();
        impl_->configured_names[logical - 1].clear();
    }
#else
    (void)logical;
#endif
}
bool MidiOutput::send(const MidiPacket &packet, std::string &error) noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    if (!packet.port || packet.port > maximum_midi_ports ||
        !impl_->ports[packet.port - 1]) {
        error = "logical MIDI output is disconnected";
        return false;
    }
    const auto status = impl_->ports[packet.port - 1]->send_message(
        packet.bytes.data(), packet.size);
    if (status != stdx::error{}) {
        error = "MIDI output transmission failed";
        impl_->ports[packet.port - 1].reset();
        return false;
    }
    error.clear();
    return true;
#else
    (void)packet;
    error = "libremidi support was not built";
    return false;
#endif
}
std::size_t MidiOutput::reconnect_missing(std::string &error) noexcept {
#ifdef NEUROGENIC_HAS_LIBREMIDI
    std::size_t connected{};
    const auto available = impl_->observer.get_output_ports();
    for (std::size_t i = 0; i < impl_->ports.size(); ++i) {
        if (impl_->ports[i] && !impl_->ports[i]->is_port_connected())
            impl_->ports[i].reset();
        if (impl_->ports[i] || impl_->configured_names[i].empty())
            continue;
        const auto found = std::find_if(
            available.begin(), available.end(), [&](const auto &port) {
                return port.port_name == impl_->configured_names[i];
            });
        if (found == available.end())
            continue;
        try {
            libremidi::output_configuration configuration;
            auto output = std::make_unique<libremidi::midi_out>(
                configuration,
                libremidi::midi_out_configuration_for(impl_->observer));
            auto status = output->open_port(*found);
            if (status != stdx::error{})
                status.throw_exception();
            impl_->ports[i] = std::move(output);
            ++connected;
        } catch (const std::exception &exception) {
            error = "MIDI output reconnect failed: " +
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
std::array<bool, maximum_midi_ports>
MidiOutput::connection_state() const noexcept {
    std::array<bool, maximum_midi_ports> result{};
#ifdef NEUROGENIC_HAS_LIBREMIDI
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = impl_->ports[i] && impl_->ports[i]->is_port_connected();
#endif
    return result;
}
} // namespace neurogenic
