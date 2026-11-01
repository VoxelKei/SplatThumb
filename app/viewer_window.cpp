#include "viewer_window.h"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

// Layout in 96-DPI units.
constexpr int kMargin = 8;
constexpr int kButtonHeight = 30;
constexpr int kStatusHeight = 22;
constexpr int kButtonPadding = 24;
constexpr int kInitialWidth = 760;
constexpr int kInitialHeight = 820;

enum Control : int {
    kAutomatic = 100,
    kSave,
    kSettings,
    kClose,
    kStatus,
};

constexpr std::array kButtons = {
    std::pair{kAutomatic, Text::Automatic},
    std::pair{kSave, Text::Save},             std::pair{kSettings, Text::OpenSettings},
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
    void set_status(const std::wstring& s) noexcept;
    void save() noexcept;
    void reset() noexcept;
    void message(Text t, UINT icon) const noexcept;

    std::filesystem::path file_;
    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
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
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    // Resize for the monitor's DPI now that it is known.
    dpi_ = GetDpiForWindow(hwnd_);
    SetWindowPos(hwnd_, nullptr, 0, 0, scaled(kInitialWidth), scaled(kInitialHeight),
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
    LoadOptions load_options;
    load_options.max_splats = max_splats_for(settings_.quality);
    LoadResult loaded = load_splats(source, c, load_options, Deadline::after(kLoadTime));
    const CameraResult automatic =
        loaded.status == Status::Ok ? auto_camera(loaded.splats) : CameraResult{};
    if (loaded.status != Status::Ok || automatic.status != Status::Ok) {
        SetCursor(old_cursor);
        message(Text::CannotLoad, MB_ICONERROR);
        return 1;
    }
    try {
        splats_ = std::move(loaded.splats);
        const std::size_t step = splats_.size() / kPreviewSplats + 1;
        preview_.reserve(splats_.size() / step + 1);
        for (std::size_t i = 0; i < splats_.size(); i += step) {
            preview_.push_back(splats_[i]);
        }
    } catch (const std::bad_alloc&) {
        SetCursor(old_cursor);
        message(Text::CannotLoad, MB_ICONERROR);
        return 1;
    }

    auto_view_ = view_from_camera(automatic.camera);
    view_ = auto_view_;
    fingerprint_ = fingerprint(source);
    views_root_ = shellext::views_root();
    if (fingerprint_ && views_root_) {
        if (const auto saved = load_view(*views_root_, *fingerprint_)) {
            view_ = *saved;
        }
    }
    EnableWindow(GetDlgItem(hwnd_, kSave), fingerprint_ && views_root_);
    EnableWindow(GetDlgItem(hwnd_, kAutomatic), fingerprint_ && views_root_);
    SetCursor(old_cursor);
    set_status(text(Text::Hint));
    dirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return std::nullopt;
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
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                              0, 0, 10, 10, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatus)), inst, nullptr);
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
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    font_ = font;
}

RECT Viewer::picture_rect() const noexcept {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    rc.bottom = std::max(rc.top, rc.bottom - scaled(kMargin * 3 + kButtonHeight + kStatusHeight));
    return rc;
}

void Viewer::layout() noexcept {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int bottom = rc.bottom - scaled(kMargin);
    const int buttons_top = bottom - scaled(kButtonHeight);
    const int status_top = buttons_top - scaled(kMargin) - scaled(kStatusHeight);
    MoveWindow(status_, scaled(kMargin), status_top, rc.right - 2 * scaled(kMargin),
               scaled(kStatusHeight), TRUE);

    const HDC dc = GetDC(hwnd_);
    const HGDIOBJ old = SelectObject(dc, font_);
    int x = scaled(kMargin);
    for (std::size_t i = 0; i < kButtons.size(); ++i) {
        const wchar_t* label = text(kButtons[i].second);
        SIZE size{};
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size);
        const int width = size.cx + scaled(kButtonPadding);
        MoveWindow(buttons_[i], x, buttons_top, width, scaled(kButtonHeight), TRUE);
        x += width + scaled(kMargin) / 2;
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
    } else if (side > 0) {
        const HBRUSH bg = CreateSolidBrush(RGB(46, 46, 46));
        r = {x0, y0, x0 + side, y0 + side};
        FillRect(dc, &r, bg);
        DeleteObject(bg);
    }
    EndPaint(hwnd_, &ps);
}

void Viewer::save() noexcept {
    if (!fingerprint_ || !views_root_ || !save_view(*views_root_, *fingerprint_, view_)) {
        message(Text::CannotSave, MB_ICONERROR);
        return;
    }
    const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    refresh_thumbnail(file_);
    SetCursor(old);
    set_status(text(Text::Saved));
}

void Viewer::reset() noexcept {
    if (fingerprint_ && views_root_) {
        remove_view(*views_root_, *fingerprint_);
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
        info->ptMinTrackSize = {scaled(480), scaled(420)};
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
        if (splats_.empty() || dragging_) {
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
        } else if ((wp & MK_CONTROL) != 0) {
            next = roll(view_, -dx * kPi);
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
        if (splats_.empty()) {
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
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kAutomatic:
            reset();
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
    case WM_DESTROY:
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
