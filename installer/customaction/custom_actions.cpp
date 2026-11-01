// Entry points Windows Installer calls (see installer/Package.wxs). They run as the
// installing user, so HKEY_CURRENT_USER is that user's hive.

#include "ply_registration.h"

#include <msi.h>
#include <msiquery.h>
#include <shlobj.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

using namespace splatthumb::installer;

void log_status(MSIHANDLE install, const wchar_t* text, LONG status) noexcept {
    wchar_t message[256];
    std::swprintf(message, sizeof message / sizeof message[0], L"SplatThumb: %ls (status %ld)",
                  text, status);
    if (const MSIHANDLE record = MsiCreateRecord(1); record != 0) {
        MsiRecordSetStringW(record, 0, message);
        MsiProcessMessage(install, INSTALLMESSAGE_INFO, record);
        MsiCloseHandle(record);
    }
}

// Lets Explorer pick up the changed handler. Thumbnails already in the cache stay until
// the cache is cleared; see the README.
void notify_shell() noexcept {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// A deferred custom action sees only CustomActionData, which Package.wxs sets to the full
// path of SplatThumb.exe. Empty if it cannot be read.
std::wstring exe_path(MSIHANDLE install) noexcept {
    try {
        std::wstring value(MAX_PATH, L'\0');
        DWORD size = static_cast<DWORD>(value.size());
        UINT status = MsiGetPropertyW(install, L"CustomActionData", value.data(), &size);
        if (status == ERROR_MORE_DATA) {
            value.assign(static_cast<std::size_t>(size) + 1, L'\0');
            size = static_cast<DWORD>(value.size());
            status = MsiGetPropertyW(install, L"CustomActionData", value.data(), &size);
        }
        if (status != ERROR_SUCCESS) {
            return {};
        }
        value.resize(size);
        return value;
    } catch (...) {
        return {};
    }
}

} // namespace

extern "C" UINT __stdcall RegisterPly(MSIHANDLE install) noexcept {
    const std::wstring exe = exe_path(install);
    if (exe.empty()) {
        log_status(install, L"no path for SplatThumb.exe in CustomActionData", ERROR_INVALID_DATA);
        return ERROR_INSTALL_FAILURE;
    }
    const LONG status = register_all(HKEY_CURRENT_USER, exe.c_str());
    log_status(install, L"register thumbnail handler", status);
    notify_shell();
    return status == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

extern "C" UINT __stdcall RestorePly(MSIHANDLE install) noexcept {
    const LONG status = restore_all(HKEY_CURRENT_USER);
    log_status(install, L"restore previous thumbnail handlers", status);
    notify_shell();
    return status == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

// After a completed uninstall (a commit action, so it never runs when the uninstall fails
// and is rolled back), deletes what SplatThumb.exe saved for the user: the settings in
// HKCU\Software\SplatThumb\Settings and the saved views in %LOCALAPPDATA%\SplatThumb.
// Package.wxs skips it when an upgrade removes the older version, so upgrading keeps them.
// Never fails the uninstall: anything left over is only logged.
extern "C" UINT __stdcall RemoveUserData(MSIHANDLE install) noexcept {
    log_status(install, L"remove settings", remove_user_settings(HKEY_CURRENT_USER));

    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local)) ||
        local == nullptr) {
        CoTaskMemFree(local);
        log_status(install, L"remove saved views: no local app data folder", ERROR_PATH_NOT_FOUND);
        return ERROR_SUCCESS;
    }
    LONG status = ERROR_SUCCESS;
    try {
        // Only the SplatThumb folder itself; remove_all deletes links, not what they point to.
        const std::filesystem::path folder = std::filesystem::path(local) / L"SplatThumb";
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        status = ec ? static_cast<LONG>(ec.value()) : ERROR_SUCCESS;
    } catch (...) {
        status = ERROR_NOT_ENOUGH_MEMORY;
    }
    CoTaskMemFree(local);
    log_status(install, L"remove saved views", status);
    return ERROR_SUCCESS;
}

// Fails on purpose, so CI can check that a failed install or uninstall rolls back. Runs
// only when the SPLATTHUMB_FAIL_FOR_TEST=1 property is given on the msiexec command line.
extern "C" UINT __stdcall FailForTest(MSIHANDLE install) noexcept {
    log_status(install, L"failing on purpose (SPLATTHUMB_FAIL_FOR_TEST)", ERROR_INSTALL_FAILURE);
    return ERROR_INSTALL_FAILURE;
}
