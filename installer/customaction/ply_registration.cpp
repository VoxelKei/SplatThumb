#include "ply_registration.h"

#include <cstddef>
#include <cwchar>
#include <iterator>
#include <string>

namespace splatthumb::installer {

namespace {

constexpr const wchar_t* kPlyExisted = L"PlyExisted";
constexpr const wchar_t* kShellExExisted = L"ShellExExisted";
// The right-click item's parent keys, deepest last.
struct VerbParent {
    const wchar_t* key;
    const wchar_t* existed;
};
constexpr VerbParent kVerbParents[] = {
    {kAssociationsKey, L"AssociationsExisted"},
    {kAssociationsPlyKey, L"AssociationsPlyExisted"},
    {kAssociationsShellKey, L"AssociationsShellExisted"},
};
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

LONG set_string(HKEY root, const wchar_t* path, const wchar_t* name,
                const std::wstring& value) noexcept {
    HKEY key = nullptr;
    LONG status =
        RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status;
}

// Records which of the right-click item's parent keys exist, unless already recorded
// (a state saved by v0.1 has none of these values).
LONG record_verb_parents(HKEY root, HKEY state) noexcept {
    for (const auto& parent : kVerbParents) {
        DWORD existed = 0;
        if (get_dword(state, parent.existed, existed)) {
            continue;
        }
        if (const LONG status = set_dword(state, parent.existed, key_exists(root, parent.key) ? 1 : 0);
            status != ERROR_SUCCESS) {
            return status;
        }
    }
    return ERROR_SUCCESS;
}

// The right-click item. MUIVerb names a string in the program, so Windows shows it in the
// user's display language; the default value is the English text for anything that does
// not read MUIVerb.
LONG write_verb(HKEY root, const wchar_t* exe) noexcept {
    try {
        const std::wstring path(exe);
        LONG status = set_string(root, kVerbKey, L"MUIVerb",
                                 L"@" + path + L",-" + std::to_wstring(kVerbTextId));
        if (status == ERROR_SUCCESS) {
            status = set_string(root, kVerbKey, nullptr, L"Set SplatThumb view");
        }
        if (status == ERROR_SUCCESS) {
            status = set_string(root, kVerbCommandKey, nullptr,
                                L"\"" + path + L"\" --view \"%1\"");
        }
        return status;
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
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

// Points the handler at us and adds the right-click item. The state is saved already.
LONG apply(HKEY root, const wchar_t* exe) noexcept {
    LONG status = point_handler_at_us(root);
    if (status == ERROR_SUCCESS && exe != nullptr && *exe != L'\0') {
        status = write_verb(root, exe);
    }
    return status;
}

} // namespace

LONG register_ply(HKEY root, const wchar_t* exe) noexcept {
    HKEY state = nullptr;
    if (RegOpenKeyExW(root, kStateKey, 0, KEY_READ | KEY_SET_VALUE, &state) == ERROR_SUCCESS) {
        DWORD complete = 0;
        const bool saved = get_dword(state, kComplete, complete) && complete == 1;
        const LONG recorded = saved ? record_verb_parents(root, state) : ERROR_SUCCESS;
        RegCloseKey(state);
        if (saved) {
            return recorded == ERROR_SUCCESS ? apply(root, exe) : recorded;
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
    if (status == ERROR_SUCCESS) {
        status = record_verb_parents(root, state);
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
    return apply(root, exe);
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
    // Missing values (a state saved by v0.1) count as "existed", so nothing is removed.
    DWORD parents_existed[std::size(kVerbParents)];
    for (std::size_t i = 0; i < std::size(kVerbParents); ++i) {
        if (!get_dword(state, kVerbParents[i].existed, parents_existed[i])) {
            parents_existed[i] = 1;
        }
    }
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
    // The right-click item is SplatThumb's own key, whoever wrote it.
    if (status = delete_tree(root, kVerbKey); status != ERROR_SUCCESS) {
        return status;
    }
    for (std::size_t i = std::size(kVerbParents); i-- > 0;) {
        if (parents_existed[i] == 0) {
            if (status = delete_if_empty(root, kVerbParents[i].key); status != ERROR_SUCCESS) {
                return status;
            }
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

LONG remove_user_settings(HKEY root) noexcept {
    if (const LONG status = delete_tree(root, kUserSettingsKey); status != ERROR_SUCCESS) {
        return status;
    }
    return delete_if_empty(root, kUserKey);
}

} // namespace splatthumb::installer
