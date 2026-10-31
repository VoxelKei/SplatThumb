// Puts a valid 3DGS header in front of arbitrary vertex data and runs the same path as
// Explorer: render_thumbnail() on a ByteSource. The header only varies in its layout, so
// the fuzzer spends its time on record values (NaN, infinities, huge scales, zero-length
// rotations). Any crash, sanitizer report or wrongly sized image is a bug; libFuzzer's
// -timeout catches runs that ignore the deadline.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"

namespace {

constexpr const char* kNormals = "property float nx\nproperty float ny\nproperty float nz\n";
constexpr const char* kCore =
    "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n";
constexpr const char* kRest =
    "property float opacity\n"
    "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
    "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n";

// First input byte: bit 0 adds normals, bit 1 adds three f_rest properties, bits 2-4 pick
// the thumbnail size, bit 5 switches to the PlayCanvas compressed layout (bit 0 then drops
// the chunk color range). Small sizes are favoured because large ones are slow under ASan. Second byte: low bits pick the sample mode.
struct Layout {
    bool compressed;
    bool normals;
    bool rest;
    int size;
    splatthumb::SampleMode mode;
    std::size_t stride;
};

Layout pick_layout(std::uint8_t a, std::uint8_t b) {
    constexpr int kMin = splatthumb::limits::kMinThumbnailSize;
    // 600 is above kMaxSupersampledSize, so it takes the path without supersampling.
    constexpr int kSizes[] = {kMin, kMin, kMin, kMin, 64, 64, 256, 600};
    Layout l{};
    l.compressed = (a & 32) != 0;
    l.normals = (a & 1) != 0;
    l.rest = (a & 2) != 0;
    l.size = kSizes[(a >> 2) & 7];
    l.mode = (b & 1) != 0 ? splatthumb::SampleMode::Uniform : splatthumb::SampleMode::Blocks;
    l.stride = (14 + (l.normals ? 3 : 0) + (l.rest ? 3 : 0)) * 4;
    return l;
}

constexpr std::size_t kChunkVertices = 256;

std::size_t chunk_bytes(const Layout& l) {
    return (l.normals ? 12 : 18) * 4; // compressed: normals bit means no color range
}

std::string compressed_header(const Layout& l, std::size_t chunks, std::size_t count) {
    std::string h = "ply\nformat binary_little_endian 1.0\nelement chunk ";
    h += std::to_string(chunks);
    h += "\n";
    for (const char* name : {"min_x", "min_y", "min_z", "max_x", "max_y", "max_z", "min_scale_x",
                             "min_scale_y", "min_scale_z", "max_scale_x", "max_scale_y",
                             "max_scale_z"}) {
        h += "property float ";
        h += name;
        h += "\n";
    }
    if (!l.normals) {
        for (const char* name : {"min_r", "min_g", "min_b", "max_r", "max_g", "max_b"}) {
            h += "property float ";
            h += name;
            h += "\n";
        }
    }
    h += "element vertex ";
    h += std::to_string(count);
    h += "\nproperty uint packed_position\nproperty uint packed_rotation\n"
         "property uint packed_scale\nproperty uint packed_color\nend_header\n";
    return h;
}

std::string header(const Layout& l, std::size_t count) {
    std::string h = "ply\nformat binary_little_endian 1.0\nelement vertex ";
    h += std::to_string(count);
    h += "\nproperty float x\nproperty float y\nproperty float z\n";
    if (l.normals) {
        h += kNormals;
    }
    h += kCore;
    if (l.rest) {
        h += "property float f_rest_0\nproperty float f_rest_1\nproperty float f_rest_2\n";
    }
    h += kRest;
    h += "end_header\n";
    return h;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) {
        return 0;
    }
    const Layout layout = pick_layout(data[0], data[1]);
    data += 2;
    size -= 2;
    std::size_t count = 0;
    std::string h;
    if (layout.compressed) {
        // Chunks first, then 16-byte vertices; as many vertices as the input covers.
        count = size / (16 + chunk_bytes(layout) / kChunkVertices + 1);
        while (count > 0 &&
               (count + kChunkVertices - 1) / kChunkVertices * chunk_bytes(layout) + count * 16 >
                   size) {
            --count;
        }
        if (count == 0) {
            return 0;
        }
        h = compressed_header(layout, (count + kChunkVertices - 1) / kChunkVertices, count);
    } else {
        count = size / layout.stride;
        if (count == 0) {
            return 0;
        }
        h = header(layout, count);
    }
    const std::size_t data_bytes =
        layout.compressed
            ? (count + kChunkVertices - 1) / kChunkVertices * chunk_bytes(layout) + count * 16
            : count * layout.stride;

    std::vector<std::byte> file(h.size() + data_bytes);
    std::memcpy(file.data(), h.data(), h.size());
    std::memcpy(file.data() + h.size(), data, data_bytes);

    splatthumb::MemoryByteSource source(file);
    splatthumb::RenderOptions options;
    options.size = layout.size;
    splatthumb::LoadOptions load;
    load.max_splats = 2000;
    load.mode = layout.mode;
    load.block_size = 7;
    const auto result = splatthumb::render_thumbnail(
        source, options, load, splatthumb::Deadline::after(std::chrono::seconds(1)));

    switch (result.status) {
    case splatthumb::Status::Ok:
        if (result.image.size != layout.size ||
            result.image.rgba.size() != static_cast<std::size_t>(layout.size) *
                                            static_cast<std::size_t>(layout.size) * 4) {
            __builtin_trap();
        }
        break;
    case splatthumb::Status::Timeout:
    case splatthumb::Status::NothingToDraw:
    case splatthumb::Status::OutOfMemory:
        break;
    default:
        // The header is always a valid standard 3DGS header and the data always fits, so
        // any header, classification or read failure is a bug.
        __builtin_trap();
    }
    return 0;
}
