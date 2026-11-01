#include "settings_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "app_icon.h"
#include "ply_search.h"
#include "splatthumb/settings.h"
#include "splatthumb/version.h"
#include "strings.h"
#include "thumbnail_refresh.h"
#include "user_store.h"

namespace splatthumb::app {

namespace {

// Messages from the remake thread.
constexpr UINT kSearchMessage = WM_APP + 1;   // wParam = .ply files found so far
constexpr UINT kProgressMessage = WM_APP + 2; // wParam = files checked, lParam = total
constexpr UINT kDoneMessage = WM_APP + 3;     // wParam = thumbnails remade, lParam = stopped

// Layout in 96-DPI units.
constexpr int kWidth = 560;
constexpr int kMargin = 14;
constexpr int kRow = 26;
constexpr int kGap = 8;
constexpr int kLabelWidth = 90;
constexpr int kComboWidth = 260;

enum Control : int {
    kEnabled = 100,
    kQuality,
    kNote,
    kSavedStatus,
    kRemake,
    kRemakeNote,
    kStop,
    kProgress,
    kClose,
    kQualityLabel,
    kAboutRule,
    kAbout,
    kRepoLink,
    kAppLink,
};

// The only pages the links may open. Clicking is the only time anything is opened; the app
// never connects to the network itself.
constexpr wchar_t kRepoUrl[] = L"https://github.com/VoxelKei/SplatThumb";
constexpr wchar_t kAppUrl[] = L"https://www.spatialograph.app/";

// Work for the remake thread. Owned by the window; the thread only reads it, except for the
// stop flag.
struct RemakeJob {
    HWND window = nullptr;
    std::filesystem::path folder;
    std::atomic<bool> stop{false};
};

// Finds the .ply files in the chosen folder and its subfolders, then remakes the thumbnails of
// those that already have one in Windows' cache. Files without one are left alone, so this
// adds nothing to the cache.
DWORD WINAPI remake_thread(void* param) noexcept {
    auto* job = static_cast<RemakeJob*>(param);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::filesystem::path> files;
    try {
        PlySearch search;
        search.roots = {job->folder};
        search.skip = system_folders();
        search.stop = &job->stop;
        search.progress = [job](std::size_t found) {
            PostMessageW(job->window, kSearchMessage, found, 0);
        };
        files = find_ply_files(search);
    } catch (...) {
        // Out of memory: nothing to remake.
    }
    std::size_t checked = 0;
    std::size_t remade = 0;
    for (const auto& file : files) {
        if (job->stop.load()) {
            break;
        }
        if (has_cached_thumbnail(file)) {
            refresh_thumbnail(file);
            ++remade;
        }
        ++checked;
        PostMessageW(job->window, kProgressMessage, checked, static_cast<LPARAM>(files.size()));
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    PostMessageW(job->window, kDoneMessage, remade, job->stop.load() ? 1 : 0);
    return 0;
}

class SettingsWindow {
public:
    int run(int show) noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) noexcept;

    HWND add(const wchar_t* cls, const wchar_t* label, DWORD style, int id) noexcept;
    void create_controls() noexcept;
    void layout() noexcept;
    void update_font() noexcept;
    int scaled(int v) const noexcept { return MulDiv(v, static_cast<int>(dpi_), 96); }

    void load_settings() noexcept;
    void save_settings() noexcept;
    void start_remake() noexcept;
    void finish_remake() noexcept;
    std::optional<std::filesystem::path> pick_folder() noexcept;

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    RemakeJob* job_ = nullptr;
    HANDLE thread_ = nullptr;
};

namespace {

// Centres the window in the work area (screen minus taskbar) of the monitor containing
// `point`, keeping it fully on screen.
void center_on_monitor(HWND hwnd, POINT point) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY), &info)) {
        return;
    }
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) {
        return;
    }
    const RECT& work = info.rcWork;
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int x = std::max(work.left, work.left + (work.right - work.left - width) / 2);
    const int y = std::max(work.top, work.top + (work.bottom - work.top - height) / 2);
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

