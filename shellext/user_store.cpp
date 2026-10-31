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
    stored.background = read_dword(kBackgroundValue);
    stored.quality = read_dword(kQualityValue);
    return settings_from_stored(stored);
}

} // namespace splatthumb::shellext
