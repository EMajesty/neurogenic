#pragma once
#include "neurogenic/midi/mapping.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace neurogenic { class MidiInput { public:explicit MidiInput(MidiMapper&);~MidiInput();MidiInput(const MidiInput&)=delete;MidiInput& operator=(const MidiInput&)=delete;std::string backend() const;std::vector<std::string> inputs() const;bool open(const std::string& selector,std::string& error);bool connect(std::uint32_t logical_port,const std::string& selector,std::string& error);bool connect_exact(std::uint32_t logical_port,const std::string& selector,std::string& error);std::size_t reconnect_missing(std::string& error) noexcept;std::array<bool,8> connection_state() const noexcept;void disconnect(std::uint32_t logical_port) noexcept;void close() noexcept;private:bool connect_impl(std::uint32_t,const std::string&,bool,std::string&);struct Impl;std::unique_ptr<Impl> impl_;}; }
