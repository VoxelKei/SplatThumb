#include "viewer_window.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <new>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "splatthumb/camera.h"
#include "splatthumb/classify.h"
#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/loader.h"
#include "splatthumb/orbit.h"
#include "splatthumb/render.h"
#include "splatthumb/settings.h"
#include "splatthumb/view.h"
#include "splatthumb/view_store.h"
#include "app_icon.h"
#include "settings_window.h"
#include "strings.h"
#include "thumbnail_refresh.h"
#include "user_store.h"

namespace splatthumb::app {

namespace {

constexpr wchar_t kClassName[] = L"SplatThumbViewer";
constexpr float kPi = std::numbers::pi_v<float>;

// While dragging, a thinned-out copy is drawn small so the window keeps up with the mouse.
constexpr std::size_t kPreviewSplats = 30'000;
constexpr int kPreviewSize = 320;
constexpr auto kPreviewTime = std::chrono::milliseconds(500);
constexpr auto kFullTime = std::chrono::seconds(10);
constexpr auto kLoadTime = std::chrono::seconds(60);
// After wheel or button changes, the full-quality picture follows this much later.
constexpr UINT kSettleMs = 250;
constexpr UINT_PTR kSettleTimer = 1;
// From the thumbnail thread: wParam = sizes made, lParam = total.
constexpr UINT kSaveStepMessage = WM_APP + 1;
// From the thumbnail thread when it has finished.
constexpr UINT kSaveDoneMessage = WM_APP + 2;

// Layout in 96-DPI units.
constexpr int kMargin = 8;
constexpr int kButtonHeight = 30;
constexpr int kStatusHeight = 22;
constexpr int kButtonPadding = 24;
constexpr int kQualityRowHeight = 26;
constexpr int kQualityComboMinWidth = 120;
constexpr int kInitialWidth = 760;
constexpr int kInitialHeight = 820;
constexpr int kMinWidth = 480;
constexpr int kMinHeight = 420;
// A saved size larger than this is ignored as damaged.
constexpr int kMaxSavedSize = 16384;

enum Control : int {
    kAutomatic = 100,
    kSave,
    kSettings,
    kClose,
    kStatus,
    kQualityLabel,
    kQualityCombo,
    kSaveProgress,
};

// Remakes the thumbnail off the window's thread, so the progress bar can move.
struct SaveJob {
    HWND window = nullptr;
    std::filesystem::path file;
};

void post_save_step(void* context, unsigned done, unsigned total) noexcept {
    PostMessageW(static_cast<HWND>(context), kSaveStepMessage, done, static_cast<LPARAM>(total));
}

DWORD WINAPI save_thread(void* param) noexcept {
    auto* job = static_cast<SaveJob*>(param);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    refresh_thumbnail(job->file, &post_save_step, job->window);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    PostMessageW(job->window, kSaveDoneMessage, 0, 0);
    delete job;
    return 0;
}

// Entries of the quality list, in order. The first follows the global setting.
constexpr std::array<std::optional<Quality>, 4> kQualityChoices = {
    std::nullopt, Quality::Fast, Quality::Standard, Quality::High};

Text quality_text(Quality q) noexcept {
    switch (q) {
    case Quality::Fast: return Text::QualityFast;
    case Quality::Standard: return Text::QualityStandard;
    case Quality::High: return Text::QualityHigh;
    }
    return Text::QualityStandard;
}

// Left to right (and in tab order).
constexpr std::array kButtons = {
    std::pair{kAutomatic, Text::Automatic},
    std::pair{kSettings, Text::OpenSettings},
    std::pair{kSave, Text::Save},
    std::pair{kClose, Text::Close},
};

class Viewer {
public:
    explicit Viewer(std::filesystem::path file) : file_(std::move(file)) {}

    int run(int show) noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) noexcept;

    bool create_window(int show) noexcept;
    // Returns the exit code to use when loading fails, or nullopt on success.
    std::optional<int> load() noexcept;
    void create_controls() noexcept;
    void update_font() noexcept;
    void layout() noexcept;
    void open_settings() noexcept;
    RECT picture_rect() const noexcept;
    int scaled(int v) const noexcept { return MulDiv(v, static_cast<int>(dpi_), 96); }

