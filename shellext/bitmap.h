#pragma once

#include <windows.h>

#include "splatthumb/render.h"

namespace splatthumb::shellext {

// Copies a premultiplied RGBA image into a new 32-bit top-down DIB section as
// premultiplied BGRA. With WTSAT_ARGB, Explorer draws the alpha only if every pixel is
// validly premultiplied (no color above its alpha); otherwise it ignores the alpha and
// shows a black background with over-bright edges (seen on Windows 11, 2026-10-31).
// Returns nullptr on failure. The caller owns the bitmap.
HBITMAP make_bitmap(const Image& image) noexcept;

} // namespace splatthumb::shellext
