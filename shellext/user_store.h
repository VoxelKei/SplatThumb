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
// Size of the viewpoint window when it was last closed, in 96-DPI pixels.
inline constexpr wchar_t kViewerWidthValue[] = L"ViewerWidth";
inline constexpr wchar_t kViewerHeightValue[] = L"ViewerHeight";

struct WindowSize {
    int width = 0;
    int height = 0;
    friend bool operator==(const WindowSize&, const WindowSize&) = default;
};

// %LOCALAPPDATA%\SplatThumb\views, or nullopt if the folder location is unavailable.
std::optional<std::filesystem::path> views_root() noexcept;

// Settings from HKCU; missing or invalid values are the defaults.
Settings read_settings() noexcept;

// Writes all settings to HKCU, creating the key if needed. Returns false if any write failed.
bool write_settings(const Settings& settings) noexcept;

// The saved viewpoint window size, or nullopt if missing or outside min..max (both values
// must be present and in range).
std::optional<WindowSize> read_viewer_size(WindowSize min, WindowSize max) noexcept;
bool write_viewer_size(WindowSize size) noexcept;

} // namespace splatthumb::shellext
