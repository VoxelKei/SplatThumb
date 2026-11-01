#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "splatthumb/byte_source.h"

namespace splatthumb {

// Spatialograph Maker project files (.spgm). From version 3 on, the app stores a preview
// image right after a fixed 12-byte start:
//
//   "SGP\0" | u32 version (3) | u32 image size T | T bytes PNG or JPEG | u32 JSON size | ...
//
// (all integers little-endian). SplatThumb shows that image and reads nothing else: not the
// JSON settings and not the splats. Older versions have no image and get no thumbnail.

enum class EmbeddedImageFormat { Png, Jpeg };

enum class SpgmProbe {
    Ok,
    NotSpgm,            // too short, or not starting with "SGP\0"
    OldVersion,         // versions 1 and 2 have no image
    UnknownVersion,     // newer than this reader knows
    NoImage,            // image size 0
    ImageTooLarge,      // over limits::kMaxEmbeddedImageBytes
    Truncated,          // the image would run past the end of the file
    UnknownImageFormat, // neither a PNG nor a JPEG signature
    ReadError,
};

std::string_view to_string(SpgmProbe probe) noexcept;

struct SpgmImage {
    std::uint32_t version = 0;
    std::uint64_t offset = 0; // of the image within the file
    std::uint32_t size = 0;
    EmbeddedImageFormat format = EmbeddedImageFormat::Png;
};

struct SpgmResult {
    SpgmProbe probe = SpgmProbe::NotSpgm;
    SpgmImage image; // valid when probe is Ok
};

// Reads the first bytes of the source and finds the embedded image. Reads at most 12 bytes
// plus the image signature.
SpgmResult probe_spgm(ByteSource& source) noexcept;

// Reads the image bytes that probe_spgm found. nullopt on a short read or out of memory.
std::optional<std::vector<std::byte>> read_spgm_image(ByteSource& source,
                                                      const SpgmImage& image) noexcept;

} // namespace splatthumb