int SettingsWindow::run(int show) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWindowClass;
    wc.hIcon = app_icon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = app_icon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    // Create the window on the monitor under the mouse pointer (so the DPI read below is
    // that monitor's), then centre it there once layout has fixed its size.
    POINT cursor{};
    const bool have_cursor = GetCursorPos(&cursor) != FALSE;
    hwnd_ = CreateWindowExW(0, kSettingsWindowClass, text(Text::SettingsTitle),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                WS_CLIPCHILDREN,
                            have_cursor ? cursor.x : CW_USEDEFAULT,
                            have_cursor ? cursor.y : CW_USEDEFAULT, kWidth, 400, nullptr,
                            nullptr, wc.hInstance, this);
    if (hwnd_ == nullptr) {
        return 1;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    create_controls();
    center_on_monitor(hwnd_, have_cursor ? cursor : POINT{0, 0});
    load_settings();
    ShowWindow(hwnd_, show);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

HWND SettingsWindow::add(const wchar_t* cls, const wchar_t* label, DWORD style, int id) noexcept {
    return CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void SettingsWindow::create_controls() noexcept {
    add(L"BUTTON", text(Text::EnableThumbnails), WS_TABSTOP | BS_AUTOCHECKBOX, kEnabled);
    add(L"STATIC", text(Text::QualityLabel), SS_LEFT | SS_CENTERIMAGE, kQualityLabel);
    const HWND q = add(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, kQuality);
    for (const Text t : {Text::QualityFast, Text::QualityStandard, Text::QualityHigh}) {
        SendMessageW(q, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text(t)));
    }
    add(L"STATIC", text(Text::SettingsNote), SS_LEFT, kNote);
    add(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX, kSavedStatus);
    add(L"BUTTON", text(Text::RemakeAll), WS_TABSTOP | BS_PUSHBUTTON, kRemake);
    const HWND stop = add(L"BUTTON", text(Text::Stop), WS_TABSTOP | BS_PUSHBUTTON, kStop);
    EnableWindow(stop, FALSE);
    add(L"STATIC", text(Text::RemakeAllNote), SS_LEFT, kRemakeNote);
    add(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX,
        kProgress);
    add(L"STATIC", L"", SS_ETCHEDHORZ, kAboutRule);
    try {
        // "SplatThumb 0.4.0 · © 2026 VoxelKei · MIT License"
        const std::string_view v = version();
        std::wstring about = L"SplatThumb ";
        about.append(v.begin(), v.end());
        about += L"  \u00B7  \u00A9 2026 VoxelKei  \u00B7  MIT License";
        add(L"STATIC", about.c_str(), SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, kAbout);
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LINK_CLASS};
        InitCommonControlsEx(&icc);
        std::wstring repo = L"GitHub: <a href=\"";
        repo += kRepoUrl;
        repo += L"\">github.com/VoxelKei/SplatThumb</a>";
        add(WC_LINK, repo.c_str(), WS_TABSTOP, kRepoLink);
        std::wstring app = L"<a href=\"";
        app += kAppUrl;
        app += L"\">Spatialograph Maker</a>";
        app += text(Text::AppLinkNote);
        add(WC_LINK, app.c_str(), WS_TABSTOP, kAppLink);
    } catch (...) {
    }
    add(L"BUTTON", text(Text::Close), WS_TABSTOP | BS_PUSHBUTTON, kClose);
    update_font();
    layout();
}

void SettingsWindow::update_font() noexcept {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                                    dpi_)) {
        return;
    }
    const HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (font == nullptr) {
        return;
    }
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    font_ = font;
}

