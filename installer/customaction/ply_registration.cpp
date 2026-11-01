#include "ply_registration.h"

#include <cstddef>
#include <cwchar>
#include <iterator>
#include <string>

namespace splatthumb::installer {

namespace {

constexpr const wchar_t* kPlyExisted = L"PlyExisted"; // the extension's key; named for .ply
constexpr const wchar_t* kShellExExisted = L"ShellExExisted";
// The names under which the right-click item's parent keys are recorded, deepest last.
constexpr const wchar_t* kVerbParentExisted[] = {
    L"AssociationsExisted",
    L"AssociationsPlyExisted", // the extension's key under SystemFileAssociations
    L"AssociationsShellExisted",
};
// Written last: a state without it was interrupted before anything else changed.
constexpr const wchar_t* kComplete = L"Complete";
constexpr const wchar_t* kThumbnailHandlerGuid = L"{e357fccd-a995-4576-b01f-234630154e96}";
constexpr const wchar_t* kVerbName = L"SplatThumb.SetView";

// The keys of one extension, relative to root. For .ply they equal the constants in the
// header.
struct Paths {
    std::wstring ext;             // Software\Classes\<ext>
    std::wstring shellex;         // ...\ShellEx
    std::wstring handler;         // ...\ShellEx\{e357...}
    std::wstring verb_parents[3]; // SystemFileAssociations, ...\<ext>, ...\<ext>\shell
    std::wstring verb;            // ...\shell\SplatThumb.SetView
    std::wstring verb_command;
    std::wstring state;
    std::wstring saved_handler; // <state>\Handler
    bool view_item = false;
};

// Throws only std::bad_alloc.
Paths paths_for(const Extension& e) {
    Paths p;
    p.ext = std::wstring(L"Software\\Classes\\") + e.name;
    p.shellex = p.ext + L"\\ShellEx";
    p.handler = p.shellex + L"\\" + kThumbnailHandlerGuid;
    p.verb_parents[0] = kAssociationsKey;
    p.verb_parents[1] = p.verb_parents[0] + L"\\" + e.name;
    p.verb_parents[2] = p.verb_parents[1] + L"\\shell";
    p.verb = p.verb_parents[2] + L"\\" + kVerbName;
    p.verb_command = p.verb + L"\\command";
    p.state = e.state_key;
    p.saved_handler = p.state + L"\\Handler";
    p.view_item = e.view_item;
    return p;
}

bool key_exists(HKEY root, const std::wstring& path) noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
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
bool handler_is_ours(HKEY root, const Paths& p) noexcept {
    wchar_t value[64] = {};
    DWORD size = sizeof value - sizeof(wchar_t); // keep a terminator
    DWORD type = 0;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, p.handler.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LONG status =
        RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(value, kClsid) == 0;
}

LONG set_string(HKEY root, const std::wstring& path, const wchar_t* name,
                const std::wstring& value) noexcept {
    HKEY key = nullptr;
    LONG status =
        RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status;
}

LONG point_handler_at_us(HKEY root, const Paths& p) noexcept {
    try {
        return set_string(root, p.handler, nullptr, kClsid);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

// Records which of the right-click item's parent keys exist, unless already recorded
// (a state saved by v0.1 has none of these values).
LONG record_verb_parents(HKEY root, HKEY state, const Paths& p) noexcept {
    if (!p.view_item) {
        return ERROR_SUCCESS;
    }
    for (std::size_t i = 0; i < std::size(kVerbParentExisted); ++i) {
        DWORD existed = 0;
        if (get_dword(state, kVerbParentExisted[i], existed)) {
            continue;
        }
        if (const LONG status = set_dword(state, kVerbParentExisted[i],
                                          key_exists(root, p.verb_parents[i]) ? 1 : 0);
            status != ERROR_SUCCESS) {
            return status;
        }
    }
    return ERROR_SUCCESS;
}

// The right-click item. MUIVerb names a string in the program, so Windows shows it in the
// user's display language; the default value is the English text for anything that does
// not read MUIVerb. Icon is the program's own icon. Uninstall deletes the whole verb key.
LONG write_verb(HKEY root, const Paths& p, const wchar_t* exe) noexcept {
    try {
        const std::wstring path(exe);
        LONG status = set_string(root, p.verb, L"MUIVerb",
                                 L"@" + path + L",-" + std::to_wstring(kVerbTextId));
        if (status == ERROR_SUCCESS) {
            status = set_string(root, p.verb, nullptr, L"Set SplatThumb view");
        }
        if (status == ERROR_SUCCESS) {
            status = set_string(root, p.verb, L"Icon", path + L",-" + std::to_wstring(kVerbIconId));
        }
        if (status == ERROR_SUCCESS) {
            status = set_string(root, p.verb_command, nullptr, L"\"" + path + L"\" --view \"%1\"");
        }
        return status;
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

// Copies the key at `from` (values and subkeys) to a new key at `to`.
LONG copy_key(HKEY root, const std::wstring& from, const std::wstring& to) noexcept {
    HKEY source = nullptr;
    LONG status = RegOpenKeyExW(root, from.c_str(), 0, KEY_READ, &source);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    HKEY target = nullptr;
    status =
        RegCreateKeyExW(root, to.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &target, nullptr);
    if (status == ERROR_SUCCESS) {
        status = RegCopyTreeW(source, nullptr, target);
        RegCloseKey(target);
    }
    RegCloseKey(source);
    return status;
}

LONG delete_tree(HKEY root, const std::wstring& path) noexcept {
    const LONG status = RegDeleteTreeW(root, path.c_str());
    return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}

// Deletes the key if it has no values and no subkeys.
LONG delete_if_empty(HKEY root, const std::wstring& path) noexcept {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
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
    return RegDeleteKeyW(root, path.c_str());
}

// Points the handler at us and adds the right-click item. The state is saved already.
LONG apply(HKEY root, const Paths& p, const wchar_t* exe) noexcept {
    LONG status = point_handler_at_us(root, p);
    if (status == ERROR_SUCCESS && p.view_item && exe != nullptr && *exe != L'\0') {
        status = write_verb(root, p, exe);
    }
    return status;
}

LONG register_paths(HKEY root, const Paths& p, const wchar_t* exe) noexcept {
    HKEY state = nullptr;
    if (RegOpenKeyExW(root, p.state.c_str(), 0, KEY_READ | KEY_SET_VALUE, &state) ==
        ERROR_SUCCESS) {
        DWORD complete = 0;
        const bool saved = get_dword(state, kComplete, complete) && complete == 1;
        const LONG recorded = saved ? record_verb_parents(root, state, p) : ERROR_SUCCESS;
        RegCloseKey(state);
        if (saved) {
            return recorded == ERROR_SUCCESS ? apply(root, p, exe) : recorded;
        }
        // Left over from an interrupted run, before anything else was changed.
        if (const LONG status = delete_tree(root, p.state); status != ERROR_SUCCESS) {
            return status;
        }
    }

    LONG status = RegCreateKeyExW(root, p.state.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                  &state, nullptr);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    status = set_dword(state, kPlyExisted, key_exists(root, p.ext) ? 1 : 0);
    if (status == ERROR_SUCCESS) {
        status = set_dword(state, kShellExExisted, key_exists(root, p.shellex) ? 1 : 0);
    }
    if (status == ERROR_SUCCESS) {
        status = record_verb_parents(root, state, p);
    }
    if (status == ERROR_SUCCESS && key_exists(root, p.handler)) {
        status = copy_key(root, p.handler, p.saved_handler);
    }
    if (status == ERROR_SUCCESS) {
        status = set_dword(state, kComplete, 1);
    }
    RegCloseKey(state);
    if (status != ERROR_SUCCESS) {
        delete_tree(root, p.state); // nothing else has changed yet
        return status;
    }
    return apply(root, p, exe);
}

LONG restore_paths(HKEY root, const Paths& p) noexcept {
    HKEY state = nullptr;
    LONG status = RegOpenKeyExW(root, p.state.c_str(), 0, KEY_READ, &state);
    if (status == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        return status;
    }
    DWORD complete = 0;
    DWORD ext_existed = 1;
    DWORD shellex_existed = 1;
    const bool saved = get_dword(state, kComplete, complete) && complete == 1 &&
                       get_dword(state, kPlyExisted, ext_existed) &&
                       get_dword(state, kShellExExisted, shellex_existed);
    // Missing values (a state saved by v0.1) count as "existed", so nothing is removed.
    DWORD parents_existed[std::size(kVerbParentExisted)];
    for (std::size_t i = 0; i < std::size(kVerbParentExisted); ++i) {
        if (!get_dword(state, kVerbParentExisted[i], parents_existed[i])) {
            parents_existed[i] = 1;
        }
    }
    RegCloseKey(state);
    if (!saved) {
        // Interrupted before the handler was changed: only the state needs to go.
        return delete_tree(root, p.state);
    }

    if (handler_is_ours(root, p)) {
        status = delete_tree(root, p.handler);
        if (status == ERROR_SUCCESS && key_exists(root, p.saved_handler)) {
            status = copy_key(root, p.saved_handler, p.handler);
        }
        if (status != ERROR_SUCCESS) {
            return status; // keep the saved state so a retry can finish
        }
    }
    if (p.view_item) {
        // The right-click item is SplatThumb's own key, whoever wrote it.
        if (status = delete_tree(root, p.verb); status != ERROR_SUCCESS) {
            return status;
        }
        for (std::size_t i = std::size(kVerbParentExisted); i-- > 0;) {
            if (parents_existed[i] == 0) {
                if (status = delete_if_empty(root, p.verb_parents[i]); status != ERROR_SUCCESS) {
                    return status;
                }
            }
        }
    }
    if (shellex_existed == 0) {
        if (status = delete_if_empty(root, p.shellex); status != ERROR_SUCCESS) {
            return status;
        }
    }
    if (ext_existed == 0) {
        if (status = delete_if_empty(root, p.ext); status != ERROR_SUCCESS) {
            return status;
        }
    }
    return delete_tree(root, p.state);
}

} // namespace

LONG register_extension(HKEY root, const Extension& ext, const wchar_t* exe) noexcept {
    try {
        return register_paths(root, paths_for(ext), exe);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LONG restore_extension(HKEY root, const Extension& ext) noexcept {
    try {
        return restore_paths(root, paths_for(ext));
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LONG register_all(HKEY root, const wchar_t* exe) noexcept {
    for (const Extension& ext : kExtensions) {
        if (const LONG status = register_extension(root, ext, exe); status != ERROR_SUCCESS) {
            return status;
        }
    }
    return ERROR_SUCCESS;
}

LONG restore_all(HKEY root) noexcept {
    LONG first_error = ERROR_SUCCESS;
    for (std::size_t i = std::size(kExtensions); i-- > 0;) {
        const LONG status = restore_extension(root, kExtensions[i]);
        if (status != ERROR_SUCCESS && first_error == ERROR_SUCCESS) {
            first_error = status;
        }
    }
    return first_error;
}

LONG remove_user_settings(HKEY root) noexcept {
    try {
        if (const LONG status = delete_tree(root, kUserSettingsKey); status != ERROR_SUCCESS) {
            return status;
        }
        return delete_if_empty(root, kUserKey);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

} // namespace splatthumb::installer
