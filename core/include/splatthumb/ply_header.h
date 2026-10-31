#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/status.h"

namespace splatthumb {

enum class ScalarType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

std::size_t scalar_size(ScalarType type) noexcept;

struct PlyProperty {
    std::string name;
    ScalarType type = ScalarType::Float32;
    bool is_list = false;
    // Byte offset within a record. Only meaningful while the element has no list property.
    std::size_t offset = 0;
};

struct PlyElement {
    std::string name;
    std::uint64_t count = 0;
    std::vector<PlyProperty> properties;
    // Sum of property sizes. Only meaningful when has_list is false.
    std::size_t record_size = 0;
    bool has_list = false;

    // Returns the property with this name, or nullptr.
    const PlyProperty* find(std::string_view property_name) const noexcept;
};

struct PlyHeader {
    // Bytes from the start of the file up to and including the end_header line break.
    std::size_t header_size = 0;
    std::vector<PlyElement> elements;

    const PlyElement* find(std::string_view element_name) const noexcept;
};

struct ParseResult {
    Status status = Status::Ok;
    PlyHeader header;
};

// Parses a binary little-endian PLY header from the start of source.
// Reads at most limits::kMaxHeaderBytes and never throws.
ParseResult parse_header(ByteSource& source) noexcept;

} // namespace splatthumb
