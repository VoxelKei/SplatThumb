#pragma once

// Builds small synthetic PLY files in memory. Tests never read PLY files from disk.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace splatthumb::test {

inline constexpr std::string_view kStandardVertexProperties =
    "property float x\n"
    "property float y\n"
    "property float z\n"
    "property float f_dc_0\n"
    "property float f_dc_1\n"
    "property float f_dc_2\n"
    "property float opacity\n"
    "property float scale_0\n"
    "property float scale_1\n"
    "property float scale_2\n"
    "property float rot_0\n"
    "property float rot_1\n"
    "property float rot_2\n"
    "property float rot_3\n";

inline constexpr std::size_t kStandardRecordSize = 14 * 4;

// "ply\nformat binary_little_endian 1.0\nelement vertex <count>\n" + properties + "end_header\n"
inline std::string standard_header(std::uint64_t count,
                                   std::string_view properties = kStandardVertexProperties) {
    std::string h = "ply\nformat binary_little_endian 1.0\nelement vertex ";
    h += std::to_string(count);
    h += "\n";
    h += properties;
    h += "end_header\n";
    return h;
}

// Header text followed by data_bytes zero bytes.
inline std::vector<std::byte> make_file(std::string_view header, std::size_t data_bytes) {
    std::vector<std::byte> bytes(header.size() + data_bytes, std::byte{0});
    for (std::size_t i = 0; i < header.size(); ++i) {
        bytes[i] = static_cast<std::byte>(header[i]);
    }
    return bytes;
}

// File values of one standard-layout record, in kStandardVertexProperties order.
struct RawSplat {
    float x = 0, y = 0, z = 0;
    float f_dc[3] = {0, 0, 0};
    float opacity = 5.0f; // sigmoid(5) ~ 0.99
    float scale[3] = {-3, -3, -3};
    float rot[4] = {1, 0, 0, 0};
};

inline void write_record(const RawSplat& s, std::byte* out) {
    const float v[14] = {s.x,        s.y,        s.z,        s.f_dc[0], s.f_dc[1],
                         s.f_dc[2],  s.opacity,  s.scale[0], s.scale[1], s.scale[2],
                         s.rot[0],   s.rot[1],   s.rot[2],   s.rot[3]};
    std::memcpy(out, v, sizeof v);
}

inline std::vector<std::byte> make_standard_file(std::span<const RawSplat> splats) {
    const std::string header = standard_header(splats.size());
    std::vector<std::byte> bytes = make_file(header, splats.size() * kStandardRecordSize);
    for (std::size_t i = 0; i < splats.size(); ++i) {
        write_record(splats[i], bytes.data() + header.size() + i * kStandardRecordSize);
    }
    return bytes;
}

// ---- PlayCanvas compressed PLY ----

inline constexpr std::string_view kChunkProperties =
    "property float min_x\nproperty float min_y\nproperty float min_z\n"
    "property float max_x\nproperty float max_y\nproperty float max_z\n"
    "property float min_scale_x\nproperty float min_scale_y\nproperty float min_scale_z\n"
    "property float max_scale_x\nproperty float max_scale_y\nproperty float max_scale_z\n";
inline constexpr std::string_view kChunkColorProperties =
    "property float min_r\nproperty float min_g\nproperty float min_b\n"
    "property float max_r\nproperty float max_g\nproperty float max_b\n";
inline constexpr std::string_view kPackedProperties =
    "property uint packed_position\nproperty uint packed_rotation\n"
    "property uint packed_scale\nproperty uint packed_color\n";

inline std::string compressed_header(std::uint64_t chunks, std::uint64_t vertices,
                                     bool color_range = true) {
    std::string h = "ply\nformat binary_little_endian 1.0\nelement chunk ";
    h += std::to_string(chunks);
    h += "\n";
    h += kChunkProperties;
    if (color_range) {
        h += kChunkColorProperties;
    }
    h += "element vertex ";
    h += std::to_string(vertices);
    h += "\n";
    h += kPackedProperties;
    h += "end_header\n";
    return h;
}

// One chunk's value ranges, in kChunkProperties (+ kChunkColorProperties) order.
struct RawChunk {
    float min_pos[3] = {-1, -1, -1};
    float max_pos[3] = {1, 1, 1};
    float min_scale[3] = {-4, -4, -4};
    float max_scale[3] = {-2, -2, -2};
    float min_color[3] = {0, 0, 0};
    float max_color[3] = {1, 1, 1};
};

// Fractions 0..1 packed as 11, 10 and 11 bits (position and scale).
inline std::uint32_t pack_11_10_11(float x, float y, float z) {
    const auto q = [](float t, std::uint32_t max) {
        return static_cast<std::uint32_t>(t * static_cast<float>(max) + 0.5f);
    };
    return (q(x, 2047) << 21) | (q(y, 1023) << 11) | q(z, 2047);
}

inline std::uint32_t pack_color(float r, float g, float b, float a) {
    const auto q = [](float t) { return static_cast<std::uint32_t>(t * 255.0f + 0.5f); };
    return (q(r) << 24) | (q(g) << 16) | (q(b) << 8) | q(a);
}

// Unit quaternion (w, x, y, z): index of the largest component, then the other three.
inline std::uint32_t pack_rotation(float w, float x, float y, float z) {
    const float q[4] = {w, x, y, z};
    std::uint32_t largest = 0;
    for (std::uint32_t i = 1; i < 4; ++i) {
        if (std::fabs(q[i]) > std::fabs(q[largest])) {
            largest = i;
        }
    }
    const float sign = q[largest] < 0 ? -1.0f : 1.0f;
    std::uint32_t packed = largest << 30;
    int shift = 20;
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        const float t = sign * q[i] / 1.41421356f + 0.5f;
        packed |= static_cast<std::uint32_t>(t * 1023.0f + 0.5f) << shift;
        shift -= 10;
    }
    return packed;
}

struct PackedSplat {
    std::uint32_t position = pack_11_10_11(0.5f, 0.5f, 0.5f);
    std::uint32_t rotation = pack_rotation(1, 0, 0, 0);
    std::uint32_t scale = pack_11_10_11(0.5f, 0.5f, 0.5f);
    std::uint32_t color = pack_color(0.5f, 0.5f, 0.5f, 1.0f);
};

inline std::vector<std::byte> make_compressed_file(std::span<const RawChunk> chunks,
                                                   std::span<const PackedSplat> splats,
                                                   bool color_range = true) {
    const std::string header = compressed_header(chunks.size(), splats.size(), color_range);
    const std::size_t chunk_size = (color_range ? 18 : 12) * 4;
    std::vector<std::byte> bytes =
        make_file(header, chunks.size() * chunk_size + splats.size() * 16);
    std::byte* out = bytes.data() + header.size();
    for (const RawChunk& c : chunks) {
        std::vector<float> v;
        v.insert(v.end(), c.min_pos, c.min_pos + 3);
        v.insert(v.end(), c.max_pos, c.max_pos + 3);
        v.insert(v.end(), c.min_scale, c.min_scale + 3);
        v.insert(v.end(), c.max_scale, c.max_scale + 3);
        if (color_range) {
            v.insert(v.end(), c.min_color, c.min_color + 3);
            v.insert(v.end(), c.max_color, c.max_color + 3);
        }
        std::memcpy(out, v.data(), v.size() * 4);
        out += v.size() * 4;
    }
    for (const PackedSplat& s : splats) {
        const std::uint32_t v[4] = {s.position, s.rotation, s.scale, s.color};
        std::memcpy(out, v, sizeof v);
        out += sizeof v;
    }
    return bytes;
}

} // namespace splatthumb::test
