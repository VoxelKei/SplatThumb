#include "splatthumb/spgm.h"

#include <array>
#include <cstring>
#include <new>
#include <span>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

constexpr std::array<std::byte, 4> kMagic = {std::byte{'S'}, std::byte{'G'}, std::byte{'P'},
                                             std::byte{0}};
constexpr std::uint32_t kImageVersion = 3;
constexpr std::uint64_t kImageOffset = 12;

constexpr std::array<std::byte, 8> kPngSignature = {
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
constexpr std::array<std::byte, 3> kJpegSignature = {std::byte{0xFF}, std::byte{0xD8},
                                                     std::byte{0xFF}};

std::uint32_t read_u32_le(const std::byte* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

template <std::size_t N>
bool starts_with(std::span<const std::byte> data, const std::array<std::byte, N>& prefix) noexcept {
    return data.size() >= N && std::memcmp(data.data(), prefix.data(), N) == 0;
}

} // namespace

std::string_view to_string(SpgmProbe probe) noexcept {
    switch (probe) {
    case SpgmProbe::Ok:
        return "ok";
    case SpgmProbe::NotSpgm:
        return "not a Spatialograph Maker project";
    case SpgmProbe::OldVersion:
        return "project saved before version 3, so it has no embedded image";
    case SpgmProbe::UnknownVersion:
        return "project version newer than this reader";
    case SpgmProbe::NoImage:
        return "project has no embedded image";
    case SpgmProbe::ImageTooLarge:
        return "embedded image is too large";
    case SpgmProbe::Truncated:
        return "embedded image runs past the end of the file";
    case SpgmProbe::UnknownImageFormat:
        return "embedded image is neither PNG nor JPEG";
    case SpgmProbe::ReadError:
        return "read error";
    }
    return "unknown";
}

SpgmResult probe_spgm(ByteSource& source) noexcept {
    SpgmResult result;
    std::array<std::byte, kImageOffset> start{};
    const std::uint64_t file_size = source.size();
    if (file_size < start.size() || source.read_at(0, start) != start.size()) {
        result.probe = file_size < start.size() ? SpgmProbe::NotSpgm : SpgmProbe::ReadError;
        return result;
    }
    if (!starts_with(std::span<const std::byte>(start), kMagic)) {
        result.probe = SpgmProbe::NotSpgm;
        return result;
    }
    const std::uint32_t version = read_u32_le(start.data() + 4);
    const std::uint32_t image_size = read_u32_le(start.data() + 8);
    result.image.version = version;
    if (version < kImageVersion) {
        result.probe = SpgmProbe::OldVersion;
        return result;
    }
    if (version > kImageVersion) {
        result.probe = SpgmProbe::UnknownVersion;
        return result;
    }
    if (image_size == 0) {
        result.probe = SpgmProbe::NoImage;
        return result;
    }
    if (image_size > limits::kMaxEmbeddedImageBytes) {
        result.probe = SpgmProbe::ImageTooLarge;
        return result;
    }
    // Both sides are small, so the sum cannot overflow.
    if (kImageOffset + image_size > file_size) {
        result.probe = SpgmProbe::Truncated;
        return result;
    }
    std::array<std::byte, kPngSignature.size()> signature{};
    const std::size_t want = image_size < signature.size() ? image_size : signature.size();
    if (source.read_at(kImageOffset, std::span<std::byte>(signature.data(), want)) != want) {
        result.probe = SpgmProbe::ReadError;
        return result;
    }
    const std::span<const std::byte> head(signature.data(), want);
    if (starts_with(head, kPngSignature)) {
        result.image.format = EmbeddedImageFormat::Png;
    } else if (starts_with(head, kJpegSignature)) {
        result.image.format = EmbeddedImageFormat::Jpeg;
    } else {
        result.probe = SpgmProbe::UnknownImageFormat;
        return result;
    }
    result.image.offset = kImageOffset;
    result.image.size = image_size;
    result.probe = SpgmProbe::Ok;
    return result;
}

std::optional<std::vector<std::byte>> read_spgm_image(ByteSource& source,
                                                      const SpgmImage& image) noexcept {
    if (image.size == 0 || image.size > limits::kMaxEmbeddedImageBytes) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    try {
        bytes.resize(image.size);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t n =
            source.read_at(image.offset + done, std::span<std::byte>(bytes).subspan(done));
        if (n == 0) {
            return std::nullopt;
        }
        done += n;
    }
    return bytes;
}

} // namespace splatthumb
