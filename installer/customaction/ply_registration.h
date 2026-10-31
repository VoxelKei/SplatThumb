#pragma once

// The .ply part of the thumbnail handler registration, done by the MSI's custom actions
// instead of its Registry table. Windows Installer deletes every key it leaves empty, up
// through the parents, so it would remove a .ply key that existed (empty) before install.
// These functions record what was there and put exactly that back.
//
// All paths are relative to `root`, which is HKEY_CURRENT_USER in the installer and a
// scratch key in the tests. Nothing here throws.

#include <windows.h>

namespace splatthumb::installer {

// Relative to root.
inline constexpr const wchar_t* kPlyKey = L"Software\\Classes\\.ply";
inline constexpr const wchar_t* kShellExKey = L"Software\\Classes\\.ply\\ShellEx";
inline constexpr const wchar_t* kHandlerKey =
    L"Software\\Classes\\.ply\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";
// What register_ply saved, for restore_ply. Under the key the MSI removes on uninstall.
inline constexpr const wchar_t* kStateKey = L"Software\\SplatThumb\\Previous";
inline constexpr const wchar_t* kSavedHandlerKey = L"Software\\SplatThumb\\Previous\\Handler";

inline constexpr const wchar_t* kClsid = L"{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}";

// Saves the current .ply handler registration (whether .ply and .ply\ShellEx exist, and a
// full copy of the handler key if there is one), then points the handler at SplatThumb.
// If a saved state already exists (repair, or a second run), keeps it and only makes sure
// the handler points at SplatThumb. Returns ERROR_SUCCESS or a Win32 error.
LONG register_ply(HKEY root) noexcept;

// Puts back what register_ply saved and deletes the saved state. Does nothing if there is
// no saved state. If the handler no longer points at SplatThumb (another application took
// it over), leaves the handler alone. Keys that did not exist before are removed only if
// they are empty. Returns ERROR_SUCCESS or a Win32 error.
LONG restore_ply(HKEY root) noexcept;

} // namespace splatthumb::installer
