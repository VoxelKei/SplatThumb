#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace splatthumb {

// How many splats a thumbnail draws at most.
enum class Quality {
    Fast,
    Standard,
    High,
};

// The user's settings. Stored as DWORD values under HKCU\Software\SplatThumb\Settings;
// these are the defaults when a value is missing.
struct Settings {
    bool enabled = true;
    Quality quality = Quality::Standard;
};

// Raw stored values, as read; nullopt when a value is missing or unreadable.
struct StoredSettings {
    std::optional<std::uint32_t> enabled;    // 0 = off, anything else = on
    std::optional<std::uint32_t> quality;    // 0 fast, 1 standard, 2 high
};

// Out-of-range values fall back to the defaults one by one.
Settings settings_from_stored(const StoredSettings& stored) noexcept;
StoredSettings stored_from_settings(const Settings& settings) noexcept;

// limits::kFastRenderSplats, kStandardRenderSplats or kMaxRenderSplats.
std::size_t max_splats_for(Quality quality) noexcept;

} // namespace splatthumb