    void set_view(const View& v, bool settle) noexcept;
    void render_if_needed(int side) noexcept;
    void paint() noexcept;
    void draw_axes(HDC dc, int x0, int y0, int side) const noexcept;
    void set_status(const std::wstring& s) noexcept;
    void save() noexcept;
    void reset() noexcept;
    void message(Text t, UINT icon) const noexcept;
    // Loads the splats again at quality, replacing splats_, preview_ and auto_view_.
    bool load_splats_at(Quality quality) noexcept;
    Quality effective_quality() const noexcept { return quality_.value_or(settings_.quality); }
    void fill_quality_combo() noexcept;
    void select_quality(const std::optional<Quality>& q) noexcept;
    void change_quality(const std::optional<Quality>& q) noexcept;

    std::filesystem::path file_;
    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
    HWND quality_label_ = nullptr;
    HWND quality_combo_ = nullptr;
    HWND total_label_ = nullptr; // "/ N in file", right of the list
    // Splats in the file, from its header (no extra reading).
    std::uint64_t total_splats_ = 0;
    HWND progress_ = nullptr;
    // While the thumbnail is being remade after "Use this view": input is ignored and the
    // window closes when it is done.
    bool saving_ = false;
    std::array<HWND, kButtons.size()> buttons_{};
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    Settings settings_;
    std::vector<Splat> splats_;
    std::vector<Splat> preview_;
    std::optional<Fingerprint> fingerprint_;
    std::optional<std::filesystem::path> views_root_;
    View view_{};
    View auto_view_{};
    // This file's own quality, as chosen in the window; nullopt follows the global setting.
    // view_.quality is kept empty; the choice is added only when saving.
    std::optional<Quality> quality_;
    // Window size at opening, in 96-DPI pixels; saved on close only if it changed.
    shellext::WindowSize opened_size_{};

    // Mouse dragging.
    bool dragging_ = false;
    bool settling_ = false;
    UINT drag_button_ = 0;
    POINT last_{};

    // The picture last drawn, as a top-down BGRA DIB.
    bool dirty_ = true;
    bool shows_preview_ = false;
    int image_size_ = 0;
    std::vector<std::uint8_t> bgra_;
};

int Viewer::run(int show) noexcept {
    if (!create_window(show)) {
        return 1;
    }
    if (const auto failed = load()) {
        DestroyWindow(hwnd_);
        return *failed;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

namespace {

// Centres the window on `cursor`, shrunk and shifted as needed so it fits inside the work
// area (screen minus taskbar) of the monitor under the pointer.
void place_near_cursor(HWND hwnd, POINT cursor) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info)) {
        return;
    }
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) {
        return;
    }
    const RECT& work = info.rcWork;
    const int width = std::min(window.right - window.left, work.right - work.left);
    const int height = std::min(window.bottom - window.top, work.bottom - work.top);
    const int x = std::clamp(cursor.x - width / 2, work.left, work.right - width);
    const int y = std::clamp(cursor.y - height / 2, work.top, work.bottom - height);
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

bool Viewer::create_window(int show) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Viewer::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = app_icon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = app_icon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    std::wstring title = text(Text::ViewerTitle);
    try {
        title += L" - ";
        title += file_.filename().wstring();
    } catch (...) {
    }
    // Open near the mouse pointer, which is where the user just clicked the right-click
    // menu. Creating the window at the pointer puts it on that monitor, so the DPI read
    // below is the right one; place_near_cursor() then centres it and keeps it on screen.
    POINT cursor{};
    const bool have_cursor = GetCursorPos(&cursor) != FALSE;
    hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            have_cursor ? cursor.x : CW_USEDEFAULT,
                            have_cursor ? cursor.y : CW_USEDEFAULT, kInitialWidth,
                            kInitialHeight, nullptr, nullptr, wc.hInstance, this);
    if (hwnd_ == nullptr) {
        return false;
    }
    // Resize for the monitor's DPI now that it is known, to the size it had when last closed.
    dpi_ = GetDpiForWindow(hwnd_);
    opened_size_ = shellext::read_viewer_size({kMinWidth, kMinHeight},
                                              {kMaxSavedSize, kMaxSavedSize})
                       .value_or(shellext::WindowSize{kInitialWidth, kInitialHeight});
    SetWindowPos(hwnd_, nullptr, 0, 0, scaled(opened_size_.width), scaled(opened_size_.height),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (have_cursor) {
        place_near_cursor(hwnd_, cursor);
    }
    create_controls();
    set_status(text(Text::Loading));
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    return true;
}