void SettingsWindow::layout() noexcept {
    const int m = scaled(kMargin);
    const int row = scaled(kRow);
    const int gap = scaled(kGap);
    const int inner = scaled(kWidth) - 2 * m;
    const auto place = [&](int id, int x, int y, int w, int h) {
        MoveWindow(GetDlgItem(hwnd_, id), x, y, w, h, FALSE);
    };
    const auto button_width = [&](int id) {
        wchar_t buf[256];
        GetWindowTextW(GetDlgItem(hwnd_, id), buf, 256);
        const HDC dc = GetDC(hwnd_);
        const HGDIOBJ old = SelectObject(dc, font_);
        SIZE size{};
        GetTextExtentPoint32W(dc, buf, static_cast<int>(wcslen(buf)), &size);
        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
        return static_cast<int>(size.cx) + scaled(28);
    };

    // The label column fits the quality label's text, at least kLabelWidth.
    const int label = std::max(scaled(kLabelWidth), button_width(kQualityLabel) - scaled(28) + gap);

    // No rows are kept empty for messages: "Settings saved." sits at the right of the first
    // row, and the remake progress to the right of the Stop button.
    int y = m;
    const int enabled_w = std::min(inner, button_width(kEnabled));
    place(kEnabled, m, y, enabled_w, row);
    place(kSavedStatus, m + enabled_w + gap, y, std::max(0, inner - enabled_w - gap), row);
    y += row + gap;
    place(kQualityLabel, m, y, label, row);
    place(kQuality, m + label, y, scaled(kComboWidth), row * 6);
    y += row + gap;
    place(kNote, m, y, inner, row * 2);
    y += row * 2 + gap * 2;

    const int remake_w = button_width(kRemake);
    const int stop_w = button_width(kStop);
    place(kRemake, m, y, remake_w, row + scaled(4));
    place(kStop, m + remake_w + gap, y, stop_w, row + scaled(4));
    const int progress_x = m + remake_w + gap + stop_w + gap;
    place(kProgress, progress_x, y, std::max(0, m + inner - progress_x), row + scaled(4));
    y += row + scaled(4) + gap;
    place(kRemakeNote, m, y, inner, row * 2);
    y += row * 2 + gap * 2;
    place(kAboutRule, m, y, inner, 2);
    y += gap;
    place(kAbout, m, y, inner, row);
    y += row;
    place(kRepoLink, m, y, inner, row);
    y += row;
    place(kAppLink, m, y, inner, row);
    y += row + gap;
    const int close_w = std::max(button_width(kClose), scaled(90));
    place(kClose, m + inner - close_w, y, close_w, row + scaled(4));
    y += row + scaled(4) + m;

    // Fit the window to the content.
    RECT rc{0, 0, scaled(kWidth), y};
    AdjustWindowRectExForDpi(&rc, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                             0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::load_settings() noexcept {
    const Settings s = shellext::read_settings();
    const StoredSettings stored = stored_from_settings(s);
    CheckDlgButton(hwnd_, kEnabled, s.enabled ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hwnd_, kQuality, CB_SETCURSEL, stored.quality.value_or(1), 0);
}

void SettingsWindow::save_settings() noexcept {
    StoredSettings stored;
    stored.enabled = IsDlgButtonChecked(hwnd_, kEnabled) == BST_CHECKED ? 1u : 0u;
    const auto q = SendDlgItemMessageW(hwnd_, kQuality, CB_GETCURSEL, 0, 0);
    if (q >= 0) {
        stored.quality = static_cast<std::uint32_t>(q);
    }
    const bool ok = shellext::write_settings(settings_from_stored(stored));
    SetDlgItemTextW(hwnd_, kSavedStatus,
                    ok ? text(Text::SettingsSaved) : text(Text::CannotSaveSettings));
}

std::optional<std::filesystem::path> SettingsWindow::pick_folder() noexcept {
    std::optional<std::filesystem::path> result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return result;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(dialog->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                try {
                    result = std::filesystem::path(path);
                } catch (...) {
                }
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

void SettingsWindow::start_remake() noexcept {
    if (job_ != nullptr) {
        return;
    }
    const auto folder = pick_folder();
    if (!folder) {
        return;
    }
    auto* job = new (std::nothrow) RemakeJob();
    if (job == nullptr) {
        return;
    }
    job->window = hwnd_;
    try {
        job->folder = *folder;
    } catch (...) {
        delete job;
        return;
    }
    thread_ = CreateThread(nullptr, 0, &remake_thread, job, 0, nullptr);
    if (thread_ == nullptr) {
        delete job;
        return;
    }
    job_ = job;
    EnableWindow(GetDlgItem(hwnd_, kRemake), FALSE);
    EnableWindow(GetDlgItem(hwnd_, kStop), TRUE);
    SetDlgItemTextW(hwnd_, kProgress, text(Text::Searching));
}

void SettingsWindow::finish_remake() noexcept {
    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    delete job_;
    job_ = nullptr;
    EnableWindow(GetDlgItem(hwnd_, kRemake), TRUE);
    EnableWindow(GetDlgItem(hwnd_, kStop), FALSE);
}

LRESULT CALLBACK SettingsWindow::window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self == nullptr || self->hwnd_ != hwnd) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

LRESULT SettingsWindow::handle(UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        const auto* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        update_font();
        layout();
        return 0;
    }
    case WM_NOTIFY: {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
        if ((hdr->idFrom == kRepoLink || hdr->idFrom == kAppLink) &&
            (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            // Open only one of the two fixed pages, whatever the control reports.
            const wchar_t* url = hdr->idFrom == kRepoLink ? kRepoUrl : kAppUrl;
            ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kRemake:
            start_remake();
            return 0;
        case kStop:
            if (job_ != nullptr) {
                job_->stop.store(true);
                EnableWindow(GetDlgItem(hwnd_, kStop), FALSE);
            }
            return 0;
        // Save as soon as a value changes; there is no Save button.
        case kEnabled:
            if (HIWORD(wp) == BN_CLICKED) {
                save_settings();
            }
            return 0;
        case kQuality:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                save_settings();
            }
            return 0;
        case kClose:
        case IDCANCEL:
            DestroyWindow(hwnd_);
            return 0;
        default:
            break;
        }
        break;
    case kSearchMessage:
        try {
            SetDlgItemTextW(hwnd_, kProgress, text(Text::Searching, wp).c_str());
        } catch (...) {
        }
        return 0;
    case kProgressMessage:
        try {
            std::wstring s = text(Text::Remaking);
            s += L" " + std::to_wstring(wp) + L" / " + std::to_wstring(lp);
            SetDlgItemTextW(hwnd_, kProgress, s.c_str());
        } catch (...) {
        }
        return 0;
    case kDoneMessage:
        finish_remake();
        try {
            const std::wstring s = lp != 0   ? std::wstring(text(Text::Stopped))
                                   : wp == 0 ? std::wstring(text(Text::NothingToRemake))
                                             : text(Text::Remade, wp);
            SetDlgItemTextW(hwnd_, kProgress, s.c_str());
        } catch (...) {
        }
        return 0;
    case WM_DESTROY:
        if (job_ != nullptr) {
            // Let the current file finish, then stop.
            job_->stop.store(true);
            finish_remake();
        }
        if (font_ != nullptr) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace

int run_settings(int show) noexcept {
    auto* window = new (std::nothrow) SettingsWindow();
    if (window == nullptr) {
        return 1;
    }
    const int code = window->run(show);
    delete window;
    return code;
}

} // namespace splatthumb::app
