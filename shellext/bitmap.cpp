#include "bitmap.h"

#include <algorithm>
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

    auto* dst = static_cast<std::uint8_t*>(bits);
    const std::uint8_t* src = image.rgba.data();
    for (std::size_t i = 0; i < side * side * 4; i += 4) {
        const unsigned a = src[i + 3];
        // Undo premultiplication; color <= alpha, so the result stays within 255.
        const auto straight = [a](std::uint8_t c) -> std::uint8_t {
            if (a == 0 || a == 255) {
                return a == 0 ? 0 : c;
            }
            return static_cast<std::uint8_t>(std::min(255u, (c * 255u + a / 2) / a));
        };
        dst[i + 0] = straight(src[i + 2]);
        dst[i + 1] = straight(src[i + 1]);
        dst[i + 2] = straight(src[i + 0]);
        dst[i + 3] = static_cast<std::uint8_t>(a);
    }
    return bitmap;
}

} // namespace splatthumb::shellext
