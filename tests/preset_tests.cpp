#include "neurogenic/project/preset.hpp"
#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace neurogenic;

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void write_u16(std::ofstream &out, std::uint16_t value) {
    out.put(static_cast<char>(value));
    out.put(static_cast<char>(value >> 8));
}
void write_u32(std::ofstream &out, std::uint32_t value) {
    write_u16(out, static_cast<std::uint16_t>(value));
    write_u16(out, static_cast<std::uint16_t>(value >> 16));
}
void write_wav(const std::filesystem::path &path, float amplitude,
               std::size_t frames = 512) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(frames * 2);
    out.write("RIFF", 4); write_u32(out, 36 + bytes);
    out.write("WAVEfmt ", 8); write_u32(out, 16); write_u16(out, 1);
    write_u16(out, 1); write_u32(out, 48000); write_u32(out, 96000);
    write_u16(out, 2); write_u16(out, 16); out.write("data", 4);
    write_u32(out, bytes);
    const auto sample = static_cast<std::int16_t>(amplitude * 32767.0F);
    for (std::size_t i = 0; i < frames; ++i)
        write_u16(out, static_cast<std::uint16_t>(sample));
}
CommandResult command(ApplicationService &application, std::string id,
                      std::map<std::string, ApplicationValue> arguments = {}) {
    return application.execute(
        {std::move(id), std::move(arguments), "preset-test", 0});
}
void accepted(ApplicationService &application, std::string id,
              std::map<std::string, ApplicationValue> arguments = {}) {
    const auto action = id;
    const auto result = command(application, std::move(id), std::move(arguments));
    if (!result.accepted)
        throw std::runtime_error(action + " failed: " + result.error);
}

