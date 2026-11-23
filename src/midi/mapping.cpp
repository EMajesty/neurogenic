#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/application/controller_session.hpp"
#include <algorithm>

namespace neurogenic {
void MidiMapper::map_button(std::uint8_t channel, std::uint8_t note,
                            std::string id, Action action) {
    buttons_[static_cast<std::uint16_t>((channel << 8) | note)] = {
        std::move(id), std::move(action)};
}

bool MidiMapper::receive(const MidiMessage &message) noexcept {
    if (!inbox_.push(message)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sequence_.fetch_add(1, std::memory_order_release);
    sequence_.notify_one();
    return true;
}

bool MidiMapper::dispatch_one() {
    MidiMessage message;
    if (!inbox_.pop(message))
        return false;
    if (raw_handler_)
        raw_handler_(message);
    const auto type = message.status & 0xF0;
    if (type != 0x90 && type != 0x80)
        return true;
    const bool note_on = type == 0x90 && message.data2 != 0;
    if (note_event_handler_)
        note_event_handler_(message.data1, message.data2 / 127.0F, note_on);
    if (note_on && note_handler_)
        note_handler_(message.data1, message.data2 / 127.0F);
    const auto key = static_cast<std::uint16_t>(((message.status & 15) << 8) |
                                                message.data1);
    if (note_on)
        if (auto it = buttons_.find(key); it != buttons_.end())
            it->second.action();
    return true;
}

std::size_t MidiMapper::dispatch_pending() {
    std::size_t count{};
    while (dispatch_one())
        ++count;
    return count;
}

bool MidiControlSurfaceAdapter::add(MidiControlBinding binding,
                                    std::string &error) {
    if (binding.channel > 15 || binding.number > 127 ||
        binding.control_id.empty()) {
        error = "MIDI control binding has an invalid channel, number or ID";
        return false;
    }
    const auto duplicate = std::find_if(
        bindings_.begin(), bindings_.end(), [&](const auto &value) {
            return value.port == binding.port && value.channel == binding.channel &&
                   value.number == binding.number && value.mode == binding.mode;
        });
    if (duplicate != bindings_.end()) {
        error = "MIDI control binding is ambiguous";
        return false;
    }
    bindings_.push_back(std::move(binding));
    error.clear();
    return true;
}

bool MidiControlSurfaceAdapter::dispatch(const MidiMessage &message,
                                         std::string &error) {
    const auto type = message.status & 0xf0;
    const auto channel = message.status & 0x0f;
    for (const auto &binding : bindings_) {
        const auto note = binding.mode == MidiControlMode::note_momentary;
        if (binding.port != message.port || binding.channel != channel ||
            binding.number != message.data1 ||
            (note ? (type != 0x80 && type != 0x90) : type != 0xb0))
            continue;
        ControllerInput input;
        input.control_id = binding.control_id;
        input.sequence = ++sequence_;
        if (note) {
            input.pressed = type == 0x90 && message.data2 != 0;
            input.value = message.data2 / 127.0;
        } else if (binding.mode == MidiControlMode::cc_absolute) {
            input.pressed = true;
            input.value = message.data2 / 127.0;
        } else if (binding.mode == MidiControlMode::cc_relative_twos_complement) {
            input.pressed = true;
            input.value = message.data2 == 64 ? 0
                          : message.data2 < 64 ? message.data2
                                               : message.data2 - 128;
        } else {
            input.pressed = true;
            input.value = static_cast<int>(message.data2) - 64;
        }
        return session_->input(input, error);
    }
    error.clear();
    return false;
}

void MidiControlSurfaceAdapter::reconnect() noexcept {
    if (session_) session_->reconnect();
}
} // namespace neurogenic
