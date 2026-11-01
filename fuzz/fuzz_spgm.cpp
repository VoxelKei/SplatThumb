// Fuzzes the .spgm reader that finds the embedded preview image. Nothing may crash, and an
// accepted image must lie inside the file, within the size limit, and start with the
// signature of the format it was reported as.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "splatthumb/limits.h"
#include "splatthumb/spgm.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    splatthumb::MemoryByteSource source(std::as_bytes(std::span<const std::uint8_t>(data, size)));
    const auto result = splatthumb::probe_spgm(source);
    if (result.probe != splatthumb::SpgmProbe::Ok) {
        return 0;
    }
    const auto& image = result.image;
    if (image.size == 0 || image.size > splatthumb::limits::kMaxEmbeddedImageBytes ||
        image.offset + image.size > size) {
        std::abort();
    }
    const auto bytes = splatthumb::read_spgm_image(source, image);
    if (!bytes || bytes->size() != image.size) {
        std::abort();
    }
    const std::uint8_t first = static_cast<std::uint8_t>((*bytes)[0]);
    const bool png = image.format == splatthumb::EmbeddedImageFormat::Png;
    if ((png && first != 0x89) || (!png && first != 0xFF)) {
        std::abort();
    }
    return 0;
}