SoundPreset fixture(const std::filesystem::path &asset) {
    SoundPreset value;
    value.metadata.id = "complete-sound";
    value.metadata.name = "Complete Sound";
    value.metadata.author = "NEUROGENIC";
    value.metadata.description = "deterministic test preset";
    value.metadata.type = PresetType::track_sound;
    value.metadata.tags = {"test", "bass"};
    value.metadata.required_capabilities = {"native-synth", "native-fx"};
    value.metadata.processor_types = {"native.gain.v1"};
    value.metadata.modulator_types = {"native.lfo.v1"};
    value.synth_parameters = {{"synth.cutoff", 1200.0},
                              {"synth.resonance", 0.2}};
    value.track_gain = 0.75F;
    value.track_pan = -0.25F;

    SliceProject samples;
    samples.sample_paths.emplace(1, asset);
    samples.slices.push_back({7, 1, 2, 3, 10, 400, "break", 60, 0.8F});
    samples.pads[0] = 7;
    samples.regions.push_back({9,
                               SampleSlotId{1},
                               2,
                               3,
                               20,
                               300,
                               "loop",
                               RegionDirection::forward,
                               RegionFadeCurve::equal_power,
                               8,
                               12,
                               0.9F,
                               true,
                               40,
                               200,
                               6});
    samples.region_pads[1] = 9;
    samples.pad_settings[0].gain = 0.7F;
    value.samples = samples;

    PresetEffectChain effects;
    EffectInstanceConfiguration effect;
    effect.id = 11;
    effect.type = EffectProcessorType::gain;
    effect.parameter_count = 1;
    effect.parameters[0] = {1, 0.5F};
    effects.instances.push_back(effect);
    effects.chain.count = 1;
    effects.chain.instances[0] = effect.id;
    value.effects = effects;

    value.modulation.modulator_count = 1;
    value.modulation.modulators[0].id = 21;
    value.modulation.modulators[0].type = ModulatorType::lfo;
    value.modulation.modulators[0].owner_kind = ModulatorOwnerKind::track;
    value.modulation.modulators[0].owner_id = 1;
    return value;
}
} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("neurogenic-preset-tests-" + std::to_string(stamp));
    try {
        std::filesystem::create_directories(root);
        const auto asset = root / "break.wav";
        write_wav(asset, 0.4F);
        PresetLibrary library(root / "library");
        const auto source = fixture(asset);
        check(validate_preset(source).valid, "valid preset rejected");

        const auto started = std::chrono::steady_clock::now();
        library.save(source);
        const auto loaded = library.load(source.metadata.id);
        check(loaded.metadata.name == source.metadata.name &&
                  loaded.synth_parameters == source.synth_parameters,
              "metadata or synth round-trip");
        check(loaded.samples && loaded.samples->slices.size() == 1 &&
                  loaded.samples->pads[0] == 7 &&
                  loaded.samples->regions.size() == 1 &&
                  loaded.samples->region_pads[1] == 9,
              "sample instrument round-trip");
        check(loaded.effects && loaded.effects->instances.size() == 1 &&
                  loaded.effects->chain.instances[0] == 11,
              "effect chain round-trip");
        check(loaded.modulation.modulator_count == 1,
              "modulation round-trip");
        check(library.enumerate(PresetType::track_sound).size() == 1,
              "preset enumeration");

        EffectRackConfiguration base_effects;
        ModulationConfiguration base_modulation;
        PresetImportResult first, second;
        std::string error;
        check(prepare_preset_import(loaded, base_effects, base_modulation,
                                    first, error),
              error.c_str());
        check(first.effect_ids.at(11) != 11 && first.modulator_ids.at(21) != 21,
              "fresh runtime IDs");
        check(prepare_preset_import(loaded, first.effects, first.modulation,
                                    second, error),
              error.c_str());
        check(second.effect_ids.at(11) != first.effect_ids.at(11) &&
                  second.modulator_ids.at(21) != first.modulator_ids.at(21),
              "multiple preset instances collide");

        library.duplicate(source.metadata.id, "copy", "Copy");
        check(library.load("copy").metadata.name == "Copy", "duplicate");
        const auto package = root / "portable-package";
        library.export_package("copy", package);
        PresetLibrary imported(root / "imported");
        imported.import_package(package, "imported-copy");
        check(imported.load("imported-copy").effects.has_value(),
              "portable import");

        auto missing = source;
        missing.metadata.id = "missing";
        missing.samples->sample_paths[1] = root / "absent.wav";
        check(!validate_preset(missing, true).valid &&
                  validate_preset(missing, false).valid,
              "missing asset policy");
        library.save(missing);
        check(library.load("missing", false).samples.has_value(),
              "inspectable missing asset");

        auto invalid = source;
        invalid.metadata.id = "invalid";
        invalid.effects->instances[0].type =
            static_cast<EffectProcessorType>(99);
        check(!validate_preset(invalid, false).valid,
              "unsupported processor accepted");
        auto invalid_modulator = source;
        invalid_modulator.metadata.modulator_types = {"missing-modulator"};
        check(!validate_preset(invalid_modulator, false).valid,
              "unsupported modulator accepted");
        auto invalid_reference = source;
        invalid_reference.modulation.route_count = 1;
        invalid_reference.modulation.routes[0].id = 99;
        invalid_reference.modulation.routes[0].source_instance = 999;
        invalid_reference.modulation.routes[0].destination = {
            ParameterTarget::subtractive_synth,
            static_cast<std::uint32_t>(SynthParameter::cutoff), 1};
        check(!validate_preset(invalid_reference, false).valid,
              "missing modulation source accepted");
        const auto before = first;
        check(!prepare_preset_import(invalid, first.effects, first.modulation,
                                     second, error) &&
                  before.effects.instance_count == first.effects.instance_count,
              "failed import changed active configuration");
        auto failed_save = source;
        failed_save.metadata.name = "must not replace";
        failed_save.synth_parameters["cutoff"] =
            std::numeric_limits<double>::quiet_NaN();
        try {
            library.save(failed_save);
            check(false, "invalid save unexpectedly succeeded");
        } catch (const std::invalid_argument &) {
        }
        check(library.load(source.metadata.id).metadata.name == source.metadata.name,
              "failed save replaced active generation");

        auto full_effects = EffectRackConfiguration{};
        full_effects.instance_count = full_effects.instances.size();
        check(!prepare_preset_import(loaded, full_effects, base_modulation,
                                     second, error) &&
                  error.find("capacity") != std::string::npos,
              "capacity exhaustion");

        AudioEngine application_engine(48000, 32);
        SampleManager sample_manager(application_engine);
        TimelineService timeline([&](const Timeline &value, std::string &message) {
            return application_engine.queue_timeline(value, message);
        });
        ApplicationService application(application_engine, timeline,
                                       sample_manager);
        accepted(application, "track.create.v1",
                 {{"track_id", std::int64_t{1}},
                  {"name", std::string("Preset target")},
                  {"length", std::int64_t{3840}},
                  {"instrument_id", std::string("synth.subtractive.v1")},
                  {"instance_id", std::int64_t{2}}});
        accepted(application, "track.select.v1",
                 {{"track_id", std::int64_t{1}}});
        accepted(application, "instrument.instance-parameter-set.v1",
                 {{"instance_id", std::int64_t{2}},
                  {"parameter_id", std::string("synth.cutoff")},
                  {"value", 4200.0}});
        const auto authored_library = root / "authored";
        const auto capture_arguments =
            std::map<std::string, ApplicationValue>{
                {"library", authored_library.string()},
                {"preset_id", std::string("captured-synth")},
                {"name", std::string("Captured Synth")},
                {"type", std::string("synth")},
                {"track_id", std::int64_t{1}}};
        accepted(application, "preset.capture.v1", capture_arguments);
        PresetLibrary authored(authored_library);
        const auto captured = authored.load("captured-synth");
        check(captured.metadata.type == PresetType::synth_patch &&
                  captured.synth_parameters.contains("synth.cutoff") &&
                  captured.synth_parameters.size() > 1 &&
                  captured.synth_parameters.at("synth.cutoff") == 4200.0,
              "semantic synth capture omitted authoritative parameters");
        const auto duplicate_arguments =
            std::map<std::string, ApplicationValue>{
                {"library", authored_library.string()},
                {"preset_id", std::string("captured-synth")},
                {"new_id", std::string("captured-copy")},
                {"new_name", std::string("Captured Copy")}};
        accepted(application, "preset.duplicate.v1", duplicate_arguments);
        check(authored.load("captured-copy").metadata.name == "Captured Copy",
              "semantic preset duplication did not preserve content");
        auto rename_arguments = duplicate_arguments;
        rename_arguments["preset_id"] = std::string("captured-copy");
        rename_arguments["new_id"] = std::string("captured-renamed");
        rename_arguments["new_name"] = std::string("Captured Renamed");
        accepted(application, "preset.rename.v1", rename_arguments);
        check(!std::filesystem::exists(authored_library / "captured-copy") &&
                  authored.load("captured-renamed").metadata.name ==
                      "Captured Renamed",
              "semantic preset rename left the source identity active");
        const auto overwrite_rejected = command(
            application, "preset.capture.v1", capture_arguments);
        check(!overwrite_rejected.accepted &&
                  authored.load("captured-synth").metadata.name ==
                      "Captured Synth",
              "capture silently overwrote an existing preset");
        auto overwrite_arguments = capture_arguments;
        overwrite_arguments["name"] = std::string("Updated Synth");
        overwrite_arguments["overwrite"] = true;
        accepted(application, "preset.capture.v1", overwrite_arguments);
        check(authored.load("captured-synth").metadata.name == "Updated Synth",
              "explicit preset update was not committed");
        accepted(application, "preset.delete.v1",
                 {{"library", authored_library.string()},
                  {"preset_id", std::string("captured-renamed")}});
        check(!std::filesystem::exists(authored_library / "captured-renamed"),
              "semantic preset deletion did not remove the selected identity");
        const auto unsafe_duplicate = duplicate_arguments;
        auto unsafe_arguments = unsafe_duplicate;
        unsafe_arguments["new_id"] = std::string("../outside");
        check(!command(application, "preset.duplicate.v1", unsafe_arguments).accepted,
              "semantic preset mutation accepted an unsafe library identity");
        accepted(application, "effect.create.v1",
                 {{"id", std::int64_t{77}},
                  {"type", std::string("native.gain.v1")}});
        accepted(application, "effect.insert.v1",
                 {{"id", std::int64_t{77}}, {"chain", std::string("track")},
                  {"index", std::int64_t{0}}, {"position", std::int64_t{0}}});
        std::array<float, 32> capture_left{}, capture_right{};
        application_engine.process(capture_left, capture_right);
        auto fx_capture = capture_arguments;
        fx_capture["preset_id"] = std::string("captured-fx");
        fx_capture["name"] = std::string("Captured FX");
        fx_capture["type"] = std::string("fx-chain");
        accepted(application, "preset.capture.v1", fx_capture);
        const auto captured_fx = authored.load("captured-fx");
        check(captured_fx.effects && captured_fx.effects->chain.count == 1 &&
                  captured_fx.effects->instances.size() == 1 &&
                  captured_fx.effects->instances[0].type ==
                      EffectProcessorType::gain,
              "semantic FX capture did not preserve chain order and type");
        accepted(application, "effect.remove.v1",
                 {{"chain", std::string("track")}, {"index", std::int64_t{0}},
                  {"position", std::int64_t{0}}});
        application_engine.process(capture_left, capture_right);
        accepted(application, "effect.delete.v1", {{"id", std::int64_t{77}}});
        application_engine.process(capture_left, capture_right);
        const auto prepare_arguments =
            std::map<std::string, ApplicationValue>{
                {"library", (root / "library").string()},
                {"preset_id", std::string("complete-sound")},
                {"mode", std::string("replace")},
                {"track_id", std::int64_t{1}}};
        accepted(application, "preset.validate.v1", prepare_arguments);
        const auto application_prepare_begin = std::chrono::steady_clock::now();
        accepted(application, "preset.prepare.v1", prepare_arguments);
        const auto application_prepare_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - application_prepare_begin)
                .count();
        check(application.snapshot().preset_application.status == "prepared",
              "preset preparation state");
        accepted(application, "preset.apply.v1");
        check(application.snapshot().preset_application.status == "activating",
              "preset reported success before acknowledgement");
        std::array<float, 128> left{}, right{};
        const auto activation_begin = std::chrono::steady_clock::now();
        application_engine.process(left, right);
        const auto activation_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - activation_begin)
                .count();
        auto applied = application.snapshot();
        check(applied.preset_application.status == "applied" &&
                  applied.project_dirty && applied.modulation.modulator_count == 1,
              "transaction activation acknowledgement");
        check(applied.mixer.configuration.tracks[0].gain == 0.75F &&
                  sample_manager.status(SampleSlotId{1}).has_sample &&
                  applied.slice_pad_assignments[0] == 7,
              "track/sample state was not finalized");
        check(application_engine.trigger_pad(36, 1.0F),
              "activated pad did not trigger");
        application_engine.process(left, right);
        check(std::any_of(left.begin(), left.end(),
                          [](float value) { return std::abs(value) > 1e-6F; }),
              "activated sample produced silence");

        const auto first_modulator = applied.modulation.modulators[0].id;
        accepted(application, "preset.prepare.v1", prepare_arguments);
        accepted(application, "preset.apply.v1");
        application_engine.process(left, right);
        applied = application.snapshot();
        check(applied.modulation.modulator_count == 1 &&
                  applied.modulation.modulators[0].id == first_modulator,
              "replacement leaked local modulation instances");
        check(std::any_of(left.begin(), left.end(),
                          [](float value) { return std::abs(value) > 1e-6F; }),
              "old sample voice did not survive preset replacement");

        auto synth_only = source;
        synth_only.metadata.id = "synth-only";
        synth_only.metadata.type = PresetType::synth_patch;
        synth_only.samples.reset();
        synth_only.effects.reset();
        synth_only.modulation = {};
        synth_only.metadata.processor_types.clear();
        synth_only.metadata.modulator_types.clear();
        library.save(synth_only);
        auto synth_arguments = prepare_arguments;
        synth_arguments["preset_id"] = std::string("synth-only");
        accepted(application, "preset.prepare.v1", synth_arguments);
        accepted(application, "preset.apply.v1");
        application_engine.process(left, right);
        check(application.snapshot().preset_application.status == "applied",
              "synth preset application");

        auto sample_only = source;
        sample_only.metadata.id = "sample-only";
        sample_only.metadata.type = PresetType::sample_instrument;
        sample_only.synth_parameters.clear();
        sample_only.effects.reset();
        sample_only.modulation = {};
        sample_only.metadata.processor_types.clear();
        sample_only.metadata.modulator_types.clear();
        library.save(sample_only);
        auto sample_arguments = prepare_arguments;
        sample_arguments["preset_id"] = std::string("sample-only");
        accepted(application, "preset.prepare.v1", sample_arguments);
        accepted(application, "preset.apply.v1");
        application_engine.process(left, right);
        check(application.snapshot().sample_slots[0].has_sample,
              "sample instrument application");
        accepted(application, "track.create.v1",
                 {{"track_id", std::int64_t{2}},
                  {"name", std::string("Sample capture")},
                  {"length", std::int64_t{3840}},
                  {"instrument_id", std::string("sample.main")},
                  {"instance_id", std::int64_t{3}}});
        auto sample_capture = capture_arguments;
        sample_capture["preset_id"] = std::string("captured-sample");
        sample_capture["name"] = std::string("Captured Sample");
        sample_capture["type"] = std::string("sample");
        sample_capture["track_id"] = std::int64_t{2};
        accepted(application, "preset.capture.v1", sample_capture);
        const auto captured_sample = authored.load("captured-sample");
        check(captured_sample.samples.has_value(),
              "semantic sample capture omitted sample configuration");
        check(!captured_sample.samples->sample_paths.empty(),
              "semantic sample capture omitted audio-active sample paths");
        check(captured_sample.samples->pads[0] == 7,
              "semantic sample capture omitted authoritative pad assignments");

        auto fx_only = source;
        fx_only.metadata.id = "fx-only";
        fx_only.metadata.type = PresetType::fx_chain;
        fx_only.synth_parameters.clear();
        fx_only.samples.reset();
        fx_only.modulation = {};
        fx_only.metadata.modulator_types.clear();
        library.save(fx_only);
        auto fx_arguments = prepare_arguments;
        fx_arguments["preset_id"] = std::string("fx-only");
        fx_arguments["mode"] = std::string("insert-fx");
        accepted(application, "preset.prepare.v1", fx_arguments);
        accepted(application, "preset.apply.v1");
        application_engine.process(left, right);
        check(application.snapshot().mixer.configuration.effects.tracks[0]
                      .count == 2,
              "FX chain insertion");

        accepted(application, "preset.prepare.v1", prepare_arguments);
        accepted(application, "preset.cancel.v1");
        check(application.snapshot().preset_application.status == "cancelled",
              "prepared application cancellation");
        const auto revision_before_cancel =
            application.snapshot().mixer.configuration.revision;
        accepted(application, "preset.prepare.v1", prepare_arguments);
        accepted(application, "preset.apply.v1");
        accepted(application, "preset.cancel.v1");
        application_engine.process(left, right);
        const auto cancelled = application.snapshot();
        if (cancelled.preset_application.status != "cancelled" ||
            cancelled.mixer.configuration.revision != revision_before_cancel)
            throw std::runtime_error(
                "queued cancellation status=" +
                cancelled.preset_application.status + " revision=" +
                std::to_string(cancelled.mixer.configuration.revision) +
                " expected=" + std::to_string(revision_before_cancel));

        auto missing_application = source;
        missing_application.metadata.id = "relink-me";
        missing_application.samples->sample_paths[1] = root / "missing.wav";
        library.save(missing_application);
        auto missing_arguments = prepare_arguments;
        missing_arguments["preset_id"] = std::string("relink-me");
        accepted(application, "preset.prepare.v1", missing_arguments);
        check(application.snapshot().preset_application.status ==
                  "awaiting-assets",
              "missing dependency was not exposed");
        accepted(application, "preset.relink-asset.v1",
                 {{"missing", (root / "missing.wav").string()},
                  {"replacement", asset.string()}});
        accepted(application, "preset.apply.v1");
        application_engine.process(left, right);
        check(application.snapshot().preset_application.status == "applied",
              "relinked preset activation");

        const auto project_path = root / "after-preset.ngp";
        accepted(application, "project.save-as.v1",
                 {{"path", project_path.string()}});
        check(std::filesystem::exists(project_path / "manifest.ngp"),
              "project save after preset application");

        const auto duplicate_a = root / "a";
        const auto duplicate_b = root / "b";
        std::filesystem::create_directories(duplicate_a);
        std::filesystem::create_directories(duplicate_b);
        write_wav(duplicate_a / "tone.wav", 0.2F);
        write_wav(duplicate_b / "tone.wav", 0.3F);
        auto prefix_relink = source;
        prefix_relink.metadata.id = "prefix-relink";
        prefix_relink.samples->sample_paths[1] =
            root / "old-library" / "tone.wav";
        library.save(prefix_relink);
        auto prefix_arguments = prepare_arguments;
        prefix_arguments["preset_id"] = std::string("prefix-relink");
        accepted(application, "preset.prepare.v1", prefix_arguments);
        accepted(application, "preset.relink-prefix.v1",
                 {{"missing_prefix", (root / "old-library").string()},
                  {"replacement_prefix", duplicate_a.string()}});
        check(application.snapshot().preset_application.missing_assets.empty(),
              "directory-prefix relinking");
        accepted(application, "preset.cancel.v1");
        auto portable = source;
        portable.metadata.id = "portable";
        portable.samples->sample_paths = {
            {1, duplicate_a / "tone.wav"}, {2, duplicate_b / "tone.wav"}};
        library.save(portable);
        const auto self_contained = root / "self-contained";
        const auto package_begin = std::chrono::steady_clock::now();
        const auto package_result =
            library.export_self_contained("portable", self_contained);
        const auto package_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - package_begin)
                .count();
        check(package_result.copied_assets == 2 &&
                  package_result.package_bytes > 0,
              "self-contained package asset copy");
        PresetLibrary package_library(root);
        const auto packaged = package_library.load("self-contained", true);
        check(packaged.samples &&
                  packaged.samples->sample_paths.at(1) !=
                      packaged.samples->sample_paths.at(2),
              "duplicate asset filenames collided");

        auto traversal = source;
        traversal.metadata.id = "traversal";
        traversal.samples->sample_paths[1] = "../escape.wav";
        library.save(traversal);
        try {
            (void)library.load("traversal", false);
            check(false, "path traversal preset loaded");
        } catch (const std::runtime_error &) {
        }

        AudioEngine congested;
        PresetActivationRequest activation;
        std::array<std::uint64_t, maximum_preset_samples> publications{};
        std::uint64_t activation_sequence{};
        std::string activation_error;
        for (std::size_t i = 0; i < 4; ++i) {
            activation.mixer.revision = i + 1;
            check(congested.publish_preset_activation(
                      activation, activation_sequence, publications,
                      activation_error),
                  "activation queue unexpectedly exhausted");
        }
        const auto before_failure = congested.mixer_state().configuration.revision;
        check(!congested.publish_preset_activation(
                  activation, activation_sequence, publications,
                  activation_error) &&
                  congested.mixer_state().configuration.revision == before_failure,
              "publication failure changed active sound");

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        std::cout << "preset_control_us=" << elapsed
                  << " prepared_effects=" << first.effects.instance_count
                  << " prepared_modulators=" << first.modulation.modulator_count
                  << " package_bytes=" << package_result.package_bytes
                  << " application_prepare_us=" << application_prepare_us
                  << " activation_callback_us=" << activation_us
                  << " package_export_us=" << package_us
                  << '\n';
        std::filesystem::remove_all(root);
        std::cout << "neurogenic.preset-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::filesystem::remove_all(root);
        std::cerr << "preset test failure: " << exception.what() << '\n';
        return 1;
    }
}
