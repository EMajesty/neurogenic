#include "neurogenic/application/controller_protocol.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <type_traits>
#include <unistd.h>

namespace neurogenic {
namespace {
constexpr std::uint16_t magic = 0x474e;
std::uint16_t read16(const std::uint8_t *p) {
    return std::uint16_t(p[0]) | std::uint16_t(p[1]) << 8;
}
std::uint32_t read32(const std::uint8_t *p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
void append16(std::vector<std::uint8_t> &v, std::uint16_t n) {
    v.push_back(n & 255);
    v.push_back(n >> 8);
}
void append32(std::vector<std::uint8_t> &v, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        v.push_back((n >> (i * 8)) & 255);
}
void append64(std::vector<std::uint8_t> &v, std::uint64_t n) {
    for (int i = 0; i < 8; ++i)
        v.push_back((n >> (i * 8)) & 255);
}
std::uint16_t crc(std::span<const std::uint8_t> data) {
    std::uint16_t value = 0xffff;
    for (auto byte : data) {
        value ^= std::uint16_t(byte) << 8;
        for (int i = 0; i < 8; ++i)
            value = value & 0x8000 ? std::uint16_t((value << 1) ^ 0x1021)
                                   : std::uint16_t(value << 1);
    }
    return value;
}
std::string escape(std::string_view s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\')
            r += '\\';
        if (static_cast<unsigned char>(c) >= 32)
            r += c;
    }
    return r;
}
std::string mode_name(std::uint16_t index) {
    switch (index) {
    case 0:
        return "step";
    case 1:
        return "pad";
    case 2:
        return "levels";
    case 3:
        return "notes";
    default:
        return {};
    }
}
double numeric(const ApplicationValue &value, double fallback = 0) {
    if (const auto *real = std::get_if<double>(&value))
        return *real;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return *integer;
    if (const auto *boolean = std::get_if<bool>(&value))
        return *boolean ? 1 : 0;
    return fallback;
}
std::string formatted(const ApplicationValue &value) {
    if (const auto *real = std::get_if<double>(&value)) {
        char text[24]{};
        std::snprintf(text, sizeof(text), "%.3g", *real);
        return text;
    }
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return std::to_string(*integer);
    if (const auto *boolean = std::get_if<bool>(&value))
        return *boolean ? "On" : "Off";
    if (const auto *text = std::get_if<std::string>(&value))
        return *text;
    return "--";
}
ContextControl parameter_control(const ApplicationState &state,
                                 std::uint16_t id,
                                 std::string_view parameter_id) {
    ContextControl result;
    result.control_id = id;
    result.parameter_id = parameter_id;
    const auto found =
        std::find_if(state.parameters.begin(), state.parameters.end(),
                     [&](const auto &p) { return p.id == parameter_id; });
    if (found == state.parameters.end()) {
        result.label = "--";
        return result;
    }
    result.active = true;
    result.label = found->name;
    result.value = formatted(found->current_value);
    result.unit = found->unit;
    result.minimum = numeric(found->minimum);
    result.maximum = numeric(found->maximum);
    result.bipolar = result.minimum < 0 && result.maximum > 0;
    result.feedback_revision = state.revision;
    return result;
}
ContextControl action_control(std::uint16_t id, std::string label,
                              std::string action, bool active = true) {
    ContextControl c;
    c.control_id = id;
    c.label = std::move(label);
    c.action_id = std::move(action);
    c.active = active;
    return c;
}
std::string_view truncate_view(std::string_view value, std::size_t maximum) {
    return value.substr(0, std::min(value.size(), maximum));
}
} // namespace

std::array<ContextControl, 16>
build_controller_context(const ApplicationState &state) {
    std::array<ContextControl, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].control_id = i;
        result[i].label = "--";
        result[i].feedback_revision = state.revision;
    }
    auto set_parameters = [&](std::initializer_list<std::string_view> ids) {
        std::size_t i{};
        for (auto id : ids) {
            result[i] =
                parameter_control(state, static_cast<std::uint16_t>(i), id);
            ++i;
        }
    };
    if (state.controller_context == "mixer")
        set_parameters({"mixer.selected.gain", "mixer.selected.pan",
                        "mixer.track.aux-1", "mixer.track.aux-2",
                        "mixer.aux.return", "mixer.master.gain",
                        "external-fx.send-gain", "external-fx.return-gain"});
    else if (state.controller_context == "samples")
        set_parameters({"sample.pitch", "sample.gain", "sample.pan",
                        "effect.delay.mix", "effect.delay.time",
                        "effect.delay.feedback"});
    else if (state.controller_context == "modulation")
        set_parameters({"synth.cutoff", "synth.resonance", "synth.pitch",
                        "synth.gain", "sample.pitch", "sample.gain",
                        "mixer.track.pan", "external-fx.wet"});
    else if (state.controller_context == "routing")
        set_parameters({"external-fx.wet", "external-fx.send-gain",
                        "external-fx.return-gain", "mixer.master.gain"});
    else if (state.controller_context == "parameters")
        set_parameters({"synth.cutoff", "synth.resonance", "synth.attack",
                        "synth.decay", "synth.sustain", "synth.release",
                        "synth.gain", "effect.delay.mix"});
    else
        set_parameters({"transport.tempo", "synth.cutoff", "synth.resonance",
                        "synth.attack", "synth.release", "sample.gain",
                        "mixer.selected.pan", "effect.delay.mix"});
    std::array<std::pair<std::string_view, std::string_view>, 8> lower{
        {{"FOCUS-", "navigation.focus-previous.v1"},
         {"FOCUS+", "navigation.focus-next.v1"},
         {"PREV", "navigation.focus-previous.v1"},
         {"NEXT", "navigation.focus-next.v1"},
         {"BACK", "navigation.cancel.v1"},
         {"ENTER", "navigation.confirm.v1"},
         {"PAGE+", "step.page-next.v1"},
         {"PLAY", "transport.play.v1"}}};
    if (state.controller_context == "samples") {
        lower[0] = {"PREVIEW", "slice.preview.v1"};
        lower[1] = {"STOP PREV", "slice.preview-stop.v1"};
    } else if (state.controller_context == "mixer") {
        lower[0] = {"MUTE", "mixer.mute-toggle.v1"};
        lower[1] = {"SOLO", "mixer.solo-toggle.v1"};
    }
    if (state.modifiers.shift || state.modifiers.shift_latched) {
        lower[0] = {"UNDO", "edit.undo.v1"};
        lower[1] = {"REDO", "edit.redo.v1"};
        lower[6] = {"PAGE-", "step.page-previous.v1"};
    }
    for (std::size_t i = 0; i < 8; ++i) {
        const bool available =
            std::find(state.available_actions.begin(),
                      state.available_actions.end(),
                      lower[i].second) != state.available_actions.end();
        result[8 + i] = action_control(8 + i, std::string(lower[i].first),
                                       std::string(lower[i].second), available);
        result[8 + i].feedback_revision = state.revision;
    }
    return result;
}
std::vector<std::uint8_t>
encode_controller_packet(const ControllerPacket &packet) {
    if (packet.payload.size() > controller_maximum_payload)
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(packet.payload.size() + 12);
    append16(out, magic);
    out.push_back(controller_protocol_major);
    out.push_back(static_cast<std::uint8_t>(packet.type));
    append16(out, static_cast<std::uint16_t>(packet.payload.size()));
    append32(out, packet.sequence);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    append16(out, crc(out));
    return out;
}
void ControllerFrameDecoder::push(std::span<const std::uint8_t> data) noexcept {
    for (auto byte : data) {
        if (bytes_.size() == capacity_) {
            bytes_.pop_front();
            ++dropped_;
        }
        bytes_.push_back(byte);
    }
}
std::optional<ControllerPacket> ControllerFrameDecoder::pop() noexcept {
    while (bytes_.size() >= 12) {
        if (bytes_[0] != (magic & 255) || bytes_[1] != (magic >> 8)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        std::array<std::uint8_t, 10> header{};
        for (std::size_t i = 0; i < header.size(); ++i)
            header[i] = bytes_[i];
        if (header[2] != controller_protocol_major) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto length = read16(header.data() + 4);
        if (length > controller_maximum_payload) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto total = std::size_t(length) + 12;
        if (bytes_.size() < total)
            return std::nullopt;
        std::vector<std::uint8_t> raw(total);
        for (std::size_t i = 0; i < total; ++i)
            raw[i] = bytes_[i];
        if (crc(std::span<const std::uint8_t>(raw.data(), total - 2)) !=
            read16(raw.data() + total - 2)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        for (std::size_t i = 0; i < total; ++i)
            bytes_.pop_front();
        ControllerPacket packet{static_cast<ControllerMessageType>(raw[3]),
                                read32(raw.data() + 6),
                                {}};
        packet.payload.assign(raw.begin() + 10, raw.end() - 2);
        return packet;
    }
    return std::nullopt;
}
ControllerHost::ControllerHost(ApplicationService &app, std::size_t capacity,
                               std::string expected_device_id)
    : application_(&app), capacity_(std::max<std::size_t>(1, capacity)),
      expected_device_id_(std::move(expected_device_id)) {}
bool ControllerHost::enqueue(ControllerPacket p) noexcept {
    if (output_.size() >= capacity_) {
        ++state_.dropped_output;
        return false;
    }
    output_.push_back(std::move(p));
    return true;
}
void ControllerHost::connected() {
    state_.connection = ControllerConnection::negotiating;
    state_.feedback_revision = ~std::uint64_t{};
    enqueue({ControllerMessageType::hello,
             sequence_++,
             {controller_protocol_major, controller_protocol_minor}});
}
void ControllerHost::disconnected(std::string reason) {
    state_.connection = ControllerConnection::disconnected;
    state_.error = std::move(reason);
    output_.clear();
}
bool ControllerHost::set_analog_calibration(std::size_t id,
                                            AnalogCalibration calibration,
                                            std::string &error) noexcept {
    if (id >= state_.calibration.size() ||
        calibration.maximum <= calibration.minimum ||
        calibration.dead_zone >= calibration.maximum - calibration.minimum) {
        error = "invalid analog calibration";
        return false;
    }
    state_.calibration[id] = calibration;
    pickup_[id] = false;
    state_.pickup_acquired[id] = false;
    state_.pickup_direction[id] = 0;
    error.clear();
    return true;
}
void ControllerHost::dispatch_input(std::string id, double value, bool pressed,
                                    std::uint64_t seq) {
    ControllerInput input{std::move(id), value, 0, pressed, seq};
    const auto profile = full_performance_profile();
    std::string error;
    ControllerMapper mapper(profile, application_->actions(), error);
    const auto resolved =
        mapper.resolve(input, application_->snapshot().modifiers, error);
    if (resolved.action) {
        if (!application_->submit(*resolved.action))
            ++state_.dropped_input;
    } else if (!resolved.parameter_id.empty()) {
        ApplicationCommand c;
        c.action_id = resolved.relative_steps ? "parameter.adjust.v1"
                                              : "parameter.set-normalized.v1";
        c.arguments["parameter_id"] = resolved.parameter_id;
        c.arguments[resolved.relative_steps ? "steps" : "value"] =
            resolved.relative_steps
                ? resolved.relative_steps
                : std::get<double>(*resolved.parameter_value);
        c.source_id = "physical-controller";
        c.source_sequence = seq;
        if (!application_->submit(std::move(c)))
            ++state_.dropped_input;
    } else
        state_.error = error;
}
bool ControllerHost::receive(const ControllerPacket &p,
                             std::string &error) noexcept {
    ++state_.received;
    if (p.payload.size() > controller_maximum_payload) {
        error = "payload exceeds protocol limit";
        return false;
    }
    if (p.type == ControllerMessageType::hello_reply) {
        if (p.payload.size() < 4) {
            error = "short hello reply";
            return false;
        }
        state_.protocol_major = p.payload[0];
        state_.protocol_minor = p.payload[1];
        const auto idlen = p.payload[2], fwlen = p.payload[3];
        if (4u + idlen + fwlen > p.payload.size()) {
            error = "invalid identity lengths";
            return false;
        }
        state_.device_id.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4), idlen);
        state_.firmware.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4 + idlen),
            fwlen);
        if (state_.protocol_major != controller_protocol_major) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = "protocol major version mismatch";
            error = state_.error;
            return false;
        }
        if (!expected_device_id_.empty() &&
            state_.device_id != expected_device_id_) {
            state_.connection = ControllerConnection::incompatible;
            state_.error =
                "controller device identity does not match configuration";
            error = state_.error;
            return false;
        }
        state_.connection = ControllerConnection::connected;
        ++state_.reconnects;
        enqueue({ControllerMessageType::capabilities, sequence_++, {}});
        enqueue({ControllerMessageType::state_sync, sequence_++, {}});
    } else if (p.type == ControllerMessageType::capabilities) {
        if (p.payload.size() != 4) {
            error = "invalid capability payload";
            return false;
        }
        state_.capabilities = read32(p.payload.data());
    } else if (p.type == ControllerMessageType::button) {
        if (p.payload.size() != 3) {
            error = "invalid button payload";
            return false;
        }
        const auto id = read16(p.payload.data());
        const bool pressed = p.payload[2] != 0;
        bool queued = true;
        if (id < 16 && pressed) {
            const auto s = application_->snapshot();
            PerformanceGrid grid;
            grid.set_mode(s.pad_mode);
            grid.set_step_page(s.step_page);
            grid.set_selected_slot(s.selected_sample_slot);
            grid.set_selected_pad(s.selected_pad);
            if (auto c =
                    grid_application_command(grid.press(id, s.modifiers.select),
                                             "physical-controller", p.sequence))
                queued = application_->submit(std::move(*c));
        } else if (id >= 16 && id < 24 && pressed) {
            const auto context =
                build_controller_context(application_->snapshot());
            const auto &control = context[id - 8];
            if (control.active && !control.action_id.empty())
                queued = application_->submit(
                    {control.action_id, {}, "physical-controller", p.sequence});
        } else if (state_.protocol_minor == 0 && id >= 24 && id <= 27 &&
                   pressed) {
            if (auto mode = mode_name(id - 24); !mode.empty())
                queued = application_->submit({"pad.mode-select.v1",
                                               {{"mode", mode}},
                                               "physical-controller",
                                               p.sequence});
        } else if (id == static_cast<std::uint16_t>(DedicatedControlId::play) &&
                   pressed)
            queued = application_->submit(
                {"transport.play.v1", {}, "physical-controller", p.sequence});
        else if (id == static_cast<std::uint16_t>(DedicatedControlId::stop) &&
                 pressed)
            queued = application_->submit(
                {"transport.stop.v1", {}, "physical-controller", p.sequence});
        else if (id == static_cast<std::uint16_t>(DedicatedControlId::record) &&
                 pressed) {
            const auto recording = application_->snapshot().recording.state;
            if (recording == RecordingState::recording)
                queued = application_->submit({"recording.stop.v1",
                                               {},
                                               "physical-controller",
                                               p.sequence});
            else if (recording == RecordingState::armed)
                queued = application_->submit({"recording.start.v1",
                                               {},
                                               "physical-controller",
                                               p.sequence});
            else {
                queued = application_->submit({"recording.arm.v1",
                                               {},
                                               "physical-controller",
                                               p.sequence});
                if (queued)
                    queued = application_->submit({"recording.start.v1",
                                                   {},
                                                   "physical-controller",
                                                   p.sequence});
            }
        } else if (id == static_cast<std::uint16_t>(DedicatedControlId::shift))
            queued = application_->submit({"modifier.shift.v1",
                                           {{"active", pressed}},
                                           "physical-controller",
                                           p.sequence});
        else if (id == static_cast<std::uint16_t>(DedicatedControlId::select))
            queued = application_->submit({"modifier.select.v1",
                                           {{"active", pressed}},
                                           "physical-controller",
                                           p.sequence});
        else if (id == static_cast<std::uint16_t>(
                           DedicatedControlId::data_push) &&
                 pressed && state_.protocol_minor <= 1)
            queued = application_->submit({"navigation.confirm.v1",
                                           {},
                                           "physical-controller",
                                           p.sequence});
        else if (id >= static_cast<std::uint16_t>(
                           DedicatedControlId::mode_step) &&
                 id <= static_cast<std::uint16_t>(
                           DedicatedControlId::mode_notes) &&
                 pressed)
            queued = application_->submit(
                {"pad.mode-select.v1",
                 {{"mode", mode_name(id - static_cast<std::uint16_t>(
                                              DedicatedControlId::mode_step))}},
                 "physical-controller",
                 p.sequence});
        else if (id == static_cast<std::uint16_t>(
                           DedicatedControlId::page_previous) &&
                 pressed)
            queued = application_->submit({"step.page-previous.v1",
                                           {},
                                           "physical-controller",
                                           p.sequence});
        else if (id == static_cast<std::uint16_t>(
                           DedicatedControlId::page_next) &&
                 pressed)
            queued = application_->submit(
                {"step.page-next.v1", {}, "physical-controller", p.sequence});
        else if (id >= 36 && id <= 43 && pressed) {
            static constexpr std::string_view contexts[]{
                "sequencer", "samples", "parameters", "mixer",
                "modulation", "routing", "recording", "midi"};
            queued = application_->submit(
                {"controller.context-select.v1",
                 {{"context", std::string(contexts[id - 36])}},
                 "physical-controller", p.sequence});
        } else if (id >= 64 && id <= 69 && pressed) {
            static constexpr std::string_view actions[]{
                "navigation.focus-previous.v1", "navigation.focus-next.v1",
                "step.page-previous.v1", "step.page-next.v1",
                "navigation.confirm.v1", "navigation.cancel.v1"};
            queued = application_->submit(
                {std::string(actions[id - 64]), {}, "physical-controller",
                 p.sequence});
        }
        if (!queued)
            ++state_.dropped_input;
    } else if (p.type == ControllerMessageType::encoder) {
        if (p.payload.size() != 4) {
            error = "invalid encoder payload";
            return false;
        }
        const auto id = read16(p.payload.data());
        const auto delta =
            static_cast<std::int16_t>(read16(p.payload.data() + 2));
        if (id < state_.encoders.size())
            state_.encoders[id] += delta;
        if (id < 8) {
            const auto snapshot = application_->snapshot();
            const auto context = build_controller_context(snapshot);
            const auto &control = context[id];
            double steps = delta;
            if (snapshot.modifiers.shift || snapshot.modifiers.shift_latched)
                steps *= 0.1;
            else if (std::abs(delta) >= 4)
                steps *= 2;
            if (control.active && !control.parameter_id.empty() &&
                !application_->submit(
                    {"parameter.adjust.v1",
                     {{"parameter_id", control.parameter_id}, {"steps", steps}},
                     "physical-controller",
                     p.sequence}))
                ++state_.dropped_input;
        } else if (id == 8) {
            const auto snapshot = application_->snapshot();
            double steps = delta;
            if (snapshot.modifiers.shift || snapshot.modifiers.shift_latched)
                steps *= 0.1;
            else if (std::abs(delta) >= 4)
                steps *= 2;
            dispatch_input("data", steps, true, p.sequence);
        }
    } else if (p.type == ControllerMessageType::analog) {
        if (p.payload.size() != 4) {
            error = "invalid analog payload";
            return false;
        }
        if (state_.protocol_minor >= 2 &&
            !(state_.capabilities & controller_analog)) {
            error = "analog controls are unavailable in negotiated profile";
            return false;
        }
        const auto id = read16(p.payload.data()),
                   value = read16(p.payload.data() + 2);
        if (id >= state_.analog.size()) {
            error = "unknown analog control";
            return false;
        }
        state_.analog[id] = value;
        const auto &calibration = state_.calibration[id];
        const auto range = std::max<std::uint16_t>(1, calibration.maximum -
                                                          calibration.minimum);
        const auto normalized =
            std::clamp((double(value) - calibration.minimum) / range, 0.0, 1.0);
        const auto difference = normalized - pickup_target_[id];
        state_.pickup_direction[id] = difference < -0.01  ? 1
                                      : difference > 0.01 ? -1
                                                          : 0;
        if (!pickup_[id] &&
            std::abs(difference) <=
                std::max(0.01, double(calibration.dead_zone) / range)) {
            pickup_[id] = true;
            state_.pickup_acquired[id] = true;
            state_.pickup_direction[id] = 0;
        }
        if (pickup_[id]) {
            static constexpr std::string_view targets[]{
                "mixer.selected.gain", "mixer.track.aux-1", "mixer.track.aux-2",
                "mixer.master.gain", "mixer.external.selected.wet"};
            if (!application_->submit(
                    {"parameter.set-normalized.v1",
                     {{"parameter_id", std::string(targets[id])},
                      {"value", normalized}},
                     "physical-controller",
                     p.sequence}))
                ++state_.dropped_input;
        }
    } else if (p.type == ControllerMessageType::state_sync)
        state_.feedback_revision = ~std::uint64_t{};
    else if (p.type == ControllerMessageType::heartbeat)
        enqueue({ControllerMessageType::heartbeat, sequence_++, p.payload});
    else if (p.type == ControllerMessageType::reset) {
        state_.connection = ControllerConnection::negotiating;
        connected();
    }
    error.clear();
    return true;
}
void ControllerHost::publish(const ApplicationState &s) {
    if (state_.connection != ControllerConnection::connected ||
        state_.feedback_revision == s.revision)
        return;
    const auto dropped_before = state_.dropped_output;
    const bool full_refresh = state_.feedback_revision == ~std::uint64_t{};
    state_.feedback_revision = s.revision;
    const std::uint32_t off = 0x080808, active = 0x0060b0, selected = 0xff7a00,
                        play = 0x00c840, record = 0xd00020, modifier = 0x8040ff;
    const auto send_led = [&](std::size_t id, std::uint32_t color,
                              std::uint8_t brightness = 192) {
        if (!full_refresh && state_.leds[id] == color)
            return;
        state_.leds[id] = color;
        std::vector<std::uint8_t> b;
        append16(b, id);
        append32(b, color);
        b.push_back(std::min(brightness, brightness_limit_));
        enqueue({ControllerMessageType::led, sequence_++, std::move(b)});
    };
    const auto track =
        std::find_if(s.tracks.begin(), s.tracks.end(),
                     [&](const auto &t) { return t.id == s.selected_track; });
    for (std::size_t i = 0; i < 16; ++i) {
        auto color = off;
        if (s.pad_mode == PadMode::step && track != s.tracks.end()) {
            const auto tick =
                static_cast<MusicalTick>(s.step_page * 16 + i) * s.step_ticks;
            if (std::any_of(track->events.begin(), track->events.end(),
                            [&](const auto &e) {
                                return e.tick == tick &&
                                       e.type == MusicalEventType::note;
                            }))
                color = active;
        } else if (s.pad_mode == PadMode::pad && s.slice_pad_assignments[i])
            color = active;
        else if (s.pad_mode == PadMode::levels)
            color = 0x002040 + static_cast<std::uint32_t>(i) * 0x000804;
        else if (s.pad_mode == PadMode::notes)
            color = 0x202060;
        if ((s.pad_mode == PadMode::step &&
             s.selected_step ==
                 static_cast<std::int64_t>(s.step_page * 16 + i)) ||
            (s.pad_mode != PadMode::step && s.selected_pad == i))
            color = selected;
        if (s.playing && i == static_cast<std::size_t>(
                                  (s.transport_tick /
                                   std::max<MusicalTick>(1, s.step_ticks)) %
                                  16))
            color = play;
        send_led(i, color);
    }
    const auto mode_base =
        static_cast<std::size_t>(DedicatedControlId::mode_step);
    for (std::size_t i = 0; i < 4; ++i)
        send_led(mode_base + i,
                 static_cast<std::size_t>(s.pad_mode) == i ? selected : off);
    send_led(static_cast<std::size_t>(DedicatedControlId::play),
             s.playing ? play : off);
    send_led(static_cast<std::size_t>(DedicatedControlId::record),
             s.recording.state == RecordingState::recording ? record : off);
    send_led(static_cast<std::size_t>(DedicatedControlId::shift),
             (s.modifiers.shift || s.modifiers.shift_latched) ? modifier : off);
    send_led(static_cast<std::size_t>(DedicatedControlId::select),
             (s.modifiers.select || s.modifiers.select_latched) ? modifier
                                                                : off);

    auto context = build_controller_context(s);
    static constexpr std::string_view analog_targets[]{
        "mixer.selected.gain", "mixer.track.aux-1", "mixer.track.aux-2",
        "mixer.master.gain", "mixer.external.selected.wet"};
    for (std::size_t i = 0; i < std::size(analog_targets); ++i) {
        const auto c = parameter_control(s, i, analog_targets[i]);
        const auto found = std::find_if(
            s.parameters.begin(), s.parameters.end(),
            [&](const auto &p) { return p.id == analog_targets[i]; });
        const auto current = found == s.parameters.end()
                                 ? c.minimum
                                 : numeric(found->effective_value,
                                           numeric(found->current_value));
        const auto range = std::max(0.000001, c.maximum - c.minimum);
        const auto target = std::clamp((current - c.minimum) / range, 0.0, 1.0);
        const auto &calibration = state_.calibration[i];
        const auto raw_range = std::max<std::uint16_t>(
            1, calibration.maximum - calibration.minimum);
        const auto physical = std::clamp(
            (double(state_.analog[i]) - calibration.minimum) / raw_range, 0.0,
            1.0);
        if (std::abs(target - last_effective_target_[i]) > 0.002 &&
            pickup_[i]) {
            if (std::abs(physical - target) > 0.02)
                pickup_[i] = false;
        }
        pickup_target_[i] = target;
        last_effective_target_[i] = target;
        state_.pickup_direction[i] =
            pickup_[i] ? 0 : (physical < target ? 1 : -1);
        state_.pickup_acquired[i] = pickup_[i];
    }
    for (auto &c : context) {
        const auto target = std::find(analog_targets, std::end(analog_targets),
                                      std::string_view(c.parameter_id));
        if (target != std::end(analog_targets)) {
            const auto index =
                static_cast<std::size_t>(target - analog_targets);
            c.pickup = pickup_[index];
            c.pickup_direction = state_.pickup_direction[index];
        }
        state_.context[c.control_id] = c;
        std::vector<std::uint8_t> b;
        append16(b, c.control_id);
        if (state_.protocol_minor == 0) {
            const auto label = truncate_view(c.label, 31);
            b.push_back(c.active);
            b.push_back(label.size());
            b.insert(b.end(), label.begin(), label.end());
        } else {
            std::uint8_t flags =
                (c.active ? 1 : 0) | (c.bipolar ? 2 : 0) | (c.pickup ? 4 : 0);
            b.push_back(flags);
            b.push_back(static_cast<std::uint8_t>(c.pickup_direction));
            append64(b, c.feedback_revision);
            const std::string fields[]{std::string(truncate_view(c.label, 24)),
                                       std::string(truncate_view(c.value, 16)),
                                       std::string(truncate_view(c.unit, 8)),
                                       formatted(c.minimum),
                                       formatted(c.maximum)};
            for (const auto &field : fields) {
                b.push_back(field.size());
                b.insert(b.end(), field.begin(), field.end());
            }
        }
        enqueue({ControllerMessageType::context, sequence_++, std::move(b)});
    }
    if (state_.dropped_output != dropped_before)
        state_.feedback_revision = ~std::uint64_t{};
}
std::optional<ControllerPacket> ControllerHost::next_output() noexcept {
    if (output_.empty())
        return {};
    auto p = std::move(output_.front());
    output_.pop_front();
    ++state_.sent;
    return p;
}
ControllerSerialTransport::ControllerSerialTransport(std::size_t c)
    : capacity_(std::max<std::size_t>(1, c)) {}
