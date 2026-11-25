#include "neurogenic/ui/slint_adapter.hpp"
#include "neurogenic/application/assignment_picker.hpp"
#include "neurogenic/application/reference_controller.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/project/preset.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace neurogenic {
namespace {
void hash_combine(std::uint64_t &seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

std::string_view slint_trigger_condition_name(TriggerCondition condition) {
    static constexpr std::array<std::string_view, 9> names{
        "Always", "First", "Not first", "Every N", "N of M",
        "Previous fired", "Previous not fired", "Fill", "Not fill"};
    return names[static_cast<std::size_t>(condition)];
}

std::uint64_t slint_structural_fingerprint(const ApplicationState &state) {
    std::uint64_t value{0xcbf29ce484222325ULL};
    const auto add = [&](std::uint64_t item) { hash_combine(value, item); };
    add(state.timeline_revision); add(state.slice_revision);
    add(state.mixer.configuration.revision); add(state.selected_track);
    add(state.selected_sample_slot.value); add(state.selected_slice);
    add(state.selected_region); add(state.selected_pattern);
    add(state.selected_step); add(state.step_page);
    add(static_cast<std::uint64_t>(state.step_resolution));
    add(std::bit_cast<std::uint64_t>(state.tempo_bpm));
    add(state.patterns.size()); add(state.clips.size());
    add(state.audio_clips.size()); add(state.tracks.size());
    add(state.performance_launching.slots.size());
    for (const auto &slot : state.performance_launching.slots) {
        add(slot.id);
        add(static_cast<std::uint64_t>(slot.logical_position + 1));
    }
    add(state.performance_launching.scenes.size());
    add(state.performance_banks.active_bank);
    add(state.performance_banks.banks.size());
    for (const auto &bank : state.performance_banks.banks) {
        add(bank.id);
        add(std::hash<std::string>{}(bank.name));
        add(bank.parameters.size()); add(bank.actions.size()); add(bank.pads.size());
        for (const auto &assignment : bank.parameters) {
            add(assignment.id); add(assignment.position); add(assignment.secondary);
            add(std::hash<std::string>{}(assignment.parameter_id));
        }
        for (const auto &assignment : bank.actions) {
            add(assignment.id); add(assignment.position); add(assignment.secondary);
            add(std::hash<std::string>{}(assignment.action_id));
        }
        for (const auto &assignment : bank.pads) {
            add(assignment.id); add(assignment.position); add(assignment.launch_slot);
        }
    }
    add(state.parameters.size()); add(state.sample_slots.size());
    add(state.modulation.modulator_count); add(state.modulation.route_count);
    for (std::size_t i = 0; i < state.modulation.modulator_count; ++i)
        add(state.modulation.modulators[i].id);
    for (std::size_t i = 0; i < state.modulation.route_count; ++i) {
        add(state.modulation.routes[i].id);
        add(state.modulation.routes[i].source_instance);
        add(state.modulation.routes[i].destination.parameter);
    }
    add(state.performance_macros.macros.size());
    add(state.interaction.revision);
    add(state.midi.route_count);
    add(static_cast<std::uint64_t>(state.midi.clock_source));
    for (std::size_t i = 0; i < state.midi.route_count; ++i) {
        const auto &route = state.midi.routes[i];
        add(route.id); add(route.input_port); add(route.output_port);
        add(static_cast<std::uint64_t>(route.input_channel + 1));
        add(static_cast<std::uint64_t>(route.output_channel + 1));
        add(route.notes | (route.control_change << 1U) | (route.clock << 2U) |
            (route.transport << 3U) | (route.enabled << 4U));
    }
    for (const auto &macro : state.performance_macros.macros) {
        add(macro.id); add(macro.assignments.size());
    }
    for (const auto &track : state.tracks) {
        add(track.id); add(track.events.size()); add(track.pattern_length);
    }
    for (const auto &slot : state.sample_slots) {
        add(slot.id.value); add(slot.revision); add(slot.active_publication);
    }
    for (const auto &parameter : state.parameters) {
        add(std::hash<std::string>{}(parameter.id));
    }
    return value;
}

std::uint64_t slint_context_pad_fingerprint(const ApplicationState &state,
                                            std::string_view context,
                                            const ModifierState &modifiers) {
    std::uint64_t value{0xcbf29ce484222325ULL};
    const auto add = [&](std::uint64_t item) { hash_combine(value, item); };
    add(std::hash<std::string_view>{}(context));
    add(modifiers.shift); add(modifiers.shift_latched); add(modifiers.select);
    add(state.selected_track); add(state.selected_pattern); add(state.step_page);
    add(state.selected_sample_slot.value); add(state.selected_slice);
    add(state.active_pattern); add(state.queued_pattern);
    add(state.performance_banks.active_bank);
    add(state.performance_banks.selected_assignment);
    for (const auto &bank : state.performance_banks.banks) {
        add(bank.id); add(bank.pads.size());
        for (const auto &pad : bank.pads) {
            add(pad.id); add(pad.position); add(pad.launch_slot);
        }
    }
    for (const auto &slot : state.performance_launching.slots) {
        add(slot.id); add(static_cast<std::uint64_t>(slot.logical_position + 1));
    }
    for (const auto &pattern : state.patterns) { add(pattern.id); add(pattern.length); }
    for (const auto &track : state.tracks) { add(track.id); add(track.muted); }
    for (const auto &slot : state.sample_slots) {
        add(slot.id.value); add(slot.has_sample);
    }
    for (const auto &slice : state.slices) add(slice.id);
    for (std::size_t i = 0; i < state.modulation.route_count; ++i) {
        const auto &route = state.modulation.routes[i];
        add(route.id); add(route.enabled);
    }
    for (const auto &macro : state.performance_macros.macros) {
        add(macro.id); add(macro.enabled);
    }
    for (const auto &scene : state.performance_macros.scenes) add(scene.id);
    return value;
}

constexpr std::size_t browser_page_size = 128;
constexpr std::size_t browser_chunk_pages = 8;
constexpr std::size_t browser_chunk_size =
    browser_page_size * browser_chunk_pages;
constexpr std::array<std::pair<std::string_view, std::string_view>, 8> pages{{
    {"performance", "Performance"}, {"sequencer", "Sequencer"},
    {"sample", "Sample"},           {"sound", "Sound"},
    {"mixer", "Mixer"},             {"fx", "FX"},
    {"browser", "Browser"},         {"setup", "Setup"}}};

std::string quantization_name(LaunchQuantization value) {
    switch (value) {
    case LaunchQuantization::immediate: return "Immediate";
    case LaunchQuantization::quarter: return "Beat";
    case LaunchQuantization::bar: return "Bar";
    case LaunchQuantization::two_bars: return "2 Bars";
    case LaunchQuantization::four_bars: return "4 Bars";
    case LaunchQuantization::eight_bars: return "8 Bars";
    case LaunchQuantization::subdivision: return "Subdivision";
    case LaunchQuantization::pattern_end: return "Pattern End";
    }
    return "Unavailable";
}

double numeric_value(const ApplicationValue &value) {
    if (const auto *real = std::get_if<double>(&value)) return *real;
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto *boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    return 0.0;
}

std::string format_value(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::abs(value) >= 100.0 ? 0 : 2)
           << value;
    return stream.str();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool browser_file_matches(std::string_view category,
                          const std::filesystem::directory_entry &entry) {
    if (entry.is_directory()) return true;
    const auto extension = lower(entry.path().extension().string());
    if (category == "samples") return extension == ".wav";
    return category == "other";
}

bool project_directory(const std::filesystem::path &path) {
    std::ifstream manifest(path / "manifest.ngp");
    std::string magic;
    unsigned version{};
    return (manifest >> magic >> version) && magic == "NEUROGENIC_PROJECT";
}

std::size_t utf8_previous(std::string_view text, std::size_t cursor) {
    cursor = std::min(cursor, text.size());
    if (cursor == 0) return 0;
    do { --cursor; } while (cursor > 0 &&
        (static_cast<unsigned char>(text[cursor]) & 0xc0U) == 0x80U);
    return cursor;
}

std::size_t utf8_next(std::string_view text, std::size_t cursor) {
    cursor = std::min(cursor, text.size());
    if (cursor == text.size()) return cursor;
    ++cursor;
    while (cursor < text.size() &&
        (static_cast<unsigned char>(text[cursor]) & 0xc0U) == 0x80U) ++cursor;
    return cursor;
}

bool valid_filename(std::string_view value) {
    return !value.empty() && value != "." && value != ".." &&
        value.find('/') == std::string_view::npos &&
        value.find('\0') == std::string_view::npos;
}

std::string preset_id_from_name(std::string_view value) {
    std::string result;
    bool separator{};
    for (const unsigned char character : value) {
        if (std::isalnum(character)) {
            result.push_back(static_cast<char>(std::tolower(character)));
            separator = false;
        } else if (!result.empty() && !separator) {
            result.push_back('-');
            separator = true;
        }
        if (result.size() == 96) break;
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result;
}
} // namespace

class SlintFrontendAdapter::BrowserWorker {
  public:
    struct Request {
        std::uint64_t id{};
        std::string category, location, filter, sort;
        std::size_t page{};
    };
    struct Result {
        std::uint64_t id{};
        SlintBrowserPageState state;
    };

    BrowserWorker() : thread_([this](std::stop_token stop) { run(stop); }) {}
    ~BrowserWorker() { shutdown(); }

    void submit(Request request) {
        std::lock_guard lock(mutex_);
        latest_id_.store(request.id, std::memory_order_release);
        request_ = std::move(request);
        condition_.notify_all();
    }
    std::optional<Result> take() {
        std::lock_guard lock(mutex_);
        auto result = std::move(result_);
        result_.reset();
        return result;
    }
    void shutdown() noexcept {
        if (!thread_.joinable()) return;
        thread_.request_stop();
        condition_.notify_all();
        thread_.join();
    }

  private:
    static Result enumerate(const Request &request, std::stop_token stop,
                            const std::atomic<std::uint64_t> &latest_id) {
        namespace fs = std::filesystem;
        Result result;
        result.id = request.id;
        auto &next = result.state;
        next.request_id = request.id;
        next.category = request.category;
        next.location = request.location;
        next.filter = request.filter;
        next.sort = request.sort;
        next.page = request.page;
        next.categories = {"projects", "samples", "instrument-presets",
                           "fx-presets", "other"};
        std::vector<SlintBrowserEntry> all;
        try {
            std::error_code error;
            const auto location = fs::absolute(request.location, error).lexically_normal();
            if (error || !fs::is_directory(location, error))
                throw std::runtime_error(error ? error.message()
                                               : "Browser location is not a directory");
            next.location = location.string();
            const auto filter = lower(request.filter);
            if (request.category == "instrument-presets" ||
                request.category == "fx-presets") {
                PresetLibrary library(location);
                const auto type = request.category == "fx-presets"
                    ? std::optional<PresetType>(PresetType::fx_chain) : std::nullopt;
                for (const auto &metadata : library.enumerate(type)) {
                    if (stop.stop_requested() ||
                        latest_id.load(std::memory_order_acquire) != request.id)
                        return result;
                    if (request.category == "instrument-presets" &&
                        metadata.type == PresetType::fx_chain) continue;
                    if (!filter.empty() &&
                        lower(metadata.name).find(filter) == std::string::npos &&
                        lower(metadata.id).find(filter) == std::string::npos) continue;
                    std::string detail = std::string(preset_type_name(metadata.type));
                    try {
                        const auto value = library.load(metadata.id, false);
                        const auto validation = validate_preset(value, true);
                        detail += validation.missing_assets.empty()
                            ? " — compatible"
                            : " — missing " +
                                std::to_string(validation.missing_assets.size()) +
                                " asset(s)";
                    } catch (const std::exception &exception) {
                        detail += " — invalid: " + std::string(exception.what());
                    }
                    all.push_back({metadata.id, metadata.name,
                        (location / metadata.id).string(),
                        request.category == "fx-presets" ? "FX preset" : "Instrument preset",
                        std::move(detail), request.category == "fx-presets"
                            ? SlintBrowserEntryKind::fx_preset
                            : SlintBrowserEntryKind::instrument_preset,
                        0, 0, false, true, false});
                }
            } else {
                const auto requested_chunk = request.page / browser_chunk_pages;
                const auto window_begin = requested_chunk * browser_chunk_size;
                const auto window_end = window_begin + browser_chunk_size;
                std::size_t matching{};
                fs::directory_iterator iterator(location,
                    fs::directory_options::skip_permission_denied, error), end;
                for (; !error && iterator != end; iterator.increment(error)) {
                    if (stop.stop_requested() ||
                        latest_id.load(std::memory_order_acquire) != request.id)
                        return result;
                    const auto &entry = *iterator;
                    if (!browser_file_matches(request.category, entry)) continue;
                    const auto name = entry.path().filename().string();
                    if (!filter.empty() && lower(name).find(filter) == std::string::npos)
                        continue;
                    SlintBrowserEntry item;
                    item.id = entry.path().lexically_normal().string();
                    item.name = name; item.path = item.id;
                    std::error_code entry_error;
                    item.directory = entry.is_directory(entry_error);
                    if (entry_error) continue;
                    if (item.directory) {
                        item.kind = SlintBrowserEntryKind::directory; item.type = "Folder";
                        if (request.category == "projects" && project_directory(entry.path())) {
                            item.kind = SlintBrowserEntryKind::project; item.type = "Project";
                        }
                    } else if (request.category == "samples") {
                        item.kind = SlintBrowserEntryKind::sample; item.type = "WAV sample";
                        item.size = entry.file_size(entry_error);
                    } else {
                        item.kind = SlintBrowserEntryKind::other;
                        item.type = entry.path().extension().string();
                        item.size = entry.file_size(entry_error);
                    }
                    entry_error.clear();
                    const auto modified = entry.last_write_time(entry_error);
                    if (!entry_error) item.modified = static_cast<std::int64_t>(
                        modified.time_since_epoch().count());
                    item.compatible = item.directory ||
                        item.kind == SlintBrowserEntryKind::sample ||
                        item.kind == SlintBrowserEntryKind::project;
                    if (matching >= window_begin && matching < window_end)
                        all.push_back(std::move(item));
                    ++matching;
                }
                if (error) throw fs::filesystem_error("directory listing", location, error);
                next.total_entries = matching;
            }
            const auto compare = [&](const auto &a, const auto &b) {
                if (a.directory != b.directory) return a.directory > b.directory;
                if (request.sort == "type" && a.type != b.type) return a.type < b.type;
                if (request.sort == "date" && a.modified != b.modified)
                    return a.modified > b.modified;
                return lower(a.name) < lower(b.name);
            };
            std::stable_sort(all.begin(), all.end(), compare);
            if (request.category == "instrument-presets" ||
                request.category == "fx-presets")
                next.total_entries = all.size();
            next.page_count = std::max<std::size_t>(1,
                (next.total_entries + browser_page_size - 1) /
                    browser_page_size);
            next.page = std::min(next.page, next.page_count - 1);
            const auto local_page =
                request.category == "instrument-presets" ||
                        request.category == "fx-presets"
                    ? next.page
                    : next.page % browser_chunk_pages;
            const auto begin = std::min(all.size(), local_page * browser_page_size);
            const auto finish = std::min(all.size(), begin + browser_page_size);
            next.entries.assign(std::make_move_iterator(all.begin() + begin),
                                std::make_move_iterator(all.begin() + finish));
            next.can_previous_page = next.page > 0;
            next.can_next_page = next.page + 1 < next.page_count;
            if (!next.entries.empty()) {
                next.entries.front().selected = true;
                next.preview = next.entries.front().name + " — " + next.entries.front().type;
            }
        } catch (const std::exception &exception) {
            next.error = exception.what();
        }
        next.loading = false;
        return result;
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Request request;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, stop, [&] { return request_.has_value(); });
                if (stop.stop_requested()) return;
                request = std::move(*request_); request_.reset();
            }
            auto result = enumerate(request, stop, latest_id_);
            if (stop.stop_requested()) return;
            std::lock_guard lock(mutex_);
            if (!request_ || request_->id <= result.id) result_ = std::move(result);
        }
    }

    std::mutex mutex_;
    std::condition_variable_any condition_;
    std::optional<Request> request_;
    std::optional<Result> result_;
    std::atomic<std::uint64_t> latest_id_{};
    std::jthread thread_;
};

std::string format_slint_musical_position(MusicalTick tick,
                                          std::uint16_t numerator,
                                          std::uint16_t denominator) {
    denominator = std::max<std::uint16_t>(1, denominator);
    numerator = std::max<std::uint16_t>(1, numerator);
    const auto beat_ticks =
        std::max<MusicalTick>(1, ticks_per_quarter * 4 / denominator);
    const auto bar_ticks = beat_ticks * numerator;
    return std::to_string(tick / bar_ticks + 1) + "." +
           std::to_string((tick % bar_ticks) / beat_ticks + 1) + "." +
           std::to_string(tick % beat_ticks);
}

std::string slint_recording_label(RecordingState state, bool playing) {
    if (state == RecordingState::recording)
        return "Recording";
    if (state == RecordingState::armed)
        return "Armed";
    return playing ? "Playing" : "Stopped";
}

SlintFrontendAdapter::SlintFrontendAdapter(ApplicationService &application,
                                           SlintSetupServices setup)
    : application_(application), bindings_(application),
      recording_workflow_(application),
      setup_services_(std::move(setup)), browser_worker_(std::make_unique<BrowserWorker>()) {
    if (setup_services_.snapshot) {
        const auto runtime = setup_services_.snapshot();
        staged_preferences_ = runtime.requested;
        setup_runtime_revision_ = runtime.revision;
    }
    (void)refresh();
    (void)navigate_browser(std::filesystem::current_path().string());
}

SlintFrontendAdapter::~SlintFrontendAdapter() { shutdown(); }

void SlintFrontendAdapter::shutdown() noexcept {
    active_ = false;
    if (browser_worker_) browser_worker_->shutdown();
}

