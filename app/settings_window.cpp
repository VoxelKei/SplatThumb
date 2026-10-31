#include "settings_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "splatthumb/camera.h"
#include "splatthumb/classify.h"
#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/settings.h"
#include "splatthumb/training_camera.h"
#include "splatthumb/view_store.h"
#include "strings.h"
#include "thumbnail_refresh.h"
#include "user_store.h"

namespace splatthumb::app {

namespace {


// A folder with more .ply files than this is only remade up to this many.
constexpr std::size_t kMaxFolderFiles = 10'000;

// Messages from the remake thread.
constexpr UINT kProgressMessage = WM_APP + 1; // wParam = files done, lParam = total
constexpr UINT kDoneMessage = WM_APP + 2;     // wParam = files done, lParam = stopped

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
    kSave,
    kSavedStatus,
    kViewCount,
    kDeleteViews,
    kUseTraining,
    kRemake,
    kStop,
    kProgress,
    kClose,
    kQualityLabel,
};

// Work for the remake thread. Owned by the window; the thread only reads it, except for the
// stop flag.
struct RemakeJob {
    HWND window = nullptr;
    std::vector<std::filesystem::path> files;
    std::optional<std::filesystem::path> views_root;
    bool use_training = false;
    std::atomic<bool> stop{false};
};

bool is_ply(const std::filesystem::path& p) {
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext == L".ply";
}

// Saves the training camera's view for a file that has no saved view yet.
void save_training_view(const std::filesystem::path& file,
                        const std::filesystem::path& views_root) noexcept {
    const auto cam = find_training_camera(file);
    if (!cam) {
        return;
    }
    FileByteSource source;
    if (!source.open(file)) {
        return;
    }
    const auto fp = fingerprint(source);
    if (!fp || load_view(views_root, *fp)) {
        return;
    }
    const Classification c = inspect(source);
    if (c.kind == SplatKind::NotSplat) {
        return;
    }
    // Only the framing depth and radius come from the splats, so a small sample is enough.
    LoadOptions options;
    options.max_splats = limits::kFastRenderSplats;
    const LoadResult loaded =
        load_splats(source, c, options, Deadline::after(std::chrono::seconds(30)));
    if (loaded.status != Status::Ok) {
        return;
    }
    const CameraResult automatic = auto_camera(loaded.splats);
    if (automatic.status != Status::Ok) {
        return;
    }
    if (const auto view = view_from_training_camera(*cam, automatic.camera)) {
        save_view(views_root, *fp, *view);
    }
}

DWORD WINAPI remake_thread(void* param) noexcept {
    auto* job = static_cast<RemakeJob*>(param);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::size_t done = 0;
    for (const auto& file : job->files) {
        if (job->stop.load()) {
            break;
        }
        if (job->use_training && job->views_root) {
            save_training_view(file, *job->views_root);
        }
        refresh_thumbnail(file);
        ++done;
        PostMessageW(job->window, kProgressMessage, done, static_cast<LPARAM>(job->files.size()));
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    PostMessageW(job->window, kDoneMessage, done, job->stop.load() ? 1 : 0);
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
    void update_view_count() noexcept;
    void delete_views() noexcept;
    void start_remake() noexcept;
    void finish_remake() noexcept;
    std::optional<std::filesystem::path> pick_folder() noexcept;

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::optional<std::filesystem::path> views_root_;
    RemakeJob* job_ = nullptr;
    HANDLE thread_ = nullptr;
};

int SettingsWindow::run(int show) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kSettingsWindowClass, text(Text::SettingsTitle),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, kWidth, 400, nullptr, nullptr,
                            wc.hInstance, this);
    if (hwnd_ == nullptr) {
        return 1;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    views_root_ = shellext::views_root();
    create_controls();
    load_settings();
    update_view_count();
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
    add(L"BUTTON", text(Text::SaveSettings), WS_TABSTOP | BS_PUSHBUTTON, kSave);
    add(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, kSavedStatus);
    add(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, kViewCount);
    add(L"BUTTON", text(Text::DeleteViews), WS_TABSTOP | BS_PUSHBUTTON, kDeleteViews);
    add(L"BUTTON", text(Text::UseTrainingCameras), WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
        kUseTraining);
    add(L"BUTTON", text(Text::RemakeFolder), WS_TABSTOP | BS_PUSHBUTTON, kRemake);
    const HWND stop = add(L"BUTTON", text(Text::Stop), WS_TABSTOP | BS_PUSHBUTTON, kStop);
    EnableWindow(stop, FALSE);
    add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kProgress);
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
    const int label = scaled(kLabelWidth);
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

