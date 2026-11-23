#pragma once
#include "neurogenic/midi/routing.hpp"
#include <memory>
#include <string>
#include <vector>
namespace neurogenic {
struct MidiPortInfo {
    MidiPortId id{};
    std::string name;
    bool connected{};
};
class MidiOutput {
  public:
    MidiOutput();
    ~MidiOutput();
    MidiOutput(const MidiOutput &) = delete;
    MidiOutput &operator=(const MidiOutput &) = delete;
    [[nodiscard]] std::string backend() const;
    [[nodiscard]] std::vector<MidiPortInfo> outputs() const;
    bool connect(MidiPortId, const std::string &selector, std::string &error);
    bool connect_exact(MidiPortId, const std::string &selector,
                       std::string &error);
    void disconnect(MidiPortId) noexcept;
    bool send(const MidiPacket &, std::string &error) noexcept;
    std::size_t reconnect_missing(std::string &error) noexcept;
    [[nodiscard]] std::array<bool, maximum_midi_ports>
    connection_state() const noexcept;

  private:
    bool connect_impl(MidiPortId, const std::string &, bool, std::string &);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace neurogenic
