#pragma once

// Builds small synthetic PLY files in memory. Tests never read PLY files from disk.

#include <cstddef>
#include <cstdint>
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

} // namespace splatthumb::test
