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

// Where the values of a Compressed file live. Chunk property offsets are within a chunk
// record; packed_* offsets are within a vertex record.
struct CompressedLayout {
    std::uint64_t chunk_data_offset = 0; // absolute file offset of the first chunk record
    std::size_t chunk_stride = 0;
    std::uint64_t chunk_count = 0;
    // min_x min_y min_z max_x max_y max_z
    std::array<std::size_t, 6> position{};
    // min_scale_x min_scale_y min_scale_z max_scale_x max_scale_y max_scale_z
    std::array<std::size_t, 6> scale{};
    // min_r min_g min_b max_r max_g max_b; older files have none, meaning 0..1.
    bool has_color_range = false;
    std::array<std::size_t, 6> color{};
    std::size_t packed_position = 0, packed_rotation = 0, packed_scale = 0, packed_color = 0;
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
    // Compressed only.
    CompressedLayout compressed;
    // Number of f_rest_* properties (0, 9, 24 or 45 for SH degree 0-3).
    std::size_t sh_rest_count = 0;
};

// Decides whether a parsed header describes a Gaussian splat file that fits in file_size bytes.
Classification classify(const PlyHeader& header, std::uint64_t file_size) noexcept;

// parse_header followed by classify.
Classification inspect(ByteSource& source) noexcept;

} // namespace splatthumb
