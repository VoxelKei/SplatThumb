#pragma once

#include <windows.h>

#include "splatthumb/render.h"

namespace splatthumb::shellext {

// Copies an RGBA image into a new 32-bit top-down DIB section (BGRA, premultiplied).
// Returns nullptr on failure. The caller owns the bitmap.
HBITMAP make_bitmap(const Image& image) noexcept;

} // namespace splatthumb::shellext