std::optional<int> Viewer::load() noexcept {
    const HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    settings_ = shellext::read_settings();
    {
        FileByteSource source;
        if (!source.open(file_)) {
            SetCursor(old_cursor);
            message(Text::CannotOpen, MB_ICONERROR);
            return 1;
        }
        const Classification c = inspect(source);
        if (c.kind == SplatKind::NotSplat) {
            SetCursor(old_cursor);
            message(Text::NotSplat, MB_ICONINFORMATION);
            return 0;
        }
        total_splats_ = c.vertex_count;
        fingerprint_ = fingerprint(source);
    }
    // The saved view decides the quality, so it is read before the splats.
    views_root_ = shellext::views_root();
    std::optional<View> saved;
    if (fingerprint_ && views_root_) {
        saved = load_view(*views_root_, *fingerprint_);
    }
    if (saved) {
        quality_ = saved->quality;
        saved->quality.reset();
    }
    if (!load_splats_at(effective_quality())) {
        SetCursor(old_cursor);
        message(Text::CannotLoad, MB_ICONERROR);
        return 1;
    }
    view_ = saved ? *saved : auto_view_;
    fill_quality_combo();
    try {
        // Digits grouped by three: "/ 2,000,000 in file".
        std::wstring digits = std::to_wstring(total_splats_);
        for (std::size_t i = digits.size(); i > 3; i -= 3) {
            digits.insert(i - 3, L",");
        }
        std::wstring label = text(Text::TotalSplats);
        if (const auto at = label.find(L"{n}"); at != std::wstring::npos) {
            label.replace(at, 3, digits);
        }
        SetWindowTextW(total_label_, label.c_str());
    } catch (...) {
    }
    layout();
    const BOOL can_save = fingerprint_ && views_root_ ? TRUE : FALSE;
    EnableWindow(GetDlgItem(hwnd_, kSave), can_save);
    EnableWindow(GetDlgItem(hwnd_, kAutomatic), can_save);
    EnableWindow(quality_combo_, can_save);
    SetCursor(old_cursor);
    set_status(text(Text::Hint));
    dirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return std::nullopt;
}

bool Viewer::load_splats_at(Quality quality) noexcept {
    FileByteSource source;
    if (!source.open(file_)) {
        return false;
    }
    const Classification c = inspect(source);
    if (c.kind == SplatKind::NotSplat) {
        return false;
    }
    LoadOptions load_options;
    load_options.max_splats = max_splats_for(quality);
    LoadResult loaded = load_splats(source, c, load_options, Deadline::after(kLoadTime));
    if (loaded.status != Status::Ok) {
        return false;
    }
    // The automatic view depends on which splats were read, as it does for the thumbnail.
    const CameraResult automatic = auto_camera(loaded.splats);
    if (automatic.status != Status::Ok) {
        return false;
    }
    try {
        std::vector<Splat> preview;
        const std::size_t step = loaded.splats.size() / kPreviewSplats + 1;
        preview.reserve(loaded.splats.size() / step + 1);
        for (std::size_t i = 0; i < loaded.splats.size(); i += step) {
            preview.push_back(loaded.splats[i]);
        }
        splats_ = std::move(loaded.splats);
        preview_ = std::move(preview);
    } catch (const std::bad_alloc&) {
        return false;
    }
    auto_view_ = view_from_camera(automatic.camera);
    dirty_ = true;
    return true;
}

void Viewer::fill_quality_combo() noexcept {
    SendMessageW(quality_combo_, CB_RESETCONTENT, 0, 0);
    for (const auto& choice : kQualityChoices) {
        std::wstring label;
        try {
            if (choice) {
                label = text(quality_text(*choice));
            } else {
                // "Same as Settings (Up to 200,000)": the {n} slot takes the current count.
                label = text(Text::QualityFollowSettings);
                const std::wstring current = text(quality_text(settings_.quality));
                if (const auto at = label.find(L"{n}"); at != std::wstring::npos) {
                    label.replace(at, 3, current);
                }
            }
        } catch (...) {
        }
        SendMessageW(quality_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    select_quality(quality_);
}

void Viewer::select_quality(const std::optional<Quality>& q) noexcept {
    for (std::size_t i = 0; i < kQualityChoices.size(); ++i) {
        if (kQualityChoices[i] == q) {
            SendMessageW(quality_combo_, CB_SETCURSEL, i, 0);
        }
    }
}

void Viewer::change_quality(const std::optional<Quality>& q) noexcept {
    if (q == quality_) {
        return;
    }
    const Quality before = effective_quality();
    const std::optional<Quality> old_choice = quality_;
    quality_ = q;
    if (effective_quality() != before) {
        const bool was_automatic = view_ == auto_view_;
        const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        const bool ok = load_splats_at(effective_quality());
        SetCursor(old);
        if (!ok) {
            quality_ = old_choice;
            select_quality(quality_);
            message(Text::CannotLoad, MB_ICONERROR);
            return;
        }
        if (was_automatic) {
            view_ = auto_view_;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    set_status(text(Text::QualityChanged));
}

void Viewer::create_controls() noexcept {
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    for (std::size_t i = 0; i < kButtons.size(); ++i) {
        const auto [id, label] = kButtons[i];
        const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                            (id == kSave ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        buttons_[i] = CreateWindowExW(0, L"BUTTON", text(label), style, 0, 0, 10, 10, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
                                      nullptr);
        EnableWindow(buttons_[i], FALSE);
    }
    EnableWindow(GetDlgItem(hwnd_, kSettings), TRUE);
    EnableWindow(GetDlgItem(hwnd_, kClose), TRUE);
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                              0, 0, 10, 10, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatus)), inst, nullptr);
    quality_label_ = CreateWindowExW(
        0, L"STATIC", text(Text::QualityLabel),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQualityLabel)), inst, nullptr);
    quality_combo_ = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQualityCombo)), inst,
        nullptr);
    EnableWindow(quality_combo_, FALSE);
    total_label_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, 0, 0,
        10, 10, hwnd_, nullptr, inst, nullptr);
    // Shown in place of the status text while the thumbnail is remade.
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, 0, 0, 10, 10,
                                hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveProgress)),
                                inst, nullptr);
    update_font();
    layout();
}

