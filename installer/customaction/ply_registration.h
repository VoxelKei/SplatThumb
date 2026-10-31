#pragma once

// The .ply part of the registration, done by the MSI's custom actions instead of its
// Registry table: the thumbnail handler and the "set the view" right-click item. Windows
// Installer deletes every key it leaves empty, up through the parents, so it would remove a
// .ply key that existed (empty) before install. These functions record what was there and
// put exactly that back.
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
// The right-click item: HKCU\Software\Classes\SystemFileAssociations\.ply\shell\<verb>.
// SystemFileAssociations adds to whatever program opens .ply files instead of replacing it.
inline constexpr const wchar_t* kAssociationsKey = L"Software\\Classes\\SystemFileAssociations";
inline constexpr const wchar_t* kAssociationsPlyKey =
    L"Software\\Classes\\SystemFileAssociations\\.ply";
inline constexpr const wchar_t* kAssociationsShellKey =
    L"Software\\Classes\\SystemFileAssociations\\.ply\\shell";
inline constexpr const wchar_t* kVerbKey =
    L"Software\\Classes\\SystemFileAssociations\\.ply\\shell\\SplatThumb.SetView";
inline constexpr const wchar_t* kVerbCommandKey =
    L"Software\\Classes\\SystemFileAssociations\\.ply\\shell\\SplatThumb.SetView\\command";
// The menu text, from SplatThumb.exe's string table (app/SplatThumb.rc, English and Japanese).
inline constexpr int kVerbTextId = 101;

// What register_ply saved, for restore_ply. Under the key the MSI removes on uninstall.
inline constexpr const wchar_t* kStateKey = L"Software\\SplatThumb\\Previous";
inline constexpr const wchar_t* kSavedHandlerKey = L"Software\\SplatThumb\\Previous\\Handler";

// The user's settings, written by SplatThumb.exe (the same key as kSettingsKey in
// shellext/user_store.h), and their parent, which the MSI also writes to.
inline constexpr const wchar_t* kUserKey = L"Software\\SplatThumb";
inline constexpr const wchar_t* kUserSettingsKey = L"Software\\SplatThumb\\Settings";

inline constexpr const wchar_t* kClsid = L"{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}";

// Saves the current .ply registration (whether .ply, .ply\ShellEx and the right-click
// item's parent keys exist, and a full copy of the handler key if there is one), then points
// the handler at SplatThumb. If `exe` is given (the full path of SplatThumb.exe), also adds
// the right-click item that runs `"<exe>" --view "%1"`. If a saved state already exists
// (repair, or a second run), keeps it and only makes sure the handler and the item are
// set. Returns ERROR_SUCCESS or a Win32 error.
LONG register_ply(HKEY root, const wchar_t* exe = nullptr) noexcept;

// Puts back what register_ply saved and deletes the saved state. Does nothing if there is
// no saved state. Removes the right-click item. If the handler no longer points at
// SplatThumb (another application took it over), leaves the handler alone. Keys that did
// not exist before are removed only if they are empty. Returns ERROR_SUCCESS or a Win32
// error.
LONG restore_ply(HKEY root) noexcept;

// Deletes the user's settings, then Software\SplatThumb if nothing else is left in it.
// Run only after a completed uninstall, not when an upgrade removes the older version.
// Returns ERROR_SUCCESS (also when there was nothing to delete) or a Win32 error.
LONG remove_user_settings(HKEY root) noexcept;

} // namespace splatthumb::installer