ControllerSerialTransport::~ControllerSerialTransport() { close(); }
bool ControllerSerialTransport::open(const std::filesystem::path &p,
                                     std::string &e) {
    close();
    fd_ = ::open(p.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        e = std::strerror(errno);
        return false;
    }
    termios t{};
    if (tcgetattr(fd_, &t) == 0) {
        cfmakeraw(&t);
        cfsetspeed(&t, B115200);
        tcsetattr(fd_, TCSANOW, &t);
    }
    identity_ = p;
    e.clear();
    return true;
}
void ControllerSerialTransport::close() noexcept {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    output_.clear();
}
bool ControllerSerialTransport::send(const ControllerPacket &p) noexcept {
    auto bytes = encode_controller_packet(p);
    if (bytes.empty())
        return false;
    if (output_.size() >= capacity_) {
        ++dropped_;
        return false;
    }
    output_.push_back(std::move(bytes));
    return true;
}
bool ControllerSerialTransport::poll(ControllerHost &host,
                                     std::string &e) noexcept {
    if (fd_ < 0) {
        e = "controller serial device is not open";
        return false;
    }
    std::array<std::uint8_t, 512> b{};
    const auto n = ::read(fd_, b.data(), b.size());
    if (n > 0)
        decoder_.push(std::span<const std::uint8_t>(b.data(), n));
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        e = std::strerror(errno);
        host.disconnected(e);
        close();
        return false;
    }
    while (auto p = decoder_.pop())
        host.receive(*p, e);
    while (auto p = host.next_output())
        send(*p);
    while (!output_.empty()) {
        auto &w = output_.front();
        const auto wrote = ::write(fd_, w.data(), w.size());
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (wrote < 0) {
            e = std::strerror(errno);
            host.disconnected(e);
            close();
            return false;
        }
        if (static_cast<std::size_t>(wrote) < w.size()) {
            w.erase(w.begin(), w.begin() + wrote);
            break;
        }
        output_.pop_front();
    }
    e.clear();
    return true;
}
bool SimulatedController::send(ControllerMessageType t,
                               std::vector<std::uint8_t> p) {
    std::string e;
    return host_->receive({t, sequence_++, std::move(p)}, e);
}
bool SimulatedController::negotiate(std::string id, std::uint8_t minor) {
    host_->connected();
    std::vector<std::uint8_t> p{controller_protocol_major, minor,
                                static_cast<std::uint8_t>(id.size()), 3};
    p.insert(p.end(), id.begin(), id.end());
    p.insert(p.end(), {'s', 'i', 'm'});
    if (!send(ControllerMessageType::hello_reply, std::move(p)))
        return false;
    if (minor >= 2) {
        const auto capabilities =
            controller_buttons | controller_relative_encoders | controller_rgb |
            controller_context_feedback | controller_heartbeat |
            controller_inventory_068;
        return send(ControllerMessageType::capabilities,
                    {static_cast<std::uint8_t>(capabilities),
                     static_cast<std::uint8_t>(capabilities >> 8),
                     static_cast<std::uint8_t>(capabilities >> 16),
                     static_cast<std::uint8_t>(capabilities >> 24)});
    }
    return true;
}
bool SimulatedController::button(std::uint16_t id, bool pressed) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    p.push_back(pressed);
    return send(ControllerMessageType::button, std::move(p));
}
bool SimulatedController::encoder(std::uint16_t id, std::int16_t d) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, d);
    return send(ControllerMessageType::encoder, std::move(p));
}
bool SimulatedController::analog(std::uint16_t id, std::uint16_t v) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, v);
    return send(ControllerMessageType::analog, std::move(p));
}
std::vector<ControllerPacket> SimulatedController::drain_feedback() {
    std::vector<ControllerPacket> r;
    while (auto p = host_->next_output())
        r.push_back(std::move(*p));
    return r;
}
std::string controller_protocol_json(const ControllerProtocolState &s) {
    std::string pickup = "[";
    for (std::size_t i = 0; i < s.pickup_direction.size(); ++i) {
        if (i)
            pickup += ",";
        pickup +=
            "{\"raw\":" + std::to_string(s.analog[i]) +
            ",\"acquired\":" + (s.pickup_acquired[i] ? "true" : "false") +
            ",\"direction\":" + std::to_string(s.pickup_direction[i]) +
            ",\"minimum\":" + std::to_string(s.calibration[i].minimum) +
            ",\"maximum\":" + std::to_string(s.calibration[i].maximum) +
            ",\"dead_zone\":" + std::to_string(s.calibration[i].dead_zone) +
            "}";
    }
    pickup += "]";
    std::string context = "[";
    for (std::size_t i = 0; i < s.context.size(); ++i) {
        if (i)
            context += ",";
        context += "{\"id\":" + std::to_string(s.context[i].control_id) +
                   ",\"label\":\"" + escape(s.context[i].label) +
                   "\",\"value\":\"" + escape(s.context[i].value) +
                   "\",\"active\":" + (s.context[i].active ? "true" : "false") +
                   "}";
    }
    context += "]";
    return "{\"schema\":\"neurogenic.controller-protocol.v1\",\"connection\":" +
           std::to_string(static_cast<int>(s.connection)) +
           ",\"device_id\":\"" + escape(s.device_id) + "\",\"protocol\":\"" +
           std::to_string(s.protocol_major) + "." +
           std::to_string(s.protocol_minor) +
           "\",\"received\":" + std::to_string(s.received) +
           ",\"sent\":" + std::to_string(s.sent) +
           ",\"dropped_input\":" + std::to_string(s.dropped_input) +
           ",\"dropped_output\":" + std::to_string(s.dropped_output) +
           ",\"reconnects\":" + std::to_string(s.reconnects) +
           ",\"feedback_revision\":" + std::to_string(s.feedback_revision) +
           ",\"pickup\":" + pickup + ",\"context\":" + context + "}";
}
} // namespace neurogenic