bool SlintFrontendAdapter::refresh() {
    const auto refresh_started = std::chrono::steady_clock::now();
    ++refresh_diagnostics_.calls;
    const auto finish = [&](SlintRefreshKind kind) {
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - refresh_started).count());
        refresh_diagnostics_.maximum_refresh_nanoseconds =
            std::max(refresh_diagnostics_.maximum_refresh_nanoseconds, elapsed);
        refresh_diagnostics_.last_kind = kind;
    };
    if (!active_)
        return false;
    if (browser_worker_)
        if (auto completed = browser_worker_->take();
            completed && completed->id == state_.browser.request_id) {
            const auto missing = state_.browser.missing_assets;
            const auto selected_missing = state_.browser.selected_missing;
            const auto confirmation_pending = state_.browser.confirmation_pending;
            const auto confirmation_action = state_.browser.confirmation_action;
            const auto confirmation_message = state_.browser.confirmation_message;
            state_.browser = std::move(completed->state);
            state_.browser.missing_assets = missing;
            state_.browser.selected_missing = std::min(
                selected_missing, missing.empty() ? std::size_t{} : missing.size() - 1);
            state_.browser.confirmation_pending = confirmation_pending;
            state_.browser.confirmation_action = confirmation_action;
            state_.browser.confirmation_message = confirmation_message;
            last_error_ = state_.browser.error;
            ++local_revision_;
        }
    const auto snapshot_started = std::chrono::steady_clock::now();
    auto snapshot_handle = application_.snapshot_shared();
    if (process_interaction_request(*snapshot_handle))
        snapshot_handle = application_.snapshot_shared();
    const auto &snapshot = *snapshot_handle;
    refresh_diagnostics_.snapshot_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - snapshot_started).count());
    std::optional<SlintSetupRuntimeSnapshot> setup_runtime;
    if (setup_services_.snapshot) {
        setup_runtime = setup_services_.snapshot();
        if (setup_runtime->revision != setup_runtime_revision_) {
            setup_runtime_revision_ = setup_runtime->revision;
            ++local_revision_;
        }
    }
    auto fingerprint = slint_structural_fingerprint(snapshot);
    if (state_.sequencer.playhead_follow && snapshot.playing &&
        snapshot.step_ticks > 0)
        hash_combine(fingerprint, static_cast<std::uint64_t>(
            snapshot.transport_tick / snapshot.step_ticks / 16));
    if (structural_fingerprint_ != 0 && fingerprint == structural_fingerprint_ &&
        published_local_revision_ == local_revision_) {
        const auto before_revision = state_.shell.revision;
        const auto before_position = state_.shell.position;
        const auto before_transport = state_.shell.transport;
        const auto before_recording_phase = state_.recording.phase;
        const auto before_recording_progress = state_.recording.progress;
        const auto before_processing_status = state_.sample.processing_status;
        const auto before_processing_progress = state_.sample.processing_progress;
        state_.shell.revision = snapshot.revision;
        state_.shell.playing = snapshot.playing;
        state_.shell.record_armed = snapshot.recording.state == RecordingState::armed;
        state_.shell.recording = snapshot.recording.state == RecordingState::recording;
        state_.shell.history_pending = snapshot.history_pending;
        state_.shell.can_undo = snapshot.can_undo;
        state_.shell.can_redo = snapshot.can_redo;
        state_.shell.undo_label = snapshot.undo_label;
        state_.shell.redo_label = snapshot.redo_label;
        state_.shell.position = format_slint_musical_position(
            snapshot.transport_tick, snapshot.time_signature_numerator,
            snapshot.time_signature_denominator);
        const auto recording = recording_workflow_.snapshot();
        state_.recording.phase = recording.status;
        state_.recording.captured_frames = recording.captured_frames;
        state_.recording.capacity_frames = recording.capacity_frames;
        state_.recording.overflow_count = recording.overflow_count;
        state_.recording.captured_events = recording.captured_events;
        state_.recording.progress = recording.progress;
        state_.recording.can_configure = recording.can_configure;
        state_.recording.can_arm = recording.can_arm;
        state_.recording.can_start = recording.can_start;
        state_.recording.can_stop = recording.can_stop;
        state_.recording.can_cancel = recording.can_cancel;
        state_.recording.can_commit =
            recording.phase == RecordingWorkflowPhase::finalizing;
        state_.recording.can_overdub = recording.can_overdub;
        state_.recording.can_replace = recording.can_replace;
        state_.recording.can_clear = recording.can_clear;
        state_.recording.can_save = recording.can_save;
        state_.recording.result_audio_active = recording.result_audio_active;
        state_.recording.result_pending = recording.result_pending;
        state_.recording.error = recording.error;
        state_.recording.session_id = recording.live_loop_session;
        state_.recording.loop_iteration = recording.loop_iteration;
        state_.recording.pass = recording.live_loop_mode;
        state_.recording.loops.clear();
        for (const auto &loop : recording.live_loop_sessions)
            state_.recording.loops.push_back({
                loop.id,
                "Loop " + std::to_string(loop.id) + " · Track " +
                    std::to_string(loop.configuration.destination_track),
                live_loop_state_name(loop.state),
                loop.mode == LiveLoopMode::overdub ? "overdub"
                    : loop.mode == LiveLoopMode::replace ? "replace" : "capture",
                loop.id == recording.live_loop_session,
                loop.state == LiveLoopState::playing});
        state_.shell.transport = recording.phase != RecordingWorkflowPhase::idle &&
                                         recording.phase != RecordingWorkflowPhase::configuring
                                     ? recording.status
                                     : slint_recording_label(snapshot.recording.state,
                                                             snapshot.playing);
        if (const auto processing = std::find_if(
                snapshot.sample_processing_jobs.begin(),
                snapshot.sample_processing_jobs.end(), [&](const auto &job) {
                    return job.id == snapshot.selected_processing_job;
                }); processing != snapshot.sample_processing_jobs.end()) {
            state_.sample.processing_status = sample_processing_status_name(processing->status);
            state_.sample.processing_progress = processing->progress;
            state_.sample.processing_can_cancel =
                processing->status == SampleProcessingStatus::draft ||
                processing->status == SampleProcessingStatus::running;
            state_.sample.processing_can_publish =
                processing->status == SampleProcessingStatus::complete;
            state_.sample.processing_audio_active =
                processing->status == SampleProcessingStatus::audio_active;
        }
        const auto playhead = snapshot.step_ticks > 0
            ? snapshot.transport_tick / snapshot.step_ticks : MusicalTick{};
        for (std::size_t i = 0; i < state_.sequencer.steps.size(); ++i)
            state_.sequencer.steps[i].playhead =
                snapshot.playing && playhead == static_cast<MusicalTick>(
                    state_.sequencer.step_page * 16 + i);
        std::size_t channel{};
        for (std::size_t i = 0; i < snapshot.mixer.configuration.track_count &&
                                channel < state_.mixer.channels.size(); ++i, ++channel) {
            state_.mixer.channels[channel].meter_left = snapshot.mixer.tracks[i].left;
            state_.mixer.channels[channel].meter_right = snapshot.mixer.tracks[i].right;
            state_.mixer.channels[channel].clip = snapshot.mixer.tracks[i].clip;
        }
        for (std::size_t i = 0; i < snapshot.mixer.configuration.group_count &&
                                channel < state_.mixer.channels.size(); ++i, ++channel) {
            state_.mixer.channels[channel].meter_left = snapshot.mixer.groups[i].left;
            state_.mixer.channels[channel].meter_right = snapshot.mixer.groups[i].right;
            state_.mixer.channels[channel].clip = snapshot.mixer.groups[i].clip;
        }
        for (std::size_t i = 0; i < mixer_aux_count &&
                                channel < state_.mixer.channels.size(); ++i, ++channel) {
            state_.mixer.channels[channel].meter_left = snapshot.mixer.aux[i].left;
            state_.mixer.channels[channel].meter_right = snapshot.mixer.aux[i].right;
            state_.mixer.channels[channel].clip = snapshot.mixer.aux[i].clip;
        }
        if (channel < state_.mixer.channels.size()) {
            state_.mixer.channels[channel].meter_left = snapshot.mixer.master.left;
            state_.mixer.channels[channel].meter_right = snapshot.mixer.master.right;
            state_.mixer.channels[channel].clip = snapshot.mixer.master.clip;
        }
        state_.setup.callback_load = snapshot.performance.audio.average_load_percent;
        state_.setup.callback_peak_load = snapshot.performance.audio.peak_load_percent;
        for (auto &presented : state_.sound.parameters) {
            const auto parameter = std::find_if(
                snapshot.parameters.begin(), snapshot.parameters.end(),
                [&](const auto &candidate) { return candidate.id == presented.id; });
            if (parameter == snapshot.parameters.end()) continue;
            const auto current = numeric_value(parameter->current_value);
            presented.value = format_value(current);
            presented.normalized = presented.maximum > presented.minimum
                ? (current - presented.minimum) /
                      (presented.maximum - presented.minimum)
                : 0.0;
        }
        if (state_.fx.selected_effect < state_.fx.effects.size()) {
            const auto effect_id = state_.fx.effects[state_.fx.selected_effect].id;
            const auto &configuration = snapshot.mixer.configuration;
            const auto effect_end = configuration.effects.instances.begin() +
                                    configuration.effects.instance_count;
            const auto instance = std::find_if(
                configuration.effects.instances.begin(), effect_end,
                [&](const auto &candidate) { return candidate.id == effect_id; });
            if (instance != effect_end) {
                const auto descriptors = effect_parameter_descriptors(instance->type);
                for (auto &presented : state_.fx.parameters) {
                    const auto descriptor = std::find_if(
                        descriptors.begin(), descriptors.end(),
                        [&](const auto &candidate) {
                            return candidate.stable_id == presented.stable_id;
                        });
                    if (descriptor == descriptors.end()) continue;
                    const auto parameter_end = instance->parameters.begin() +
                                               instance->parameter_count;
                    const auto parameter = std::find_if(
                        instance->parameters.begin(), parameter_end,
                        [&](const auto &candidate) {
                            return candidate.id == descriptor->id;
                        });
                    const auto current = parameter == parameter_end
                        ? descriptor->default_value : parameter->value;
                    presented.value = format_value(current);
                    presented.normalized = presented.maximum > presented.minimum
                        ? (current - presented.minimum) /
                              (presented.maximum - presented.minimum)
                        : 0.0;
                }
            }
        }
        for (auto &presented : state_.fx.macros) {
            const auto effective = std::find_if(
                snapshot.performance_macros.effective_values.begin(),
                snapshot.performance_macros.effective_values.end(),
                [&](const auto &candidate) { return candidate.first == presented.id; });
            if (effective != snapshot.performance_macros.effective_values.end())
                presented.value = effective->second;
        }
        const auto context_name = state_.context.identity;
        const auto context = resolve_controller_context(
            snapshot, context_name, snapshot.modifiers);
        for (std::size_t i = 0; i < 8; ++i) {
            const auto copy = [](const ContextControl &from) {
                auto result = SlintContextItem{from.label.empty() ? "Unavailable" : from.label,
                    from.value.empty() ? "--" : from.value, from.unit,
                    from.parameter_id, from.action_id, from.arguments,
                    from.active, {}, {}, false};
                result.bank_id = from.bank_id;
                result.role = from.role;
                result.locked = from.locked;
                return result;
            };
            state_.context.encoders[i] = copy(context[i]);
            state_.context.functions[i] = copy(context[8 + i]);
        }
        const auto pad_fingerprint = slint_context_pad_fingerprint(
            snapshot, context_name, snapshot.modifiers);
        if (pad_fingerprint != context_pad_fingerprint_) {
            if (const auto cached = context_pad_cache_.find(pad_fingerprint);
                cached != context_pad_cache_.end()) {
                state_.context.pads = cached->second;
            } else {
                const auto context_pads = resolve_controller_pads(
                    snapshot, context_name, snapshot.modifiers);
                for (std::size_t i = 0; i < context_pads.size(); ++i) {
                    const auto &from = context_pads[i];
                    state_.context.pads[i] = {from.label, from.role, i,
                        from.active, from.queued, from.selected, from.available};
                }
                if (context_pad_cache_.size() >= 32) context_pad_cache_.clear();
                context_pad_cache_.emplace(pad_fingerprint,
                                           state_.context.pads);
            }
            context_pad_fingerprint_ = pad_fingerprint;
        }
        if (state_.shell.position == before_position &&
            state_.shell.transport == before_transport &&
            state_.recording.phase == before_recording_phase &&
            state_.recording.progress == before_recording_progress &&
            state_.sample.processing_status == before_processing_status &&
            state_.sample.processing_progress == before_processing_progress &&
            snapshot.revision == before_revision) {
            ++refresh_diagnostics_.skipped;
            finish(SlintRefreshKind::none);
            return false;
        }
        ++refresh_diagnostics_.telemetry_updates;
        finish(SlintRefreshKind::telemetry);
        return true;
    }
    const auto construction_started = std::chrono::steady_clock::now();
    SlintUiState next;
    next.text_entry = state_.text_entry;
    next.context.pads = state_.context.pads;
    next.shell.revision = snapshot.revision;
    next.shell.playing = snapshot.playing;
    next.shell.record_armed = snapshot.recording.state == RecordingState::armed;
    next.shell.recording = snapshot.recording.state == RecordingState::recording;
    next.shell.project_dirty = snapshot.project_dirty;
    next.shell.history_pending = snapshot.history_pending;
    next.shell.can_undo = snapshot.can_undo;
    next.shell.can_redo = snapshot.can_redo;
    next.shell.undo_label = snapshot.undo_label;
    next.shell.redo_label = snapshot.redo_label;
    next.shell.project_name = snapshot.project_name.empty() ? "Untitled"
                                                             : snapshot.project_name;
    next.shell.tempo_bpm = snapshot.tempo_bpm;
    next.shell.transport =
        slint_recording_label(snapshot.recording.state, snapshot.playing);
    const auto recording = recording_workflow_.snapshot();
    next.recording.visible = state_.recording.visible;
    next.recording.mode = recording_workflow_mode_name(
        recording.configuration.mode);
    next.recording.phase = recording.status;
    next.recording.source = recording.configuration.source;
    next.recording.destination_slot = recording.configuration.destination.value;
    next.recording.destination = "Sample slot " +
                                 std::to_string(recording.configuration.destination.value);
    next.recording.quantization = recording.configuration.quantization;
    next.recording.captured_frames = recording.captured_frames;
    next.recording.capacity_frames = recording.capacity_frames;
    next.recording.overflow_count = recording.overflow_count;
    next.recording.captured_events = recording.captured_events;
    next.recording.progress = recording.progress;
    next.recording.can_configure = recording.can_configure;
    next.recording.can_arm = recording.can_arm;
    next.recording.can_start = recording.can_start;
    next.recording.can_stop = recording.can_stop;
    next.recording.can_cancel = recording.can_cancel;
    next.recording.can_commit = recording.phase == RecordingWorkflowPhase::finalizing;
    next.recording.can_overdub = recording.can_overdub;
    next.recording.can_replace = recording.can_replace;
    next.recording.can_clear = recording.can_clear;
    next.recording.can_save = recording.can_save;
    next.recording.result_audio_active = recording.result_audio_active;
    next.recording.result_pending = recording.result_pending;
    next.recording.error = recording.error;
    next.recording.session_id = recording.live_loop_session;
    next.recording.loop_iteration = recording.loop_iteration;
    next.recording.pass = recording.live_loop_mode;
    for (const auto &loop : recording.live_loop_sessions)
        next.recording.loops.push_back({
            loop.id,
            "Loop " + std::to_string(loop.id) + " · Track " +
                std::to_string(loop.configuration.destination_track),
            live_loop_state_name(loop.state),
            loop.mode == LiveLoopMode::overdub ? "overdub"
                : loop.mode == LiveLoopMode::replace ? "replace" : "capture",
            loop.id == recording.live_loop_session,
            loop.state == LiveLoopState::playing});
    if (recording.phase != RecordingWorkflowPhase::idle &&
        recording.phase != RecordingWorkflowPhase::configuring)
        next.shell.transport = recording.status;
    next.shell.time_signature =
        std::to_string(snapshot.time_signature_numerator) + "/" +
        std::to_string(snapshot.time_signature_denominator);
    next.shell.position = format_slint_musical_position(
        snapshot.transport_tick, snapshot.time_signature_numerator,
        snapshot.time_signature_denominator);
    const auto *shared_destination =
        reference_destination(snapshot.interaction.destination);
    next.shell.page_id = shared_destination ? shared_destination->page_id
        : state_.shell.page_id.empty() ? "performance" : state_.shell.page_id;
    next.shell.destination_id = shared_destination
        ? shared_destination->id
        : state_.shell.destination_id.empty() ? next.shell.page_id
                                              : state_.shell.destination_id;
    next.shell.effective_shift = state_.shell.effective_shift;
    next.shell.root_revision = state_.shell.root_revision;
    next.shell.focus_identity = snapshot.interaction.focus_identity;
    next.shell.navigation_scope = snapshot.interaction.navigation_scope;
    next.shell.data_role = snapshot.interaction.data_role;
    next.shell.modal_identity = snapshot.interaction.modal.identity;
    next.shell.modal_active = !snapshot.interaction.modal.identity.empty();
    next.shell.modal_confirm_focused =
        snapshot.interaction.focus_identity == "modal.confirm";
    next.shell.page_title = shared_destination ? shared_destination->title
        : state_.shell.page_title.empty() ? "Performance"
                                          : state_.shell.page_title;
    const auto &destinations = next.shell.effective_shift
                                   ? reference_secondary_destinations()
                                   : reference_primary_destinations();
    for (const auto &destination : destinations)
        next.shell.pages.push_back({destination.id, destination.short_label,
                                    destination.available});
    std::uint64_t discovery_fingerprint{0xcbf29ce484222325ULL};
    for (const auto &action : snapshot.available_actions)
        hash_combine(discovery_fingerprint, std::hash<std::string>{}(action));
    for (const auto &parameter : snapshot.parameters)
    {
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(parameter.id));
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(parameter.name));
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(parameter.unit));
        hash_combine(discovery_fingerprint,
                     static_cast<std::uint64_t>(parameter.type));
        hash_combine(discovery_fingerprint,
                     std::hash<double>{}(parameter.step));
        hash_combine(discovery_fingerprint,
                     parameter.continuous | (parameter.automatable << 1U) |
                         (parameter.modulatable << 2U));
        for (const auto &choice : parameter.choices)
            hash_combine(discovery_fingerprint,
                         std::hash<std::string>{}(choice));
    }
    // Object choices are copied metadata used by typed object-reference fields.
    // Include stable identity and labels, but never mutable parameter values.
    for (const auto &track : snapshot.tracks) {
        hash_combine(discovery_fingerprint, track.id);
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(track.name));
    }
    for (const auto &pattern : snapshot.patterns) {
        hash_combine(discovery_fingerprint, pattern.id);
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(pattern.name));
    }
    for (const auto &macro : snapshot.performance_macros.macros) {
        hash_combine(discovery_fingerprint, macro.id);
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(macro.name));
    }
    for (const auto &scene : snapshot.performance_macros.scenes) {
        hash_combine(discovery_fingerprint, scene.id);
        hash_combine(discovery_fingerprint,
                     std::hash<std::string>{}(scene.name));
    }
    if (assignment_discovery_fingerprint_ != discovery_fingerprint) {
        ++refresh_diagnostics_.assignment_discovery_rebuilds;
        cached_performance_parameters_.clear();
        cached_performance_actions_.clear();
        cached_setup_targets_.clear();
        AssignmentTargetPicker picker(application_);
        for (const auto &target : picker.targets()) {
            auto kind = std::string("performance-role");
            if (target.kind == AssignmentTargetKind::action) kind = "action";
            else if (target.kind == AssignmentTargetKind::parameter) kind = "parameter";
            else if (target.kind == AssignmentTargetKind::contextual_role)
                kind = "contextual-role";
            cached_setup_targets_.push_back(
                {std::move(kind), target.id, target.label, false,
                 target.arguments});
        }
        for (const auto &target : picker.targets([](const auto &action) {
                 return performance_safe_assignment_action(action);
             })) {
            auto item = SlintPerformancePageState::AssignmentTargetItem{
                target.kind == AssignmentTargetKind::action ? "action"
                                                             : "parameter",
                target.id, target.label, target.category, false,
                target.available, target.arguments};
            if (target.kind == AssignmentTargetKind::action)
                cached_performance_actions_.push_back(std::move(item));
            else if (target.kind == AssignmentTargetKind::parameter)
                cached_performance_parameters_.push_back(std::move(item));
        }
        assignment_discovery_fingerprint_ = discovery_fingerprint;
    }
    next.performance.launch_quantization =
        quantization_name(snapshot.launch_quantization);
    for (const auto &bank : snapshot.performance_banks.banks) {
        const auto selected = bank.id == snapshot.performance_banks.active_bank;
        next.performance.banks.push_back({bank.id, bank.name, selected});
        if (selected) next.performance.bank_name = bank.name;
    }
    next.performance.selected_assignment =
        snapshot.performance_banks.selected_assignment;
    next.performance.editor_visible = state_.performance.editor_visible;
    next.performance.editor_position = state_.performance.editor_position;
    next.performance.editor_secondary = state_.performance.editor_secondary;
    next.performance.editor_action = state_.performance.editor_action;
    next.performance.editor_pad = state_.performance.editor_pad;
    next.performance.editor_label = state_.performance.editor_label;
    next.performance.editor_behavior = state_.performance.editor_behavior;
    next.performance.editor_argument_names =
        state_.performance.editor_argument_names;
    next.performance.editor_argument_values =
        state_.performance.editor_argument_values;
    next.performance.editor_argument_hints =
        state_.performance.editor_argument_hints;
    next.performance.selected_assignment_target =
        state_.performance.selected_assignment_target;
    next.performance.assignment_targets = next.performance.editor_action
        ? cached_performance_actions_ : cached_performance_parameters_;
    if (next.performance.editor_pad) {
        next.performance.assignment_targets.clear();
        for (const auto &slot : snapshot.performance_launching.slots)
            next.performance.assignment_targets.push_back(
                {"launch-slot", std::to_string(slot.id),
                 "Launch slot " + std::to_string(slot.id), "Musical launch",
                 false, true, {}});
    }
    if (next.performance.selected_assignment_target >=
        next.performance.assignment_targets.size())
        next.performance.selected_assignment_target = 0;
    if (!next.performance.assignment_targets.empty())
        next.performance.assignment_targets[
            next.performance.selected_assignment_target].selected = true;
    const PerformanceBank *position_bank{};
    for (const auto &bank : snapshot.performance_banks.banks)
        if (bank.id == snapshot.performance_banks.active_bank) position_bank = &bank;
    for (std::uint32_t position = 0; position < 8; ++position) {
        for (const bool secondary : {false, true}) {
            const auto parameter_assigned = position_bank && std::any_of(
                position_bank->parameters.begin(), position_bank->parameters.end(),
                [&](const auto &value) { return value.position == position &&
                    value.secondary == secondary; });
            next.performance.assignment_positions.push_back(
                {position, std::string(secondary ? "SHIFT+P" : "P") +
                     std::to_string(position + 1), "Encoder", secondary, false,
                 false, parameter_assigned,
                 next.performance.editor_visible && !next.performance.editor_action &&
                     next.performance.editor_position == position &&
                     next.performance.editor_secondary == secondary});
            const auto action_assigned = position_bank && std::any_of(
                position_bank->actions.begin(), position_bank->actions.end(),
                [&](const auto &value) { return value.position == position &&
                    value.secondary == secondary; });
            next.performance.assignment_positions.push_back(
                {position, std::string(secondary ? "SHIFT+F" : "F") +
                     std::to_string(position + 1), "Function", secondary, true,
                 false, action_assigned,
                 next.performance.editor_visible && next.performance.editor_action &&
                     next.performance.editor_position == position &&
                     next.performance.editor_secondary == secondary});
        }
    }
    for (std::uint32_t position = 0; position < 16; ++position) {
        const auto assigned = position_bank && std::any_of(
            position_bank->pads.begin(), position_bank->pads.end(),
            [&](const auto &value) { return value.position == position; });
        next.performance.assignment_positions.push_back(
            {position, "PAD" + std::to_string(position + 1), "Pad", false,
             false, true, assigned,
             next.performance.editor_visible && next.performance.editor_pad &&
                 next.performance.editor_position == position});
    }
    for (const auto &scene : snapshot.performance_launching.scenes) {
        const auto queued = std::any_of(
            snapshot.performance_launching.pending.begin(),
            snapshot.performance_launching.pending.end(),
            [&](const auto &launch) { return launch.scene == scene.id; });
        next.performance.scenes.push_back(
            {scene.name.empty() ? "Scene " + std::to_string(scene.id)
                                : scene.name,
             queued});
    }
    if (const auto track = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(), [&](const auto &t) {
                return t.id == snapshot.selected_track;
            });
        track != snapshot.tracks.end())
        next.performance.selected_track = next.sequencer.selected_track =
            track->name.empty() ? "Track " + std::to_string(track->id)
                                : track->name;
    if (snapshot.active_pattern != 0)
        next.performance.active_content =
            "Pattern " + std::to_string(snapshot.active_pattern);
    else if (snapshot.playback_mode == TimelinePlaybackMode::song &&
             !snapshot.audio_clips.empty())
        next.performance.active_content = "Arrangement";

    if (const auto owner = std::find_if(
            snapshot.performance_launching.ownership.begin(),
            snapshot.performance_launching.ownership.end(), [&](const auto &v) {
                return v.track == snapshot.selected_track;
            });
        owner != snapshot.performance_launching.ownership.end()) {
        switch (owner->owner) {
        case TrackPlaybackOwner::arrangement: next.performance.track_owner = "Arrangement"; break;
        case TrackPlaybackOwner::session: next.performance.track_owner = "Session"; break;
        case TrackPlaybackOwner::transitioning_to_session:
            next.performance.track_owner = "Queued Session"; break;
        case TrackPlaybackOwner::transitioning_to_arrangement:
            next.performance.track_owner = "Queued Arrangement"; break;
        }
    }

    // Destination identity is semantic. Secondary destinations may reuse a
    // page component but must not inherit that component's contextual bank.
    const auto &context_page = next.shell.destination_id;
    next.context.bank = context_bank_ == 0 ? "Primary" : "Secondary";
    next.context.identity = std::string(context_page) +
                            (context_bank_ == 0 ? ":primary" : ":secondary");
    const auto &context_name = next.context.identity;
    const auto context =
        resolve_controller_context(snapshot, context_name, snapshot.modifiers);
    for (std::size_t i = 0; i < 8; ++i) {
        const auto copy = [](const ContextControl &from) {
            auto result = SlintContextItem{from.label.empty() ? "Unavailable"
                                                       : from.label,
                                    from.value.empty() ? "--" : from.value,
                                    from.unit,
                                    from.parameter_id,
                                    from.action_id,
                                    from.arguments,
                                    from.active, {}, {}, false};
            result.bank_id = from.bank_id;
            result.role = from.role;
            result.locked = from.locked;
            return result;
        };
        next.context.encoders[i] = copy(context[i]);
        next.context.functions[i] = copy(context[8 + i]);
    }
    const auto pad_fingerprint = slint_context_pad_fingerprint(
        snapshot, context_name, snapshot.modifiers);
    if (pad_fingerprint != context_pad_fingerprint_) {
        if (const auto cached = context_pad_cache_.find(pad_fingerprint);
            cached != context_pad_cache_.end()) {
            next.context.pads = cached->second;
        } else {
            const auto context_pads =
                resolve_controller_pads(snapshot, context_name, snapshot.modifiers);
            for (std::size_t i = 0; i < context_pads.size(); ++i) {
                const auto &from = context_pads[i];
                next.context.pads[i] = {from.label, from.role, i,
                    from.active, from.queued, from.selected, from.available};
            }
            if (context_pad_cache_.size() >= 32) context_pad_cache_.clear();
            context_pad_cache_.emplace(pad_fingerprint, next.context.pads);
        }
        context_pad_fingerprint_ = pad_fingerprint;
    }

    // The current reference surface exposes sixteen positions, but assignments
    // are explicit logical metadata rather than collection order or protocol
    // control IDs. Other clients may expose a different position count.
    const auto &slots = snapshot.performance_launching.slots;
    for (const auto &slot : slots)
        if (slot.logical_position < 0)
            next.performance.unassigned_slots.push_back(slot.id);
    for (std::size_t i = 0; i < next.performance.pads.size(); ++i) {
        auto &pad = next.performance.pads[i];
        pad.label = std::to_string(i + 1);
        pad.selected = snapshot.selected_pad == i;
        const auto bank = std::find_if(
            snapshot.performance_banks.banks.begin(),
            snapshot.performance_banks.banks.end(), [&](const auto &value) {
                return value.id == snapshot.performance_banks.active_bank;
            });
        const auto bank_assignment = bank == snapshot.performance_banks.banks.end()
            ? static_cast<const PerformancePadAssignment *>(nullptr)
            : [&]() -> const PerformancePadAssignment * {
                  const auto found = std::find_if(
                      bank->pads.begin(), bank->pads.end(), [&](const auto &value) {
                          return value.position == i;
                      });
                  return found == bank->pads.end() ? nullptr : &*found;
              }();
        const auto assigned = std::find_if(
            slots.begin(), slots.end(), [&](const auto &slot) {
                return bank_assignment ? slot.id == bank_assignment->launch_slot
                                       : snapshot.performance_banks.banks.empty() &&
                                             slot.logical_position ==
                                                 static_cast<std::int32_t>(i);
            });
        if (assigned != slots.end()) {
            const auto &slot = *assigned;
            pad.slot_id = slot.id;
            pad.available = true;
            pad.label = bank_assignment && !bank_assignment->label.empty()
                            ? bank_assignment->label
                            : "Slot " + std::to_string(slot.id);
            const auto pending = std::find_if(
                snapshot.performance_launching.pending.begin(),
                snapshot.performance_launching.pending.end(),
                [&](const auto &launch) { return launch.slot == slot.id; });
            pad.queued =
                pending != snapshot.performance_launching.pending.end();
            const auto ownership = std::find_if(
                snapshot.performance_launching.ownership.begin(),
                snapshot.performance_launching.ownership.end(),
                [&](const auto &owner) {
                    return owner.slot == slot.id &&
                           owner.owner == TrackPlaybackOwner::session;
                });
            pad.active =
                ownership != snapshot.performance_launching.ownership.end();
            pad.status = pad.queued ? "Queued" : pad.active ? "Playing" : "Stopped";
        }
        if (bank_assignment && assigned == slots.end()) {
            pad.label = bank_assignment->label.empty()
                            ? "Missing slot " +
                                  std::to_string(bank_assignment->launch_slot)
                            : bank_assignment->label;
            pad.status = "Unavailable";
        }
    }

    next.sequencer.playhead_follow = state_.sequencer.playhead_follow;
    next.sequencer.selected_lock_index = state_.sequencer.selected_lock_index;
    next.sequencer.confirm_delete_track = state_.sequencer.confirm_delete_track;
    next.sequencer.confirm_delete_pattern = state_.sequencer.confirm_delete_pattern;
    next.sequencer.confirm_delete_clip = state_.sequencer.confirm_delete_clip;
    next.sequencer.resolution = step_resolution_name(snapshot.step_resolution);
    next.sequencer.arrangement_view = shared_destination
        ? shared_destination->id == "song" ||
              snapshot.interaction.subview == "arrangement"
        : state_.sequencer.arrangement_view;
    next.sequencer.selected_clip_id = state_.sequencer.selected_clip_id;
    next.sequencer.selected_clip_kind = state_.sequencer.selected_clip_kind;
    const auto displayed_pattern = snapshot.selected_pattern;
    next.sequencer.selected_pattern_id = displayed_pattern;
    for (const auto &pattern : snapshot.patterns) {
        next.sequencer.patterns.push_back(
            {pattern.id, pattern.name.empty() ? "Pattern " + std::to_string(pattern.id)
                                               : pattern.name,
             pattern.id == displayed_pattern,
             pattern.id == snapshot.active_pattern,
             pattern.id == snapshot.queued_pattern});
        if (pattern.id == displayed_pattern) {
            next.sequencer.selected_pattern = next.sequencer.patterns.back().name;
            next.sequencer.pattern_length = std::to_string(pattern.length) + " ticks";
        }
    }
    const auto track = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                    [&](const auto &v) { return v.id == snapshot.selected_track; });
    const auto playhead = snapshot.step_ticks > 0
        ? (snapshot.transport_tick / snapshot.step_ticks) : MusicalTick{};
    for (const auto &candidate : snapshot.tracks)
        next.sequencer.tracks.push_back({candidate.id, candidate.name,
            candidate.instrument_id, candidate.id == snapshot.selected_track,
            candidate.muted});
    const auto step_count = track == snapshot.tracks.end() || snapshot.step_ticks <= 0
        ? std::uint64_t{}
        : static_cast<std::uint64_t>((track->pattern_length + snapshot.step_ticks - 1) /
                                     snapshot.step_ticks);
    next.sequencer.step_page_count = std::max<std::uint64_t>(1, (step_count + 15) / 16);
    next.sequencer.step_page = next.sequencer.playhead_follow && snapshot.playing
        ? std::min<std::uint64_t>(static_cast<std::uint64_t>(playhead) / 16,
                                  next.sequencer.step_page_count - 1)
        : std::min(snapshot.step_page, next.sequencer.step_page_count - 1);
    next.sequencer.page_label = "Page " +
        std::to_string(next.sequencer.step_page + 1) + "/" +
        std::to_string(next.sequencer.step_page_count);
    next.sequencer.can_page_previous = next.sequencer.step_page > 0;
    next.sequencer.can_page_next =
        next.sequencer.step_page + 1 < next.sequencer.step_page_count;
    for (std::size_t i = 0; i < next.sequencer.steps.size(); ++i) {
        const auto step = next.sequencer.step_page * 16 + i;
        auto &item = next.sequencer.steps[i];
        item.label = std::to_string(step + 1);
        item.selected = snapshot.selected_step == static_cast<std::int64_t>(step);
        item.playhead = playhead == static_cast<MusicalTick>(step);
        item.available = track != snapshot.tracks.end() && snapshot.step_ticks > 0 &&
                         static_cast<MusicalTick>(step) * snapshot.step_ticks <
                             track->pattern_length;
        item.active = item.available && std::any_of(
            track->events.begin(), track->events.end(), [&](const auto &event) {
                return event.type == MusicalEventType::note &&
                       event.tick / snapshot.step_ticks == static_cast<MusicalTick>(step);
            });
        item.status = item.active ? "Note" : "Empty";
    }
    if (snapshot.selected_event) {
        next.sequencer.has_selected_event = true;
        next.sequencer.selected_velocity =
            std::to_string(static_cast<int>(snapshot.selected_event->velocity * 127.0F));
        next.sequencer.selected_duration =
            std::to_string(snapshot.selected_event->duration) + " ticks";
        next.sequencer.selected_pitch = std::to_string(snapshot.selected_event->pitch);
        next.sequencer.selected_probability =
            std::to_string(static_cast<int>(snapshot.selected_event->trigger_probability * 100.0F)) + "%";
        next.sequencer.selected_condition = std::string(
            slint_trigger_condition_name(snapshot.selected_event->trigger_condition));
        if (snapshot.selected_event->trigger_condition == TriggerCondition::every_nth_loop ||
            snapshot.selected_event->trigger_condition == TriggerCondition::n_of_m)
            next.sequencer.selected_condition += " " +
                std::to_string(snapshot.selected_event->condition_n) + "/" +
                std::to_string(snapshot.selected_event->condition_m);
        next.sequencer.selected_ratchet =
            std::to_string(snapshot.selected_event->ratchet_count) + "x";
        next.sequencer.selected_timing =
            std::to_string(snapshot.selected_event->timing_offset) + " ticks";
    }
    for (const auto &parameter : snapshot.parameters)
        if (parameter.modulatable)
            next.sequencer.lock_parameters.push_back(parameter.id);
    if (!next.sequencer.lock_parameters.empty()) {
        next.sequencer.selected_lock_index = std::min(
            next.sequencer.selected_lock_index,
            next.sequencer.lock_parameters.size() - 1);
        next.sequencer.lock_parameter =
            next.sequencer.lock_parameters[next.sequencer.selected_lock_index];
        const auto lock = std::find_if(snapshot.selected_step_locks.begin(),
            snapshot.selected_step_locks.end(), [&](const auto &candidate) {
                return candidate.parameter_id == next.sequencer.lock_parameter;
            });
        next.sequencer.lock_available = next.sequencer.has_selected_event;
        next.sequencer.lock_value = lock == snapshot.selected_step_locks.end()
            ? "Not set" : format_value(lock->value);
    }
    for (const auto &clip : snapshot.clips)
        next.sequencer.arrangement.push_back(
            {clip.id, "Pattern " + std::to_string(clip.pattern),
             std::to_string(clip.start) + " + " + std::to_string(clip.duration),
             "Pattern", 0, clip.pattern, clip.start, clip.duration,
             clip.id == next.sequencer.selected_clip_id &&
                 next.sequencer.selected_clip_kind == "Pattern",
             clip.enabled});
    for (const auto &clip : snapshot.audio_clips)
        next.sequencer.arrangement.push_back(
            {clip.clip.id, "Audio " + std::to_string(clip.clip.id),
             std::to_string(clip.clip.start) + " + " + std::to_string(clip.clip.duration),
             "Audio", clip.clip.track, clip.clip.sample_slot,
             clip.clip.start, clip.clip.duration,
             clip.clip.id == next.sequencer.selected_clip_id &&
                 next.sequencer.selected_clip_kind == "Audio",
             clip.clip.enabled});
    if (next.sequencer.selected_clip_id != 0 &&
        std::none_of(next.sequencer.arrangement.begin(),
                     next.sequencer.arrangement.end(), [&](const auto &item) {
                         return item.id == next.sequencer.selected_clip_id &&
                                item.kind == next.sequencer.selected_clip_kind;
                     })) {
        next.sequencer.selected_clip_id = 0;
        next.sequencer.selected_clip_kind.clear();
        next.sequencer.confirm_delete_clip = false;
    }
    next.sequencer.loop_range = snapshot.arrangement_loop_enabled
        ? std::to_string(snapshot.arrangement_loop_start) + " - " +
              std::to_string(snapshot.arrangement_loop_end)
        : "Off";

    next.sample.slot = snapshot.selected_sample_slot.value;
    next.sample.zoom = state_.sample.zoom <= 0 ? 1.0F : state_.sample.zoom;
    next.sample.offset = state_.sample.offset;
    if (const auto slot = std::find_if(
            snapshot.sample_slots.begin(), snapshot.sample_slots.end(),
            [&](const auto &value) {
                return value.id == snapshot.selected_sample_slot;
            });
        slot != snapshot.sample_slots.end()) {
        next.sample.active_revision = slot->active_revision;
        next.sample.frames = slot->frames;
        next.sample.sample_rate = slot->source_sample_rate;
        next.sample.channels = slot->channels;
        next.sample.duration_seconds = slot->duration_seconds;
        next.sample.name = slot->source_path.empty()
            ? "Slot " + std::to_string(slot->id.value)
            : slot->source_path.filename().string();
        next.sample.error = slot->error;
        next.sample.ready = slot->state == SampleSlotLoadState::loaded;
        next.sample.loading = slot->state == SampleSlotLoadState::queued ||
                              slot->state == SampleSlotLoadState::preparing ||
                              slot->state == SampleSlotLoadState::prepared ||
                              slot->state == SampleSlotLoadState::pending_activation;
        next.sample.missing = slot->state == SampleSlotLoadState::error;
        switch (slot->state) {
        case SampleSlotLoadState::empty: next.sample.status = "Empty"; break;
        case SampleSlotLoadState::queued: next.sample.status = "Queued"; break;
        case SampleSlotLoadState::preparing: next.sample.status = "Loading"; break;
        case SampleSlotLoadState::prepared: next.sample.status = "Prepared"; break;
        case SampleSlotLoadState::pending_activation:
            next.sample.status = "Activating"; break;
        case SampleSlotLoadState::loaded: next.sample.status = "Ready"; break;
        case SampleSlotLoadState::error: next.sample.status = "Error"; break;
        }
        next.sample.metadata = slot->has_sample
            ? std::to_string(slot->source_sample_rate) + " Hz  " +
                  std::to_string(slot->channels) + " ch  " +
                  std::to_string(slot->frames) + " frames"
            : "No active sample";
    }
    next.sample.waveform.reserve(snapshot.waveform_peaks.size());
    for (const auto &peak : snapshot.waveform_peaks)
        next.sample.waveform.push_back(
            {peak.left_min, peak.left_max, peak.right_min, peak.right_max});
    for (const auto &slice : snapshot.slices) {
        const auto assigned = std::any_of(
            snapshot.slice_pad_assignments.begin(),
            snapshot.slice_pad_assignments.end(),
            [&](const auto &id) { return id && *id == slice.id; });
        next.sample.slices.push_back(
            {slice.id, slice.start_frame, slice.end_frame,
             slice.name.empty() ? "Slice " + std::to_string(slice.id)
                                : slice.name,
             slice.id == snapshot.selected_slice, assigned});
    }
    for (const auto &region : snapshot.regions) {
        next.sample.regions.push_back(
            {region.id, region.start_frame, region.end_frame,
             region.name.empty() ? "Region " + std::to_string(region.id)
                                 : region.name,
             std::to_string(region.fade_in_frames) + "/" +
                 std::to_string(region.fade_out_frames),
             region.loop_enabled
                 ? std::to_string(region.loop_start_frame) + ".." +
                       std::to_string(region.loop_end_frame)
                 : "Off",
             region.id == snapshot.selected_region});
        if (region.id == snapshot.selected_region) {
            next.sample.region_start = region.start_frame;
            next.sample.region_end = region.end_frame;
            next.sample.fade_in = region.fade_in_frames;
            next.sample.fade_out = region.fade_out_frames;
            next.sample.loop_enabled = region.loop_enabled;
            next.sample.loop_start = region.loop_start_frame;
            next.sample.loop_end = region.loop_end_frame;
        }
    }
    if (const auto analysis = std::find_if(
            snapshot.sample_analysis_sessions.begin(),
            snapshot.sample_analysis_sessions.end(), [&](const auto &value) {
                return value.id == snapshot.selected_analysis_session;
            });
        analysis != snapshot.sample_analysis_sessions.end()) {
        next.sample.analysis_status = sample_analysis_status_name(analysis->status);
        if (!analysis->error.empty())
            next.sample.error = analysis->error;
    }
    if (const auto processing = std::find_if(
            snapshot.sample_processing_jobs.begin(),
            snapshot.sample_processing_jobs.end(), [&](const auto &value) {
                return value.id == snapshot.selected_processing_job;
            });
        processing != snapshot.sample_processing_jobs.end()) {
        next.sample.processing_status =
            sample_processing_status_name(processing->status);
        next.sample.processing_job = processing->id;
        next.sample.processing_progress = processing->progress;
        next.sample.processing_destination = processing->result.destination.value;
        next.sample.processing_can_cancel =
            processing->status == SampleProcessingStatus::draft ||
            processing->status == SampleProcessingStatus::running;
        next.sample.processing_can_publish =
            processing->status == SampleProcessingStatus::complete;
        next.sample.processing_audio_active =
            processing->status == SampleProcessingStatus::audio_active;
        if (!processing->error.empty())
            next.sample.error = processing->error;
    }

    next.sound.subview = state_.sound.subview.empty() ? "instrument"
                                                       : state_.sound.subview;
    next.sound.preset_library = state_.sound.preset_library;
    next.sound.preset_id = state_.sound.preset_id;
    next.sound.selected_parameter = state_.sound.selected_parameter;
    next.sound.selected_modulation = state_.sound.selected_modulation;
    if (const auto track = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(), [&](const auto &value) {
                return value.id == snapshot.selected_track;
            });
        track != snapshot.tracks.end()) {
        next.sound.track_id = track->id;
        next.sound.track = track->name.empty()
            ? "Track " + std::to_string(track->id) : track->name;
        next.sound.instrument_id = track->instrument_id;
        next.sound.instrument_instance = track->instrument_instance_id;
        next.sound.synth = track->instrument_id == subtractive_instrument_id;
        next.sound.sample = track->instrument_id == sample_instrument_id;
        next.sound.instrument_available = next.sound.synth || next.sound.sample;
        next.sound.instrument = next.sound.synth ? "Subtractive Synth"
            : next.sound.sample ? "Sample Instrument" : "Unavailable";
    }
    next.sound.groups = next.sound.synth
        ? std::vector<std::string>{"Oscillator", "Filter", "Envelope", "Output"}
        : std::vector<std::string>{"Playback", "Envelope", "Voice"};
    const auto prefix = next.sound.synth ? std::string_view("synth.")
                                         : std::string_view("sample.");
    for (const auto &parameter : snapshot.parameters) {
        if (parameter.id.rfind(prefix, 0) != 0) continue;
        const auto minimum = numeric_value(parameter.minimum);
        const auto maximum = numeric_value(parameter.maximum);
        const auto current = numeric_value(parameter.current_value);
        std::string group = "Output";
        if (parameter.id.find("attack") != std::string::npos ||
            parameter.id.find("decay") != std::string::npos ||
            parameter.id.find("sustain") != std::string::npos ||
            parameter.id.find("release") != std::string::npos)
            group = "Envelope";
        else if (parameter.id.find("cutoff") != std::string::npos ||
                 parameter.id.find("resonance") != std::string::npos)
            group = "Filter";
        else if (parameter.id.find("waveform") != std::string::npos ||
                 parameter.id.find("pitch") != std::string::npos ||
                 parameter.id.find("pulse") != std::string::npos)
            group = next.sound.synth ? "Oscillator" : "Playback";
        next.sound.parameters.push_back(
            {parameter.id, parameter.name, format_value(current), parameter.unit,
             group, minimum, maximum,
             maximum > minimum ? (current - minimum) / (maximum - minimum) : 0.0,
             true, parameter.automatable, parameter.modulatable});
    }
    next.sound.sample_settings = snapshot.selected_pad_settings;
    for (std::size_t index = 0; index < snapshot.modulation.route_count; ++index) {
        const auto &route = snapshot.modulation.routes[index];
        const auto source = std::find_if(
            snapshot.modulation.modulators.begin(),
            snapshot.modulation.modulators.begin() +
                snapshot.modulation.modulator_count,
            [&](const auto &value) { return value.id == route.source_instance; });
        const auto local = route.track_id == snapshot.selected_track ||
            (route.destination_owner_explicit &&
             route.destination_owner_id == snapshot.selected_track) ||
            (source != snapshot.modulation.modulators.begin() +
                           snapshot.modulation.modulator_count &&
             source->owner_kind == ModulatorOwnerKind::track &&
             source->owner_id == snapshot.selected_track);
        if (!local) continue;
        next.sound.modulation.push_back(
            {route.id, route.source_instance,
             route.source_instance ? "Modulator " + std::to_string(route.source_instance)
                                   : "Source " + std::to_string(static_cast<unsigned>(route.source)),
             "Parameter " + std::to_string(route.destination.parameter),
             format_value(route.depth), route.enabled});
    }
    next.sound.preset_status = snapshot.preset_application.status;
    next.sound.error = snapshot.preset_application.error;
    if (next.sound.selected_parameter >= next.sound.parameters.size())
        next.sound.selected_parameter = 0;
    if (next.sound.selected_modulation >= next.sound.modulation.size())
        next.sound.selected_modulation = 0;

    const auto route_name = [](MixerBusId group, std::int8_t output) {
        if (group) return "Group " + std::to_string(group);
        if (output >= 0)
            return "Output " + std::to_string(output + 1) + "/" +
                   std::to_string(output + 2);
        return std::string("Master");
    };
    next.mixer.bank = state_.mixer.bank;
    next.mixer.selected_channel = state_.mixer.selected_channel;
    const auto &configuration = snapshot.mixer.configuration;
    for (std::size_t i = 0; i < configuration.track_count; ++i) {
        const auto &channel = configuration.tracks[i];
        const auto summary = std::find_if(
            snapshot.tracks.begin(), snapshot.tracks.end(),
            [&](const auto &track) { return track.id == channel.track; });
        next.mixer.channels.push_back(
            {channel.track,
             summary != snapshot.tracks.end() && !summary->name.empty()
                 ? summary->name : "Track " + std::to_string(channel.track),
             "Track", route_name(channel.destination_group, channel.output_pair),
             channel.gain, channel.pan, snapshot.mixer.tracks[i].left,
             snapshot.mixer.tracks[i].right, channel.aux_send, channel.mute,
             channel.solo, snapshot.mixer.tracks[i].clip,
             false, true});
    }
    for (std::size_t i = 0; i < configuration.group_count; ++i) {
        const auto &group = configuration.groups[i];
        const auto name = std::string(group.name.data());
        next.mixer.channels.push_back(
            {group.id, name.empty() ? "Group " + std::to_string(group.id) : name,
             "Group", route_name(group.destination_group, group.output_pair),
             group.gain, group.pan, snapshot.mixer.groups[i].left,
             snapshot.mixer.groups[i].right, group.aux_send, group.mute,
             group.solo, snapshot.mixer.groups[i].clip,
             false, true});
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i) {
        const auto &aux = configuration.aux[i];
        next.mixer.channels.push_back(
            {i, "Aux " + std::to_string(i + 1), "Aux Return",
             route_name(0, aux.output_pair), aux.return_gain, 0.0F,
             snapshot.mixer.aux[i].left, snapshot.mixer.aux[i].right, {},
             aux.mute, false, snapshot.mixer.aux[i].clip, false, true});
    }
    next.mixer.channels.push_back(
        {0, "Master", "Master",
         "Output " + std::to_string(configuration.master.output_left + 1) +
             "/" + std::to_string(configuration.master.output_right + 1),
         configuration.master.gain, 0.0F, snapshot.mixer.master.left,
         snapshot.mixer.master.right, {}, configuration.master.mute, false,
         snapshot.mixer.master.clip, false, true});
    if (next.mixer.channels.empty())
        next.mixer.error = "No mixer channels are available";
    if (next.mixer.selected_channel >= next.mixer.channels.size())
        next.mixer.selected_channel = 0;
    if (!next.mixer.channels.empty()) {
        next.mixer.channels[next.mixer.selected_channel].selected = true;
        next.mixer.selected_id = next.mixer.channels[next.mixer.selected_channel].id;
    }
    constexpr std::size_t mixer_bank_size = 8;
    const auto maximum_bank = next.mixer.channels.empty()
        ? std::size_t{} : (next.mixer.channels.size() - 1) / mixer_bank_size;
    next.mixer.bank = std::min(next.mixer.bank, maximum_bank);
    next.mixer.bank_label = "Channels " +
        std::to_string(next.mixer.bank * mixer_bank_size + 1) + "-" +
        std::to_string(std::min(next.mixer.channels.size(),
                                (next.mixer.bank + 1) * mixer_bank_size));
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        const auto &loop = configuration.external_fx[i];
        const auto name = std::string(loop.name.data());
        next.mixer.external_fx.push_back(
            {i, loop.latency_frames,
             name.empty() ? "External FX " + std::to_string(i + 1) : name,
             loop.enabled ? loop.bypass ? "Bypassed" : "Active" : "Disabled",
             "Send " + std::to_string(loop.send_left + 1) + "/" +
                 std::to_string(loop.send_right + 1) + " Return " +
                 std::to_string(loop.return_left + 1) + "/" +
                 std::to_string(loop.return_right + 1),
             loop.send_gain, loop.return_gain, loop.wet,
             snapshot.mixer.external_returns[i].left,
             snapshot.mixer.external_returns[i].right, loop.enabled,
             loop.bypass, loop.return_mute,
             snapshot.mixer.external_send_available[i],
             snapshot.mixer.external_return_available[i]});
    }

    next.fx.subview = state_.fx.subview.empty() ? "chain" : state_.fx.subview;
    next.fx.selected_owner = state_.fx.selected_owner;
    next.fx.selected_effect = state_.fx.selected_effect;
    next.fx.selected_parameter = state_.fx.selected_parameter;
    next.fx.selected_modulator_type = state_.fx.selected_modulator_type;
    next.fx.selected_modulator = state_.fx.selected_modulator;
    next.fx.selected_modulator_parameter = state_.fx.selected_modulator_parameter;
    next.fx.selected_destination = state_.fx.selected_destination;
    next.fx.selected_route = state_.fx.selected_route;
    next.fx.selected_macro = state_.fx.selected_macro;
    next.fx.selected_macro_assignment = state_.fx.selected_macro_assignment;
    next.fx.selected_scene = state_.fx.selected_scene;
    for (std::size_t i = 0; i < configuration.track_count; ++i) {
        const auto id = configuration.tracks[i].track;
        const auto summary = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
            [&](const auto &track) { return track.id == id; });
        next.fx.owners.push_back({"track",
            summary != snapshot.tracks.end() && !summary->name.empty()
                ? summary->name : "Track " + std::to_string(id),
            static_cast<std::int64_t>(i), false});
    }
    for (std::size_t i = 0; i < configuration.group_count; ++i) {
        const auto &group = configuration.groups[i];
        const auto name = std::string(group.name.data());
        next.fx.owners.push_back({"group", name.empty()
            ? "Group " + std::to_string(group.id) : name,
            static_cast<std::int64_t>(group.id), false});
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i)
        next.fx.owners.push_back({"aux", "Aux " + std::to_string(i + 1),
                                  static_cast<std::int64_t>(i), false});
    next.fx.owners.push_back({"master", "Master", 0, false});
    if (next.fx.selected_owner >= next.fx.owners.size()) next.fx.selected_owner = 0;
    const EffectChainConfiguration *fx_chain = nullptr;
    if (!next.fx.owners.empty()) {
        auto &owner = next.fx.owners[next.fx.selected_owner];
        owner.selected = true;
        next.fx.owner_kind = owner.kind;
        next.fx.owner_name = owner.name;
        next.fx.owner_index = owner.index;
        if (owner.kind == "track") fx_chain = &configuration.effects.tracks[owner.index];
        else if (owner.kind == "aux") fx_chain = &configuration.effects.aux[owner.index];
        else if (owner.kind == "master") fx_chain = &configuration.effects.master;
        else if (owner.kind == "group") {
            const auto group = std::find_if(configuration.groups.begin(),
                configuration.groups.begin() + configuration.group_count,
                [&](const auto &value) { return value.id == owner.index; });
            if (group != configuration.groups.begin() + configuration.group_count)
                fx_chain = &configuration.effects.groups[group - configuration.groups.begin()];
        }
    }
    const auto type_name = [](EffectProcessorType type) {
        switch (type) {
        case EffectProcessorType::gain: return std::string("Gain / Utility");
        case EffectProcessorType::stereo_delay: return std::string("Stereo Delay");
        case EffectProcessorType::low_pass_filter: return std::string("Low-pass Filter");
        case EffectProcessorType::saturation: return std::string("Saturation");
        }
        return std::string("Unknown");
    };
    if (fx_chain) {
        next.fx.chain_bypass = fx_chain->bypass;
        for (std::size_t position = 0; position < fx_chain->count; ++position) {
            const auto id = fx_chain->instances[position];
            const auto instance = std::find_if(configuration.effects.instances.begin(),
                configuration.effects.instances.begin() + configuration.effects.instance_count,
                [&](const auto &value) { return value.id == id; });
            if (instance == configuration.effects.instances.begin() +
                                configuration.effects.instance_count) continue;
            next.fx.effects.push_back({instance->id, type_name(instance->type),
                std::string(effect_processor_type_id(instance->type)),
                instance->bypass, false});
        }
    }
    if (next.fx.selected_effect >= next.fx.effects.size()) next.fx.selected_effect = 0;
    if (!next.fx.effects.empty()) {
        next.fx.effects[next.fx.selected_effect].selected = true;
        const auto id = next.fx.effects[next.fx.selected_effect].id;
        const auto instance = std::find_if(configuration.effects.instances.begin(),
            configuration.effects.instances.begin() + configuration.effects.instance_count,
            [&](const auto &value) { return value.id == id; });
        if (instance != configuration.effects.instances.begin() + configuration.effects.instance_count) {
            for (const auto &descriptor : effect_parameter_descriptors(instance->type)) {
                const auto value = std::find_if(instance->parameters.begin(),
                    instance->parameters.begin() + instance->parameter_count,
                    [&](const auto &item) { return item.id == descriptor.id; });
                const auto current = value == instance->parameters.begin() + instance->parameter_count
                    ? descriptor.default_value : value->value;
                next.fx.parameters.push_back({descriptor.id,
                    std::string(descriptor.stable_id), std::string(descriptor.name),
                    format_value(current), std::string(descriptor.unit),
                    descriptor.minimum, descriptor.maximum,
                    descriptor.maximum > descriptor.minimum
                        ? (current - descriptor.minimum) /
                              (descriptor.maximum - descriptor.minimum) : 0.0,
                    descriptor.automatable, descriptor.modulatable, false});
            }
        }
    }
    if (next.fx.selected_parameter >= next.fx.parameters.size()) next.fx.selected_parameter = 0;
    if (!next.fx.parameters.empty()) next.fx.parameters[next.fx.selected_parameter].selected = true;
    for (const auto &type : modulator_type_registry())
        if (type.type != ModulatorType::performance_macro)
            next.fx.modulator_types.push_back(
                {static_cast<std::int64_t>(type.type), std::string(type.name),
                 std::string(type.category), false});
    if (next.fx.selected_modulator_type >= next.fx.modulator_types.size())
        next.fx.selected_modulator_type = 0;
    if (!next.fx.modulator_types.empty())
        next.fx.modulator_types[next.fx.selected_modulator_type].selected = true;
    for (const auto &parameter : snapshot.parameters) {
        const auto destination = resolve_parameter_destination(parameter.id);
        if (!destination || !parameter.modulatable ||
            parameter.type != ParameterType::real)
            continue;
        next.fx.destinations.push_back(
            {parameter.id, parameter.name,
             format_value(numeric_value(parameter.minimum)) + ".." +
                 format_value(numeric_value(parameter.maximum)) + " " + parameter.unit,
             numeric_value(parameter.minimum), numeric_value(parameter.maximum), false});
    }
    for (const auto &parameter : next.fx.parameters)
        if (parameter.modulatable && next.fx.selected_effect < next.fx.effects.size())
            next.fx.destinations.push_back(
                {"fx." + std::to_string(next.fx.effects[next.fx.selected_effect].id) +
                     "." + std::to_string(parameter.id),
                 next.fx.effects[next.fx.selected_effect].name + " / " +
                     parameter.name,
                 format_value(parameter.minimum) + ".." +
                     format_value(parameter.maximum) + " " + parameter.unit,
                 parameter.minimum, parameter.maximum, false});
    if (next.fx.selected_destination >= next.fx.destinations.size())
        next.fx.selected_destination = 0;
    if (!next.fx.destinations.empty())
        next.fx.destinations[next.fx.selected_destination].selected = true;
    for (std::size_t i = 0; i < snapshot.modulation.modulator_count; ++i) {
        const auto &modulator = snapshot.modulation.modulators[i];
        const auto *descriptor = find_modulator_type(modulator.type);
        const auto custom = std::string(modulator.display_name.data());
        next.fx.modulators.push_back({modulator.id,
            custom.empty() ? "Modulator " + std::to_string(modulator.id) : custom,
            descriptor ? std::string(descriptor->name) : "Unknown",
            std::to_string(static_cast<unsigned>(modulator.owner_kind)) + ":" +
                std::to_string(modulator.owner_id), modulator.enabled, false});
    }
    if (next.fx.selected_modulator >= next.fx.modulators.size()) next.fx.selected_modulator = 0;
    if (!next.fx.modulators.empty()) next.fx.modulators[next.fx.selected_modulator].selected = true;
    if (!next.fx.modulators.empty()) {
        const auto selected_id = next.fx.modulators[next.fx.selected_modulator].id;
        const auto instance = std::find_if(
            snapshot.modulation.modulators.begin(),
            snapshot.modulation.modulators.begin() + snapshot.modulation.modulator_count,
            [=](const auto &value) { return value.id == selected_id; });
        if (instance != snapshot.modulation.modulators.begin() +
                            snapshot.modulation.modulator_count)
            if (const auto *type = find_modulator_type(instance->type))
                for (const auto &parameter : type->parameters) {
                    const auto current = modulator_parameter_value(
                        instance->settings, instance->type, parameter.numeric_id);
                    next.fx.modulator_parameters.push_back(
                        {std::string(parameter.id), std::string(parameter.name),
                         format_value(current), std::string(parameter.unit),
                         parameter.minimum, parameter.maximum, current,
                         parameter.discrete, false});
                }
    }
    if (next.fx.selected_modulator_parameter >=
        next.fx.modulator_parameters.size())
        next.fx.selected_modulator_parameter = 0;
    if (!next.fx.modulator_parameters.empty())
        next.fx.modulator_parameters[next.fx.selected_modulator_parameter]
            .selected = true;
    for (std::size_t i = 0; i < snapshot.modulation.route_count; ++i) {
        const auto &route = snapshot.modulation.routes[i];
        auto destination_label = std::string("Parameter ") +
                                 std::to_string(route.destination.parameter);
        for (const auto &parameter : snapshot.parameters)
            if (const auto destination =
                    resolve_parameter_destination(parameter.id);
                destination &&
                destination->target == route.destination.target &&
                destination->parameter == route.destination.parameter &&
                (!route.destination.instance || !destination->instance ||
                 destination->instance == route.destination.instance)) {
                destination_label = parameter.name;
                break;
            }
        if (route.destination.target == ParameterTarget::native_effect)
            destination_label = "FX " +
                std::to_string(route.destination.instance) + " / " +
                destination_label;
        next.fx.routes.push_back({route.id, route.source_instance,
            route.source_instance ? "Modulator " + std::to_string(route.source_instance)
                                  : "Source " + std::to_string(static_cast<unsigned>(route.source)),
            std::move(destination_label),
            format_value(route.depth), route.enabled, false, true});
    }
    if (next.fx.selected_route >= next.fx.routes.size()) next.fx.selected_route = 0;
    if (!next.fx.routes.empty()) next.fx.routes[next.fx.selected_route].selected = true;
    for (const auto &macro : snapshot.performance_macros.macros) {
        const auto effective = std::find_if(snapshot.performance_macros.effective_values.begin(),
            snapshot.performance_macros.effective_values.end(),
            [&](const auto &value) { return value.first == macro.id; });
        next.fx.macros.push_back({macro.id, macro.name.empty()
            ? "Macro " + std::to_string(macro.id) : macro.name,
            std::to_string(macro.assignments.size()) + " assignments",
            effective == snapshot.performance_macros.effective_values.end()
                ? macro.value : effective->second, macro.enabled, false});
    }
    if (next.fx.selected_macro >= next.fx.macros.size()) next.fx.selected_macro = 0;
    if (!next.fx.macros.empty()) next.fx.macros[next.fx.selected_macro].selected = true;
    if (!next.fx.macros.empty()) {
        const auto macro_id = next.fx.macros[next.fx.selected_macro].id;
        const auto macro = std::find_if(
            snapshot.performance_macros.macros.begin(),
            snapshot.performance_macros.macros.end(),
            [=](const auto &value) { return value.id == macro_id; });
        if (macro != snapshot.performance_macros.macros.end())
            for (const auto &assignment : macro->assignments)
                next.fx.macro_assignments.push_back(
                    {assignment.id, macro->id, assignment.destination_id,
                     format_value(assignment.minimum) + ".." +
                         format_value(assignment.maximum),
                     std::to_string(static_cast<unsigned>(assignment.curve)),
                     assignment.minimum, assignment.maximum,
                     assignment.enabled, false});
    }
    if (next.fx.selected_macro_assignment >=
        next.fx.macro_assignments.size())
        next.fx.selected_macro_assignment = 0;
    if (!next.fx.macro_assignments.empty())
        next.fx.macro_assignments[next.fx.selected_macro_assignment].selected = true;
    for (const auto &scene : snapshot.performance_macros.scenes)
        next.fx.scenes.push_back(
            {scene.id, scene.name,
             std::to_string(scene.macro_values.size()) + " macros / " +
                 format_value(scene.morph_duration) +
                 (scene.tempo_sync ? " beats" : " seconds"),
             false});
    if (next.fx.selected_scene >= next.fx.scenes.size())
        next.fx.selected_scene = 0;
    if (!next.fx.scenes.empty())
        next.fx.scenes[next.fx.selected_scene].selected = true;
    next.fx.error = last_error_;
    next.browser = state_.browser;
    next.browser.missing_assets.clear();
    const auto preset_dependencies =
        (next.browser.category == "instrument-presets" ||
         next.browser.category == "fx-presets") &&
        !snapshot.preset_application.missing_assets.empty();
    if (preset_dependencies)
        for (const auto &path : snapshot.preset_application.missing_assets)
            next.browser.missing_assets.push_back(path.string());
    else
        for (const auto &path : snapshot.missing_assets)
            next.browser.missing_assets.push_back(path.string());
    if (next.browser.selected_missing >= next.browser.missing_assets.size())
        next.browser.selected_missing = 0;
    if (next.browser.categories.empty())
        next.browser.categories = {"projects", "samples", "instrument-presets",
                                   "fx-presets", "other"};
    next.browser.recent_locations = browser_recent_locations_;
    if (next.browser.category == "samples")
        next.browser.destination = "Sample slot " +
            std::to_string(snapshot.selected_sample_slot.value);
    else if (next.browser.category == "instrument-presets")
        next.browser.destination = "Track " + std::to_string(snapshot.selected_track);
    else if (next.browser.category == "fx-presets")
        next.browser.destination = "Track " +
            std::to_string(snapshot.selected_track) + " effect chain";
    else if (next.browser.category == "projects")
        next.browser.destination = "Current application session";
    next.setup = state_.setup;
    next.setup.mapping_targets.clear();
    next.setup.mapping_targets = cached_setup_targets_;
    if (setup_runtime) {
        const auto &runtime = *setup_runtime;
        next.setup.audio_backend = runtime.audio_backend;
        next.setup.audio_status = runtime.audio_status;
        next.setup.active_audio_input = runtime.active_audio_input.empty()
            ? "Unavailable" : runtime.active_audio_input;
        next.setup.active_audio_output = runtime.active_audio_output.empty()
            ? "Unavailable" : runtime.active_audio_output;
        next.setup.active_sample_rate = runtime.active_sample_rate;
        next.setup.active_buffer_size = runtime.active_buffer_size;
        next.setup.active_input_channels = runtime.active_input_channels;
        next.setup.active_output_channels = runtime.active_output_channels;
        next.setup.audio_running = runtime.audio_running;
        next.setup.xruns_available = runtime.xruns_available;
        next.setup.xruns = runtime.xruns;
        next.setup.audio_inputs = runtime.audio_inputs;
        next.setup.audio_outputs = runtime.audio_outputs;
        next.setup.midi_inputs = runtime.midi_inputs;
        next.setup.midi_outputs = runtime.midi_outputs;
        next.setup.controllers = runtime.controllers;
        next.setup.mappings = runtime.mappings;
        next.setup.warnings = runtime.lifecycle_warnings;
        next.setup.system = runtime.system;
        next.setup.version = runtime.version;
    }
    if (next.setup.selected_controller >= next.setup.controllers.size())
        next.setup.selected_controller = 0;
    for (std::size_t i = 0; i < next.setup.controllers.size(); ++i)
        next.setup.controllers[i].selected = i == next.setup.selected_controller;
    if (next.setup.selected_mapping >= next.setup.mappings.size())
        next.setup.selected_mapping = 0;
    for (std::size_t i = 0; i < next.setup.mappings.size(); ++i)
        next.setup.mappings[i].selected = i == next.setup.selected_mapping;
    if (next.setup.selected_mapping_target >= next.setup.mapping_targets.size())
        next.setup.selected_mapping_target = 0;
    for (std::size_t i = 0; i < next.setup.mapping_targets.size(); ++i)
        next.setup.mapping_targets[i].selected =
            i == next.setup.selected_mapping_target;
    if (!next.setup.controllers.empty())
        next.setup.controller_learn_status =
            next.setup.controllers[next.setup.selected_controller].learn_status;
    next.setup.requested_audio_input = staged_preferences_.audio_input;
    next.setup.requested_audio_output = staged_preferences_.audio_output;
    next.setup.requested_buffer_size = staged_preferences_.audio_buffer_frames;
    next.setup.requested_input_channels = staged_preferences_.audio_input_channels;
    next.setup.requested_output_channels = staged_preferences_.audio_output_channels;
    next.setup.fullscreen = staged_preferences_.fullscreen;
    next.setup.reopen_last_project = staged_preferences_.reopen_last_project;
    next.setup.ui_scale = staged_preferences_.ui_scale.value_or(1.0F);
    next.setup.startup_project = staged_preferences_.last_project.string();
    next.setup.restart_required = setup_runtime &&
        ((!next.setup.requested_audio_input.empty() &&
          next.setup.requested_audio_input != next.setup.active_audio_input) ||
         (!next.setup.requested_audio_output.empty() &&
          next.setup.requested_audio_output != next.setup.active_audio_output) ||
         (next.setup.active_buffer_size != 0 &&
          next.setup.requested_buffer_size != next.setup.active_buffer_size) ||
         next.setup.requested_input_channels != next.setup.active_input_channels ||
         next.setup.requested_output_channels != next.setup.active_output_channels);
    next.setup.pending = setup_runtime && !(staged_preferences_ == setup_runtime->requested);
    next.setup.callback_load = snapshot.performance.audio.average_load_percent;
    next.setup.callback_peak_load = snapshot.performance.audio.peak_load_percent;
    next.setup.midi_clock = snapshot.midi.clock_source == MidiClockSource::external
        ? "External" : "Internal";
    next.setup.midi_routes.clear();
    for (std::size_t i = 0; i < snapshot.midi.route_count; ++i) {
        const auto &route = snapshot.midi.routes[i];
        next.setup.midi_routes.push_back({
            route.id, route.input_port, route.output_port,
            route.input_channel, route.output_channel, route.notes,
            route.control_change, route.clock, route.transport, route.enabled,
            i == state_.setup.selected_midi_route});
    }
    if (!next.setup.midi_routes.empty())
        next.setup.selected_midi_route = std::min(
            state_.setup.selected_midi_route,
            next.setup.midi_routes.size() - 1);
    next.setup.error = last_error_;
    next.mixer.error = last_error_;
    next.shell.notification = last_error_;
    state_ = std::move(next);
    structural_fingerprint_ = fingerprint;
    published_local_revision_ = local_revision_;
    refresh_diagnostics_.construction_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - construction_started).count());
    ++refresh_diagnostics_.full_rebuilds;
    finish(SlintRefreshKind::full);
    return true;
}

