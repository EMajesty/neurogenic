#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/application/controller_session.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace neurogenic {
namespace {
std::string_view mode_name(MidiControlMode mode) {
    switch (mode) {
    case MidiControlMode::note_momentary: return "note";
    case MidiControlMode::cc_absolute: return "cc-absolute";
    case MidiControlMode::cc_relative_twos_complement: return "cc-relative-2c";
    case MidiControlMode::cc_relative_binary_offset: return "cc-relative-offset";
    }
    return "cc-absolute";
}
std::optional<MidiControlMode> parse_mode(std::string_view value) {
    if (value == "note") return MidiControlMode::note_momentary;
    if (value == "cc-absolute") return MidiControlMode::cc_absolute;
    if (value == "cc-relative-2c")
        return MidiControlMode::cc_relative_twos_complement;
    if (value == "cc-relative-offset")
        return MidiControlMode::cc_relative_binary_offset;
    return {};
}
std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    while (true) {
        const auto split = line.find('|');
        result.push_back(line.substr(0, split));
        if (split == std::string_view::npos) break;
        line.remove_prefix(split + 1);
    }
    return result;
}
bool parse_u32(std::string_view text, std::uint32_t &value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace
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
    std::string surface_error;
    const auto consumed = std::any_of(
        control_surfaces_.begin(), control_surfaces_.end(),
        [&](auto *surface) { return surface->dispatch(message, surface_error); });
    if (consumed) return true;
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

bool parse_midi_controller_profile(std::string_view source,
                                   MidiControllerProfile &profile,
                                   std::string &error) {
    std::ostringstream controller_source;
    MidiControllerProfile parsed;
    std::istringstream input{std::string(source)};
    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        const auto parts = fields(line);
        if (!parts.empty() && parts[0] == "midi-device") {
            if (parts.size() != 2) {
                error = "line " + std::to_string(line_number) +
                        ": invalid MIDI device selector";
                return false;
            }
            parsed.port_selector = parts[1];
        } else if (!parts.empty() && parts[0] == "midi") {
            if (parts.size() < 6 || parts.size() > 7) {
                error = "line " + std::to_string(line_number) +
                        ": invalid MIDI mapping";
                return false;
            }
            std::uint32_t port{}, channel{}, number{};
            const auto mode = parse_mode(parts[4]);
            if (!parse_u32(parts[1], port) || !parse_u32(parts[2], channel) ||
                !parse_u32(parts[3], number) || !mode || !port || channel > 15 ||
                number > 127 || parts[5].empty()) {
                error = "line " + std::to_string(line_number) +
                        ": invalid MIDI port/channel/message";
                return false;
            }
            parsed.midi.push_back({port, static_cast<std::uint8_t>(channel),
                                   static_cast<std::uint8_t>(number), *mode,
                                   std::string(parts[5]),
                                   parts.size() == 7 && parts[6] == "feedback"});
        } else
            controller_source << line << '\n';
    }
    if (!parse_controller_profile(controller_source.str(), parsed.controller,
                                  error))
        return false;
    profile = std::move(parsed);
    error.clear();
    return true;
}

std::string serialize_midi_controller_profile(
    const MidiControllerProfile &profile) {
    auto output = serialize_controller_profile(profile.controller);
    if (!profile.port_selector.empty())
        output += "midi-device|" + profile.port_selector + "\n";
    for (const auto &binding : profile.midi) {
        output += "midi|" + std::to_string(binding.port) + "|" +
                  std::to_string(binding.channel) + "|" +
                  std::to_string(binding.number) + "|" +
                  std::string(mode_name(binding.mode)) + "|" +
                  binding.control_id;
        if (binding.feedback) output += "|feedback";
        output += '\n';
    }
    return output;
}

bool load_midi_controller_profile(const std::filesystem::path &path,
                                  MidiControllerProfile &profile,
                                  std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open MIDI controller profile: " + path.string();
        return false;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return parse_midi_controller_profile(contents.str(), profile, error);
}

