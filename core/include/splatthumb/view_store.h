#pragma once

#include <filesystem>
#include <optional>

#include "splatthumb/fingerprint.h"
#include "splatthumb/view.h"

namespace splatthumb {

// Saved views live in one folder per user (on Windows %LOCALAPPDATA%\SplatThumb\views):
//   <root>/<first two hex digits>/<fingerprint hex>.view
// Looking one up is a single file open, however many views are saved.
std::filesystem::path view_file_path(const std::filesystem::path& root, const Fingerprint& fp);

// Reads and parses the view for fp. nullopt when there is none, it cannot be read, or it is
// invalid (see parse_view); callers then use the automatic view.
std::optional<View> load_view(const std::filesystem::path& root, const Fingerprint& fp) noexcept;

// Writes the view for fp, creating folders as needed. Writes a temporary file first and
// renames it, so a reader never sees half a file.
bool save_view(const std::filesystem::path& root, const Fingerprint& fp, const View& view) noexcept;

// Removes the view for fp. Returns true when there is none afterwards.
bool remove_view(const std::filesystem::path& root, const Fingerprint& fp) noexcept;

} // namespace splatthumb