    int y = m;
    place(kEnabled, m, y, inner, row);
    y += row + gap;
    place(kQualityLabel, m, y, label, row);
    place(kQuality, m + label, y, scaled(kComboWidth), row * 6);
    y += row + gap;
    place(kNote, m, y, inner, row * 2);
    y += row * 2 + gap;
    const int save_w = button_width(kSave);
    place(kSave, m, y, save_w, row + scaled(4));
    place(kSavedStatus, m + save_w + gap, y, inner - save_w - gap, row + scaled(4));
    y += row + scaled(4) + gap * 3;

    const int delete_w = button_width(kDeleteViews);
    place(kViewCount, m, y, inner - delete_w - gap, row + scaled(4));
    place(kDeleteViews, m + inner - delete_w, y, delete_w, row + scaled(4));
    y += row + scaled(4) + gap * 2;
    place(kUseTraining, m, y, inner, row * 2);
    y += row * 2 + gap;
    const int remake_w = button_width(kRemake);
    const int stop_w = button_width(kStop);
    place(kRemake, m, y, remake_w, row + scaled(4));
    place(kStop, m + remake_w + gap, y, stop_w, row + scaled(4));
    y += row + scaled(4) + gap;
    place(kProgress, m, y, inner, row);
    y += row + gap * 2;
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

void SettingsWindow::update_view_count() noexcept {
    const std::size_t n = views_root_ ? count_views(*views_root_) : 0;
    try {
        SetDlgItemTextW(hwnd_, kViewCount, text(Text::SavedViews, n).c_str());
    } catch (...) {
    }
    EnableWindow(GetDlgItem(hwnd_, kDeleteViews), n > 0 && job_ == nullptr);
}

void SettingsWindow::delete_views() noexcept {
    if (!views_root_) {
        return;
    }
    const std::size_t n = count_views(*views_root_);
    try {
        const std::wstring question = text(Text::ConfirmDeleteViews, n);
        if (MessageBoxW(hwnd_, question.c_str(), text(Text::AppName),
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    } catch (...) {
        return;
    }
    remove_all_views(*views_root_);
    update_view_count();
    SetDlgItemTextW(hwnd_, kProgress, text(Text::ViewsDeleted));
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
    try {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(*folder, ec);
             !ec && it != std::filesystem::directory_iterator() &&
             job->files.size() < kMaxFolderFiles;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && is_ply(it->path())) {
                job->files.push_back(it->path());
            }
        }
        std::sort(job->files.begin(), job->files.end());
    } catch (...) {
        job->files.clear();
    }
    if (job->files.empty()) {
        delete job;
        SetDlgItemTextW(hwnd_, kProgress, text(Text::NoPlyFiles));
        return;
    }
    job->window = hwnd_;
    job->views_root = views_root_;
    job->use_training = IsDlgButtonChecked(hwnd_, kUseTraining) == BST_CHECKED;
    thread_ = CreateThread(nullptr, 0, &remake_thread, job, 0, nullptr);
    if (thread_ == nullptr) {
        delete job;
        return;
    }
    job_ = job;
    EnableWindow(GetDlgItem(hwnd_, kRemake), FALSE);
    EnableWindow(GetDlgItem(hwnd_, kDeleteViews), FALSE);
    EnableWindow(GetDlgItem(hwnd_, kStop), TRUE);
    SetDlgItemTextW(hwnd_, kProgress, text(Text::Remaking));
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
    update_view_count();
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
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kSave:
            save_settings();
            return 0;
        case kDeleteViews:
            delete_views();
            return 0;
        case kRemake:
            start_remake();
            return 0;
        case kStop:
            if (job_ != nullptr) {
                job_->stop.store(true);
                EnableWindow(GetDlgItem(hwnd_, kStop), FALSE);
            }
            return 0;
        case kEnabled:
        case kQuality:
            SetDlgItemTextW(hwnd_, kSavedStatus, L"");
            return 0;
        case kClose:
        case IDCANCEL:
            DestroyWindow(hwnd_);
            return 0;
        default:
            break;
        }
        break;
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
            const std::wstring s =
                lp != 0 ? std::wstring(text(Text::Stopped)) : text(Text::Remade, wp);
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
