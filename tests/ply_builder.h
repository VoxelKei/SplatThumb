#pragma once

// Builds small synthetic PLY files in memory. Tests never read PLY files from disk.

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

} // namespace splatthumb::test
