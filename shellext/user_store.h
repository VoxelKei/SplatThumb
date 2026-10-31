#pragma once

#include <filesystem>
#include <optional>

#include "splatthumb/settings.h"

namespace splatthumb::shellext {

// Per-user places SplatThumb reads (and the app writes). Nothing outside these is used,
// and uninstalling removes both.
inline constexpr wchar_t kSettingsKey[] = L"Software\\SplatThumb\\Settings";
inline constexpr wchar_t kEnabledValue[] = L"Enabled";
inline constexpr wchar_t kQualityValue[] = L"Quality";

// %LOCALAPPDATA%\SplatThumb\views, or nullopt if the folder location is unavailable.
std::optional<std::filesystem::path> views_root() noexcept;

// Settings from HKCU; missing or invalid values are the defaults.
Settings read_settings() noexcept;

// Writes all settings to HKCU, creating the key if needed. Returns false if any write failed.
bool write_settings(const Settings& settings) noexcept;

} // namespace splatthumb::shellext
