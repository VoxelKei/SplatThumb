#pragma once

#include <windows.h>

#include "resource.h"

namespace splatthumb::app {

// The application icon at `size` pixels, the nearest size in the .ico. Shared: Windows
// keeps it for the life of the process, so it is never destroyed.
inline HICON app_icon(int size) noexcept {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                         MAKEINTRESOURCEW(IDI_SPLATTHUMB), IMAGE_ICON, size,
                                         size, LR_SHARED));
}

} // namespace splatthumb::app
