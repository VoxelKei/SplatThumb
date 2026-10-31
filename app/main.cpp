// SplatThumb.exe
//   SplatThumb.exe                     the settings window (from the Start menu)
//   SplatThumb.exe --view <file.ply>   the viewpoint window (from the right-click menu)
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <string_view>

#include "settings_window.h"
#include "strings.h"
#include "viewer_window.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    using namespace splatthumb::app;
    // COM for the thumbnail cache and shell items; single-threaded for the UI.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int code = 0;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr && argc == 3 && std::wstring_view(argv[1]) == L"--view") {
        try {
            // Windows' shell calls below need a full path.
            code = run_viewer(std::filesystem::absolute(std::filesystem::path(argv[2])), show);
        } catch (...) {
            code = 1;
        }
    } else if (argv != nullptr && argc == 1) {
        code = run_settings(show);
    } else {
        MessageBoxW(nullptr, text(Text::Usage), text(Text::AppName), MB_OK | MB_ICONINFORMATION);
    }
    if (argv != nullptr) {
        LocalFree(argv);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return code;
}
