#pragma once

#include <windows.h>

#include <cstddef>
#include <span>

#include "splatthumb/spgm.h"

namespace splatthumb::shellext {

// Decodes an embedded preview image (from a .spgm file) with Windows' own PNG or JPEG
// decoder, the one named by `format` and no other, and scales it so that its longer side is
// at most `max_side` pixels, keeping the aspect ratio; images smaller than that are not
// enlarged. Transparent pixels are laid over SplatThumb's dark gray, so the result is opaque.
// Images wider or taller than limits::kMaxEmbeddedImageSide are refused before decoding
// their pixels. Returns S_OK and a new top-down 32-bit DIB section in *bitmap, or a failure
// HRESULT and nullptr. COM must be initialized on the calling thread.
HRESULT decode_embedded_image(std::span<const std::byte> data, EmbeddedImageFormat format,
                              UINT max_side, HBITMAP* bitmap) noexcept;

} // namespace splatthumb::shellext
