// Entry points Windows Installer calls (see installer/Package.wxs). They run as the
// installing user, so HKEY_CURRENT_USER is that user's hive.

#include "ply_registration.h"

#include <msi.h>
#include <msiquery.h>
#include <shlobj.h>

#include <cwchar>

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

} // namespace

extern "C" UINT __stdcall RegisterPly(MSIHANDLE install) noexcept {
    const LONG status = register_ply(HKEY_CURRENT_USER);
    log_status(install, L"register .ply thumbnail handler", status);
    notify_shell();
    return status == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

extern "C" UINT __stdcall RestorePly(MSIHANDLE install) noexcept {
    const LONG status = restore_ply(HKEY_CURRENT_USER);
    log_status(install, L"restore previous .ply thumbnail handler", status);
    notify_shell();
    return status == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

// Fails on purpose, so CI can check that a failed install or uninstall rolls back. Runs
// only when the SPLATTHUMB_FAIL_FOR_TEST=1 property is given on the msiexec command line.
extern "C" UINT __stdcall FailForTest(MSIHANDLE install) noexcept {
    log_status(install, L"failing on purpose (SPLATTHUMB_FAIL_FOR_TEST)", ERROR_INSTALL_FAILURE);
    return ERROR_INSTALL_FAILURE;
}
