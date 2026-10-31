#include "splatthumb/ply_header.h"

#include <array>
#include <charconv>
#include <new>
#include <optional>
#include <string_view>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

constexpr std::size_t kMaxTokens = 6;

struct Tokens {
    std::array<std::string_view, kMaxTokens> items{};
    std::size_t count = 0;
    bool overflow = false;
};

Tokens split(std::string_view line) noexcept {
    Tokens tokens;
    std::size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
            ++pos;
        }
        if (pos == line.size()) {
            break;
        }
        std::size_t end = pos;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') {
            ++end;
        }
        if (tokens.count == kMaxTokens) {
            tokens.overflow = true;
            break;
        }
        tokens.items[tokens.count++] = line.substr(pos, end - pos);
        pos = end;
    }
    return tokens;
}

std::optional<ScalarType> parse_type(std::string_view s) noexcept {
    if (s == "char" || s == "int8") return ScalarType::Int8;
    if (s == "uchar" || s == "uint8") return ScalarType::UInt8;
    if (s == "short" || s == "int16") return ScalarType::Int16;
    if (s == "ushort" || s == "uint16") return ScalarType::UInt16;
    if (s == "int" || s == "int32") return ScalarType::Int32;
    if (s == "uint" || s == "uint32") return ScalarType::UInt32;
    if (s == "float" || s == "float32") return ScalarType::Float32;
    if (s == "double" || s == "float64") return ScalarType::Float64;
    return std::nullopt;
}

bool is_integer(ScalarType type) noexcept {
    return type != ScalarType::Float32 && type != ScalarType::Float64;
}

// Digits only: no sign, no whitespace, no trailing characters.
std::optional<std::uint64_t> parse_count(std::string_view s) noexcept {
    if (s.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

bool is_valid_line_char(char c) noexcept {
    return c == '\t' || (c >= 0x20 && c <= 0x7e);
}

Status parse_lines(std::string_view text, PlyHeader& header) {
    std::size_t pos = 0;
    std::size_t line_number = 0;
    bool format_seen = false;

    while (true) {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) {
            return text.size() >= limits::kMaxHeaderBytes ? Status::HeaderTooLarge
                                                          : Status::MissingEndHeader;
        }
        std::string_view line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() > limits::kMaxHeaderLineLength) {
            return Status::LineTooLong;
        }
        for (const char c : line) {
            if (!is_valid_line_char(c)) {
                return line_number == 0 ? Status::NotPly : Status::InvalidCharacter;
            }
        }

        const std::size_t this_line = line_number++;
        if (this_line == 0) {
            if (line != "ply") {
                return Status::NotPly;
            }
            continue;
        }

        const Tokens t = split(line);
        if (t.count == 0) {
            return Status::UnknownKeyword;
        }
        const std::string_view keyword = t.items[0];

        if (keyword == "comment" || keyword == "obj_info") {
            continue;
        }
        if (keyword == "format") {
            if (format_seen || !header.elements.empty()) {
                return Status::MissingFormat;
            }
            if (t.overflow || t.count != 3 || t.items[1] != "binary_little_endian" ||
                t.items[2] != "1.0") {
                return Status::UnsupportedFormat;
            }
            format_seen = true;
            continue;
        }
        if (!format_seen) {
            return Status::MissingFormat;
        }
        if (keyword == "end_header") {
            if (t.count != 1) {
                return Status::UnknownKeyword;
            }
            header.header_size = pos;
            return Status::Ok;
        }
        if (keyword == "element") {
            if (t.overflow || t.count != 3) {
                return Status::MalformedElement;
            }
            const std::string_view name = t.items[1];
            if (name.size() > limits::kMaxNameLength) {
                return Status::NameTooLong;
            }
            const auto count = parse_count(t.items[2]);
            if (!count) {
                return Status::MalformedElement;
            }
            if (header.find(name) != nullptr) {
                return Status::DuplicateName;
            }
            if (header.elements.size() == limits::kMaxElements) {
                return Status::TooManyElements;
            }
            PlyElement& element = header.elements.emplace_back();
            element.name = std::string(name);
            element.count = *count;
            continue;
        }
        if (keyword == "property") {
            if (header.elements.empty() || t.overflow) {
                return Status::MalformedProperty;
            }
            PlyElement& element = header.elements.back();
            PlyProperty property;
            std::string_view name;
            if (t.count >= 2 && t.items[1] == "list") {
                if (t.count != 5) {
                    return Status::MalformedProperty;
                }
                const auto count_type = parse_type(t.items[2]);
                const auto item_type = parse_type(t.items[3]);
                if (!count_type || !item_type) {
                    return Status::UnknownPropertyType;
                }
                if (!is_integer(*count_type)) {
                    return Status::MalformedProperty;
                }
                property.type = *item_type;
                property.is_list = true;
                name = t.items[4];
            } else {
                if (t.count != 3) {
                    return Status::MalformedProperty;
                }
                const auto type = parse_type(t.items[1]);
                if (!type) {
                    return Status::UnknownPropertyType;
                }
                property.type = *type;
                name = t.items[2];
            }
            if (name.size() > limits::kMaxNameLength) {
                return Status::NameTooLong;
            }
            if (element.find(name) != nullptr) {
                return Status::DuplicateName;
            }
            if (element.properties.size() == limits::kMaxPropertiesPerElement) {
                return Status::TooManyProperties;
            }
            property.name = std::string(name);
            if (property.is_list) {
                element.has_list = true;
            } else {
                // Cannot overflow: at most kMaxPropertiesPerElement * 8 bytes.
                property.offset = element.record_size;
                element.record_size += scalar_size(property.type);
            }
            element.properties.push_back(std::move(property));
            continue;
        }
        return Status::UnknownKeyword;
    }
}

} // namespace

std::size_t scalar_size(ScalarType type) noexcept {
    switch (type) {
    case ScalarType::Int8:
    case ScalarType::UInt8: return 1;
    case ScalarType::Int16:
    case ScalarType::UInt16: return 2;
    case ScalarType::Int32:
    case ScalarType::UInt32:
    case ScalarType::Float32: return 4;
    case ScalarType::Float64: return 8;
    }
    return 0;
}

const PlyProperty* PlyElement::find(std::string_view property_name) const noexcept {
    for (const PlyProperty& p : properties) {
        if (p.name == property_name) {
            return &p;
        }
    }
    return nullptr;
}

const PlyElement* PlyHeader::find(std::string_view element_name) const noexcept {
    for (const PlyElement& e : elements) {
        if (e.name == element_name) {
            return &e;
        }
    }
    return nullptr;
}

ParseResult parse_header(ByteSource& source) noexcept {
    ParseResult result;
    try {
        std::string buffer(limits::kMaxHeaderBytes, '\0');
        std::size_t filled = 0;
        while (filled < buffer.size()) {
            auto* dst = reinterpret_cast<std::byte*>(buffer.data() + filled);
            const std::size_t n = source.read_at(filled, {dst, buffer.size() - filled});
            if (n == 0) {
                break;
            }
            if (n > buffer.size() - filled) {
                result.status = Status::ReadError;
                return result;
            }
            filled += n;
        }
        if (filled < buffer.size() && filled < source.size()) {
            result.status = Status::ReadError;
            return result;
        }
        result.status = parse_lines(std::string_view(buffer.data(), filled), result.header);
    } catch (const std::bad_alloc&) {
        result.status = Status::OutOfMemory;
    }
    if (result.status != Status::Ok) {
        result.header = {};
    }
    return result;
}

} // namespace splatthumb