bool SlintFrontendAdapter::execute(ApplicationCommand command) {
    if (!active_) {
        last_error_ = "Slint frontend is shutting down";
        return false;
    }
    command.source_id = "slint-development-ui";
    if (command.source_sequence == 0)
        command.source_sequence = ++local_revision_;
    const auto result = application_.execute(command);
    last_error_ = result.accepted ? std::string{} : result.error;
    (void)refresh();
    state_.shell.notification = last_error_;
    return result.accepted;
}

bool SlintFrontendAdapter::open_modal(std::string identity, std::string target,
                                      bool destructive) {
    std::string error;
    if (!application_.open_interaction_modal(std::move(identity),
                                             std::move(target), destructive,
                                             error)) {
        last_error_ = std::move(error);
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::process_interaction_request(
    const ApplicationState &snapshot) {
    const auto &interaction = snapshot.interaction;
    if (interaction.request == ApplicationState::InteractionRequest::none ||
        interaction.request_revision <= consumed_interaction_request_)
        return false;
    consumed_interaction_request_ = interaction.request_revision;
    const auto modal = interaction.modal;
    const auto request = interaction.request;
    std::string close_error;
    if (!modal.identity.empty())
        (void)application_.close_interaction_modal(modal.generation, close_error);

    if (request == ApplicationState::InteractionRequest::save) {
        if (snapshot.project_path.empty())
            (void)request_text_entry("project-save-as", {});
        else
            (void)execute({"project.save.v1", {}, {}, 0});
        return true;
    }
    if (request == ApplicationState::InteractionRequest::focus_next ||
        request == ApplicationState::InteractionRequest::focus_previous ||
        request == ApplicationState::InteractionRequest::first ||
        request == ApplicationState::InteractionRequest::last) {
        if (!state_.browser.entries.empty()) {
            std::size_t selected = state_.browser.selected;
            if (request == ApplicationState::InteractionRequest::first)
                selected = 0;
            else if (request == ApplicationState::InteractionRequest::last)
                selected = state_.browser.entries.size() - 1;
            else if (request == ApplicationState::InteractionRequest::focus_next)
                selected = std::min(selected + 1, state_.browser.entries.size() - 1);
            else if (selected > 0)
                --selected;
            (void)select_browser_entry(selected);
        }
        return true;
    }
    if (request == ApplicationState::InteractionRequest::page_next ||
        request == ApplicationState::InteractionRequest::page_previous) {
        (void)change_browser_page(
            request == ApplicationState::InteractionRequest::page_next ? 1 : -1);
        return true;
    }
    if (request == ApplicationState::InteractionRequest::activate) {
        (void)activate_browser_entry();
        return true;
    }
    if (request == ApplicationState::InteractionRequest::parent) {
        (void)browser_parent();
        return true;
    }
    const bool confirm = request == ApplicationState::InteractionRequest::confirm;
    if (modal.identity == "sequencer.pattern-delete") {
        state_.sequencer.confirm_delete_pattern = false;
        if (confirm)
            (void)execute({"pattern.delete.v1",
                           {{"id", static_cast<std::int64_t>(
                                std::stoull(modal.target_identity))}}, {}, 0});
    } else if (modal.identity == "sequencer.track-delete") {
        state_.sequencer.confirm_delete_track = false;
        if (confirm)
            (void)execute({"track.delete.v1",
                           {{"track_id", static_cast<std::int64_t>(
                                std::stoull(modal.target_identity))}}, {}, 0});
    } else if (modal.identity == "sequencer.clip-delete") {
        state_.sequencer.confirm_delete_clip = false;
        if (confirm) {
            const auto split = modal.target_identity.find(':');
            if (split != std::string::npos) {
                const auto kind = modal.target_identity.substr(0, split);
                const auto id = static_cast<std::int64_t>(
                    std::stoull(modal.target_identity.substr(split + 1)));
                (void)execute({kind == "Audio"
                                  ? "arrangement.audio-clip-delete.v1"
                                  : "arrangement.clip-delete.v1",
                               {{"id", id}}, {}, 0});
            }
        }
    } else if (modal.identity == "browser.confirmation") {
        (void)confirm_browser_action(confirm);
    }
    ++local_revision_;
    return true;
}

bool SlintFrontendAdapter::invoke_action(std::string action_id) {
    return execute({std::move(action_id), {}, {}, 0});
}

bool SlintFrontendAdapter::turn_encoder(std::size_t index, double steps,
                                        bool fine) {
    if (index >= state_.context.encoders.size() ||
        !state_.context.encoders[index].available) {
        last_error_ = "Encoder assignment is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto &context = state_.context.encoders[index];
    if (context.role == "step-property") {
        const auto property = context.arguments.find("property");
        if (property == context.arguments.end() ||
            !std::holds_alternative<std::string>(property->second)) {
            last_error_ = "Step-property binding is invalid";
            return false;
        }
        if (!execute({"step.property-select.v1",
                      {{"property", std::get<std::string>(property->second)}},
                      {}, 0}))
            return false;
        return execute({"navigation.adjust.v1",
                        {{"steps", steps * (fine ? 0.1 : 1.0)}}, {}, 0});
    }
    if (context.role == "step-lock") {
        if (!execute({"step.lock-select.v1",
                      {{"parameter_id", context.parameter_id}}, {}, 0}))
            return false;
        return execute({"navigation.adjust.v1",
                        {{"steps", steps * (fine ? 0.1 : 1.0)}}, {}, 0});
    }
    if (context.parameter_id.empty()) {
        last_error_ = "Encoder assignment is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto discovery = discover_application(application_);
    const auto descriptor = std::find_if(
        discovery.parameters.begin(), discovery.parameters.end(),
        [&](const auto &parameter) {
            return parameter.id == context.parameter_id;
        });
    if (descriptor == discovery.parameters.end()) {
        last_error_ = "Encoder parameter is no longer discoverable";
        state_.shell.notification = last_error_;
        return false;
    }
    BindingReference reference{BindingTargetKind::explicit_object,
                               descriptor->parent,
                               context.parameter_id, {},
                               0};
    const auto error = bindings_.apply(
        reference,
        {BindingOperation::relative_steps, steps * (fine ? 0.1 : 1.0)});
    last_error_ = error ? error.message : std::string{};
    (void)refresh();
    state_.shell.notification = last_error_;
    return !error;
}

bool SlintFrontendAdapter::invoke_function(std::size_t index) {
    if (index >= state_.context.functions.size() ||
        !state_.context.functions[index].available ||
        state_.context.functions[index].action_id.empty()) {
        last_error_ = "Function assignment is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({state_.context.functions[index].action_id,
                    state_.context.functions[index].arguments, {}, 0});
}

bool SlintFrontendAdapter::change_context_bank(int direction) {
    if (direction == 0) return true;
    context_bank_ = context_bank_ == 0 ? 1 : 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::press_pad(std::size_t index, bool pressed) {
    if (index >= state_.performance.pads.size()) {
        last_error_ = "Pad index is out of range";
        return false;
    }
    if (!pressed)
        return execute({"pad.release.v1",
                        {{"pad", static_cast<std::int64_t>(index)}}, {}, 0});
    if (state_.shell.page_id == "performance" &&
        state_.performance.pads[index].available)
        return execute({"performance.slot-launch.v1",
                        {{"slot", static_cast<std::int64_t>(
                                      state_.performance.pads[index].slot_id)}}, {}, 0});
    PerformanceGrid grid;
    const auto snapshot = application_.snapshot();
    grid.set_mode(snapshot.pad_mode);
    grid.set_step_page(snapshot.step_page);
    grid.set_selected_pad(snapshot.selected_pad);
    grid.set_selected_slot(snapshot.selected_sample_slot);
    const auto command = grid_application_command(
        grid.press(index, snapshot.modifiers.select), "slint-development-ui");
    if (!command) {
        last_error_ = "Pad action is unavailable";
        return false;
    }
    return execute(*command);
}

bool SlintFrontendAdapter::assign_performance_pad(std::size_t position,
                                                  std::uint64_t slot_id) {
    if (position >= state_.performance.pads.size()) {
        last_error_ = "Logical pad position is unavailable on this surface";
        return false;
    }
    const auto snapshot = application_.snapshot();
    if (snapshot.performance_banks.active_bank) {
        PerformanceAssignmentId next{1};
        for (const auto &bank : snapshot.performance_banks.banks) {
            for (const auto &v : bank.parameters) next = std::max(next, v.id + 1);
            for (const auto &v : bank.actions) next = std::max(next, v.id + 1);
            for (const auto &v : bank.pads) next = std::max(next, v.id + 1);
        }
        const auto bank = std::find_if(snapshot.performance_banks.banks.begin(),
            snapshot.performance_banks.banks.end(), [&](const auto &value) {
                return value.id == snapshot.performance_banks.active_bank;
            });
        if (bank != snapshot.performance_banks.banks.end())
            if (const auto existing = std::find_if(
                    bank->pads.begin(), bank->pads.end(), [&](const auto &value) {
                        return value.position == position;
                    }); existing != bank->pads.end()) next = existing->id;
        return execute({"performance.bank-pad-assign.v1",
                        {{"bank", static_cast<std::int64_t>(
                                      snapshot.performance_banks.active_bank)},
                         {"assignment", static_cast<std::int64_t>(next)},
                         {"position", static_cast<std::int64_t>(position)},
                         {"slot", static_cast<std::int64_t>(slot_id)}}, {}, 0});
    }
    return execute({"performance.pad-assign.v1",
                    {{"position", static_cast<std::int64_t>(position)},
                     {"slot", static_cast<std::int64_t>(slot_id)}}, {}, 0});
}

bool SlintFrontendAdapter::clear_performance_pad(std::size_t position) {
    if (position >= state_.performance.pads.size()) {
        last_error_ = "Logical pad position is unavailable on this surface";
        return false;
    }
    const auto snapshot = application_.snapshot();
    if (snapshot.performance_banks.active_bank) {
        const auto bank = std::find_if(snapshot.performance_banks.banks.begin(),
            snapshot.performance_banks.banks.end(), [&](const auto &value) {
                return value.id == snapshot.performance_banks.active_bank;
            });
        if (bank == snapshot.performance_banks.banks.end()) return false;
        const auto assignment = std::find_if(
            bank->pads.begin(), bank->pads.end(), [&](const auto &value) {
                return value.position == position;
            });
        if (assignment == bank->pads.end()) return true;
        return execute({"performance.assignment-clear.v1",
                        {{"bank", static_cast<std::int64_t>(bank->id)},
                         {"assignment", static_cast<std::int64_t>(assignment->id)}},
                        {}, 0});
    }
    return execute({"performance.pad-clear.v1",
                    {{"position", static_cast<std::int64_t>(position)}}, {}, 0});
}

bool SlintFrontendAdapter::manage_performance_bank(std::string operation) {
    const auto snapshot = application_.snapshot();
    PerformanceBankId next{1};
    for (const auto &bank : snapshot.performance_banks.banks)
        next = std::max(next, bank.id + 1);
    if (operation == "create")
        return execute({"performance.bank-create.v1",
                        {{"bank", static_cast<std::int64_t>(next)},
                         {"name", std::string("Performance ") +
                                      std::to_string(next)}}, {}, 0});
    if (operation == "next")
        return execute({"performance.bank-next.v1", {}, {}, 0});
    if (operation == "previous")
        return execute({"performance.bank-previous.v1", {}, {}, 0});
    const auto active = snapshot.performance_banks.active_bank;
    if (!active) return false;
    if (operation == "duplicate")
        return execute({"performance.bank-duplicate.v1",
                        {{"source", static_cast<std::int64_t>(active)},
                         {"bank", static_cast<std::int64_t>(next)},
                         {"name", state_.performance.bank_name + " Copy"}}, {}, 0});
    if (operation == "delete")
        return execute({"performance.bank-delete.v1",
                        {{"bank", static_cast<std::int64_t>(active)}}, {}, 0});
    last_error_ = "Unknown Performance bank operation";
    return false;
}

bool SlintFrontendAdapter::select_performance_bank(std::size_t index) {
    if (index >= state_.performance.banks.size()) return false;
    return execute({"performance.bank-select.v1",
                    {{"bank", static_cast<std::int64_t>(
                                  state_.performance.banks[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::assign_performance_parameter(
    std::size_t position, bool secondary, std::string parameter_id,
    std::string label) {
    const auto snapshot = application_.snapshot();
    if (!snapshot.performance_banks.active_bank || position > 65535) return false;
    PerformanceAssignmentId next{1};
    const PerformanceBank *active{};
    for (const auto &bank : snapshot.performance_banks.banks) {
        if (bank.id == snapshot.performance_banks.active_bank) active = &bank;
        for (const auto &v : bank.parameters) next = std::max(next, v.id + 1);
        for (const auto &v : bank.actions) next = std::max(next, v.id + 1);
        for (const auto &v : bank.pads) next = std::max(next, v.id + 1);
    }
    if (active)
        if (const auto existing = std::find_if(
                active->parameters.begin(), active->parameters.end(),
                [&](const auto &value) {
                    return value.position == position &&
                           value.secondary == secondary;
                }); existing != active->parameters.end()) next = existing->id;
    return execute({"performance.parameter-assign.v1",
                    {{"bank", static_cast<std::int64_t>(
                                  snapshot.performance_banks.active_bank)},
                     {"assignment", static_cast<std::int64_t>(next)},
                     {"position", static_cast<std::int64_t>(position)},
                     {"secondary", secondary},
                     {"parameter_id", std::move(parameter_id)},
                     {"label", std::move(label)}}, {}, 0});
}

bool SlintFrontendAdapter::open_performance_assignment_editor(
    std::size_t position, bool secondary, bool action) {
    if (position > 65535 || application_.snapshot().performance_banks.active_bank == 0) {
        last_error_ = "Select a Performance bank and valid logical position";
        return false;
    }
    state_.performance.editor_position = static_cast<std::uint32_t>(position);
    state_.performance.editor_secondary = secondary;
    state_.performance.editor_action = action;
    state_.performance.editor_pad = false;
    state_.performance.editor_visible = true;
    state_.performance.selected_assignment_target = 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_performance_assignment_target(std::size_t index) {
    if (index >= state_.performance.assignment_targets.size()) return false;
    state_.performance.selected_assignment_target = index;
    state_.performance.editor_argument_names.fill({});
    state_.performance.editor_argument_values.fill({});
    state_.performance.editor_argument_hints.fill({});
    const auto &arguments = state_.performance.assignment_targets[index].arguments;
    for (std::size_t field = 0;
         field < std::min(arguments.size(),
                          state_.performance.editor_argument_names.size()); ++field) {
        const auto &argument = arguments[field];
        state_.performance.editor_argument_names[field] = argument.name;
        auto type = std::string("text");
        if (argument.type == ValueType::boolean) type = "boolean";
        else if (argument.type == ValueType::integer) type = "integer";
        else if (argument.type == ValueType::real) type = "real";
        state_.performance.editor_argument_hints[field] =
            argument.label + " · " + type +
            (argument.required ? " · required" : " · optional");
        if (!argument.choices.empty()) {
            state_.performance.editor_argument_hints[field] += " · ";
            for (std::size_t choice = 0; choice < argument.choices.size(); ++choice) {
                if (choice) state_.performance.editor_argument_hints[field] += " | ";
                state_.performance.editor_argument_hints[field] +=
                    argument.choices[choice].label;
            }
        }
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::confirm_performance_assignment(
    std::string label, std::string behavior,
    std::array<std::string, 4> argument_names,
    std::array<std::string, 4> argument_values) {
    if (!state_.performance.editor_visible ||
        state_.performance.selected_assignment_target >=
            state_.performance.assignment_targets.size()) {
        last_error_ = "Select a compatible Performance assignment target";
        return false;
    }
    const auto target = state_.performance.assignment_targets[
        state_.performance.selected_assignment_target];
    if (state_.performance.editor_pad) {
        std::uint64_t slot{};
        const auto parsed = std::from_chars(target.id.data(),
                                             target.id.data() + target.id.size(),
                                             slot);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != target.id.data() + target.id.size()) {
            last_error_ = "Selected launch target has an invalid stable identity";
            return false;
        }
        const auto accepted = assign_performance_pad(
            state_.performance.editor_position, slot);
        if (accepted) cancel_performance_assignment_editor();
        return accepted;
    }
    if (!state_.performance.editor_action) {
        const auto accepted = assign_performance_parameter(
            state_.performance.editor_position,
            state_.performance.editor_secondary, target.id, std::move(label));
        if (accepted) cancel_performance_assignment_editor();
        return accepted;
    }
    AssignmentTargetPicker picker(application_);
    AssignmentProposal proposal{AssignmentTargetKind::action, target.id, {}};
    const auto targets = picker.targets([](const auto &action) {
        return performance_safe_assignment_action(action);
    });
    const auto descriptor = std::find_if(targets.begin(), targets.end(),
        [&](const auto &candidate) {
            return candidate.kind == AssignmentTargetKind::action &&
                   candidate.id == target.id;
        });
    if (descriptor == targets.end()) return false;
    for (std::size_t i = 0; i < argument_names.size(); ++i) {
        if (argument_names[i].empty() && argument_values[i].empty()) continue;
        const auto field = std::find_if(descriptor->arguments.begin(),
            descriptor->arguments.end(), [&](const auto &value) {
                return value.name == argument_names[i];
            });
        if (field == descriptor->arguments.end()) {
            last_error_ = "Unknown action argument: " + argument_names[i];
            return false;
        }
        ApplicationValue value;
        if (!AssignmentTargetPicker::parse_argument(
                *field, argument_values[i], value, last_error_))
            return false;
        proposal.arguments.emplace(argument_names[i], std::move(value));
    }
    if (behavior == "momentary") proposal.arguments["active"] = true;
    if (!picker.validate(proposal, last_error_, [](const auto &action) {
            return performance_safe_assignment_action(action);
        })) return false;
    const auto snapshot = application_.snapshot();
    PerformanceAssignmentId next{1};
    const PerformanceBank *active{};
    for (const auto &bank : snapshot.performance_banks.banks) {
        if (bank.id == snapshot.performance_banks.active_bank) active = &bank;
        for (const auto &v : bank.parameters) next = std::max(next, v.id + 1);
        for (const auto &v : bank.actions) next = std::max(next, v.id + 1);
        for (const auto &v : bank.pads) next = std::max(next, v.id + 1);
    }
    if (active)
        if (const auto existing = std::find_if(
                active->actions.begin(), active->actions.end(), [&](const auto &value) {
                    return value.position == state_.performance.editor_position &&
                           value.secondary == state_.performance.editor_secondary;
                }); existing != active->actions.end()) next = existing->id;
    ApplicationCommand command{"performance.action-assign.v1",
        {{"bank", static_cast<std::int64_t>(snapshot.performance_banks.active_bank)},
         {"assignment", static_cast<std::int64_t>(next)},
         {"position", static_cast<std::int64_t>(state_.performance.editor_position)},
         {"secondary", state_.performance.editor_secondary},
         {"action_id", target.id}, {"behavior", behavior}, {"label", label}}, {}, 0};
    for (std::size_t i = 0; i < argument_names.size(); ++i)
        if (!argument_names[i].empty()) {
            const auto prefix = "argument_" + std::to_string(i + 1);
            command.arguments[prefix + "_name"] = argument_names[i];
            command.arguments[prefix + "_value"] = argument_values[i];
        }
    const auto accepted = execute(std::move(command));
    if (accepted) cancel_performance_assignment_editor();
    return accepted;
}

void SlintFrontendAdapter::cancel_performance_assignment_editor() noexcept {
    state_.performance.editor_visible = false;
    state_.performance.editor_pad = false;
    state_.performance.editor_label.clear();
    state_.performance.editor_argument_names.fill({});
    state_.performance.editor_argument_values.fill({});
    state_.performance.editor_argument_hints.fill({});
    ++local_revision_;
}

bool SlintFrontendAdapter::clear_performance_assignment_editor() {
    if (!state_.performance.editor_visible) return false;
    const auto snapshot = application_.snapshot();
    const auto bank = snapshot.performance_banks.active_bank;
    if (!bank) return false;
    const auto active = std::find_if(snapshot.performance_banks.banks.begin(),
        snapshot.performance_banks.banks.end(),
        [&](const auto &value) { return value.id == bank; });
    if (active == snapshot.performance_banks.banks.end()) return false;
    PerformanceAssignmentId assignment{};
    if (state_.performance.editor_pad) {
        const auto found = std::find_if(active->pads.begin(), active->pads.end(),
            [&](const auto &value) { return value.position ==
                state_.performance.editor_position; });
        if (found != active->pads.end()) assignment = found->id;
    } else if (state_.performance.editor_action) {
        const auto found = std::find_if(active->actions.begin(), active->actions.end(),
            [&](const auto &value) { return value.position ==
                state_.performance.editor_position && value.secondary ==
                state_.performance.editor_secondary; });
        if (found != active->actions.end()) assignment = found->id;
    } else {
        const auto found = std::find_if(active->parameters.begin(), active->parameters.end(),
            [&](const auto &value) { return value.position ==
                state_.performance.editor_position && value.secondary ==
                state_.performance.editor_secondary; });
        if (found != active->parameters.end()) assignment = found->id;
    }
    if (!assignment) {
        last_error_ = "The selected Performance position is already unassigned";
        return false;
    }
    const auto accepted = execute({"performance.assignment-clear.v1",
        {{"bank", static_cast<std::int64_t>(bank)},
         {"assignment", static_cast<std::int64_t>(assignment)}}, {}, 0});
    if (accepted) cancel_performance_assignment_editor();
    return accepted;
}

bool SlintFrontendAdapter::select_performance_pad_assignment(
    std::size_t position) {
    const auto snapshot = application_.snapshot();
    const auto bank = std::find_if(snapshot.performance_banks.banks.begin(),
        snapshot.performance_banks.banks.end(), [&](const auto &value) {
            return value.id == snapshot.performance_banks.active_bank;
        });
    if (bank == snapshot.performance_banks.banks.end() || position >= 16) {
        last_error_ = "Select a Performance bank and logical pad";
        return false;
    }
    state_.performance.editor_position = static_cast<std::uint32_t>(position);
    state_.performance.editor_secondary = false;
    state_.performance.editor_action = false;
    state_.performance.editor_pad = true;
    state_.performance.editor_visible = true;
    state_.performance.selected_assignment_target = 0;
    state_.performance.assignment_status = "Editing PAD" +
        std::to_string(position + 1) + " without triggering it";
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_page(std::size_t index) {
    if (index >= pages.size()) {
        last_error_ = "Page is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.shell.page_id = std::string(pages[index].first);
    state_.shell.destination_id = state_.shell.page_id;
    state_.shell.page_title = std::string(pages[index].second);
    context_bank_ = 0;
    state_.shell.notification.clear();
    last_error_ = state_.shell.notification;
    ++local_revision_;
    return true;
}

bool SlintFrontendAdapter::select_reference_page(std::size_t index) {
    const auto &destinations = state_.shell.effective_shift
                                   ? reference_secondary_destinations()
                                   : reference_primary_destinations();
    if (index >= destinations.size() || !destinations[index].available) {
        last_error_ = "Page is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto &destination = destinations[index];
    if (state_.shell.destination_id == destination.id)
        ++state_.shell.root_revision;
    state_.shell.destination_id = destination.id;
    state_.shell.page_id = destination.page_id;
    state_.shell.page_title = destination.title;
    if (destination.id == "song") state_.sequencer.arrangement_view = true;
    else if (destination.id == "sequencer")
        state_.sequencer.arrangement_view = false;
    if (destination.page_id == "sound") state_.sound.subview = destination.subview;
    if (destination.page_id == "fx")
        state_.fx.subview = destination.subview == "routes" ? "modulation"
                           : destination.subview == "scenes" ? "macros"
                                                               : destination.subview;
    if (destination.page_id == "setup") state_.setup.subview = destination.subview;
    context_bank_ = 0;
    state_.shell.notification.clear();
    last_error_.clear();
    std::string interaction_error;
    if (!application_.set_interaction_destination(
            destination.id, destination.subview.empty() ? "root"
                                                        : destination.subview,
            interaction_error)) {
        last_error_ = std::move(interaction_error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return true;
}

bool SlintFrontendAdapter::set_reference_shift(bool effective) {
    if (state_.shell.effective_shift == effective) return true;
    state_.shell.effective_shift = effective;
    state_.shell.pages.clear();
    const auto &destinations = effective ? reference_secondary_destinations()
                                         : reference_primary_destinations();
    for (const auto &destination : destinations)
        state_.shell.pages.push_back({destination.id, destination.short_label,
                                      destination.available});
    ++local_revision_;
    return true;
}

bool SlintFrontendAdapter::select_browser_category(std::string category) {
    static constexpr std::array<std::string_view, 5> valid{
        "projects", "samples", "instrument-presets", "fx-presets", "other"};
    if (std::find(valid.begin(), valid.end(), category) == valid.end()) {
        last_error_ = "Unknown browser category";
        state_.browser.error = last_error_;
        return false;
    }
    state_.browser.category = std::move(category);
    state_.browser.filter.clear();
    return navigate_browser(state_.browser.location.empty()
                                ? std::filesystem::current_path().string()
                                : state_.browser.location);
}

bool SlintFrontendAdapter::navigate_browser(std::string path) {
    if (!active_ || !browser_worker_) return false;
    state_.browser.location = std::move(path);
    state_.browser.loading = true;
    state_.browser.error.clear();
    state_.browser.entries.clear();
    state_.browser.selected = 0;
    state_.browser.page = 0;
    ++state_.browser.request_id;
    browser_worker_->submit({state_.browser.request_id,
        state_.browser.category.empty() ? "projects" : state_.browser.category,
        state_.browser.location, state_.browser.filter,
        state_.browser.sort.empty() ? "name" : state_.browser.sort, 0});
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::browser_parent() {
    if (state_.browser.location.empty()) return false;
    return navigate_browser(std::filesystem::path(state_.browser.location).parent_path().string());
}

bool SlintFrontendAdapter::select_browser_entry(std::size_t index) {
    if (index >= state_.browser.entries.size()) return false;
    state_.browser.selected = index;
    for (std::size_t i = 0; i < state_.browser.entries.size(); ++i)
        state_.browser.entries[i].selected = i == index;
    const auto &entry = state_.browser.entries[index];
    state_.browser.preview = entry.name + " — " + entry.type;
    std::string ignored;
    (void)application_.set_interaction_focus(
        "browser.entry:" + entry.id, "browser.list", "Browser entry",
        ignored);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_browser_filter(std::string filter) {
    state_.browser.filter = std::move(filter);
    return navigate_browser(state_.browser.location);
}

bool SlintFrontendAdapter::cycle_browser_sort() {
    state_.browser.sort = state_.browser.sort == "name" ? "type" :
                          state_.browser.sort == "type" ? "date" : "name";
    return navigate_browser(state_.browser.location);
}

bool SlintFrontendAdapter::change_browser_page(int direction) {
    if (!browser_worker_ || direction == 0) return false;
    const auto magnitude = static_cast<std::size_t>(
        direction < 0 ? -static_cast<std::int64_t>(direction) : direction);
    const auto target = direction < 0
        ? (magnitude > state_.browser.page ? 0
                                           : state_.browser.page - magnitude)
        : std::min(state_.browser.page + magnitude,
                   state_.browser.page_count - 1);
    if (target == state_.browser.page) return false;
    state_.browser.loading = true;
    state_.browser.entries.clear();
    state_.browser.page = target;
    ++state_.browser.request_id;
    browser_worker_->submit({state_.browser.request_id, state_.browser.category,
        state_.browser.location, state_.browser.filter, state_.browser.sort, target});
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::refresh_browser() {
    if (!browser_worker_ || state_.browser.location.empty()) return false;
    state_.browser.loading = true;
    state_.browser.error.clear();
    ++state_.browser.request_id;
    browser_worker_->submit({state_.browser.request_id, state_.browser.category,
        state_.browser.location, state_.browser.filter, state_.browser.sort,
        state_.browser.page});
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::activate_browser_entry() {
    if (state_.browser.selected >= state_.browser.entries.size()) return false;
    const auto entry = state_.browser.entries[state_.browser.selected];
    if (entry.kind == SlintBrowserEntryKind::project)
        return browser_project_action("open", entry.path);
    if (entry.kind == SlintBrowserEntryKind::directory)
        return navigate_browser(entry.path);
    if (entry.kind == SlintBrowserEntryKind::sample) {
        if (!load_sample(entry.path)) return false;
        return select_page(2);
    }
    if (entry.kind == SlintBrowserEntryKind::instrument_preset ||
        entry.kind == SlintBrowserEntryKind::fx_preset)
        return browser_preset_action("apply");
    last_error_ = "Selected content cannot be opened";
    state_.browser.error = last_error_;
    return false;
}

bool SlintFrontendAdapter::browser_project_action(std::string action,
                                                   std::string path) {
    const auto dirty = application_.snapshot().project_dirty;
    if ((action == "open" || action == "new") && dirty) {
        state_.browser.confirmation_pending = true;
        state_.browser.confirmation_action = action + "\n" + path;
        state_.browser.confirmation_message =
            "The current project has unsaved changes. Discard them?";
        return open_modal("browser.confirmation",
                          state_.browser.confirmation_action, true);
    }
    if (action == "open")
        return execute({"project.open.v1", {{"path", path}, {"discard", false}}, {}, 0});
    if (action == "new")
        return execute({"project.new.v1", {{"name", std::string("Untitled")},
                                            {"discard", false}}, {}, 0});
    if (action == "save") return execute({"project.save.v1", {}, {}, 0});
    if (action == "save-as")
        return execute({"project.save-as.v1", {{"path", path}}, {}, 0});
    last_error_ = "Unsupported project browser action";
    return false;
}

bool SlintFrontendAdapter::browser_preset_action(std::string action) {
    if (state_.browser.selected >= state_.browser.entries.size()) return false;
    const auto &entry = state_.browser.entries[state_.browser.selected];
    if (entry.kind != SlintBrowserEntryKind::instrument_preset &&
        entry.kind != SlintBrowserEntryKind::fx_preset) return false;
    const auto library = state_.browser.location;
    const auto mode = entry.kind == SlintBrowserEntryKind::fx_preset
        ? std::string("insert-fx") : std::string("replace");
    if (action == "rename" || action == "duplicate")
        return request_text_entry(action == "rename" ? "preset-rename"
                                                       : "preset-duplicate",
                                  entry.name);
    if (action == "delete") {
        state_.browser.confirmation_pending = true;
        state_.browser.confirmation_action = "preset-delete\n" + library +
            "\n" + entry.id;
        state_.browser.confirmation_message =
            "Delete preset '" + entry.name + "'? This cannot be undone.";
        return open_modal("browser.confirmation",
                          state_.browser.confirmation_action, true);
    }
    if (action == "save")
        return request_text_entry(entry.kind == SlintBrowserEntryKind::fx_preset
                                      ? "preset-capture-fx"
                                      : "preset-capture-sound", "New Preset");
    if (action == "export-portable")
        return request_text_entry("preset-export-portable",
                                  entry.id + "-portable");
    if (action == "update") {
        const auto type = entry.kind == SlintBrowserEntryKind::fx_preset
            ? std::string("fx-chain")
            : entry.metadata.rfind("sample", 0) == 0
                ? std::string("sample")
                : entry.metadata.rfind("track-sound", 0) == 0
                    ? std::string("track-sound") : std::string("synth");
        state_.browser.confirmation_pending = true;
        auto target_track = state_.sound.track_id;
        if (entry.kind == SlintBrowserEntryKind::fx_preset &&
            state_.fx.owner_kind == "track" && state_.fx.owner_index >= 0 &&
            static_cast<std::size_t>(state_.fx.owner_index) <
                state_.mixer.channels.size())
            target_track = state_.mixer.channels[
                static_cast<std::size_t>(state_.fx.owner_index)].id;
        state_.browser.confirmation_action = "preset-update\n" + library +
            "\n" + entry.id + "\n" + entry.name + "\n" + type + "\n" +
            std::to_string(target_track);
        state_.browser.confirmation_message =
            "Replace this preset with the current configuration?";
        ++local_revision_;
        return refresh();
    }
    if (action == "validate")
        return execute({"preset.validate.v1", {{"library", library},
            {"preset_id", entry.id}, {"mode", mode}}, {}, 0});
    if (action == "apply") {
        if (!execute({"preset.prepare.v1", {{"library", library},
            {"preset_id", entry.id}, {"mode", mode}}, {}, 0})) return false;
        return execute({"preset.apply.v1", {}, {}, 0});
    }
    last_error_ = "Preset operation is unavailable";
    state_.browser.error = last_error_;
    return false;
}

bool SlintFrontendAdapter::confirm_browser_action(bool accept) {
    if (!state_.browser.confirmation_pending) return false;
    const auto interaction = application_.snapshot().interaction;
    if (!interaction.modal.identity.empty()) {
        std::string ignored;
        (void)application_.close_interaction_modal(
            interaction.modal.generation, ignored);
    }
    const auto encoded = state_.browser.confirmation_action;
    state_.browser.confirmation_pending = false;
    state_.browser.confirmation_action.clear();
    state_.browser.confirmation_message.clear();
    ++local_revision_;
    if (!accept) return refresh();
    const auto split = encoded.find('\n');
    const auto action = encoded.substr(0, split);
    const auto path = split == std::string::npos ? std::string{} : encoded.substr(split + 1);
    if (action == "open")
        return execute({"project.open.v1", {{"path", path}, {"discard", true}}, {}, 0});
    if (action == "new")
        return execute({"project.new.v1", {{"name", std::string("Untitled")},
                                            {"discard", true}}, {}, 0});
    if (action == "new-named")
        return execute({"project.new.v1", {{"name", path},
                                            {"discard", true}}, {}, 0});
    if (action == "save-as-overwrite")
        return execute({"project.save-as.v1",
            {{"path", path},
             {"name", std::filesystem::path(path).filename().string()}}, {}, 0});
    if (action == "recording-export-overwrite") {
        if (recording_workflow_.configuration().mode ==
            RecordingWorkflowMode::live_loop) {
            std::string error;
            const auto saved = recording_workflow_.save_live_loop(path, error);
            if (!saved) {
                last_error_ = std::move(error);
                state_.shell.notification = last_error_;
            }
            return saved;
        } else
            return execute({"recording.save.v1",
                {{"path", path}, {"format", std::string("float32")}}, {}, 0});
    }
    if (action == "processing-export-overwrite")
        return processing_action("save", path);
    if (action == "profile-save-overwrite")
        return save_setup_controller_profile(path);
    if (action == "preset-delete") {
        const auto second = path.find('\n');
        if (second == std::string::npos) return false;
        const auto accepted_delete = execute({"preset.delete.v1",
            {{"library", path.substr(0, second)},
             {"preset_id", path.substr(second + 1)}}, {}, 0});
        if (accepted_delete) (void)refresh_browser();
        return accepted_delete;
    }
    if (action == "preset-update") {
        std::array<std::string, 5> fields;
        std::size_t start{};
        for (auto &field : fields) {
            const auto end = path.find('\n', start);
            field = path.substr(start, end - start);
            if (end == std::string::npos) { start = path.size(); break; }
            start = end + 1;
        }
        std::int64_t track{};
        try { track = std::stoll(fields[4]); }
        catch (...) { return false; }
        const auto updated = execute({"preset.capture.v1",
            {{"library", fields[0]}, {"preset_id", fields[1]},
             {"name", fields[2]}, {"type", fields[3]},
             {"track_id", track}, {"overwrite", true}}, {}, 0});
        if (updated) (void)refresh_browser();
        return updated;
    }
    return false;
}

bool SlintFrontendAdapter::select_missing_asset(std::size_t index) {
    if (index >= state_.browser.missing_assets.size()) return false;
    state_.browser.selected_missing = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::relink_missing_asset(bool map_directory) {
    if (state_.browser.selected_missing >= state_.browser.missing_assets.size() ||
        state_.browser.selected >= state_.browser.entries.size()) return false;
    const auto missing = std::filesystem::path(
        state_.browser.missing_assets[state_.browser.selected_missing]);
    auto replacement = std::filesystem::path(
        state_.browser.entries[state_.browser.selected].path);
    const auto snapshot = application_.snapshot();
    const auto preset_dependency =
        !snapshot.preset_application.missing_assets.empty() &&
        (state_.browser.category == "instrument-presets" ||
         state_.browser.category == "fx-presets");
    if (map_directory && preset_dependency)
        return execute({"preset.relink-prefix.v1",
            {{"missing_prefix", missing.parent_path().string()},
             {"replacement_prefix", state_.browser.location}}, {}, 0});
    if (map_directory)
        return execute({"project.relink-prefix.v1",
            {{"missing_prefix", missing.parent_path().string()},
             {"replacement_prefix", state_.browser.location}}, {}, 0});
    if (!std::filesystem::is_regular_file(replacement)) {
        last_error_ = "Replacement must be an existing compatible file";
        state_.browser.error = last_error_;
        return false;
    }
    return execute({preset_dependency ? "preset.relink-asset.v1"
                                      : "project.relink-sample.v1",
        {{"missing", missing.string()}, {"replacement", replacement.string()}}, {}, 0});
}

bool SlintFrontendAdapter::request_text_entry(std::string operation,
                                               std::string current) {
    static constexpr std::array<std::string_view, 16> supported{
        "project-new", "project-save-as", "track-rename", "pattern-rename",
        "recording-export", "processing-export", "profile-save", "browser-filter",
        "preset-capture-sound", "preset-capture-fx", "preset-rename",
        "preset-duplicate", "preset-export-portable", "macro-create",
        "macro-rename", "macro-scene-capture"};
    if (std::find(supported.begin(), supported.end(), operation) == supported.end())
        return false;
    state_.text_entry = {};
    state_.text_entry.visible = true;
    state_.text_entry.operation = std::move(operation);
    state_.text_entry.text = std::move(current);
    state_.text_entry.cursor = state_.text_entry.text.size();
    state_.text_entry.title = state_.text_entry.operation == "browser-filter"
        ? "Filter content" : state_.text_entry.operation.find("rename") != std::string::npos
            ? "Rename" : state_.text_entry.operation.find("export") != std::string::npos
                ? "Export filename" : "Name";
    state_.text_entry.context_track = state_.sound.track_id;
    state_.text_entry.context_library = state_.browser.location;
    if (state_.browser.selected < state_.browser.entries.size()) {
        const auto &selected = state_.browser.entries[state_.browser.selected];
        if (selected.kind == SlintBrowserEntryKind::instrument_preset ||
            selected.kind == SlintBrowserEntryKind::fx_preset) {
            state_.text_entry.context_preset_id = selected.id;
            state_.text_entry.context_preset_type =
                selected.kind == SlintBrowserEntryKind::fx_preset
                    ? "fx-chain" : state_.sound.sample
                        ? "sample" : "synth";
        }
    }
    if (state_.text_entry.operation == "preset-capture-fx")
    {
        state_.text_entry.context_preset_type = "fx-chain";
        if (state_.fx.owner_kind == "track" && state_.fx.owner_index >= 0 &&
            static_cast<std::size_t>(state_.fx.owner_index) <
                state_.mixer.channels.size())
            state_.text_entry.context_track = state_.mixer.channels[
                static_cast<std::size_t>(state_.fx.owner_index)].id;
    }
    else if (state_.text_entry.operation == "preset-capture-sound")
        state_.text_entry.context_preset_type =
            state_.sound.sample ? "sample" : "synth";
    else if (state_.text_entry.operation == "macro-rename" &&
             state_.fx.selected_macro < state_.fx.macros.size())
        state_.text_entry.context_id =
            state_.fx.macros[state_.fx.selected_macro].id;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::text_entry_insert(std::string text) {
    if (!state_.text_entry.visible || text.empty()) return false;
    state_.text_entry.text.insert(state_.text_entry.cursor, text);
    state_.text_entry.cursor += text.size();
    state_.text_entry.error.clear();
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::text_entry_action(std::string action) {
    auto &entry = state_.text_entry;
    if (!entry.visible) return false;
    if (action == "left") entry.cursor = utf8_previous(entry.text, entry.cursor);
    else if (action == "right") entry.cursor = utf8_next(entry.text, entry.cursor);
    else if (action == "backspace") {
        const auto previous = utf8_previous(entry.text, entry.cursor);
        entry.text.erase(previous, entry.cursor - previous); entry.cursor = previous;
    } else if (action == "clear") { entry.text.clear(); entry.cursor = 0; }
    else if (action == "uppercase") entry.uppercase = !entry.uppercase;
    else if (action == "symbols") entry.symbols = !entry.symbols;
    else if (action == "cancel") { entry = {}; ++local_revision_; return refresh(); }
    else if (action == "confirm") {
        const auto operation = entry.operation;
        const auto value = entry.text;
        const auto context_library = entry.context_library;
        const auto context_preset_id = entry.context_preset_id;
        const auto context_preset_type = entry.context_preset_type;
        const auto context_track = entry.context_track;
        const bool filename = operation == "project-save-as" ||
            operation == "recording-export" || operation == "processing-export" ||
            operation == "profile-save" ||
            operation == "preset-export-portable";
        if ((filename && !valid_filename(value)) || (!filename && value.empty())) {
            entry.error = filename ? "Enter a valid filename without '/'" : "Text is required";
            ++local_revision_; return refresh();
        }
        entry.visible = false;
        bool accepted{};
        if (operation == "track-rename")
            accepted = execute({"track.rename.v1", {{"name", value}}, {}, 0});
        else if (operation == "pattern-rename")
            accepted = execute({"pattern.rename.v1",
                {{"id", static_cast<std::int64_t>(state_.sequencer.selected_pattern_id)},
                 {"name", value}}, {}, 0});
        else if (operation == "macro-create")
            accepted = execute({"macro.create.v1",
                {{"name", value}, {"owner", std::string("project")},
                 {"value", 0.0}}, {}, 0});
        else if (operation == "macro-rename")
            accepted = execute({"macro.rename.v1",
                {{"id", static_cast<std::int64_t>(entry.context_id)},
                 {"name", value}}, {}, 0});
        else if (operation == "macro-scene-capture")
            accepted = execute({"macro.scene-capture.v1",
                {{"name", value}, {"duration", 0.0},
                 {"tempo_sync", false}, {"curve", std::string("linear")}},
                {}, 0});
        else if (operation == "project-new") {
            if (application_.snapshot().project_dirty) {
                state_.browser.confirmation_pending = true;
                state_.browser.confirmation_action = "new-named\n" + value;
                state_.browser.confirmation_message =
                    "The current project has unsaved changes. Discard them?";
                accepted = true;
            } else
                accepted = execute({"project.new.v1",
                    {{"name", value}, {"discard", false}}, {}, 0});
        } else if (operation == "browser-filter")
            accepted = set_browser_filter(value);
        else if (operation == "preset-capture-sound" ||
                 operation == "preset-capture-fx") {
            const auto id = preset_id_from_name(value);
            if (id.empty() || context_library.empty())
                last_error_ = id.empty() ? "Preset name has no usable identifier"
                                         : "Open a preset library first";
            else {
                accepted = execute({"preset.capture.v1",
                    {{"library", context_library}, {"preset_id", id},
                     {"name", value}, {"type", context_preset_type},
                     {"track_id", static_cast<std::int64_t>(context_track)},
                     {"overwrite", false}}, {}, 0});
                if (accepted) (void)refresh_browser();
            }
        } else if (operation == "preset-rename" ||
                   operation == "preset-duplicate") {
            const auto id = preset_id_from_name(value);
            if (id.empty() || context_library.empty() ||
                context_preset_id.empty())
                last_error_ = "Preset selection or name is unavailable";
            else {
                accepted = execute({operation == "preset-rename"
                        ? "preset.rename.v1" : "preset.duplicate.v1",
                    {{"library", context_library},
                     {"preset_id", context_preset_id}, {"new_id", id},
                     {"new_name", value}}, {}, 0});
                if (accepted) (void)refresh_browser();
            }
        }
        else if (operation == "preset-export-portable") {
            if (context_library.empty() || context_preset_id.empty())
                last_error_ = "Preset selection is unavailable";
            else
                accepted = execute({"preset.export-portable.v1",
                    {{"library", context_library},
                     {"preset_id", context_preset_id},
                     {"destination", (std::filesystem::path(context_library)
                                          .parent_path() / value).string()}}, {}, 0});
        }
        else {
            auto path = std::filesystem::path(state_.browser.location) / value;
            if (operation == "project-save-as") {
                if (std::filesystem::exists(path)) {
                    state_.browser.confirmation_pending = true;
                    state_.browser.confirmation_action =
                        "save-as-overwrite\n" + path.string();
                    state_.browser.confirmation_message =
                        "The selected project file already exists. Replace it?";
                    accepted = true;
                } else {
                    accepted = execute({"project.save-as.v1",
                        {{"path", path.string()},
                         {"name", path.filename().string()}}, {}, 0});
                }
            }
            else if (operation == "recording-export" ||
                     operation == "processing-export" ||
                     operation == "profile-save") {
                if (std::filesystem::exists(path)) {
                    state_.browser.confirmation_pending = true;
                    state_.browser.confirmation_action = operation +
                        "-overwrite\n" + path.string();
                    state_.browser.confirmation_message =
                        "The selected file already exists. Replace it?";
                    accepted = true;
                } else if (operation == "recording-export")
                    if (recording_workflow_.configuration().mode ==
                        RecordingWorkflowMode::live_loop)
                        accepted = recording_workflow_.save_live_loop(
                            path.string(), last_error_);
                    else
                        accepted = execute({"recording.save.v1",
                            {{"path", path.string()},
                             {"format", std::string("float32")}}, {}, 0});
                else if (operation == "processing-export")
                    accepted = processing_action("save", path.string());
                else
                    accepted = save_setup_controller_profile(path.string());
            }
        }
        if (!accepted) {
            state_.text_entry.visible = true;
            state_.text_entry.error = last_error_;
        }
        ++local_revision_;
        return refresh() || accepted;
    } else return false;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_track(int direction) {
    return invoke_action(direction < 0 ? "track.previous.v1" : "track.next.v1");
}

bool SlintFrontendAdapter::launch_scene(std::size_t index) {
    const auto snapshot = application_.snapshot();
    if (index >= snapshot.performance_launching.scenes.size()) {
        last_error_ = "Performance scene is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"performance.scene-launch.v1",
                    {{"scene", static_cast<std::int64_t>(
                                   snapshot.performance_launching.scenes[index].id)}},
                    {}, 0});
}

bool SlintFrontendAdapter::return_to_arrangement() {
    return execute({"performance.return-arrangement.v1",
                    {{"track", static_cast<std::int64_t>(
                                   application_.snapshot().selected_track)}},
                    {}, 0});
}

bool SlintFrontendAdapter::set_launch_quantization(std::string value) {
    return execute({"pattern.launch-quantization.v1",
                    {{"quantization", std::move(value)}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_launch_quantization() {
    const auto current = application_.snapshot().launch_quantization;
    switch (current) {
    case LaunchQuantization::immediate:
        return set_launch_quantization("quarter");
    case LaunchQuantization::quarter:
        return set_launch_quantization("bar");
    case LaunchQuantization::bar:
        return set_launch_quantization("two-bars");
    default: return set_launch_quantization("immediate");
    }
}

bool SlintFrontendAdapter::transport_record() {
    state_.recording.visible = true;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_recording_mode(std::string mode) {
    auto configuration = recording_workflow_.configuration();
    if (mode == "audio") {
        configuration.mode = RecordingWorkflowMode::audio;
        configuration.source = "external-stereo";
    } else if (mode == "resample") {
        configuration.mode = RecordingWorkflowMode::resample;
        configuration.source = "master-post";
    } else if (mode == "live-loop") {
        configuration.mode = RecordingWorkflowMode::live_loop;
        configuration.source = "master-post";
        configuration.destination_track = application_.snapshot().selected_track;
    } else if (mode == "arrangement")
        configuration.mode = RecordingWorkflowMode::arrangement;
    else if (mode == "performance")
        configuration.mode = RecordingWorkflowMode::performance;
    else {
        last_error_ = "Recording mode is unavailable";
        return false;
    }
    configuration.destination = application_.snapshot().selected_sample_slot;
    std::string error;
    if (!recording_workflow_.configure(std::move(configuration), error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_recording_source(std::string source,
                                                 std::uint64_t source_id) {
    auto configuration = recording_workflow_.configuration();
    configuration.source = std::move(source);
    configuration.source_id = source_id;
    std::string error;
    if (!recording_workflow_.configure(std::move(configuration), error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_recording_destination(std::uint32_t slot) {
    auto configuration = recording_workflow_.configuration();
    configuration.destination = SampleSlotId{slot};
    std::string error;
    if (!recording_workflow_.configure(std::move(configuration), error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_recording_quantization(std::string value) {
    if (value != "immediate" && value != "beat" && value != "bar") {
        last_error_ = "Recording quantization is unavailable";
        return false;
    }
    auto configuration = recording_workflow_.configuration();
    configuration.quantization = std::move(value);
    std::string error;
    if (!recording_workflow_.configure(std::move(configuration), error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_live_loop(std::size_t index) {
    const auto recording = recording_workflow_.snapshot();
    if (index >= recording.live_loop_sessions.size()) {
        last_error_ = "Live-loop session is unavailable";
        return false;
    }
    std::string error;
    if (!recording_workflow_.select_live_loop(
            recording.live_loop_sessions[index].id, error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::recording_action(std::string action) {
    std::string error;
    bool accepted{};
    if (action == "arm" &&
        recording_workflow_.configuration().mode == RecordingWorkflowMode::audio &&
        setup_services_.snapshot) {
        const auto available = setup_services_.snapshot().active_input_channels;
        const auto needed = recording_workflow_.configuration().source ==
                                    "external-mono"
                                ? 1U
                                : 2U;
        if (available < needed)
            error = "Selected external input channels are unavailable";
        else
            accepted = recording_workflow_.arm(error);
    } else if (action == "arm") accepted = recording_workflow_.arm(error);
    else if (action == "start") accepted = recording_workflow_.start(error);
    else if (action == "stop") accepted = recording_workflow_.stop(error);
    else if (action == "overdub") accepted = recording_workflow_.overdub(false, error);
    else if (action == "replace") accepted = recording_workflow_.overdub(true, error);
    else if (action == "clear") accepted = recording_workflow_.clear_live_loop(error);
    else if (action == "save")
        return request_text_entry("recording-export", "live-loop.wav");
    else if (action == "cancel") accepted = recording_workflow_.cancel(error);
    else if (action == "commit") accepted = recording_workflow_.commit(error);
    else {
        error = "Recording operation is unavailable";
    }
    if (!accepted) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
    }
    ++local_revision_;
    (void)refresh();
    return accepted;
}

void SlintFrontendAdapter::close_recording() noexcept {
    state_.recording.visible = false;
    ++local_revision_;
}

bool SlintFrontendAdapter::select_pattern(std::size_t index, bool launch) {
    if (index >= state_.sequencer.patterns.size()) {
        last_error_ = "Pattern is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto id = static_cast<std::int64_t>(state_.sequencer.patterns[index].id);
    if (!execute({"pattern.select.v1", {{"pattern_id", id}}, {}, 0}))
        return false;
    return !launch || execute({"pattern.launch.v1", {{"id", id}, {"queued", true}}, {}, 0});
}

bool SlintFrontendAdapter::create_pattern(bool duplicate) {
    const auto snapshot = application_.snapshot();
    const auto name = duplicate ? "Pattern Copy" : "New Pattern";
    if (duplicate) {
        if (!snapshot.selected_pattern) {
            last_error_ = "No pattern is selected";
            state_.shell.notification = last_error_;
            return false;
        }
        if (!execute({"pattern.duplicate.v1",
                      {{"source", static_cast<std::int64_t>(snapshot.selected_pattern)},
                       {"name", std::string(name)}}, {}, 0}))
            return false;
        const auto after = application_.snapshot();
        const auto created = std::max_element(
            after.patterns.begin(), after.patterns.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
        return created != after.patterns.end() &&
               execute({"pattern.select.v1",
                        {{"pattern_id", static_cast<std::int64_t>(created->id)}},
                        {}, 0});
    }
    const auto length = snapshot.step_ticks > 0 ? snapshot.step_ticks * 16
                                                 : ticks_per_quarter * 4;
    if (!execute({"pattern.create.v1",
                  {{"name", std::string(name)},
                   {"length", static_cast<std::int64_t>(length)}}, {}, 0}))
        return false;
    const auto after = application_.snapshot();
    const auto created = std::max_element(
        after.patterns.begin(), after.patterns.end(),
        [](const auto &a, const auto &b) { return a.id < b.id; });
    return created != after.patterns.end() &&
           execute({"pattern.select.v1",
                    {{"pattern_id", static_cast<std::int64_t>(created->id)}},
                    {}, 0});
}

bool SlintFrontendAdapter::select_step(std::size_t index) {
    if (index >= state_.sequencer.steps.size() ||
        !state_.sequencer.steps[index].available) {
        last_error_ = "Step is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto step = static_cast<std::int64_t>(
        state_.sequencer.step_page * 16 + index);
    if (!execute({"step.toggle.v1", {{"step", step}}, {}, 0}))
        return false;
    // A newly inserted note becomes the editable event. Removal intentionally
    // leaves no selected event rather than targeting a stale identity.
    if (state_.sequencer.steps[index].active)
        return execute({"step.event-select.v1", {{"step", step}}, {}, 0});
    return true;
}

bool SlintFrontendAdapter::set_step_velocity(double value) {
    return execute({"step.velocity-set.v1", {{"velocity", value}}, {}, 0});
}

bool SlintFrontendAdapter::set_step_duration(std::int64_t ticks) {
    return execute({"step.duration-set.v1", {{"duration_ticks", ticks}}, {}, 0});
}

bool SlintFrontendAdapter::change_step_page(int direction) {
    if (direction == 0) return false;
    const auto page = static_cast<std::int64_t>(state_.sequencer.step_page) +
                      (direction > 0 ? 1 : -1);
    if (page < 0 || page >= static_cast<std::int64_t>(
                             state_.sequencer.step_page_count)) {
        last_error_ = "Step page is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.sequencer.playhead_follow = false;
    return execute({"step.page-select.v1", {{"page", page}}, {}, 0});
}

bool SlintFrontendAdapter::set_playhead_follow(bool enabled) {
    state_.sequencer.playhead_follow = enabled;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_step_property(std::string property,
                                              int direction) {
    const auto snapshot = application_.snapshot();
    if (!snapshot.selected_event || direction == 0) {
        last_error_ = "No note event is selected";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto &event = *snapshot.selected_event;
    if (property == "velocity")
        return set_step_velocity(std::clamp(
            static_cast<double>(event.velocity) + direction / 127.0, 0.0, 1.0));
    if (property == "duration")
        return set_step_duration(std::max<MusicalTick>(
            1, event.duration + direction * snapshot.step_ticks));
    if (property == "pitch")
        return execute({"step.pitch-set.v1",
            {{"pitch", static_cast<std::int64_t>(std::clamp(
                static_cast<int>(event.pitch) + direction, 0, 127))}}, {}, 0});
    if (property == "probability")
        return execute({"step.probability-set.v1",
            {{"probability", std::clamp(
                static_cast<double>(event.trigger_probability) + direction * 0.05,
                0.0, 1.0)}}, {}, 0});
    if (property == "ratchet")
        return execute({"step.ratchet-set.v1",
            {{"count", static_cast<std::int64_t>(std::clamp(
                static_cast<int>(event.ratchet_count) + direction, 1, 8))}}, {}, 0});
    if (property == "timing") {
        const auto limit = std::max<MusicalTick>(1, snapshot.step_ticks - 1);
        return execute({"step.timing-offset-set.v1",
            {{"offset_ticks", static_cast<std::int64_t>(std::clamp<MusicalTick>(
                event.timing_offset + direction, -limit, limit))}}, {}, 0});
    }
    last_error_ = "Step property is unavailable";
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_step_condition() {
    const auto snapshot = application_.snapshot();
    if (!snapshot.selected_event) return false;
    static constexpr std::array<std::string_view, 9> values{
        "always", "first", "not-first", "every-n", "n-of-m",
        "previous-fired", "previous-not-fired", "fill", "not-fill"};
    const auto next = (static_cast<std::size_t>(
        snapshot.selected_event->trigger_condition) + 1) % values.size();
    return execute({"step.condition-set.v1",
        {{"condition", std::string(values[next])},
         {"n", std::int64_t{1}}, {"m", std::int64_t{2}}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_lock_parameter(int direction) {
    if (state_.sequencer.lock_parameters.empty() || direction == 0) return false;
    const auto size = state_.sequencer.lock_parameters.size();
    state_.sequencer.selected_lock_index = direction > 0
        ? (state_.sequencer.selected_lock_index + 1) % size
        : (state_.sequencer.selected_lock_index + size - 1) % size;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_selected_lock(std::string operation,
                                              int direction) {
    if (state_.sequencer.lock_parameters.empty()) return false;
    const auto id = state_.sequencer.lock_parameters[
        state_.sequencer.selected_lock_index];
    if (operation == "remove")
        return execute({"step.lock-remove.v1", {{"parameter_id", id}}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto descriptor = std::find_if(snapshot.parameters.begin(),
        snapshot.parameters.end(), [&](const auto &candidate) {
            return candidate.id == id;
        });
    if (descriptor == snapshot.parameters.end()) return false;
    auto value = numeric_value(descriptor->current_value);
    const auto lock = std::find_if(snapshot.selected_step_locks.begin(),
        snapshot.selected_step_locks.end(), [&](const auto &candidate) {
            return candidate.parameter_id == id;
        });
    if (lock != snapshot.selected_step_locks.end()) value = lock->value;
    if (operation == "adjust")
        value += direction * descriptor->step;
    value = std::clamp(value, numeric_value(descriptor->minimum),
                       numeric_value(descriptor->maximum));
    return execute({"step.lock-set.v1",
        {{"parameter_id", id}, {"value", value}}, {}, 0});
}

bool SlintFrontendAdapter::manage_pattern(std::string operation) {
    const auto id = static_cast<std::int64_t>(state_.sequencer.selected_pattern_id);
    if (operation == "delete") {
        state_.sequencer.confirm_delete_pattern = true;
        return id > 0 && open_modal("sequencer.pattern-delete",
                                    std::to_string(id), true);
    }
    if (operation == "cancel-delete") {
        return execute({"navigation.cancel.v1", {}, {}, 0});
    }
    if (operation == "confirm-delete") {
        if (!execute({"navigation.focus-next.v1", {}, {}, 0})) return false;
        return execute({"navigation.confirm.v1", {}, {}, 0});
    }
    if (operation == "launch")
        return id > 0 && execute({"pattern.launch.v1",
            {{"id", id}, {"queued", true}}, {}, 0});
    if (operation != "length-up" && operation != "length-down") {
        last_error_ = "Pattern operation is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto snapshot = application_.snapshot();
    const auto pattern = std::find_if(snapshot.patterns.begin(), snapshot.patterns.end(),
        [&](const auto &candidate) { return candidate.id == state_.sequencer.selected_pattern_id; });
    if (pattern == snapshot.patterns.end()) return false;
    const auto delta = snapshot.step_ticks * 16;
    const auto length = operation == "length-up" ? pattern->length + delta
        : std::max<MusicalTick>(snapshot.step_ticks, pattern->length - delta);
    return execute({"pattern.length-set.v1",
        {{"id", id}, {"length", static_cast<std::int64_t>(length)}}, {}, 0});
}

bool SlintFrontendAdapter::select_track_index(std::size_t index) {
    if (index >= state_.sequencer.tracks.size()) return false;
    return execute({"track.select.v1",
        {{"track_id", static_cast<std::int64_t>(
            state_.sequencer.tracks[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::manage_track(std::string operation) {
    if (operation == "delete") {
        state_.sequencer.confirm_delete_track = true;
        return open_modal("sequencer.track-delete",
                          std::to_string(application_.snapshot().selected_track),
                          true);
    }
    if (operation == "cancel-delete") {
        return execute({"navigation.cancel.v1", {}, {}, 0});
    }
    if (operation == "confirm-delete") {
        if (!execute({"navigation.focus-next.v1", {}, {}, 0})) return false;
        return execute({"navigation.confirm.v1", {}, {}, 0});
    }
    if (operation == "duplicate")
        return execute({"track.duplicate.v1", {}, {}, 0});
    if (operation == "mute")
        return execute({"track.mute-toggle.v1", {}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto selected = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
        [&](const auto &candidate) { return candidate.id == snapshot.selected_track; });
    const auto length = selected == snapshot.tracks.end()
        ? ticks_per_quarter * 4 : selected->pattern_length;
    if (operation == "create-synth")
        return execute({"track.create.v1",
            {{"name", std::string("Synth Track")},
             {"length", static_cast<std::int64_t>(length)},
             {"instrument_id", std::string(subtractive_instrument_id)}}, {}, 0});
    if (operation == "create-sample")
        return execute({"track.create.v1",
            {{"name", std::string("Sample Track")},
             {"length", static_cast<std::int64_t>(length)},
             {"instrument_id", std::string(sample_instrument_id)}}, {}, 0});
    last_error_ = "Track operation is unavailable";
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_step_resolution() {
    const auto current = application_.snapshot().step_resolution;
    const auto next = static_cast<StepResolution>((static_cast<unsigned>(current) + 1) % 8);
    return execute({"step.resolution-set.v1",
                    {{"resolution", std::string(step_resolution_name(next))}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_arrangement_view() {
    state_.sequencer.arrangement_view = !state_.sequencer.arrangement_view;
    std::string error;
    if (!application_.set_interaction_destination(
            state_.shell.destination_id.empty() ? "sequencer"
                                                : state_.shell.destination_id,
            state_.sequencer.arrangement_view ? "arrangement" : "root", error)) {
        last_error_ = std::move(error);
        state_.shell.notification = last_error_;
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_arrangement_clip(std::size_t index) {
    if (index >= state_.sequencer.arrangement.size()) {
        last_error_ = "Arrangement clip is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.sequencer.selected_clip_id = state_.sequencer.arrangement[index].id;
    state_.sequencer.selected_clip_kind = state_.sequencer.arrangement[index].kind;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::manage_arrangement(std::string operation) {
    const auto snapshot = application_.snapshot();
    const auto quantum = std::max<MusicalTick>(
        1, snapshot.step_ticks > 0 ? snapshot.step_ticks : ticks_per_quarter / 4);
    if (operation == "create-pattern") {
        if (!snapshot.selected_pattern) {
            last_error_ = "Select a reusable pattern before placing a clip";
            state_.shell.notification = last_error_;
            return false;
        }
        const auto pattern = std::find_if(snapshot.patterns.begin(),
            snapshot.patterns.end(), [&](const auto &item) {
                return item.id == snapshot.selected_pattern;
            });
        if (pattern == snapshot.patterns.end()) return false;
        return execute({"arrangement.clip-create.v1",
            {{"pattern", static_cast<std::int64_t>(pattern->id)},
             {"start", static_cast<std::int64_t>(snapshot.transport_tick)},
             {"duration", static_cast<std::int64_t>(pattern->length)}}, {}, 0});
    }
    if (operation == "create-audio")
        return execute({"arrangement.audio-clip-create.v1",
            {{"track", static_cast<std::int64_t>(snapshot.selected_track)},
             {"sample_slot", static_cast<std::int64_t>(snapshot.selected_sample_slot.value)},
             {"start", static_cast<std::int64_t>(snapshot.transport_tick)},
             {"duration", static_cast<std::int64_t>(ticks_per_quarter * 4)}}, {}, 0});
    if (operation == "loop-toggle" || operation == "loop-start" ||
        operation == "loop-end") {
        bool enabled = snapshot.arrangement_loop_enabled;
        auto start = snapshot.arrangement_loop_start;
        auto end = snapshot.arrangement_loop_end;
        if (operation == "loop-toggle") enabled = !enabled;
        else if (operation == "loop-start") {
            start = snapshot.transport_tick;
            if (end <= start) end = start + ticks_per_quarter * 4;
            enabled = true;
        } else {
            end = snapshot.transport_tick;
            if (end <= start) {
                last_error_ = "Loop end must be after loop start";
                state_.shell.notification = last_error_;
                return false;
            }
            enabled = true;
        }
        return execute({"arrangement.loop-set.v1",
                        {{"enabled", enabled},
                         {"start", static_cast<std::int64_t>(start)},
                         {"end", static_cast<std::int64_t>(end)}}, {}, 0});
    }
    const auto selected = std::find_if(state_.sequencer.arrangement.begin(),
        state_.sequencer.arrangement.end(), [&](const auto &item) {
            return item.id == state_.sequencer.selected_clip_id &&
                   item.kind == state_.sequencer.selected_clip_kind;
        });
    if (selected == state_.sequencer.arrangement.end()) {
        last_error_ = "Select an arrangement clip first";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto prefix = selected->kind == "Audio"
        ? std::string("arrangement.audio-clip-")
        : std::string("arrangement.clip-");
    if (operation == "delete") {
        state_.sequencer.confirm_delete_clip = true;
        return open_modal("sequencer.clip-delete",
                          selected->kind + ":" + std::to_string(selected->id),
                          true);
    }
    if (operation == "cancel-delete") {
        return execute({"navigation.cancel.v1", {}, {}, 0});
    }
    if (operation == "confirm-delete") {
        if (!execute({"navigation.focus-next.v1", {}, {}, 0})) return false;
        return execute({"navigation.confirm.v1", {}, {}, 0});
    }
    if (operation == "duplicate")
        return execute({prefix + "duplicate.v1",
            {{"source", static_cast<std::int64_t>(selected->id)},
             {"start", static_cast<std::int64_t>(selected->start + selected->duration)}}, {}, 0});
    if (operation == "seek")
        return execute({"arrangement.seek.v1",
                        {{"tick", static_cast<std::int64_t>(selected->start)}}, {}, 0});
    auto start = selected->start;
    auto duration = selected->duration;
    if (operation == "move-left") start = std::max<MusicalTick>(0, start - quantum);
    else if (operation == "move-right") start += quantum;
    else if (operation == "shorter") duration = std::max<MusicalTick>(quantum, duration - quantum);
    else if (operation == "longer") duration += quantum;
    else {
        last_error_ = "Arrangement operation is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({prefix + "update.v1",
                    {{"id", static_cast<std::int64_t>(selected->id)},
                     {"start", static_cast<std::int64_t>(start)},
                     {"duration", static_cast<std::int64_t>(duration)}}, {}, 0});
}

bool SlintFrontendAdapter::select_sample_slot(int direction) {
    const auto snapshot = application_.snapshot();
    if (snapshot.sample_slots.empty()) {
        last_error_ = "No sample slots are available";
        state_.shell.notification = last_error_;
        return false;
    }
    auto found = std::find_if(snapshot.sample_slots.begin(),
                              snapshot.sample_slots.end(), [&](const auto &slot) {
                                  return slot.id == snapshot.selected_sample_slot;
                              });
    auto index = found == snapshot.sample_slots.end()
        ? std::size_t{}
        : static_cast<std::size_t>(found - snapshot.sample_slots.begin());
    index = direction < 0
        ? (index + snapshot.sample_slots.size() - 1) % snapshot.sample_slots.size()
        : (index + 1) % snapshot.sample_slots.size();
    return execute({"sample.slot-select.v1",
                    {{"slot_id", static_cast<std::int64_t>(
                                     snapshot.sample_slots[index].id.value)}}, {}, 0});
}

bool SlintFrontendAdapter::load_sample(std::string path) {
    if (path.empty()) {
        last_error_ = "A WAV path is required";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({state_.sample.ready ? "sample.replace.v1" : "sample.load.v1",
                    {{"slot_id", static_cast<std::int64_t>(state_.sample.slot)},
                     {"path", std::move(path)}}, {}, 0});
}

bool SlintFrontendAdapter::select_slice(std::size_t index) {
    if (index >= state_.sample.slices.size()) {
        last_error_ = "Slice is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"slice.select.v1",
                    {{"slice_id", static_cast<std::int64_t>(
                                      state_.sample.slices[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::create_slice(std::uint64_t frame) {
    if (!state_.sample.ready || frame >= state_.sample.frames) {
        last_error_ = "Slice position is outside the active sample";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto length = std::max<std::uint64_t>(
        1, state_.sample.sample_rate ? state_.sample.sample_rate / 4 : 1);
    const auto end = std::min(state_.sample.frames, frame + length);
    return execute({"slice.create.v1",
                    {{"start_frame", static_cast<std::int64_t>(frame)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::move_selected_slice(std::uint64_t start,
                                                std::uint64_t end) {
    return execute({"slice.bounds-set.v1",
                    {{"start_frame", static_cast<std::int64_t>(start)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::delete_selected_slice() {
    return invoke_action("slice.delete.v1");
}

bool SlintFrontendAdapter::assign_selected_slice(std::size_t pad) {
    return execute({"slice.pad-assign.v1",
                    {{"pad", static_cast<std::int64_t>(pad)}}, {}, 0});
}

bool SlintFrontendAdapter::preview_sample(std::string_view target) {
    if (target == "sample")
        return invoke_action("sample.preview.v1");
    if (target == "slice")
        return invoke_action("slice.preview.v1");
    if (target == "region")
        return invoke_action("region.preview.v1");
    last_error_ = "Preview target is unavailable";
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::stop_preview() {
    return invoke_action("slice.preview-stop.v1");
}

bool SlintFrontendAdapter::analyze_transients() {
    return execute({"sample-analysis.start.v1",
                    {{"algorithm", std::string("transients")},
                     {"sensitivity", 0.5},
                     {"minimum_frames", static_cast<std::int64_t>(
                                            std::max<std::uint32_t>(1, state_.sample.sample_rate / 20))}},
                    {}, 0});
}

bool SlintFrontendAdapter::apply_analysis(bool accept) {
    if (!accept)
        return invoke_action("sample-analysis.discard.v1");
    return execute({"sample-analysis.apply.v1",
                    {{"target", std::string("slices")},
                     {"mode", std::string("replace")},
                     {"assign_pads", false}}, {}, 0});
}

bool SlintFrontendAdapter::select_region(std::size_t index) {
    if (index >= state_.sample.regions.size()) {
        last_error_ = "Region is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"region.select.v1",
                    {{"region_id", static_cast<std::int64_t>(
                                       state_.sample.regions[index].id)}}, {}, 0});
}

bool SlintFrontendAdapter::set_region_bounds(std::uint64_t start,
                                              std::uint64_t end) {
    return execute({"region.bounds-set.v1",
                    {{"start_frame", static_cast<std::int64_t>(start)},
                     {"end_frame", static_cast<std::int64_t>(end)}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_region_loop() {
    return execute({"region.loop-set.v1",
                    {{"enabled", !state_.sample.loop_enabled},
                     {"start_frame", static_cast<std::int64_t>(state_.sample.region_start)},
                     {"end_frame", static_cast<std::int64_t>(state_.sample.region_end)},
                     {"crossfade_frames", std::int64_t{0}}}, {}, 0});
}

bool SlintFrontendAdapter::set_region_fades(std::uint64_t fade_in,
                                             std::uint64_t fade_out) {
    return execute({"region.fades-set.v1",
                    {{"fade_in_frames", static_cast<std::int64_t>(fade_in)},
                     {"fade_out_frames", static_cast<std::int64_t>(fade_out)},
                     {"curve", std::string("linear")}}, {}, 0});
}

bool SlintFrontendAdapter::start_processing(std::string operation) {
    if (operation != "normalize-peak" && operation != "reverse" &&
        operation != "trim" && operation != "fade") {
        last_error_ = "Processing operation is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto source = state_.sample.region_end > state_.sample.region_start
        ? std::string("region") : std::string("sample");
    std::map<std::string, ApplicationValue> create{{"source", source}};
    if (source == "region")
        create.emplace("source_id", static_cast<std::int64_t>(
            application_.snapshot().selected_region));
    if (!execute({"sample-processing.create.v1", std::move(create), {}, 0}))
        return false;
    std::map<std::string, ApplicationValue> arguments{{"operation", operation}};
    if (operation == "trim") {
        arguments.emplace("start_frame", static_cast<std::int64_t>(state_.sample.region_start));
        arguments.emplace("end_frame", static_cast<std::int64_t>(state_.sample.region_end));
    } else if (operation == "fade") {
        arguments.emplace("start_frame", static_cast<std::int64_t>(state_.sample.fade_in));
        arguments.emplace("end_frame", static_cast<std::int64_t>(state_.sample.fade_out));
    }
    return execute({"sample-processing.operation-add.v1", std::move(arguments), {}, 0}) &&
           invoke_action("sample-processing.start.v1");
}

bool SlintFrontendAdapter::processing_action(std::string action,
                                              std::string value) {
    if (action == "cancel")
        return invoke_action("sample-processing.cancel.v1");
    if (action == "discard")
        return invoke_action("sample-processing.discard.v1");
    if (action == "publish") {
        auto slot = state_.sample.slot;
        if (!value.empty()) {
            try {
                const auto parsed = std::stoull(value);
                if (parsed >= invalid_sample_slot.value)
                    throw std::out_of_range("slot");
                slot = static_cast<std::uint32_t>(parsed);
            } catch (const std::exception &) {
                last_error_ = "Processing destination slot is invalid";
                state_.shell.notification = last_error_;
                return false;
            }
        }
        return execute({"sample-processing.publish.v1",
                        {{"slot_id", static_cast<std::int64_t>(slot)}}, {}, 0});
    }
    if (action == "save") {
        if (value.empty()) {
            last_error_ = "A result WAV path is required";
            state_.shell.notification = last_error_;
            return false;
        }
        return execute({"sample-processing.save.v1",
                        {{"path", std::move(value)},
                         {"format", std::string("float32")},
                         {"overwrite", false}}, {}, 0});
    }
    last_error_ = "Processing operation is unavailable";
    return false;
}

bool SlintFrontendAdapter::set_waveform_view(float zoom, float offset) {
    if (!std::isfinite(zoom) || !std::isfinite(offset))
        return false;
    state_.sample.zoom = std::clamp(zoom, 1.0F, 64.0F);
    state_.sample.offset = std::clamp(offset, 0.0F, 1.0F);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_sound_subview(std::string subview) {
    if (subview != "instrument" && subview != "modulation" &&
        subview != "presets") {
        last_error_ = "Sound subview is unavailable";
        return false;
    }
    state_.sound.subview = std::move(subview);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::set_track_instrument(std::string instrument_id) {
    if (instrument_id != sample_instrument_id &&
        instrument_id != subtractive_instrument_id) {
        last_error_ = "Instrument type is unsupported";
        state_.shell.notification = last_error_;
        return false;
    }
    return execute({"track.instrument-set.v1",
                    {{"instrument_id", std::move(instrument_id)}}, {}, 0});
}

bool SlintFrontendAdapter::select_sound_parameter(std::size_t index) {
    if (index >= state_.sound.parameters.size()) {
        last_error_ = "Sound parameter is unavailable";
        return false;
    }
    state_.sound.selected_parameter = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_sound_parameter(std::size_t index,
                                                   double steps) {
    if (index >= state_.sound.parameters.size() || !std::isfinite(steps)) {
        last_error_ = "Sound parameter is unavailable";
        return false;
    }
    const auto &parameter = state_.sound.parameters[index];
    const auto descriptor = application_.parameters().get(parameter.id);
    if (!descriptor) {
        last_error_ = "Sound parameter is stale";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto value = std::clamp(numeric_value(descriptor->current_value) +
                                      descriptor->step * steps,
                                  parameter.minimum, parameter.maximum);
    if (state_.sound.synth)
        return execute({"instrument.instance-parameter-set.v1",
                        {{"instance_id", static_cast<std::int64_t>(
                                             state_.sound.instrument_instance)},
                         {"parameter_id", parameter.id}, {"value", value}}, {}, 0});
    return execute({"parameter.set-normalized.v1",
                    {{"parameter_id", parameter.id},
                     {"value", parameter.maximum > parameter.minimum
                                   ? (value - parameter.minimum) /
                                         (parameter.maximum - parameter.minimum)
                                   : 0.0}}, {}, 0});
}

bool SlintFrontendAdapter::cycle_sample_playback_mode() {
    auto mode = std::string("one-shot");
    switch (state_.sound.sample_settings.mode) {
    case SamplePlaybackMode::one_shot: mode = "gate"; break;
    case SamplePlaybackMode::gate: mode = "toggle"; break;
    case SamplePlaybackMode::toggle: mode = "one-shot"; break;
    }
    return execute({"pad.playback-mode-set.v1", {{"mode", mode}}, {}, 0});
}

bool SlintFrontendAdapter::adjust_sample_instrument(std::string property,
                                                     double steps) {
    const auto settings = state_.sound.sample_settings;
    if (property == "pitch")
        return execute({"pad.pitch-set.v1",
                        {{"semitones", std::clamp<double>(
                            settings.semitones + steps, -48.0, 48.0)}}, {}, 0});
    if (property == "fine")
        return execute({"pad.fine-set.v1",
                        {{"cents", std::clamp<double>(
                            settings.fine_cents + steps, -100.0, 100.0)}}, {}, 0});
    if (property == "gain")
        return execute({"pad.gain-set.v1",
                        {{"gain", std::clamp<double>(
                            settings.gain + steps * 0.05, 0.0, 4.0)}}, {}, 0});
    const auto delta = steps * 0.01;
    auto attack = settings.attack_seconds;
    auto decay = settings.decay_seconds;
    auto sustain = settings.sustain;
    auto release = settings.release_seconds;
    if (property == "attack") attack = std::clamp<float>(attack + delta, 0, 60);
    else if (property == "decay") decay = std::clamp<float>(decay + delta, 0, 60);
    else if (property == "sustain") sustain = std::clamp<float>(sustain + delta, 0, 1);
    else if (property == "release") release = std::clamp<float>(release + delta, 0, 60);
    else {
        last_error_ = "Sample instrument property is unavailable";
        return false;
    }
    return execute({"pad.envelope-set.v1",
                    {{"attack", static_cast<double>(attack)},
                     {"decay", static_cast<double>(decay)},
                     {"sustain", static_cast<double>(sustain)},
                     {"release", static_cast<double>(release)}}, {}, 0});
}

bool SlintFrontendAdapter::select_sound_modulation(std::size_t index) {
    if (index >= state_.sound.modulation.size()) {
        last_error_ = "Modulation assignment is unavailable";
        return false;
    }
    state_.sound.selected_modulation = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_sound_modulation(std::string operation) {
    if (state_.sound.selected_modulation >= state_.sound.modulation.size()) {
        last_error_ = "Modulation assignment is unavailable";
        return false;
    }
    const auto &route = state_.sound.modulation[state_.sound.selected_modulation];
    if (operation == "delete")
        return execute({"modulation.route-delete.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)}}, {}, 0});
    if (operation == "toggle")
        return execute({"modulation.route-enable.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)},
                         {"enabled", !route.enabled}}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto found = std::find_if(snapshot.modulation.routes.begin(),
        snapshot.modulation.routes.begin() + snapshot.modulation.route_count,
        [&](const auto &value) { return value.id == route.id; });
    if (found == snapshot.modulation.routes.begin() + snapshot.modulation.route_count) {
        last_error_ = "Modulation assignment is stale";
        return false;
    }
    const auto delta = operation == "depth-up" ? 0.05F
                     : operation == "depth-down" ? -0.05F : 0.0F;
    if (delta == 0) {
        last_error_ = "Modulation operation is unavailable";
        return false;
    }
    return execute({"modulation.route-depth.v1",
                    {{"route_id", static_cast<std::int64_t>(route.id)},
                     {"depth", static_cast<double>(
                         std::clamp(found->depth + delta, -1.0F, 1.0F))}}, {}, 0});
}

bool SlintFrontendAdapter::preset_action(std::string operation,
                                          std::string library,
                                          std::string preset_id) {
    state_.sound.preset_library = library;
    state_.sound.preset_id = preset_id;
    if (operation == "browse") {
        if (!select_page(6)) return false;
        return select_browser_category("instrument-presets");
    }
    if (operation == "browse-fx") {
        if (!select_page(6)) return false;
        return select_browser_category("fx-presets");
    }
    if (operation == "save")
        return request_text_entry("preset-capture-sound", "New Preset");
    if (operation == "save-fx") {
        if (state_.fx.owner_kind != "track") {
            last_error_ = "FX preset capture currently requires a track owner";
            state_.shell.notification = last_error_;
            return false;
        }
        return request_text_entry("preset-capture-fx", "New FX Chain");
    }
    if (operation == "apply") return invoke_action("preset.apply.v1");
    if (operation == "cancel") return invoke_action("preset.cancel.v1");
    if (library.empty() || preset_id.empty()) {
        last_error_ = "Preset library and ID are required";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto action = operation == "validate" ? "preset.validate.v1"
                                                 : "preset.prepare.v1";
    return execute({action,
                    {{"library", std::move(library)},
                     {"preset_id", std::move(preset_id)},
                     {"mode", std::string("replace")},
                     {"track_id", static_cast<std::int64_t>(state_.sound.track_id)}},
                    {}, 0});
}

bool SlintFrontendAdapter::select_mixer_channel(std::size_t index) {
    if (index >= state_.mixer.channels.size()) {
        last_error_ = "Mixer channel is unavailable";
        state_.shell.notification = last_error_;
        return false;
    }
    state_.mixer.selected_channel = index;
    ++local_revision_;
    const auto &channel = state_.mixer.channels[index];
    if (channel.type == "Track") {
        const auto snapshot = application_.snapshot();
        const auto found = std::find_if(
            snapshot.mixer.configuration.tracks.begin(),
            snapshot.mixer.configuration.tracks.begin() +
                snapshot.mixer.configuration.track_count,
            [&](const auto &track) { return track.track == channel.id; });
        if (found == snapshot.mixer.configuration.tracks.begin() +
                         snapshot.mixer.configuration.track_count)
            return false;
        return execute({"mixer.channel-select.v1",
                        {{"index", static_cast<std::int64_t>(found -
                            snapshot.mixer.configuration.tracks.begin())}}, {}, 0});
    }
    if (channel.type == "Group")
        return execute({"mixer.group-select.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)}}, {}, 0});
    (void)refresh();
    return true;
}

bool SlintFrontendAdapter::bank_mixer(int direction) {
    constexpr std::size_t bank_size = 8;
    const auto count = state_.mixer.channels.size();
    const auto maximum = count ? (count - 1) / bank_size : 0;
    if (direction < 0 && state_.mixer.bank) --state_.mixer.bank;
    else if (direction > 0 && state_.mixer.bank < maximum) ++state_.mixer.bank;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_mixer(std::string operation, double steps) {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size() ||
        !std::isfinite(steps)) {
        last_error_ = "Selected mixer channel is unavailable";
        return false;
    }
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    const auto delta = operation == "pan" ? 0.05 * steps : 0.05 * steps;
    const auto gain = std::clamp<double>(channel.gain + delta, 0.0, 2.0);
    const auto pan = std::clamp<double>(channel.pan + delta, -1.0, 1.0);
    const auto aux_index = operation == "aux-2" ? 1U : 0U;
    const auto aux = std::clamp<double>(channel.aux_sends[aux_index] + delta,
                                        0.0, 1.0);
    if (channel.type == "Track") {
        if (operation == "gain")
            return execute({"mixer.gain-set.v1", {{"value", gain}}, {}, 0});
        if (operation == "pan")
            return execute({"mixer.pan-set.v1", {{"value", pan}}, {}, 0});
        if (operation == "aux-1" || operation == "aux-2")
            return execute({"mixer.aux-send-set.v1",
                            {{"aux", static_cast<std::int64_t>(aux_index)},
                             {"value", aux}}, {}, 0});
    } else if (channel.type == "Group") {
        if (operation == "gain" || operation == "pan")
            return execute({"mixer.group-levels.v1",
                            {{"id", static_cast<std::int64_t>(channel.id)},
                             {"gain", gain}, {"pan", pan}}, {}, 0});
        if (operation == "aux-1" || operation == "aux-2")
            return execute({"mixer.group-aux-send.v1",
                            {{"id", static_cast<std::int64_t>(channel.id)},
                             {"aux", static_cast<std::int64_t>(aux_index)},
                             {"value", aux}}, {}, 0});
    } else if (channel.type == "Aux Return" && operation == "gain") {
        const auto snapshot = application_.snapshot();
        const auto &bus = snapshot.mixer.configuration.aux[channel.id];
        return execute({"mixer.aux-set.v1",
                        {{"aux", static_cast<std::int64_t>(channel.id)},
                         {"gain", static_cast<double>(bus.gain)},
                         {"return_gain", gain},
                         {"effect_enabled", bus.effect_enabled}}, {}, 0});
    } else if (channel.type == "Master" && operation == "gain")
        return execute({"mixer.master-gain-set.v1", {{"value", gain}}, {}, 0});
    last_error_ = operation + " is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::toggle_mixer(std::string operation) {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.type == "Track")
        return invoke_action(operation == "mute" ? "mixer.mute-toggle.v1"
                                                  : "mixer.solo-toggle.v1");
    if (channel.type == "Group")
        return execute({operation == "mute" ? "mixer.group-mute-toggle.v1"
                                             : "mixer.group-solo-toggle.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)}}, {}, 0});
    if (channel.type == "Master" && operation == "mute")
        return invoke_action("mixer.master-mute-toggle.v1");
    last_error_ = operation + " is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_mixer_route() {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.route.rfind("Group ", 0) == 0) {
        last_error_ = "Return the track to Master before selecting a direct output";
        state_.shell.notification = last_error_;
        return false;
    }
    // Skip the default master pair (1/2) when leaving Master; the mixer
    // validator prevents an implicit duplicate path onto the master outputs.
    const auto next = channel.route == "Master" ? 2 :
        channel.route == "Output 3/4" ? 4 : -1;
    if (channel.type == "Track")
        return execute({"mixer.output-pair-set.v1",
                        {{"pair", static_cast<std::int64_t>(next)}}, {}, 0});
    if (channel.type == "Group")
        return execute({"mixer.group-output.v1",
                        {{"id", static_cast<std::int64_t>(channel.id)},
                         {"group_id", std::int64_t{0}},
                         {"output_pair", static_cast<std::int64_t>(next)}}, {}, 0});
    last_error_ = "Output routing is unavailable for " + channel.type;
    state_.shell.notification = last_error_;
    return false;
}

bool SlintFrontendAdapter::cycle_mixer_group() {
    if (state_.mixer.selected_channel >= state_.mixer.channels.size()) return false;
    const auto &channel = state_.mixer.channels[state_.mixer.selected_channel];
    if (channel.type != "Track") {
        last_error_ = "Group assignment is available only for tracks";
        state_.shell.notification = last_error_;
        return false;
    }
    const auto snapshot = application_.snapshot();
    const auto track = std::find_if(
        snapshot.mixer.configuration.tracks.begin(),
        snapshot.mixer.configuration.tracks.begin() +
            snapshot.mixer.configuration.track_count,
        [&](const auto &value) { return value.track == channel.id; });
    if (track == snapshot.mixer.configuration.tracks.begin() +
                     snapshot.mixer.configuration.track_count)
        return false;
    MixerBusId destination{};
    if (!snapshot.mixer.configuration.group_count) {
        last_error_ = "No group buses are available";
        state_.shell.notification = last_error_;
        return false;
    }
    for (std::size_t i = 0; i < snapshot.mixer.configuration.group_count; ++i) {
        if (snapshot.mixer.configuration.groups[i].id == track->destination_group) {
            if (i + 1 < snapshot.mixer.configuration.group_count)
                destination = snapshot.mixer.configuration.groups[i + 1].id;
            break;
        }
        if (!track->destination_group && i == 0)
            destination = snapshot.mixer.configuration.groups[0].id;
    }
    return execute({"mixer.track-destination.v1",
                    {{"track_id", static_cast<std::int64_t>(channel.id)},
                     {"group_id", static_cast<std::int64_t>(destination)}}, {}, 0});
}

bool SlintFrontendAdapter::select_external_fx(std::size_t index) {
    if (index >= state_.mixer.external_fx.size()) return false;
    return execute({"mixer.fx-loop-select.v1",
                    {{"index", static_cast<std::int64_t>(index)}}, {}, 0});
}

bool SlintFrontendAdapter::adjust_external_fx(std::string operation,
                                               double steps) {
    const auto snapshot = application_.snapshot();
    const auto index = snapshot.selected_external_fx_loop;
    if (index >= mixer_external_fx_count) return false;
    const auto &loop = snapshot.mixer.configuration.external_fx[index];
    auto send = static_cast<double>(loop.send_gain);
    auto receive = static_cast<double>(loop.return_gain);
    auto wet = static_cast<double>(loop.wet);
    if (operation == "send") send = std::clamp(send + steps * .05, 0.0, 2.0);
    else if (operation == "return") receive = std::clamp(receive + steps * .05, 0.0, 2.0);
    else if (operation == "wet") wet = std::clamp(wet + steps * .05, 0.0, 1.0);
    else if (operation == "latency")
        return execute({"mixer.fx-loop-latency.v1",
                        {{"index", static_cast<std::int64_t>(index)},
                         {"frames", static_cast<std::int64_t>(std::clamp<double>(
                             loop.latency_frames + steps * 16.0, 0.0,
                             maximum_external_fx_latency_frames))}}, {}, 0});
    else return false;
    return execute({"mixer.fx-loop-levels.v1",
                    {{"index", static_cast<std::int64_t>(index)},
                     {"send_gain", send}, {"return_gain", receive},
                     {"wet", wet}}, {}, 0});
}

bool SlintFrontendAdapter::toggle_external_fx(std::string operation) {
    const auto snapshot = application_.snapshot();
    const auto index = snapshot.selected_external_fx_loop;
    if (index >= mixer_external_fx_count) return false;
    const auto &loop = snapshot.mixer.configuration.external_fx[index];
    const auto source = loop.source == ExternalFxSource::track ? "track" :
        loop.source == ExternalFxSource::auxiliary ? "aux" :
        loop.source == ExternalFxSource::group ? "group" : "master";
    if (operation == "return-mute")
        return execute({"mixer.fx-loop-levels.v1",
                        {{"index", static_cast<std::int64_t>(index)},
                         {"send_gain", static_cast<double>(loop.send_gain)},
                         {"return_gain", static_cast<double>(loop.return_gain)},
                         {"wet", static_cast<double>(loop.wet)},
                         {"return_mute", !loop.return_mute}}, {}, 0});
    const auto cycle_pair = [](std::uint8_t left) {
        return static_cast<std::uint8_t>(left < 2 ? 2 : left < 4 ? 4 : 0);
    };
    const auto send_left = operation == "send-pair" ? cycle_pair(loop.send_left)
                                                     : loop.send_left;
    const auto return_left = operation == "return-pair"
        ? cycle_pair(loop.return_left) : loop.return_left;
    return execute({"mixer.fx-loop-set.v1",
                    {{"index", static_cast<std::int64_t>(index)},
                     {"source", std::string(source)},
                     {"source_id", static_cast<std::int64_t>(loop.source_id)},
                     {"send_left", static_cast<std::int64_t>(send_left)},
                     {"send_right", static_cast<std::int64_t>(send_left + 1)},
                     {"return_left", static_cast<std::int64_t>(return_left)},
                     {"return_right", static_cast<std::int64_t>(return_left + 1)},
                     {"enabled", operation == "enabled" ? !loop.enabled : loop.enabled},
                     {"bypass", operation == "bypass" ? !loop.bypass : loop.bypass}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_subview(std::string subview) {
    static constexpr std::array<std::string_view, 6> valid{
        "chain", "parameters", "modulation", "macros", "external", "presets"};
    if (std::find(valid.begin(), valid.end(), subview) == valid.end()) {
        last_error_ = "Unknown FX subview";
        return false;
    }
    state_.fx.subview = std::move(subview);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_fx_owner(std::size_t index) {
    if (index >= state_.fx.owners.size()) return false;
    state_.fx.selected_owner = index;
    state_.fx.selected_effect = state_.fx.selected_parameter = 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_effect(std::size_t index) {
    if (index >= state_.fx.effects.size()) return false;
    state_.fx.selected_effect = index;
    state_.fx.selected_parameter = 0;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::add_effect(std::string type) {
    const auto snapshot = application_.snapshot();
    EffectInstanceId id = 1;
    for (std::size_t i = 0; i < snapshot.mixer.configuration.effects.instance_count; ++i)
        id = std::max(id, snapshot.mixer.configuration.effects.instances[i].id + 1);
    if (!execute({"effect.create.v1",
                  {{"id", static_cast<std::int64_t>(id)}, {"type", std::move(type)}}, {}, 0}))
        return false;
    return execute({"effect.insert.v1",
                    {{"id", static_cast<std::int64_t>(id)},
                     {"chain", state_.fx.owner_kind},
                     {"index", state_.fx.owner_index},
                     {"position", static_cast<std::int64_t>(state_.fx.effects.size())}}, {}, 0});
}

bool SlintFrontendAdapter::edit_effect(std::string operation) {
    if (operation == "chain-bypass")
        return execute({"effect.chain-bypass.v1",
                        {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                         {"bypass", !state_.fx.chain_bypass}}, {}, 0});
    if (state_.fx.selected_effect >= state_.fx.effects.size()) return false;
    const auto effect = state_.fx.effects[state_.fx.selected_effect];
    if (operation == "bypass")
        return execute({"effect.bypass.v1",
                        {{"id", static_cast<std::int64_t>(effect.id)},
                         {"bypass", !effect.bypass}}, {}, 0});
    const auto position = static_cast<std::int64_t>(state_.fx.selected_effect);
    if (operation == "up" || operation == "down") {
        const auto to = position + (operation == "up" ? -1 : 1);
        if (to < 0 || to >= static_cast<std::int64_t>(state_.fx.effects.size())) return false;
        return execute({"effect.reorder.v1",
                        {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                         {"from", position}, {"to", to}}, {}, 0});
    }
    if (operation == "remove") {
        if (!execute({"effect.remove.v1",
                      {{"chain", state_.fx.owner_kind}, {"index", state_.fx.owner_index},
                       {"position", position}}, {}, 0})) return false;
        return execute({"effect.delete.v1",
                        {{"id", static_cast<std::int64_t>(effect.id)}}, {}, 0});
    }
    last_error_ = "Unsupported effect operation";
    return false;
}

bool SlintFrontendAdapter::select_effect_parameter(std::size_t index) {
    if (index >= state_.fx.parameters.size()) return false;
    state_.fx.selected_parameter = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_effect_parameter(double steps) {
    if (state_.fx.selected_effect >= state_.fx.effects.size() ||
        state_.fx.selected_parameter >= state_.fx.parameters.size() ||
        !std::isfinite(steps)) return false;
    const auto &effect = state_.fx.effects[state_.fx.selected_effect];
    const auto &parameter = state_.fx.parameters[state_.fx.selected_parameter];
    const auto current = parameter.minimum + parameter.normalized *
        (parameter.maximum - parameter.minimum);
    const auto value = std::clamp(current + steps *
        (parameter.maximum - parameter.minimum) / 100.0,
        parameter.minimum, parameter.maximum);
    return execute({"effect.parameter-set.v1",
                    {{"id", static_cast<std::int64_t>(effect.id)},
                     {"parameter", static_cast<std::int64_t>(parameter.id)},
                     {"value", value}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_modulator(std::size_t index) {
    if (index >= state_.fx.modulators.size()) return false;
    state_.fx.selected_modulator = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_fx_modulator_type(std::size_t index) {
    if (index >= state_.fx.modulator_types.size()) return false;
    state_.fx.selected_modulator_type = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::manage_fx_modulator(std::string operation) {
    if (operation == "create") {
        if (state_.fx.selected_modulator_type >=
            state_.fx.modulator_types.size())
            return false;
        auto owner = ModulatorOwnerKind::master;
        std::uint64_t owner_id{};
        if (state_.fx.owner_kind == "track") {
            owner = ModulatorOwnerKind::track;
            owner_id = state_.sound.track_id;
        } else if (state_.fx.owner_kind == "group") {
            owner = ModulatorOwnerKind::group;
            owner_id = static_cast<std::uint64_t>(state_.fx.owner_index);
        } else if (state_.fx.owner_kind == "aux") {
            owner = ModulatorOwnerKind::auxiliary;
            owner_id = static_cast<std::uint64_t>(state_.fx.owner_index);
        }
        return execute({"modulator.create.v1",
            {{"type", state_.fx.modulator_types[
                          state_.fx.selected_modulator_type].id},
             {"owner", static_cast<std::int64_t>(owner)},
             {"owner_id", static_cast<std::int64_t>(owner_id)},
             {"name", std::string("LFO")}}, {}, 0});
    }
    if (state_.fx.selected_modulator >= state_.fx.modulators.size())
        return false;
    const auto &source = state_.fx.modulators[state_.fx.selected_modulator];
    if (operation == "duplicate")
        return execute({"modulator.duplicate.v1",
            {{"id", static_cast<std::int64_t>(source.id)}}, {}, 0});
    if (operation == "toggle")
        return execute({"modulator.enable.v1",
            {{"id", static_cast<std::int64_t>(source.id)},
             {"enabled", !source.enabled}}, {}, 0});
    if (operation == "delete")
        return execute({"modulator.delete.v1",
            {{"id", static_cast<std::int64_t>(source.id)}}, {}, 0});
    return false;
}

bool SlintFrontendAdapter::select_fx_destination(std::size_t index) {
    if (index >= state_.fx.destinations.size()) return false;
    state_.fx.selected_destination = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_fx_modulator_parameter(std::size_t index) {
    if (index >= state_.fx.modulator_parameters.size()) return false;
    state_.fx.selected_modulator_parameter = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_fx_modulator_parameter(double steps) {
    if (state_.fx.selected_modulator >= state_.fx.modulators.size() ||
        state_.fx.selected_modulator_parameter >=
            state_.fx.modulator_parameters.size())
        return false;
    const auto &source = state_.fx.modulators[state_.fx.selected_modulator];
    const auto &parameter = state_.fx.modulator_parameters[
        state_.fx.selected_modulator_parameter];
    const auto increment = parameter.discrete
        ? 1.0 : std::max((parameter.maximum - parameter.minimum) / 100.0, .001);
    return execute({"modulator.parameter-set.v1",
        {{"id", static_cast<std::int64_t>(source.id)},
         {"parameter", parameter.id},
         {"value", std::clamp(parameter.current + steps * increment,
                               parameter.minimum, parameter.maximum)}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_route(std::size_t index) {
    if (index >= state_.fx.routes.size()) return false;
    state_.fx.selected_route = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_fx_route(std::string operation) {
    if (state_.fx.selected_route >= state_.fx.routes.size()) return false;
    const auto &route = state_.fx.routes[state_.fx.selected_route];
    if (operation == "remove")
        return execute({"modulation.route-delete.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)}}, {}, 0});
    if (operation == "toggle")
        return execute({"modulation.route-enable.v1",
                        {{"route_id", static_cast<std::int64_t>(route.id)},
                         {"enabled", !route.enabled}}, {}, 0});
    const auto snapshot = application_.snapshot();
    const auto found = std::find_if(snapshot.modulation.routes.begin(),
        snapshot.modulation.routes.begin() + snapshot.modulation.route_count,
        [&](const auto &value) { return value.id == route.id; });
    if (found == snapshot.modulation.routes.begin() + snapshot.modulation.route_count)
        return false;
    const auto delta = operation == "depth-down" ? -0.05 : 0.05;
    return execute({"modulation.route-depth.v1",
                    {{"route_id", static_cast<std::int64_t>(route.id)},
                     {"depth", std::clamp<double>(found->depth + delta, -1.0, 1.0)}}, {}, 0});
}

bool SlintFrontendAdapter::create_fx_route() {
    if (state_.fx.selected_modulator >= state_.fx.modulators.size() ||
        state_.fx.selected_destination >= state_.fx.destinations.size()) {
        last_error_ = "Select a modulator and compatible destination first";
        return false;
    }
    const auto &source = state_.fx.modulators[state_.fx.selected_modulator];
    const auto &destination = state_.fx.destinations[state_.fx.selected_destination];
    return execute({"modulator.route-create.v1",
                    {{"source_id", static_cast<std::int64_t>(source.id)},
                     {"destination", destination.id},
                     {"depth", 0.5}}, {}, 0});
}

bool SlintFrontendAdapter::select_fx_macro(std::size_t index) {
    if (index >= state_.fx.macros.size()) return false;
    state_.fx.selected_macro = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_fx_macro(double steps) {
    if (state_.fx.selected_macro >= state_.fx.macros.size()) return false;
    const auto &macro = state_.fx.macros[state_.fx.selected_macro];
    return execute({"macro.value-set.v1",
                    {{"id", static_cast<std::int64_t>(macro.id)},
                     {"value", std::clamp<double>(macro.value + steps * .05, 0.0, 1.0)}}, {}, 0});
}

bool SlintFrontendAdapter::manage_fx_macro(std::string operation) {
    if (operation == "create")
        return request_text_entry("macro-create", "Macro");
    if (state_.fx.selected_macro >= state_.fx.macros.size()) return false;
    const auto &macro = state_.fx.macros[state_.fx.selected_macro];
    if (operation == "rename")
        return request_text_entry("macro-rename", macro.name);
    if (operation == "delete")
        return execute({"macro.delete.v1",
            {{"id", static_cast<std::int64_t>(macro.id)}}, {}, 0});
    return false;
}

bool SlintFrontendAdapter::select_fx_macro_assignment(std::size_t index) {
    if (index >= state_.fx.macro_assignments.size()) return false;
    state_.fx.selected_macro_assignment = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::manage_fx_macro_assignment(std::string operation) {
    if (state_.fx.selected_macro >= state_.fx.macros.size()) return false;
    const auto &macro = state_.fx.macros[state_.fx.selected_macro];
    if (operation == "add") {
        if (state_.fx.selected_destination >= state_.fx.destinations.size()) {
            last_error_ = "Select a compatible parameter destination first";
            return false;
        }
        const auto &destination =
            state_.fx.destinations[state_.fx.selected_destination];
        return execute({"macro.assignment-set.v1",
            {{"macro_id", static_cast<std::int64_t>(macro.id)},
             {"destination", destination.id},
             {"minimum", destination.minimum},
             {"maximum", destination.maximum},
             {"curve", std::string("linear")},
             {"polarity", std::string("unipolar")}, {"steps", std::int64_t{2}}},
            {}, 0});
    }
    if (state_.fx.selected_macro_assignment >=
        state_.fx.macro_assignments.size())
        return false;
    const auto &assignment = state_.fx.macro_assignments[
        state_.fx.selected_macro_assignment];
    if (operation == "remove")
        return execute({"macro.assignment-delete.v1",
            {{"macro_id", static_cast<std::int64_t>(macro.id)},
             {"assignment_id", static_cast<std::int64_t>(assignment.id)}}, {}, 0});
    if (operation == "invert")
        return execute({"macro.assignment-update.v1",
            {{"macro_id", static_cast<std::int64_t>(macro.id)},
             {"assignment_id", static_cast<std::int64_t>(assignment.id)},
             {"destination", assignment.destination},
             {"minimum", static_cast<double>(assignment.maximum)},
             {"maximum", static_cast<double>(assignment.minimum)},
             {"curve", std::string("linear")},
             {"polarity", std::string("unipolar")}, {"steps", std::int64_t{2}}},
            {}, 0});
    return false;
}

bool SlintFrontendAdapter::select_fx_scene(std::size_t index) {
    if (index >= state_.fx.scenes.size()) return false;
    state_.fx.selected_scene = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::manage_fx_scene(std::string operation) {
    if (operation == "capture")
        return request_text_entry("macro-scene-capture", "Scene");
    if (state_.fx.selected_scene >= state_.fx.scenes.size()) return false;
    const auto &scene = state_.fx.scenes[state_.fx.selected_scene];
    if (operation == "recall")
        return execute({"macro.scene-recall.v1",
            {{"id", static_cast<std::int64_t>(scene.id)}}, {}, 0});
    if (operation == "update")
        return execute({"macro.scene-update.v1",
            {{"id", static_cast<std::int64_t>(scene.id)}, {"name", scene.name}},
            {}, 0});
    if (operation == "delete")
        return execute({"macro.scene-delete.v1",
            {{"id", static_cast<std::int64_t>(scene.id)}}, {}, 0});
    return false;
}

bool SlintFrontendAdapter::select_setup_subview(std::string subview) {
    static constexpr std::array<std::string_view, 6> valid{
        "audio", "midi", "controllers", "preferences", "diagnostics", "system"};
    if (std::find(valid.begin(), valid.end(), subview) == valid.end()) {
        last_error_ = "Unknown Setup section";
        return false;
    }
    state_.setup.subview = std::move(subview);
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_setup_device(std::string kind,
                                                std::size_t index) {
    const std::vector<SlintSetupDeviceItem> *items{};
    std::string *target{};
    if (kind == "audio-input") {
        items = &state_.setup.audio_inputs;
        target = &staged_preferences_.audio_input;
    } else if (kind == "audio-output") {
        items = &state_.setup.audio_outputs;
        target = &staged_preferences_.audio_output;
    } else if (kind == "midi-input") {
        items = &state_.setup.midi_inputs;
    } else if (kind == "midi-output") {
        items = &state_.setup.midi_outputs;
    } else {
        last_error_ = "Unknown device kind";
        return false;
    }
    if (index >= items->size() || !(*items)[index].available) {
        last_error_ = "Selected device is unavailable";
        return false;
    }
    if (target)
        *target = (*items)[index].id;
    else {
        auto &ports = kind == "midi-input" ? staged_preferences_.midi_inputs
                                           : staged_preferences_.midi_outputs;
        ports = {(*items)[index].id};
    }
    last_error_.clear();
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::adjust_setup_value(std::string property,
                                               int direction) {
    if (direction == 0) return true;
    if (property == "buffer") {
        static constexpr std::array<std::uint32_t, 4> values{64, 128, 256, 512};
        auto found = std::lower_bound(values.begin(), values.end(),
                                      staged_preferences_.audio_buffer_frames);
        std::ptrdiff_t index = found == values.end() ? 3 : found - values.begin();
        index = std::clamp<std::ptrdiff_t>(index + (direction > 0 ? 1 : -1), 0, 3);
        staged_preferences_.audio_buffer_frames = values[index];
    } else if (property == "input-channels" || property == "output-channels") {
        auto &value = property == "input-channels"
                          ? staged_preferences_.audio_input_channels
                          : staged_preferences_.audio_output_channels;
        value = static_cast<std::uint16_t>(std::clamp<int>(
            static_cast<int>(value) + (direction > 0 ? 1 : -1), 1, 6));
    } else if (property == "ui-scale") {
        staged_preferences_.ui_scale = std::clamp(
            staged_preferences_.ui_scale.value_or(1.0F) +
                (direction > 0 ? 0.1F : -0.1F),
            1.0F, 2.5F);
    } else if (property == "midi-clock") {
        return execute({"midi.clock-source-set.v1",
                        {{"source", state_.setup.midi_clock == "Internal"
                                        ? std::string("external")
                                        : std::string("internal")}}, {}, 0});
    } else {
        last_error_ = "Unsupported Setup value";
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::toggle_setup_preference(std::string property) {
    if (property == "fullscreen")
        staged_preferences_.fullscreen = !staged_preferences_.fullscreen;
    else if (property == "reopen-last-project")
        staged_preferences_.reopen_last_project =
            !staged_preferences_.reopen_last_project;
    else {
        last_error_ = "Unsupported preference";
        return false;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::setup_preferences_action(std::string action) {
    if (!setup_services_.snapshot) {
        last_error_ = "Runtime configuration is unavailable";
        return false;
    }
    if (action == "revert") {
        staged_preferences_ = setup_services_.snapshot().requested;
        last_error_.clear();
    } else if (action == "save") {
        if (!setup_services_.save) {
            last_error_ = "Preference saving is unavailable";
            return false;
        }
        std::string error;
        if (!setup_services_.save(staged_preferences_, error)) {
            last_error_ = std::move(error);
            return false;
        }
        last_error_.clear();
    } else {
        last_error_ = "Unknown preference operation";
        return false;
    }
    ++local_revision_;
    (void)refresh();
    state_.shell.notification = last_error_.empty()
        ? (action == "save" ? "Preferences saved" : "Pending changes reverted")
        : last_error_;
    return last_error_.empty();
}

bool SlintFrontendAdapter::select_setup_controller(std::size_t index) {
    if (index >= state_.setup.controllers.size()) return false;
    state_.setup.selected_controller = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::select_setup_midi_route(std::size_t index) {
    if (index >= state_.setup.midi_routes.size()) return false;
    state_.setup.selected_midi_route = index;
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::edit_setup_midi_route(std::string operation) {
    if (operation == "add") {
        std::uint32_t id{1};
        for (const auto &route : state_.setup.midi_routes)
            id = std::max(id, route.id + 1);
        return execute({"midi.route-set.v1",
            {{"id", static_cast<std::int64_t>(id)},
             {"input_port", std::int64_t{1}}, {"output_port", std::int64_t{2}},
             {"input_channel", std::int64_t{-1}}, {"output_channel", std::int64_t{-1}},
             {"notes", true}, {"cc", true}, {"clock", false},
             {"transport", false}, {"enabled", true}}, {}, 0});
    }
    if (state_.setup.selected_midi_route >= state_.setup.midi_routes.size()) {
        last_error_ = "Select a MIDI route first";
        return false;
    }
    const auto &route = state_.setup.midi_routes[state_.setup.selected_midi_route];
    if (operation == "delete")
        return execute({"midi.route-delete.v1",
                        {{"id", static_cast<std::int64_t>(route.id)}}, {}, 0});
    auto notes = route.notes;
    auto cc = route.control_change;
    auto clock = route.clock;
    auto transport = route.transport;
    auto enabled = route.enabled;
    if (operation == "toggle-notes") notes = !notes;
    else if (operation == "toggle-cc") cc = !cc;
    else if (operation == "toggle-clock") clock = !clock;
    else if (operation == "toggle-transport") transport = !transport;
    else if (operation == "toggle-enabled") enabled = !enabled;
    else { last_error_ = "Unknown MIDI route operation"; return false; }
    return execute({"midi.route-set.v1",
        {{"id", static_cast<std::int64_t>(route.id)},
         {"input_port", static_cast<std::int64_t>(route.input_port)},
         {"output_port", static_cast<std::int64_t>(route.output_port)},
         {"input_channel", static_cast<std::int64_t>(route.input_channel)},
         {"output_channel", static_cast<std::int64_t>(route.output_channel)},
         {"notes", notes}, {"cc", cc}, {"clock", clock},
         {"transport", transport}, {"enabled", enabled}}, {}, 0});
}

bool SlintFrontendAdapter::setup_midi_learn(std::string operation,
                                             std::string control_id,
                                             std::string mode) {
    if (state_.setup.selected_controller >= state_.setup.controllers.size()) {
        last_error_ = "Select a controller first";
        return false;
    }
    const auto &id = state_.setup.controllers[state_.setup.selected_controller].id;
    std::string error;
    bool result{};
    if (operation == "cancel" && setup_services_.cancel_midi_learn)
        result = setup_services_.cancel_midi_learn(id, error);
    else if (operation == "confirm" && setup_services_.confirm_midi_learn)
        result = setup_services_.confirm_midi_learn(id, error);
    else if (operation == "start" && setup_services_.begin_midi_learn)
        result = setup_services_.begin_midi_learn(id, control_id, mode, error);
    else
        error = "MIDI learn operation is unavailable";
    last_error_ = result ? std::string{} : std::move(error);
    state_.setup.controller_learn_status = result
        ? (operation == "start" ? "Waiting for MIDI input" :
           operation == "confirm" ? "Mapping committed" : "Cancelled")
        : "Unavailable";
    ++local_revision_;
    (void)refresh();
    return result;
}
bool SlintFrontendAdapter::select_setup_mapping(std::size_t index) {
    if (index >= state_.setup.mappings.size()) return false;
    state_.setup.selected_mapping = index;
    ++local_revision_;
    return refresh();
}
bool SlintFrontendAdapter::remove_setup_mapping() {
    if (state_.setup.selected_mapping >= state_.setup.mappings.size() ||
        !setup_services_.remove_midi_binding) {
        last_error_ = "Select a removable MIDI mapping first";
        return false;
    }
    const auto mapping = state_.setup.mappings[state_.setup.selected_mapping];
    std::string error;
    const auto result = setup_services_.remove_midi_binding(
        mapping.endpoint_id, mapping.binding_index, error);
    last_error_ = result ? std::string{} : std::move(error);
    ++local_revision_;
    (void)refresh();
    return result;
}

bool SlintFrontendAdapter::author_setup_midi_mapping(
    std::uint32_t port, std::uint8_t channel, std::uint8_t number,
    std::string mode, std::string control_id, bool feedback,
    bool replace_conflict) {
    if (state_.setup.selected_controller >= state_.setup.controllers.size() ||
        !setup_services_.set_midi_binding) {
        last_error_ = "Manual MIDI mapping authoring is unavailable";
        return false;
    }
    MidiControlMode parsed;
    if (mode == "note" || mode == "note-momentary")
        parsed = MidiControlMode::note_momentary;
    else if (mode == "cc-absolute" || mode == "absolute")
        parsed = MidiControlMode::cc_absolute;
    else if (mode == "cc-relative-2c" || mode == "relative-2c")
        parsed = MidiControlMode::cc_relative_twos_complement;
    else if (mode == "cc-relative-offset" || mode == "relative-offset")
        parsed = MidiControlMode::cc_relative_binary_offset;
    else {
        last_error_ = "Unsupported MIDI mapping mode";
        return false;
    }
    if (!port || port > maximum_midi_ports || channel > 15 || number > 127 ||
        control_id.empty()) {
        last_error_ = "MIDI port, channel, number or logical control is invalid";
        return false;
    }
    const auto &endpoint =
        state_.setup.controllers[state_.setup.selected_controller].id;
    std::optional<std::size_t> conflict;
    for (const auto &mapping : state_.setup.mappings)
        if (mapping.endpoint_id == endpoint && mapping.port == port &&
            mapping.channel == channel && mapping.number == number &&
            mapping.midi_mode == parsed) {
            conflict = mapping.binding_index;
            break;
        }
    if (conflict && !replace_conflict) {
        last_error_ = "Mapping conflict requires explicit replacement confirmation";
        const auto existing = std::find_if(state_.setup.mappings.begin(),
            state_.setup.mappings.end(), [&](const auto &value) {
                return value.endpoint_id == endpoint &&
                       value.binding_index == *conflict;
            });
        state_.setup.conflict_visible = true;
        state_.setup.conflict_existing_source = existing == state_.setup.mappings.end()
            ? "Unavailable" : existing->source;
        state_.setup.conflict_existing_target = existing == state_.setup.mappings.end()
            ? "Unavailable" : existing->target_label;
        state_.setup.conflict_proposed_source = "Port " + std::to_string(port) +
            " / channel " + std::to_string(channel + 1) + " / " +
            std::to_string(number) + " / " + mode;
        state_.setup.conflict_proposed_target = control_id;
        state_.setup.conflict_reason = "The physical MIDI source and scope overlap";
        pending_mapping_conflict_ = PendingMappingConflict{
            port, channel, number, mode, control_id, feedback};
        ++local_revision_;
        (void)refresh();
        return false;
    }
    const auto index = conflict ? *conflict
        : static_cast<std::size_t>(std::count_if(
              state_.setup.mappings.begin(), state_.setup.mappings.end(),
              [&](const auto &mapping) { return mapping.endpoint_id == endpoint; }));
    std::string error;
    const auto accepted = setup_services_.set_midi_binding(
        endpoint, index,
        MidiControlBinding{port, channel, number, parsed,
                           std::move(control_id), feedback}, error);
    last_error_ = accepted ? std::string{} : std::move(error);
    if (accepted) cancel_setup_mapping_conflict();
    ++local_revision_;
    (void)refresh();
    return accepted;
}

bool SlintFrontendAdapter::confirm_setup_mapping_conflict() {
    if (!pending_mapping_conflict_) {
        last_error_ = "No mapping conflict is awaiting replacement";
        return false;
    }
    const auto proposal = *pending_mapping_conflict_;
    return author_setup_midi_mapping(proposal.port, proposal.channel,
        proposal.number, proposal.mode, proposal.control_id,
        proposal.feedback, true);
}

void SlintFrontendAdapter::cancel_setup_mapping_conflict() noexcept {
    pending_mapping_conflict_.reset();
    state_.setup.conflict_visible = false;
    state_.setup.conflict_existing_source.clear();
    state_.setup.conflict_existing_target.clear();
    state_.setup.conflict_proposed_source.clear();
    state_.setup.conflict_proposed_target.clear();
    state_.setup.conflict_reason.clear();
    ++local_revision_;
}

bool SlintFrontendAdapter::edit_setup_mapping(std::string target_kind,
                                              std::string target_id,
                                              bool pickup, bool invert,
                                              double fine_scale,
                                              double acceleration) {
    if (state_.setup.selected_mapping >= state_.setup.mappings.size() ||
        !setup_services_.set_controller_binding) {
        last_error_ = "Select an editable MIDI mapping first";
        return false;
    }
    const auto mapping = state_.setup.mappings[state_.setup.selected_mapping];
    ControlBinding binding;
    binding.control_id = mapping.control_id;
    binding.pickup = pickup;
    binding.invert = invert;
    binding.fine_scale = fine_scale;
    binding.acceleration = acceleration;
    if (target_id == mapping.target_id)
        binding.arguments = mapping.arguments;
    if (target_kind == "action")
        binding.action_id = std::move(target_id);
    else if (target_kind == "parameter")
        binding.parameter_id = std::move(target_id);
    else if (target_kind == "contextual-role" ||
             target_kind == "performance-role") {
        const auto separator = target_id.rfind('.');
        if (separator == std::string::npos) {
            if (target_id == "focus.data") binding.semantic = "focus-data";
            else { last_error_ = "Contextual role has no logical position"; return false; }
        } else {
            const auto prefix = target_id.substr(0, separator);
            const auto index_text = target_id.substr(separator + 1);
            std::uint32_t index{};
            const auto parsed = std::from_chars(index_text.data(),
                index_text.data() + index_text.size(), index);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != index_text.data() + index_text.size()) {
                last_error_ = "Contextual role position is invalid";
                return false;
            }
            binding.index = static_cast<std::int32_t>(index);
            if (prefix == "context.encoder" || prefix == "performance.parameter")
                binding.semantic = "context-encoder";
            else if (prefix == "context.function" || prefix == "performance.action")
                binding.semantic = "context-button";
            else if (prefix == "context.pad" || prefix == "performance.pad")
                binding.semantic = "performance-pad";
            else { last_error_ = "Contextual role is unsupported"; return false; }
            if (target_kind == "performance-role") binding.context = "performance";
        }
    }
    else {
        last_error_ = "Mapping target kind is unsupported";
        return false;
    }
    std::string error;
    const auto result = setup_services_.set_controller_binding(
        mapping.endpoint_id, mapping.controller_binding_index,
        std::move(binding), error);
    last_error_ = result ? std::string{} : std::move(error);
    ++local_revision_;
    (void)refresh();
    return result;
}

bool SlintFrontendAdapter::select_setup_mapping_target(std::size_t index) {
    if (index >= state_.setup.mapping_targets.size()) return false;
    state_.setup.selected_mapping_target = index;
    state_.setup.mapping_argument_names.fill({});
    state_.setup.mapping_argument_values.fill({});
    state_.setup.mapping_argument_hints.fill({});
    const auto &arguments = state_.setup.mapping_targets[index].arguments;
    for (std::size_t field = 0;
         field < std::min(arguments.size(),
                          state_.setup.mapping_argument_names.size()); ++field) {
        state_.setup.mapping_argument_names[field] = arguments[field].name;
        state_.setup.mapping_argument_hints[field] = arguments[field].label +
            (arguments[field].required ? " · required" : " · optional");
        for (const auto &choice : arguments[field].choices)
            state_.setup.mapping_argument_hints[field] += " · " + choice.label;
    }
    ++local_revision_;
    return refresh();
}

bool SlintFrontendAdapter::apply_setup_mapping_target(
    std::array<std::string, 4> argument_values) {
    if (state_.setup.selected_mapping >= state_.setup.mappings.size() ||
        state_.setup.selected_mapping_target >=
            state_.setup.mapping_targets.size()) {
        last_error_ = "Select a mapping and compatible target first";
        return false;
    }
    const auto &mapping = state_.setup.mappings[state_.setup.selected_mapping];
    const auto &target =
        state_.setup.mapping_targets[state_.setup.selected_mapping_target];
    if (target.kind == "action") {
        AssignmentProposal proposal{AssignmentTargetKind::action, target.id, {}};
        for (std::size_t field = 0; field < target.arguments.size(); ++field) {
            if (field >= argument_values.size()) {
                if (target.arguments[field].required) {
                    last_error_ = "The action has more arguments than this profile format supports";
                    return false;
                }
                break;
            }
            ApplicationValue parsed;
            if (!AssignmentTargetPicker::parse_argument(
                    target.arguments[field], argument_values[field], parsed,
                    last_error_)) return false;
            if (!std::holds_alternative<std::monostate>(parsed))
                proposal.arguments.emplace(target.arguments[field].name,
                                           std::move(parsed));
        }
        AssignmentTargetPicker picker(application_);
        if (!picker.validate(proposal, last_error_)) return false;
        ControlBinding binding;
        binding.control_id = mapping.control_id;
        binding.action_id = target.id;
        binding.arguments = std::move(proposal.arguments);
        binding.pickup = mapping.pickup;
        binding.invert = mapping.invert;
        binding.fine_scale = mapping.fine_scale;
        binding.acceleration = mapping.acceleration;
        std::string error;
        const auto result = setup_services_.set_controller_binding &&
            setup_services_.set_controller_binding(mapping.endpoint_id,
                mapping.controller_binding_index, std::move(binding), error);
        last_error_ = result ? std::string{} : std::move(error);
        ++local_revision_;
        (void)refresh();
        return result;
    }
    return edit_setup_mapping(target.kind, target.id, mapping.pickup,
        mapping.invert, mapping.fine_scale, mapping.acceleration);
}

bool SlintFrontendAdapter::save_setup_controller_profile(std::string path) {
    if (state_.setup.selected_controller >= state_.setup.controllers.size() ||
        !setup_services_.save_controller_profile) {
        last_error_ = "Controller profile saving is unavailable";
        return false;
    }
    std::string error;
    const auto result = setup_services_.save_controller_profile(
        state_.setup.controllers[state_.setup.selected_controller].id, path,
        error);
    last_error_ = result ? std::string{} : std::move(error);
    ++local_revision_;
    (void)refresh();
    return result;
}

bool SlintFrontendAdapter::request_application_exit() {
    if (!setup_services_.request_exit) {
        last_error_ = "Application exit is unavailable";
        return false;
    }
    const auto accepted = setup_services_.request_exit();
    last_error_ = accepted ? std::string{} : "Application exit was cancelled";
    return accepted;
}

void SlintFrontendAdapter::dismiss_notification() noexcept {
    last_error_.clear();
    state_.shell.notification.clear();
    ++local_revision_;
}

bool SlintFrontendAdapter::keyboard_action(std::string_view key, bool pressed) {
    if (key == "space" && pressed)
        return invoke_action(state_.shell.playing ? "transport.stop.v1"
                                                 : "transport.play.v1");
    if (key == "shift")
        return execute(
            {"modifier.shift.v1", {{"active", pressed}}, {}, 0});
    if (pressed && key.size() == 1 && key[0] >= '1' && key[0] <= '8')
        return press_pad(static_cast<std::size_t>(key[0] - '1'));
    if (pressed && key.size() == 2 && key[0] == 'f' && key[1] >= '1' &&
        key[1] <= '8')
        return select_page(static_cast<std::size_t>(key[1] - '1'));
    last_error_ = "Keyboard action is unavailable";
    return false;
}

} // namespace neurogenic