void Viewer::update_font() noexcept {
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
    for (const HWND b : buttons_) {
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(quality_label_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(quality_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(total_label_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    font_ = font;
}

RECT Viewer::picture_rect() const noexcept {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    rc.bottom = std::max(rc.top, rc.bottom - scaled(kMargin * 4 + kButtonHeight +
                                                    kQualityRowHeight + kStatusHeight));
    return rc;
}

void Viewer::layout() noexcept {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int bottom = rc.bottom - scaled(kMargin);
    const int buttons_top = bottom - scaled(kButtonHeight);
    const int quality_top = buttons_top - scaled(kMargin) - scaled(kQualityRowHeight);
    const int status_top = quality_top - scaled(kMargin) - scaled(kStatusHeight);
    MoveWindow(status_, scaled(kMargin), status_top, rc.right - 2 * scaled(kMargin),
               scaled(kStatusHeight), TRUE);
    {
        // A slim bar in the middle of the status line.
        const int w = std::min(scaled(320), static_cast<int>(rc.right) - 2 * scaled(kMargin));
        const int h = scaled(8);
        MoveWindow(progress_, (rc.right - w) / 2, status_top + (scaled(kStatusHeight) - h) / 2, w,
                   h, TRUE);
    }

    const HDC dc = GetDC(hwnd_);
    const HGDIOBJ old = SelectObject(dc, font_);
    const auto text_width = [&](const wchar_t* s) {
        SIZE size{};
        GetTextExtentPoint32W(dc, s, static_cast<int>(wcslen(s)), &size);
        return static_cast<int>(size.cx);
    };
    // Each row is centred in the window; if it is wider than the window it starts at the margin.
    const auto row_left = [&](int row_width) {
        return std::max(scaled(kMargin), static_cast<int>(rc.right - row_width) / 2);
    };
    {
        const int label_w = text_width(text(Text::QualityLabel)) + scaled(4);
        // The list is as wide as its longest entry plus the drop-down arrow.
        int combo_w = scaled(kQualityComboMinWidth);
        const auto count = SendMessageW(quality_combo_, CB_GETCOUNT, 0, 0);
        for (LRESULT i = 0; i < count; ++i) {
            wchar_t item[128] = {};
            const auto len = SendMessageW(quality_combo_, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0);
            if (len > 0 && len < static_cast<LRESULT>(std::size(item))) {
                SendMessageW(quality_combo_, CB_GETLBTEXT, static_cast<WPARAM>(i),
                             reinterpret_cast<LPARAM>(item));
                combo_w = std::max(combo_w, text_width(item) + GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_) + scaled(16));
            }
        }
        wchar_t total[64] = {};
        GetWindowTextW(total_label_, total, static_cast<int>(std::size(total)));
        const int total_w = total[0] != L'\0' ? text_width(total) + scaled(4) : 0;
        const int gap = scaled(kMargin);
        const int x = row_left(label_w + gap + combo_w + (total_w > 0 ? gap + total_w : 0));
        MoveWindow(quality_label_, x, quality_top, label_w, scaled(kQualityRowHeight), TRUE);
        // A drop-down list's height includes its open list.
        MoveWindow(quality_combo_, x + label_w + gap, quality_top, combo_w,
                   scaled(kQualityRowHeight) * 8, TRUE);
        MoveWindow(total_label_, x + label_w + gap + combo_w + gap, quality_top, total_w,
                   scaled(kQualityRowHeight), TRUE);
    }
    std::array<int, kButtons.size()> widths{};
    int total = 0;
    for (std::size_t i = 0; i < kButtons.size(); ++i) {
        widths[i] = text_width(text(kButtons[i].second)) + scaled(kButtonPadding);
        total += widths[i] + (i > 0 ? scaled(kMargin) / 2 : 0);
    }
    int x = row_left(total);
    for (std::size_t i = 0; i < kButtons.size(); ++i) {
        MoveWindow(buttons_[i], x, buttons_top, widths[i], scaled(kButtonHeight), TRUE);
        x += widths[i] + scaled(kMargin) / 2;
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
}

void Viewer::set_status(const std::wstring& s) noexcept {
    SetWindowTextW(status_, s.c_str());
}

void Viewer::message(Text t, UINT icon) const noexcept {
    MessageBoxW(hwnd_, text(t), text(Text::AppName), MB_OK | icon);
}

void Viewer::set_view(const View& v, bool settle) noexcept {
    view_ = v;
    dirty_ = true;
    if (settle) {
        // Draw the quick picture now and the full one once input stops.
        settling_ = true;
        SetTimer(hwnd_, kSettleTimer, kSettleMs, nullptr);
    }
    const RECT pic = picture_rect();
    InvalidateRect(hwnd_, &pic, FALSE);
}

void Viewer::render_if_needed(int side) noexcept {
    const bool quick = dragging_ || settling_;
    const int want = quick ? std::min(side, kPreviewSize) : std::min(side, limits::kMaxThumbnailSize);
    if (!dirty_ && image_size_ == want && shows_preview_ == quick) {
        return;
    }
    if (splats_.empty() || want < limits::kMinThumbnailSize) {
        return;
    }
    const CameraResult cam = camera_from_view(view_);
    if (cam.status != Status::Ok) {
        return;
    }
    RenderOptions options;
    options.size = want;
    options.background = Background::Gray;
    RenderResult r = render_splats(quick ? preview_ : splats_, cam.camera, options,
                                   Deadline::after(quick ? std::chrono::milliseconds(kPreviewTime)
                                                         : std::chrono::milliseconds(kFullTime)));
    if (r.status != Status::Ok && !quick) {
        // Too slow at full quality: keep the window usable with the quick picture.
        options.size = std::min(side, kPreviewSize);
        r = render_splats(preview_, cam.camera, options, Deadline::after(kPreviewTime));
    }
    if (r.status == Status::NothingToDraw) {
        // Looking away from everything: show the empty background.
        constexpr std::uint8_t level = 46; // the dark gray of the thumbnails
        const auto n = static_cast<std::size_t>(options.size);
        r.image.size = options.size;
        try {
            r.image.rgba.assign(n * n * 4, level);
        } catch (const std::bad_alloc&) {
            return;
        }
    } else if (r.status != Status::Ok) {
        return;
    }
    try {
        const std::size_t n = r.image.rgba.size();
        bgra_.resize(n);
        for (std::size_t i = 0; i < n; i += 4) {
            bgra_[i + 0] = r.image.rgba[i + 2];
            bgra_[i + 1] = r.image.rgba[i + 1];
            bgra_[i + 2] = r.image.rgba[i + 0];
            bgra_[i + 3] = 255;
        }
    } catch (const std::bad_alloc&) {
        return;
    }
    image_size_ = r.image.size;
    shows_preview_ = quick;
    dirty_ = false;
}

void Viewer::paint() noexcept {
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(hwnd_, &ps);
    const RECT pic = picture_rect();
    const int side = std::min(pic.right - pic.left, pic.bottom - pic.top);
    const int x0 = pic.left + (pic.right - pic.left - side) / 2;
    const int y0 = pic.top + (pic.bottom - pic.top - side) / 2;

    // Dark surround so the picture's own background reads as part of the thumbnail.
    const HBRUSH surround = CreateSolidBrush(RGB(24, 24, 24));
    RECT r = pic;
    r.right = x0;
    FillRect(dc, &r, surround);
    r = pic;
    r.left = x0 + side;
    FillRect(dc, &r, surround);
    r = {x0, pic.top, x0 + side, y0};
    FillRect(dc, &r, surround);
    r = {x0, y0 + side, x0 + side, pic.bottom};
    FillRect(dc, &r, surround);
    DeleteObject(surround);

    if (side > 0) {
        render_if_needed(side);
    }
    if (side > 0 && image_size_ > 0 && !bgra_.empty()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = image_size_;
        info.bmiHeader.biHeight = -image_size_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x0, y0, side, side, 0, 0, image_size_, image_size_, bgra_.data(),
                      &info, DIB_RGB_COLORS, SRCCOPY);
        // Axes only while the quick (thinned-out) picture is shown, i.e. while moving.
        if (shows_preview_) {
            draw_axes(dc, x0, y0, side);
        }
    } else if (side > 0) {
        const HBRUSH bg = CreateSolidBrush(RGB(46, 46, 46));
        r = {x0, y0, x0 + side, y0 + side};
        FillRect(dc, &r, bg);
        DeleteObject(bg);
    }
    EndPaint(hwnd_, &ps);
}

// Draws the file's X, Y and Z axes through the origin over the picture: X red, Y green,
// Z blue, the negative halves dimmer, with a label at each positive end. Only this window
// shows them; thumbnails never do. The projection matches render_splats.
void Viewer::draw_axes(HDC dc, int x0, int y0, int side) const noexcept {
    const CameraResult cam = camera_from_view(view_);
    if (cam.status != Status::Ok || side <= 0) {
        return;
    }
    const Camera& c = cam.camera;
    const float focal = static_cast<float>(side) * 0.5f / std::tan(c.fov_y * 0.5f);
    const float near_z = std::max(c.radius * 0.01f, 1e-6f);
    const float length = auto_view_.radius; // about the size of the subject
    if (!std::isfinite(focal) || !(length > 0.0f)) {
        return;
    }
    // Camera-space point: x right, y up, z forward.
    const auto to_camera = [&](const Vec3& p) {
        const Vec3 d{p[0] - c.eye[0], p[1] - c.eye[1], p[2] - c.eye[2]};
        const auto dot = [](const Vec3& a, const Vec3& b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        return Vec3{dot(d, c.right), dot(d, c.up), dot(d, c.forward)};
    };
    // Picture coordinates of a camera-space point in front of the camera. Kept in double: a
    // point near the camera plane lands far off the picture, and is clipped below.
    const auto to_picture = [&](const Vec3& q, double& u, double& v) {
        const double half = static_cast<double>(side) * 0.5;
        u = half + static_cast<double>(focal) * q[0] / q[2];
        v = half - static_cast<double>(focal) * q[1] / q[2];
    };
    // Draws a to b: cut at the near plane, projected, then clipped as a 2D line to a band
    // around the picture (Liang-Barsky) so the line keeps its direction when an end is far
    // off screen. Returns false if nothing is left.
    const auto segment = [&](const Vec3& a_world, const Vec3& b_world, POINT& pa, POINT& pb) {
        Vec3 a = to_camera(a_world);
        Vec3 b = to_camera(b_world);
        if (a[2] < near_z && b[2] < near_z) {
            return false;
        }
        const auto cut = [&](Vec3& behind, const Vec3& front) {
            const float t = (near_z - behind[2]) / (front[2] - behind[2]);
            for (std::size_t i = 0; i < 3; ++i) {
                behind[i] += (front[i] - behind[i]) * t;
            }
        };
        if (a[2] < near_z) {
            cut(a, b);
        } else if (b[2] < near_z) {
            cut(b, a);
        }
        double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        to_picture(a, u0, v0);
        to_picture(b, u1, v1);
        if (!std::isfinite(u0) || !std::isfinite(v0) || !std::isfinite(u1) || !std::isfinite(v1)) {
            return false;
        }
        const double lo = -static_cast<double>(side);
        const double hi = 2.0 * static_cast<double>(side);
        const double du = u1 - u0;
        const double dv = v1 - v0;
        double t0 = 0.0, t1 = 1.0;
        const auto clip = [&](double p, double q) {
            if (p == 0.0) {
                return q >= 0.0;
            }
            const double r = q / p;
            if (p < 0.0) {
                if (r > t1) return false;
                t0 = std::max(t0, r);
            } else {
                if (r < t0) return false;
                t1 = std::min(t1, r);
            }
            return true;
        };
        if (!clip(-du, u0 - lo) || !clip(du, hi - u0) || !clip(-dv, v0 - lo) ||
            !clip(dv, hi - v0)) {
            return false;
        }
        pa = POINT{x0 + static_cast<LONG>(std::lround(u0 + t0 * du)),
                   y0 + static_cast<LONG>(std::lround(v0 + t0 * dv))};
        pb = POINT{x0 + static_cast<LONG>(std::lround(u0 + t1 * du)),
                   y0 + static_cast<LONG>(std::lround(v0 + t1 * dv))};
        return true;
    };

    struct Axis {
        Vec3 dir;
        COLORREF bright;
        COLORREF dim;
        const wchar_t* label;
    };
    const Axis axes[] = {
        {{1, 0, 0}, RGB(235, 70, 70), RGB(120, 45, 45), L"X"},
        {{0, 1, 0}, RGB(80, 210, 80), RGB(45, 105, 45), L"Y"},
        {{0, 0, 1}, RGB(80, 140, 255), RGB(45, 70, 130), L"Z"},
    };
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, x0, y0, x0 + side, y0 + side);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);
    const int width = std::max(1, scaled(2));
    for (const Axis& axis : axes) {
        const Vec3 pos{axis.dir[0] * length, axis.dir[1] * length, axis.dir[2] * length};
        const Vec3 neg{-pos[0], -pos[1], -pos[2]};
        const Vec3 origin{0, 0, 0};
        for (const bool positive : {false, true}) {
            POINT a{}, b{};
            if (!segment(origin, positive ? pos : neg, a, b)) {
                continue;
            }
            const HPEN pen = CreatePen(PS_SOLID, width, positive ? axis.bright : axis.dim);
            if (pen == nullptr) {
                continue;
            }
            const HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, a.x, a.y, nullptr);
            LineTo(dc, b.x, b.y);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        const Vec3 tip = to_camera(pos);
        double tu = 0, tv = 0;
        if (tip[2] >= near_z) {
            to_picture(tip, tu, tv);
        }
        if (tip[2] >= near_z && tu > -side && tu < 2.0 * side && tv > -side && tv < 2.0 * side) {
            const POINT p{x0 + static_cast<LONG>(std::lround(tu)),
                          y0 + static_cast<LONG>(std::lround(tv))};
            SetTextColor(dc, axis.bright);
            TextOutW(dc, p.x + scaled(4), p.y - scaled(8), axis.label, 1);
        }
    }
    RestoreDC(dc, saved);
}

void Viewer::save() noexcept {
    View saved = view_;
    saved.quality = quality_;
    if (!fingerprint_ || !views_root_ || !save_view(*views_root_, *fingerprint_, saved)) {
        message(Text::CannotSave, MB_ICONERROR);
        return;
    }
    // Remake the thumbnail on another thread with a progress bar; the window closes when it
    // is done (kSaveDoneMessage). If the thread cannot start, do it here instead.
    auto* job = new (std::nothrow) SaveJob{hwnd_, {}};
    HANDLE thread = nullptr;
    if (job != nullptr) {
        try {
            job->file = file_;
            thread = CreateThread(nullptr, 0, &save_thread, job, 0, nullptr);
        } catch (...) {
        }
        if (thread == nullptr) {
            delete job;
        }
    }
    if (thread == nullptr) {
        const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        refresh_thumbnail(file_);
        SetCursor(old);
        DestroyWindow(hwnd_);
        return;
    }
    CloseHandle(thread);
    saving_ = true;
    for (const HWND b : buttons_) {
        EnableWindow(b, FALSE);
    }
    EnableWindow(quality_combo_, FALSE);
    SendMessageW(progress_, PBM_SETRANGE32, 0, 1);
    SendMessageW(progress_, PBM_SETPOS, 0, 0);
    ShowWindow(status_, SW_HIDE);
    ShowWindow(progress_, SW_SHOW);
}

void Viewer::reset() noexcept {
    if (fingerprint_ && views_root_) {
        remove_view(*views_root_, *fingerprint_);
    }
    // Back to the global quality too: the quality is stored in the view file.
    if (quality_) {
        const bool reload = effective_quality() != settings_.quality;
        quality_.reset();
        select_quality(quality_);
        if (reload) {
            const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            const bool ok = load_splats_at(settings_.quality);
            SetCursor(old);
            if (!ok) {
                message(Text::CannotLoad, MB_ICONERROR);
            }
        }
    }
    set_view(auto_view_, false);
    const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    refresh_thumbnail(file_);
    SetCursor(old);
    set_status(text(Text::ResetDone));
}

LRESULT CALLBACK Viewer::window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    Viewer* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Viewer*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self == nullptr || self->hwnd_ != hwnd) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

// Brings an open settings window to the front, or starts SplatThumb.exe again without
// arguments to open one. A separate process, so either window can be closed first.
void Viewer::open_settings() noexcept {
    if (const HWND open = FindWindowW(kSettingsWindowClass, nullptr); open != nullptr) {
        if (IsIconic(open)) {
            ShowWindow(open, SW_RESTORE);
        }
        SetForegroundWindow(open);
        return;
    }
    std::wstring exe(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (length == 0 || length >= exe.size()) {
        return;
    }
    exe.resize(length);
    ShellExecuteW(hwnd_, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT Viewer::handle(UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND: {
        // The picture area is painted fully in WM_PAINT; erase only the rest to avoid flicker.
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        rc.top = picture_rect().bottom;
        FillRect(reinterpret_cast<HDC>(wp), &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_SIZE:
        layout();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = {scaled(kMinWidth), scaled(kMinHeight)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        update_font();
        const auto* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        if (splats_.empty() || dragging_ || saving_) {
            return 0;
        }
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const RECT pic = picture_rect();
        if (!PtInRect(&pic, pt)) {
            return 0;
        }
        SetFocus(hwnd_);
        SetCapture(hwnd_);
        dragging_ = true;
        drag_button_ = msg;
        last_ = pt;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!dragging_) {
            return 0;
        }
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const RECT pic = picture_rect();
        const float side = static_cast<float>(
            std::max(1L, std::min(pic.right - pic.left, pic.bottom - pic.top)));
        const float dx = static_cast<float>(pt.x - last_.x) / side;
        const float dy = static_cast<float>(pt.y - last_.y) / side;
        last_ = pt;
        if (dx == 0.0f && dy == 0.0f) {
            return 0;
        }
        View next = view_;
        if (drag_button_ == WM_RBUTTONDOWN || (wp & MK_SHIFT) != 0) {
            // The picture follows the mouse: half the picture height is about one radius.
            next = pan(view_, -dx * 2.2f, -dy * 2.2f);
        } else {
            // The subject turns with the mouse, as if grabbed.
            next = orbit(view_, -dx * kPi, dy * kPi);
        }
        set_view(next, false);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (dragging_) {
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (dragging_) {
            dragging_ = false;
            dirty_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        if (splats_.empty() || saving_) {
            return 0;
        }
        const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
        set_view(zoom(view_, std::pow(0.85f, notches)), true);
        return 0;
    }
    case WM_TIMER:
        if (wp == kSettleTimer) {
            KillTimer(hwnd_, kSettleTimer);
            settling_ = false;
            dirty_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case kSaveStepMessage:
        SendMessageW(progress_, PBM_SETRANGE32, 0, lp);
        SendMessageW(progress_, PBM_SETPOS, wp, 0);
        return 0;
    case kSaveDoneMessage:
        saving_ = false;
        DestroyWindow(hwnd_);
        return 0;
    case WM_CLOSE:
        // The thumbnail thread posts to this window; it closes itself when the thread is done.
        if (saving_) {
            return 0;
        }
        break;
    case WM_COMMAND:
        if (saving_) {
            return 0;
        }
        switch (LOWORD(wp)) {
        case kAutomatic:
            reset();
            return 0;
        case kQualityCombo:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const auto i = SendMessageW(quality_combo_, CB_GETCURSEL, 0, 0);
                if (i >= 0 && static_cast<std::size_t>(i) < kQualityChoices.size()) {
                    change_quality(kQualityChoices[static_cast<std::size_t>(i)]);
                }
            }
            return 0;
        case kSave:
            save();
            return 0;
        case kSettings:
            open_settings();
            return 0;
        case kClose:
        case IDCANCEL:
            DestroyWindow(hwnd_);
            return 0;
        default:
            break;
        }
        break;
    case WM_DESTROY: {
        // Remember the size for next time. The normal (not maximized or minimized) size is
        // used, converted to 96-DPI pixels so it fits a monitor with another scale.
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd_, &placement) && dpi_ > 0) {
            const RECT& r = placement.rcNormalPosition;
            const shellext::WindowSize size{MulDiv(r.right - r.left, 96, static_cast<int>(dpi_)),
                                            MulDiv(r.bottom - r.top, 96, static_cast<int>(dpi_))};
            // Rounding can move a size by a pixel; only a real change is written.
            if (std::abs(size.width - opened_size_.width) > 1 ||
                std::abs(size.height - opened_size_.height) > 1) {
                shellext::write_viewer_size(size);
            }
        }
        if (font_ != nullptr) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace

int run_viewer(const std::filesystem::path& file, int show) noexcept {
    Viewer* viewer = new (std::nothrow) Viewer(file);
    if (viewer == nullptr) {
        return 1;
    }
    const int code = viewer->run(show);
    delete viewer;
    return code;
}

} // namespace splatthumb::app