bool save_midi_controller_profile(const std::filesystem::path &path,
                                  const MidiControllerProfile &profile,
                                  std::string &error) {
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot create temporary MIDI controller profile";
            return false;
        }
        output << serialize_midi_controller_profile(profile);
        output.flush();
        if (!output) {
            error = "cannot write MIDI controller profile";
            return false;
        }
    }
    std::error_code filesystem_error;
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary);
        error = "cannot replace MIDI controller profile: " +
                filesystem_error.message();
        return false;
    }
    error.clear();
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
    std::lock_guard lock(mutex_);
    if (binding.channel > 15 || binding.number > 127 ||
        binding.control_id.empty()) {
        error = "MIDI control binding has an invalid channel, number or ID";
        return false;
    }
    const auto control = std::find_if(
        session_->descriptor().controls.begin(),
        session_->descriptor().controls.end(), [&](const auto &value) {
            return value.id == binding.control_id && value.available;
        });
    if (control == session_->descriptor().controls.end()) {
        error = "MIDI binding references an unavailable physical control";
        return false;
    }
    const bool note = binding.mode == MidiControlMode::note_momentary;
    const bool relative =
        binding.mode == MidiControlMode::cc_relative_twos_complement ||
        binding.mode == MidiControlMode::cc_relative_binary_offset;
    if ((note && control->type != ControlType::momentary_button &&
         control->type != ControlType::toggle_button) ||
        (relative && control->type != ControlType::relative_encoder) ||
        (!note && !relative &&
         control->type != ControlType::absolute_potentiometer &&
         control->type != ControlType::fader &&
         control->type != ControlType::crossfader)) {
        error = "MIDI binding mode is incompatible with the physical control";
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
    std::lock_guard lock(mutex_);
    if (learn_.state == MidiLearnState::waiting &&
        std::chrono::steady_clock::now() >= learn_deadline_)
        learn_.state = MidiLearnState::timed_out;
    const auto type = message.status & 0xf0;
    const auto channel = message.status & 0x0f;
    if (learn_.state == MidiLearnState::waiting) {
        const auto note = learn_mode_ == MidiControlMode::note_momentary;
        if ((note && (type == 0x80 || type == 0x90)) || (!note && type == 0xb0)) {
            MidiControlBinding learned{message.port,
                                       static_cast<std::uint8_t>(channel),
                                       message.data1,
                                       learn_mode_, learn_.control_id, false};
            const auto duplicate = std::find_if(
                bindings_.begin(), bindings_.end(), [&](const auto &value) {
                    return value.port == learned.port &&
                           value.channel == learned.channel &&
                           value.number == learned.number &&
                           value.mode == learned.mode;
                });
            if (duplicate != bindings_.end()) {
                error = "MIDI control binding is ambiguous";
                learn_.error = error;
                return true;
            }
            bindings_.push_back(learned);
            learn_.state = MidiLearnState::learned;
            learn_.learned = learned;
            return true;
        }
    }
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

bool MidiControlSurfaceAdapter::begin_learn(
    std::string control_id, MidiControlMode mode,
    std::chrono::milliseconds timeout, std::string &error) {
    std::lock_guard lock(mutex_);
    const auto available = std::any_of(
        session_->descriptor().controls.begin(), session_->descriptor().controls.end(),
        [&](const auto &control) { return control.id == control_id && control.available; });
    if (!available || timeout <= std::chrono::milliseconds::zero()) {
        error = available ? "MIDI learn timeout must be positive"
                          : "MIDI learn target control is unavailable";
        return false;
    }
    learn_ = {};
    learn_.state = MidiLearnState::waiting;
    learn_.control_id = std::move(control_id);
    learn_mode_ = mode;
    learn_deadline_ = std::chrono::steady_clock::now() + timeout;
    error.clear();
    return true;
}

void MidiControlSurfaceAdapter::cancel_learn() noexcept {
    std::lock_guard lock(mutex_);
    if (learn_.state == MidiLearnState::waiting)
        learn_.state = MidiLearnState::cancelled;
}

void MidiControlSurfaceAdapter::update_learn_timeout() noexcept {
    std::lock_guard lock(mutex_);
    if (learn_.state == MidiLearnState::waiting &&
        std::chrono::steady_clock::now() >= learn_deadline_)
        learn_.state = MidiLearnState::timed_out;
}

void MidiControlSurfaceAdapter::observe(const ApplicationState &state) {
    std::lock_guard lock(mutex_);
    if (feedback_.size() >= 128) return;
    session_->observe(state);
    while (feedback_.size() < 128 && session_->has_pending_feedback()) {
        auto value = session_->next_feedback();
        if (!value) break;
        const auto binding = std::find_if(
            bindings_.begin(), bindings_.end(), [&](const auto &candidate) {
                return candidate.control_id == value->control_id &&
                       candidate.feedback;
            });
        if (binding == bindings_.end()) continue;
        const auto enabled = value->color != 0 && value->color != 0x080808;
        const auto status = binding->mode == MidiControlMode::note_momentary
                                ? std::uint8_t(0x90 | binding->channel)
                                : std::uint8_t(0xb0 | binding->channel);
        feedback_.push_back({status, binding->number,
                             static_cast<std::uint8_t>(enabled ? 127 : 0), 0,
                             binding->port, 3});
    }
}

void MidiControlSurfaceAdapter::requeue_feedback(MidiMessage value) {
    std::lock_guard lock(mutex_);
    feedback_.push_front(std::move(value));
}

std::optional<MidiMessage> MidiControlSurfaceAdapter::next_feedback() {
    std::lock_guard lock(mutex_);
    if (feedback_.empty()) return {};
    auto value = feedback_.front();
    feedback_.pop_front();
    return value;
}

std::vector<MidiControlBinding> MidiControlSurfaceAdapter::bindings() const {
    std::lock_guard lock(mutex_);
    return bindings_;
}

MidiLearnStatus MidiControlSurfaceAdapter::learn_status() const {
    std::lock_guard lock(mutex_);
    return learn_;
}

void MidiControlSurfaceAdapter::reconnect() noexcept {
    if (session_) session_->reconnect();
}
} // namespace neurogenic
