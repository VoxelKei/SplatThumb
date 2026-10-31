#pragma once

#include <windows.h>

#include "splatthumb/render.h"

namespace splatthumb::shellext {

// Copies a premultiplied RGBA image into a new 32-bit top-down DIB section with straight
// (not premultiplied) BGRA, which is what Explorer expects with WTSAT_ARGB, as in the
// Windows SDK thumbnail provider sample. Opaque images are unchanged by the conversion.
// Returns nullptr on failure. The caller owns the bitmap.
HBITMAP make_bitmap(const Image& image) noexcept;

} // namespace splatthumb::shellext
