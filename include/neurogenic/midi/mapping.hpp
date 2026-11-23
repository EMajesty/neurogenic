#pragma once
#include "neurogenic/application/controller.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neurogenic {
class ControllerSession;
struct MidiMessage {
    std::uint8_t status{}, data1{}, data2{};
    std::uint64_t timestamp_ns{};
    std::uint32_t port{1};
    std::uint8_t size{3};
};

// One MIDI transport producer enqueues messages; one control thread dispatches
// them. Mapping configuration is completed before the dispatcher starts.
class MidiMapper {
  public:
    using Action = std::function<void()>;
    using NoteHandler = std::function<void(std::uint8_t, float)>;
    using NoteEventHandler = std::function<void(std::uint8_t, float, bool)>;
    using RawHandler = std::function<void(const MidiMessage &)>;
    void map_button(std::uint8_t channel, std::uint8_t note, std::string id,
                    Action action);
    void set_note_handler(NoteHandler handler) {
        note_handler_ = std::move(handler);
    }
    void set_note_event_handler(NoteEventHandler handler) {
        note_event_handler_ = std::move(handler);
    }
    void set_raw_handler(RawHandler handler) {
        raw_handler_ = std::move(handler);
    }
    void add_control_surface(class MidiControlSurfaceAdapter &surface) {
        control_surfaces_.push_back(&surface);
    }
    bool receive(const MidiMessage &) noexcept;
    bool dispatch_one();
    std::size_t dispatch_pending();
    [[nodiscard]] std::uint64_t dropped_messages() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    friend class ControlDispatcher;
    struct Entry {
        std::string id;
        Action action;
    };
    SpscQueue<MidiMessage, 257> inbox_;
    std::unordered_map<std::uint16_t, Entry> buttons_;
    NoteHandler note_handler_;
    NoteEventHandler note_event_handler_;
    RawHandler raw_handler_;
    std::vector<class MidiControlSurfaceAdapter *> control_surfaces_;
    std::atomic<std::uint64_t> dropped_{}, sequence_{};
};

enum class MidiControlMode : std::uint8_t {
    note_momentary,
    cc_absolute,
    cc_relative_twos_complement,
    cc_relative_binary_offset
};
struct MidiControlBinding {
    std::uint32_t port{1};
    std::uint8_t channel{}, number{};
    MidiControlMode mode{MidiControlMode::cc_absolute};
    std::string control_id;
    bool feedback{};
};
struct MidiControllerProfile {
    ControllerProfile controller;
    std::string port_selector;
    std::vector<MidiControlBinding> midi;
};
enum class MidiLearnState : std::uint8_t { idle, waiting, learned, cancelled, timed_out };
struct MidiLearnStatus {
    MidiLearnState state{MidiLearnState::idle};
    std::string control_id, error;
    std::optional<MidiControlBinding> learned;
};
bool parse_midi_controller_profile(std::string_view, MidiControllerProfile &,
                                   std::string &error);
[[nodiscard]] std::string
serialize_midi_controller_profile(const MidiControllerProfile &);
bool load_midi_controller_profile(const std::filesystem::path &,
                                  MidiControllerProfile &, std::string &error);
bool save_midi_controller_profile(const std::filesystem::path &,
                                  const MidiControllerProfile &,
                                  std::string &error);
// Control-surface interpretation is opt-in and remains separate from ordinary
// musical note/raw MIDI delivery in MidiMapper.
class MidiControlSurfaceAdapter {
  public:
    explicit MidiControlSurfaceAdapter(ControllerSession &session)
        : session_(&session) {}
    bool add(MidiControlBinding, std::string &error);
    bool dispatch(const MidiMessage &, std::string &error);
    bool begin_learn(std::string control_id, MidiControlMode,
                     std::chrono::milliseconds timeout, std::string &error);
    void cancel_learn() noexcept;
    void update_learn_timeout() noexcept;
    [[nodiscard]] MidiLearnStatus learn_status() const;
    [[nodiscard]] std::vector<MidiControlBinding> bindings() const;
    void observe(const ApplicationState &);
    [[nodiscard]] std::optional<MidiMessage> next_feedback();
    void requeue_feedback(MidiMessage);
    void reconnect() noexcept;
  private:
    ControllerSession *session_{};
    std::vector<MidiControlBinding> bindings_;
    std::deque<MidiMessage> feedback_;
    MidiLearnStatus learn_;
    MidiControlMode learn_mode_{MidiControlMode::cc_absolute};
    std::chrono::steady_clock::time_point learn_deadline_{};
    mutable std::mutex mutex_;
    std::uint64_t sequence_{};
};
} // namespace neurogenic
