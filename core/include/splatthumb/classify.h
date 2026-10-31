#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "splatthumb/byte_source.h"
#include "splatthumb/ply_header.h"
#include "splatthumb/status.h"

namespace splatthumb {

enum class SplatKind {
    NotSplat,
    // INRIA-style 3DGS PLY: x y z f_dc_* opacity scale_* rot_* as float.
    Standard,
    // PlayCanvas compressed PLY: chunk element plus packed_* vertex properties.
    Compressed,
};

// Byte offsets within a vertex record of a Standard file.
struct StandardLayout {
    std::size_t x = 0, y = 0, z = 0;
    std::array<std::size_t, 3> f_dc{};
    std::size_t opacity = 0;
    std::array<std::size_t, 3> scale{};
    std::array<std::size_t, 4> rot{};
};

struct Classification {
    SplatKind kind = SplatKind::NotSplat;
    // Ok when kind is not NotSplat; otherwise the reason.
    Status status = Status::Ok;

    std::uint64_t vertex_count = 0;
    // Absolute file offset of the first vertex record, and the record size.
    std::uint64_t vertex_data_offset = 0;
    std::size_t vertex_stride = 0;

    // Standard only.
    StandardLayout layout;
    // Number of f_rest_* properties (0, 9, 24 or 45 for SH degree 0-3).
    std::size_t sh_rest_count = 0;
};

// Decides whether a parsed header describes a Gaussian splat file that fits in file_size bytes.
Classification classify(const PlyHeader& header, std::uint64_t file_size) noexcept;

// parse_header followed by classify.
Classification inspect(ByteSource& source) noexcept;

} // namespace splatthumb
