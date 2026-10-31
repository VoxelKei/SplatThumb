#include "ply_registration.h"

#include <cwchar>

namespace splatthumb::installer {

namespace {

constexpr const wchar_t* kPlyExisted = L"PlyExisted";
constexpr const wchar_t* kShellExExisted = L"ShellExExisted";
// Written last: a state without it was interrupted before anything else changed.
constexpr const wchar_t* kComplete = L"Complete";

bool key_exists(HKEY root, const wchar_t* path) noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

LONG set_dword(HKEY key, const wchar_t* name, DWORD value) noexcept {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                          sizeof value);
}

bool get_dword(HKEY key, const wchar_t* name, DWORD& value) noexcept {
    DWORD size = sizeof value;
    DWORD type = 0;
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
               ERROR_SUCCESS &&
           type == REG_DWORD && size == sizeof value;
}

// True if the handler key's default value is SplatThumb's CLSID.
bool handler_is_ours(HKEY root) noexcept {
    wchar_t value[64] = {};
    DWORD size = sizeof value - sizeof(wchar_t); // keep a terminator
    DWORD type = 0;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, kHandlerKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LONG status =
        RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(value, kClsid) == 0;
}

LONG point_handler_at_us(HKEY root) noexcept {
    HKEY key = nullptr;
    LONG status = RegCreateKeyExW(root, kHandlerKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                  nullptr);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    const DWORD bytes = static_cast<DWORD>((std::wcslen(kClsid) + 1) * sizeof(wchar_t));
    status = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(kClsid), bytes);
    RegCloseKey(key);
    return status;
}

// Copies the key at `from` (values and subkeys) to a new key at `to`.
LONG copy_key(HKEY root, const wchar_t* from, const wchar_t* to) noexcept {
    HKEY source = nullptr;
    LONG status = RegOpenKeyExW(root, from, 0, KEY_READ, &source);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    HKEY target = nullptr;
    status = RegCreateKeyExW(root, to, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &target, nullptr);
    if (status == ERROR_SUCCESS) {
        status = RegCopyTreeW(source, nullptr, target);
        RegCloseKey(target);
    }
    RegCloseKey(source);
    return status;
}

LONG delete_tree(HKEY root, const wchar_t* path) noexcept {
    const LONG status = RegDeleteTreeW(root, path);
    return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}

// Deletes the key if it has no values and no subkeys.
LONG delete_if_empty(HKEY root, const wchar_t* path) noexcept {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    if (status == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        return status;
    }
    DWORD subkeys = 0;
    DWORD values = 0;
    status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                              nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    if (subkeys != 0 || values != 0) {
        return ERROR_SUCCESS;
    }
    return RegDeleteKeyW(root, path);
}

} // namespace

LONG register_ply(HKEY root) noexcept {
    HKEY state = nullptr;
    if (RegOpenKeyExW(root, kStateKey, 0, KEY_READ, &state) == ERROR_SUCCESS) {
        DWORD complete = 0;
        const bool saved = get_dword(state, kComplete, complete) && complete == 1;
        RegCloseKey(state);
        if (saved) {
            return point_handler_at_us(root);
        }
        // Left over from an interrupted run, before anything else was changed.
        if (const LONG status = delete_tree(root, kStateKey); status != ERROR_SUCCESS) {
            return status;
        }
    }

    LONG status = RegCreateKeyExW(root, kStateKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &state,
                                  nullptr);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    status = set_dword(state, kPlyExisted, key_exists(root, kPlyKey) ? 1 : 0);
    if (status == ERROR_SUCCESS) {
        status = set_dword(state, kShellExExisted, key_exists(root, kShellExKey) ? 1 : 0);
    }
    if (status == ERROR_SUCCESS && key_exists(root, kHandlerKey)) {
        status = copy_key(root, kHandlerKey, kSavedHandlerKey);
    }
    if (status == ERROR_SUCCESS) {
        status = set_dword(state, kComplete, 1);
    }
    RegCloseKey(state);
    if (status != ERROR_SUCCESS) {
        delete_tree(root, kStateKey); // nothing else has changed yet
        return status;
    }
    return point_handler_at_us(root);
}

LONG restore_ply(HKEY root) noexcept {
    HKEY state = nullptr;
    LONG status = RegOpenKeyExW(root, kStateKey, 0, KEY_READ, &state);
    if (status == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        return status;
    }
    DWORD complete = 0;
    DWORD ply_existed = 1;
    DWORD shellex_existed = 1;
    const bool saved = get_dword(state, kComplete, complete) && complete == 1 &&
                       get_dword(state, kPlyExisted, ply_existed) &&
                       get_dword(state, kShellExExisted, shellex_existed);
    RegCloseKey(state);
    if (!saved) {
        // Interrupted before the handler was changed: only the state needs to go.
        return delete_tree(root, kStateKey);
    }

    if (handler_is_ours(root)) {
        status = delete_tree(root, kHandlerKey);
        if (status == ERROR_SUCCESS && key_exists(root, kSavedHandlerKey)) {
            status = copy_key(root, kSavedHandlerKey, kHandlerKey);
        }
        if (status != ERROR_SUCCESS) {
            return status; // keep the saved state so a retry can finish
        }
    }
    if (shellex_existed == 0) {
        if (status = delete_if_empty(root, kShellExKey); status != ERROR_SUCCESS) {
            return status;
        }
    }
    if (ply_existed == 0) {
        if (status = delete_if_empty(root, kPlyKey); status != ERROR_SUCCESS) {
            return status;
        }
    }
    return delete_tree(root, kStateKey);
}

} // namespace splatthumb::installer
