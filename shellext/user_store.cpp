#include "user_store.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace splatthumb::shellext {

namespace {

std::optional<std::uint32_t> read_dword(const wchar_t* name) noexcept {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

bool write_dword(const wchar_t* name, std::uint32_t value) noexcept {
    const DWORD v = value;
    return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v)) ==
           ERROR_SUCCESS;
}

} // namespace

std::optional<std::filesystem::path> views_root() noexcept {
    PWSTR folder = nullptr;
    // KF_FLAG_DONT_VERIFY: only the path is needed; do not create or check anything.
    const HRESULT hr =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &folder);
    std::optional<std::filesystem::path> root;
    if (SUCCEEDED(hr) && folder != nullptr) {
        try {
            root = std::filesystem::path(folder) / L"SplatThumb" / L"views";
        } catch (...) {
            root.reset();
        }
    }
    CoTaskMemFree(folder);
    return root;
}

Settings read_settings() noexcept {
    StoredSettings stored;
    stored.enabled = read_dword(kEnabledValue);
    stored.quality = read_dword(kQualityValue);
    return settings_from_stored(stored);
}

bool write_settings(const Settings& settings) noexcept {
    const StoredSettings stored = stored_from_settings(settings);
    bool ok = true;
    ok = write_dword(kEnabledValue, stored.enabled.value_or(1)) && ok;
    ok = write_dword(kQualityValue, stored.quality.value_or(1)) && ok;
    return ok;
}

std::optional<WindowSize> read_viewer_size(WindowSize min, WindowSize max) noexcept {
    const auto w = read_dword(kViewerWidthValue);
    const auto h = read_dword(kViewerHeightValue);
    if (!w || !h) {
        return std::nullopt;
    }
    // Compare as unsigned before converting, so huge stored values cannot wrap.
    if (*w < static_cast<std::uint32_t>(min.width) || *w > static_cast<std::uint32_t>(max.width) ||
        *h < static_cast<std::uint32_t>(min.height) || *h > static_cast<std::uint32_t>(max.height)) {
        return std::nullopt;
    }
    return WindowSize{static_cast<int>(*w), static_cast<int>(*h)};
}

bool write_viewer_size(WindowSize size) noexcept {
    if (size.width <= 0 || size.height <= 0) {
        return false;
    }
    bool ok = write_dword(kViewerWidthValue, static_cast<std::uint32_t>(size.width));
    ok = write_dword(kViewerHeightValue, static_cast<std::uint32_t>(size.height)) && ok;
    return ok;
}

} // namespace splatthumb::shellext
