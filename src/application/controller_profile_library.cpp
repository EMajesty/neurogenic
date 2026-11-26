#include "neurogenic/application/controller_profile_library.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace neurogenic {
namespace {
bool safe_identity(std::string_view value) {
    if (value.empty() || value.size() > 128) return false;
    for (const unsigned char c : value)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
std::string escape(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ')
            out.push_back(static_cast<char>(c));
        else {
            out += '%'; out += hex[c >> 4]; out += hex[c & 15];
        }
    }
    return out;
}
std::optional<std::string> unescape(std::string_view value) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') { out += value[i]; continue; }
        if (i + 2 >= value.size()) return {};
        const auto high = nibble(value[i + 1]), low = nibble(value[i + 2]);
        if (high < 0 || low < 0) return {};
        out += static_cast<char>((high << 4) | low); i += 2;
    }
    return out;
}
std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> result;
    while (true) {
        const auto at = line.find('|');
        result.push_back(line.substr(0, at));
        if (at == std::string_view::npos) break;
        line.remove_prefix(at + 1);
    }
    return result;
}
bool atomic_text(const std::filesystem::path &path, std::string_view text,
                 std::string &error) {
    const auto temporary = path.string() + ".tmp";
    { std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out) { error = "cannot create temporary profile-library file"; return false; }
      out << text; out.flush();
      if (!out) { error = "cannot write profile-library file"; return false; } }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) { std::filesystem::remove(temporary); error = "cannot atomically replace profile-library file: " + ec.message(); return false; }
    return true;
}
} // namespace

