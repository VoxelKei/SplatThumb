#include "png_writer.h"

#include <cstdint>
#include <fstream>
#include <vector>

// stb_image_write is third-party code: built here only, without our warning flags,
// encoding to memory so it never touches the file system itself.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace splatthumb::cli {

namespace {

void append(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<unsigned char>*>(context);
    const auto* bytes = static_cast<const unsigned char*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool write_png(const std::filesystem::path& path, const Image& img) noexcept {
    try {
        if (img.size <= 0 ||
            img.rgba.size() != static_cast<std::size_t>(img.size) * static_cast<std::size_t>(img.size) * 4) {
            return false;
        }
        // PNG stores straight alpha.
        std::vector<unsigned char> straight(img.rgba.begin(), img.rgba.end());
        for (std::size_t i = 0; i < straight.size(); i += 4) {
            const unsigned a = straight[i + 3];
            for (std::size_t ch = 0; ch < 3; ++ch) {
                straight[i + ch] = a == 0 ? 0 : static_cast<unsigned char>(
                                                    (straight[i + ch] * 255u + a / 2) / a);
            }
        }
        std::vector<unsigned char> encoded;
        if (stbi_write_png_to_func(append, &encoded, img.size, img.size, 4, straight.data(),
                                   img.size * 4) == 0) {
            return false;
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(encoded.data()),
                   static_cast<std::streamsize>(encoded.size()));
        return static_cast<bool>(file);
    } catch (...) {
        return false;
    }
}

} // namespace splatthumb::cli
