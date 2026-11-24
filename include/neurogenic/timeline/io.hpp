#pragma once
#include "neurogenic/timeline/model.hpp"
#include <filesystem>

namespace neurogenic {
inline constexpr unsigned timeline_file_version = 11;
Timeline load_timeline(const std::filesystem::path &);
void save_timeline(const Timeline &, const std::filesystem::path &);
} // namespace neurogenic
