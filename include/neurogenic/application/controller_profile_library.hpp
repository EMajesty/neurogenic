#pragma once

#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller.hpp"
#include "neurogenic/midi/mapping.hpp"
#include <filesystem>
#include <map>

namespace neurogenic {

inline constexpr std::size_t maximum_controller_profiles = 64;

struct ControllerProfileLibraryEntry {
    std::string id, name, descriptor_id, error;
    MidiControllerProfile profile;
    bool built_in{}, valid{}, available{true};
};

struct ControllerProfileAssociation {
    std::string endpoint_id, profile_id;
};

// Appliance-owned definitions and endpoint associations. It owns no active
// transport and no project state; ControllerCoordinator performs activation.
class ControllerProfileLibrary {
  public:
    explicit ControllerProfileLibrary(ApplicationService &application)
        : application_(&application) {}

    bool register_descriptor(ControllerDeviceDescriptor, std::string &error);
    bool create(std::string id, std::string name, std::string descriptor_id,
                MidiControllerProfile, bool built_in, std::string &error);
    bool rename(std::string_view id, std::string name, std::string &error);
    bool duplicate(std::string_view source_id, std::string new_id,
                   std::string new_name, std::string &error);
    bool replace(std::string_view id, MidiControllerProfile,
                 std::string &error);
    bool remove(std::string_view id, bool allow_active, std::string &error);
    bool associate(std::string endpoint_id, std::string profile_id,
                   std::string &error);
    void disassociate(std::string_view endpoint_id) noexcept;
    [[nodiscard]] const ControllerProfileLibraryEntry *find(
        std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::string> associated_profile(
        std::string_view endpoint_id) const;
    [[nodiscard]] std::vector<ControllerProfileLibraryEntry> entries() const;
    [[nodiscard]] std::vector<ControllerProfileAssociation> associations() const;

    bool save(const std::filesystem::path &directory, std::string &error) const;
    bool load(const std::filesystem::path &directory, std::string &error);
    bool import_profile(const std::filesystem::path &, std::string id,
                        std::string name, std::string descriptor_id,
                        std::string &error);
    bool export_profile(std::string_view id, const std::filesystem::path &,
                        std::string &error) const;
    bool restore_known_valid(std::string_view id,
                             const std::filesystem::path &directory,
                             std::string &error);

  private:
    bool validate(ControllerProfileLibraryEntry &, std::string &error) const;
    ApplicationService *application_{};
    std::map<std::string, ControllerDeviceDescriptor, std::less<>> descriptors_;
    std::map<std::string, ControllerProfileLibraryEntry, std::less<>> entries_;
    std::map<std::string, std::string, std::less<>> associations_;
};

} // namespace neurogenic
