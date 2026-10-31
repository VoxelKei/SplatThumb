#include "bitmap.h"

#include <cstddef>
#include <cstdint>

namespace splatthumb::shellext {

HBITMAP make_bitmap(const Image& image) noexcept {
    const auto side = static_cast<std::size_t>(image.size);
    if (image.size <= 0 || image.rgba.size() != side * side * 4) {
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = image.size;
    info.bmiHeader.biHeight = -image.size; // negative: rows top to bottom
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        return nullptr;
    }

    // RGBA to BGRA.
    auto* dst = static_cast<std::uint8_t*>(bits);
    const std::uint8_t* src = image.rgba.data();
    for (std::size_t i = 0; i < side * side * 4; i += 4) {
        dst[i + 0] = src[i + 2];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 0];
        dst[i + 3] = src[i + 3];
    }
    return bitmap;
}

} // namespace splatthumb::shellext