bool ControllerProfileLibrary::register_descriptor(ControllerDeviceDescriptor value,
                                                    std::string &error) {
    if (!validate_device_descriptor(value, error)) return false;
    descriptors_.insert_or_assign(value.descriptor_id, std::move(value));
    error.clear(); return true;
}
bool ControllerProfileLibrary::validate(ControllerProfileLibraryEntry &entry,
                                        std::string &error) const {
    const auto descriptor = descriptors_.find(entry.descriptor_id);
    if (descriptor == descriptors_.end()) {
        entry.valid = false; entry.available = false;
        entry.error = "device descriptor is unavailable"; error = entry.error;
        return false;
    }
    entry.available = true;
    entry.valid = validate_controller_configuration(descriptor->second,
        entry.profile.controller, *application_, error);
    entry.error = entry.valid ? std::string{} : error;
    return entry.valid;
}
bool ControllerProfileLibrary::create(std::string id, std::string name,
    std::string descriptor_id, MidiControllerProfile profile, bool built_in,
    std::string &error) {
    if (!safe_identity(id) || name.empty() || name.size() > 128 ||
        entries_.size() >= maximum_controller_profiles || entries_.contains(id)) {
        error = "profile identity/name is invalid, duplicate, or capacity is exhausted";
        return false;
    }
    ControllerProfileLibraryEntry entry{std::move(id), std::move(name),
        std::move(descriptor_id), {}, std::move(profile), built_in};
    if (!validate(entry, error)) return false;
    entries_.emplace(entry.id, std::move(entry)); error.clear(); return true;
}
bool ControllerProfileLibrary::rename(std::string_view id, std::string name,
                                      std::string &error) {
    auto found = entries_.find(id);
    if (found == entries_.end() || found->second.built_in || name.empty() ||
        name.size() > 128) { error = "profile cannot be renamed"; return false; }
    found->second.name = std::move(name); error.clear(); return true;
}
bool ControllerProfileLibrary::duplicate(std::string_view source_id,
    std::string new_id, std::string new_name, std::string &error) {
    const auto source = entries_.find(source_id);
    if (source == entries_.end()) { error = "source profile is unavailable"; return false; }
    auto profile = source->second.profile;
    profile.controller.profile_id = new_id;
    return create(std::move(new_id), std::move(new_name),
                  source->second.descriptor_id, std::move(profile), false, error);
}
bool ControllerProfileLibrary::replace(std::string_view id,
    MidiControllerProfile profile, std::string &error) {
    auto found = entries_.find(id);
    if (found == entries_.end() || found->second.built_in) {
        error = "profile is unavailable or read-only"; return false;
    }
    auto candidate = found->second; candidate.profile = std::move(profile);
    if (!validate(candidate, error)) return false;
    found->second = std::move(candidate); error.clear(); return true;
}
bool ControllerProfileLibrary::remove(std::string_view id, bool allow_active,
                                      std::string &error) {
    const auto found = entries_.find(id);
    if (found == entries_.end() || found->second.built_in) {
        error = "profile is unavailable or built-in"; return false;
    }
    const bool active = std::any_of(associations_.begin(), associations_.end(),
        [&](const auto &pair) { return pair.second == id; });
    if (active && !allow_active) { error = "profile is associated with an endpoint"; return false; }
    if (active) std::erase_if(associations_, [&](const auto &pair) { return pair.second == id; });
    entries_.erase(found); error.clear(); return true;
}
bool ControllerProfileLibrary::associate(std::string endpoint_id,
    std::string profile_id, std::string &error) {
    if (!safe_identity(endpoint_id) || !entries_.contains(profile_id)) {
        error = "endpoint or profile identity is unavailable"; return false;
    }
    associations_.insert_or_assign(std::move(endpoint_id), std::move(profile_id));
    error.clear(); return true;
}
void ControllerProfileLibrary::disassociate(std::string_view endpoint_id) noexcept {
    const auto found = associations_.find(endpoint_id);
    if (found != associations_.end()) associations_.erase(found);
}
const ControllerProfileLibraryEntry *ControllerProfileLibrary::find(
    std::string_view id) const noexcept {
    const auto found = entries_.find(id); return found == entries_.end() ? nullptr : &found->second;
}
std::optional<std::string> ControllerProfileLibrary::associated_profile(
    std::string_view endpoint_id) const {
    const auto found = associations_.find(endpoint_id);
    if (found == associations_.end()) return {};
    return found->second;
}
std::vector<ControllerProfileLibraryEntry> ControllerProfileLibrary::entries() const {
    std::vector<ControllerProfileLibraryEntry> result;
    for (const auto &[id, value] : entries_) result.push_back(value);
    return result;
}
std::vector<ControllerProfileAssociation> ControllerProfileLibrary::associations() const {
    std::vector<ControllerProfileAssociation> result;
    for (const auto &[endpoint, profile] : associations_) result.push_back({endpoint, profile});
    return result;
}
bool ControllerProfileLibrary::save(const std::filesystem::path &directory,
                                    std::string &error) const {
    std::error_code ec; std::filesystem::create_directories(directory, ec);
    if (ec) { error = "cannot create profile-library directory: " + ec.message(); return false; }
    std::ostringstream manifest; manifest << "controller-profile-library|1\n";
    for (const auto &[id, entry] : entries_) {
        if (!save_midi_controller_profile(directory / (id + ".ngc"), entry.profile, error)) return false;
        if (!save_midi_controller_profile(directory / (id + ".known-valid.ngc"), entry.profile, error)) return false;
        manifest << "profile|" << id << '|' << escape(entry.name) << '|'
                 << entry.descriptor_id << '|' << (entry.built_in ? 1 : 0) << '\n';
    }
    for (const auto &[endpoint, profile] : associations_)
        manifest << "association|" << endpoint << '|' << profile << '\n';
    if (!atomic_text(directory / "library.ngcl", manifest.str(), error)) return false;
    error.clear(); return true;
}
bool ControllerProfileLibrary::load(const std::filesystem::path &directory,
                                    std::string &error) {
    std::ifstream input(directory / "library.ngcl", std::ios::binary);
    if (!input) { error = "cannot open profile-library manifest"; return false; }
    std::map<std::string, ControllerProfileLibraryEntry, std::less<>> candidate;
    std::map<std::string, std::string, std::less<>> associations;
    std::string line; bool header{}; std::size_t count{};
    while (std::getline(input, line)) {
        const auto fields = split(line);
        if (!header) { header = fields.size() == 2 && fields[0] == "controller-profile-library" && fields[1] == "1"; if (!header) { error = "invalid profile-library manifest version"; return false; } continue; }
        if (fields.empty() || fields[0].empty()) continue;
        if (fields[0] == "profile") {
            if (fields.size() != 5 || !safe_identity(fields[1]) || !safe_identity(fields[3]) || ++count > maximum_controller_profiles) { error = "invalid profile-library entry"; return false; }
            auto name = unescape(fields[2]); MidiControllerProfile profile;
            if (!name) { error = "invalid escaped profile name"; return false; }
            if (!load_midi_controller_profile(
                    directory / (std::string(fields[1]) + ".ngc"), profile,
                    error)) {
                std::string recovery_error;
                if (!load_midi_controller_profile(
                        directory / (std::string(fields[1]) +
                                     ".known-valid.ngc"),
                        profile, recovery_error))
                    return false;
            }
            ControllerProfileLibraryEntry entry{std::string(fields[1]), *name, std::string(fields[3]), {}, std::move(profile), fields[4] == "1"};
            if (!validate(entry, error) || !candidate.emplace(entry.id, std::move(entry)).second) return false;
        } else if (fields[0] == "association") {
            if (fields.size() != 3 || !safe_identity(fields[1]) || !safe_identity(fields[2])) { error = "invalid profile-library association"; return false; }
            associations.emplace(fields[1], fields[2]);
        } else { error = "unknown profile-library record"; return false; }
    }
    if (!header) { error = "empty profile-library manifest"; return false; }
    for (const auto &[endpoint, profile] : associations)
        if (!candidate.contains(profile)) { error = "profile-library association references a missing profile"; return false; }
    entries_ = std::move(candidate); associations_ = std::move(associations);
    error.clear(); return true;
}
bool ControllerProfileLibrary::import_profile(const std::filesystem::path &path,
    std::string id, std::string name, std::string descriptor_id, std::string &error) {
    MidiControllerProfile profile;
    if (!load_midi_controller_profile(path, profile, error)) return false;
    profile.controller.profile_id = id;
    return create(std::move(id), std::move(name), std::move(descriptor_id), std::move(profile), false, error);
}
bool ControllerProfileLibrary::export_profile(std::string_view id,
    const std::filesystem::path &path, std::string &error) const {
    const auto entry = find(id);
    if (!entry) { error = "profile is unavailable"; return false; }
    return save_midi_controller_profile(path, entry->profile, error);
}
bool ControllerProfileLibrary::restore_known_valid(std::string_view id,
    const std::filesystem::path &directory, std::string &error) {
    auto found = entries_.find(id);
    if (found == entries_.end()) { error = "profile is unavailable"; return false; }
    MidiControllerProfile profile;
    if (!load_midi_controller_profile(directory / (std::string(id) + ".known-valid.ngc"), profile, error)) return false;
    auto candidate = found->second; candidate.profile = std::move(profile);
    if (!validate(candidate, error)) return false;
    found->second = std::move(candidate); error.clear(); return true;
}
} // namespace neurogenic
