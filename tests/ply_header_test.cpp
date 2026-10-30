#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/limits.h"
#include "splatthumb/ply_header.h"

namespace {

using splatthumb::MemoryByteSource;
using splatthumb::ParseResult;
using splatthumb::ScalarType;
using splatthumb::Status;
using namespace splatthumb::test;

ParseResult parse(const std::string& text) {
    const std::vector<std::byte> bytes = make_file(text, 0);
    MemoryByteSource src(bytes);
    return splatthumb::parse_header(src);
}

Status parse_status(const std::string& text) {
    return parse(text).status;
}

const std::string kPrefix = "ply\nformat binary_little_endian 1.0\n";

TEST(ParseHeader, ParsesElementsAndProperties) {
    const std::string text = kPrefix +
                             "element vertex 7\nproperty float x\nproperty uchar r\n"
                             "element face 2\nproperty list uchar int vertex_indices\n"
                             "end_header\n";
    const ParseResult r = parse(text);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.header.header_size, text.size());
    ASSERT_EQ(r.header.elements.size(), 2u);
    const auto& v = r.header.elements[0];
    EXPECT_EQ(v.name, "vertex");
    EXPECT_EQ(v.count, 7u);
    EXPECT_EQ(v.record_size, 5u);
    EXPECT_FALSE(v.has_list);
    EXPECT_EQ(v.properties[1].type, ScalarType::UInt8);
    EXPECT_EQ(v.properties[1].offset, 4u);
    EXPECT_TRUE(r.header.elements[1].has_list);
}

TEST(ParseHeader, TypeAliases) {
    const ParseResult r = parse(kPrefix +
                                "element vertex 1\nproperty int8 a\nproperty uint16 b\n"
                                "property int32 c\nproperty float64 d\nend_header\n");
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.header.elements[0].record_size, 1u + 2u + 4u + 8u);
}

TEST(ParseHeader, HeaderSizeStopsAtEndHeader) {
    const std::string text = kPrefix + "element vertex 1\nproperty float x\nend_header\n";
    const std::vector<std::byte> bytes = make_file(text, 4);
    MemoryByteSource src(bytes);
    EXPECT_EQ(splatthumb::parse_header(src).header.header_size, text.size());
}

TEST(ParseHeader, EmptyFile) {
    EXPECT_EQ(parse_status(""), Status::MissingEndHeader);
}

TEST(ParseHeader, NotPly) {
    EXPECT_EQ(parse_status("PLY\nformat binary_little_endian 1.0\nend_header\n"), Status::NotPly);
    EXPECT_EQ(parse_status("plyx\n"), Status::NotPly);
    EXPECT_EQ(parse_status(std::string("\x89PNG\r\n\x1a\n", 8)), Status::NotPly);
}

TEST(ParseHeader, UnsupportedFormats) {
    EXPECT_EQ(parse_status("ply\nformat ascii 1.0\nend_header\n"), Status::UnsupportedFormat);
    EXPECT_EQ(parse_status("ply\nformat binary_big_endian 1.0\nend_header\n"),
              Status::UnsupportedFormat);
    EXPECT_EQ(parse_status("ply\nformat binary_little_endian 2.0\nend_header\n"),
              Status::UnsupportedFormat);
}

TEST(ParseHeader, FormatMustComeBeforeElements) {
    EXPECT_EQ(parse_status("ply\nelement vertex 1\nend_header\n"), Status::MissingFormat);
    EXPECT_EQ(parse_status("ply\nend_header\n"), Status::MissingFormat);
    EXPECT_EQ(parse_status(kPrefix + "format binary_little_endian 1.0\nend_header\n"),
              Status::MissingFormat);
}

TEST(ParseHeader, MissingEndHeader) {
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty float x\n"),
              Status::MissingEndHeader);
    EXPECT_EQ(parse_status(kPrefix + "end_header"), Status::MissingEndHeader);
}

TEST(ParseHeader, HeaderLargerThanLimit) {
    std::string text = kPrefix;
    while (text.size() < splatthumb::limits::kMaxHeaderBytes) {
        text += "comment padding padding padding padding padding padding\n";
    }
    text += "end_header\n";
    EXPECT_EQ(parse_status(text), Status::HeaderTooLarge);
}

TEST(ParseHeader, LineTooLong) {
    const std::string text =
        kPrefix + "comment " + std::string(splatthumb::limits::kMaxHeaderLineLength, 'a') +
        "\nend_header\n";
    EXPECT_EQ(parse_status(text), Status::LineTooLong);
}

TEST(ParseHeader, InvalidCharacters) {
    EXPECT_EQ(parse_status(kPrefix + std::string("comment a\0b\nend_header\n", 21)),
              Status::InvalidCharacter);
    EXPECT_EQ(parse_status(kPrefix + "comment caf\xc3\xa9\nend_header\n"),
              Status::InvalidCharacter);
}

TEST(ParseHeader, MalformedElementCounts) {
    for (const char* count : {"-1", "abc", "1x", "", "18446744073709551616", "+5", "0x10"}) {
        EXPECT_EQ(parse_status(kPrefix + "element vertex " + count + "\nend_header\n"),
                  Status::MalformedElement)
            << "count: " << count;
    }
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1 2\nend_header\n"),
              Status::MalformedElement);
}

TEST(ParseHeader, LargestCountParses) {
    const ParseResult r = parse(kPrefix + "element vertex 18446744073709551615\nend_header\n");
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.header.elements[0].count, UINT64_MAX);
}

TEST(ParseHeader, MalformedProperties) {
    EXPECT_EQ(parse_status(kPrefix + "property float x\nend_header\n"),
              Status::MalformedProperty);
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty float\nend_header\n"),
              Status::MalformedProperty);
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty list uchar x\nend_header\n"),
              Status::MalformedProperty);
    EXPECT_EQ(
        parse_status(kPrefix + "element vertex 1\nproperty list float int x\nend_header\n"),
        Status::MalformedProperty);
}

TEST(ParseHeader, UnknownPropertyType) {
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty half x\nend_header\n"),
              Status::UnknownPropertyType);
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty list uchar bogus x\nend_header\n"),
              Status::UnknownPropertyType);
}

TEST(ParseHeader, UnknownKeyword) {
    EXPECT_EQ(parse_status(kPrefix + "elements vertex 1\nend_header\n"), Status::UnknownKeyword);
    EXPECT_EQ(parse_status(kPrefix + "\nend_header\n"), Status::UnknownKeyword);
    EXPECT_EQ(parse_status(kPrefix + "end_header now\n"), Status::UnknownKeyword);
}

TEST(ParseHeader, DuplicateNames) {
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nelement vertex 1\nend_header\n"),
              Status::DuplicateName);
    EXPECT_EQ(parse_status(kPrefix +
                           "element vertex 1\nproperty float x\nproperty float x\nend_header\n"),
              Status::DuplicateName);
}

TEST(ParseHeader, NameTooLong) {
    const std::string name(splatthumb::limits::kMaxNameLength + 1, 'n');
    EXPECT_EQ(parse_status(kPrefix + "element " + name + " 1\nend_header\n"), Status::NameTooLong);
    EXPECT_EQ(parse_status(kPrefix + "element vertex 1\nproperty float " + name + "\nend_header\n"),
              Status::NameTooLong);
}

TEST(ParseHeader, TooManyElements) {
    std::string text = kPrefix;
    for (std::size_t i = 0; i <= splatthumb::limits::kMaxElements; ++i) {
        text += "element e" + std::to_string(i) + " 1\n";
    }
    EXPECT_EQ(parse_status(text + "end_header\n"), Status::TooManyElements);
}

TEST(ParseHeader, TooManyProperties) {
    std::string text = kPrefix + "element vertex 1\n";
    for (std::size_t i = 0; i <= splatthumb::limits::kMaxPropertiesPerElement; ++i) {
        text += "property float p" + std::to_string(i) + "\n";
    }
    EXPECT_EQ(parse_status(text + "end_header\n"), Status::TooManyProperties);
}

// A source that claims more bytes than it delivers.
class ShortReadSource final : public splatthumb::ByteSource {
public:
    std::uint64_t size() const noexcept override { return 1000; }
    std::size_t read_at(std::uint64_t, std::span<std::byte>) noexcept override { return 0; }
};

TEST(ParseHeader, ReadErrorIsReported) {
    ShortReadSource src;
    EXPECT_EQ(splatthumb::parse_header(src).status, Status::ReadError);
}

// A source that reports reading more than was asked.
class OverReadSource final : public splatthumb::ByteSource {
public:
    std::uint64_t size() const noexcept override { return 1u << 20; }
    std::size_t read_at(std::uint64_t, std::span<std::byte> out) noexcept override {
        return out.size() + 1;
    }
};

TEST(ParseHeader, OverlongReadIsRejected) {
    OverReadSource src;
    EXPECT_EQ(splatthumb::parse_header(src).status, Status::ReadError);
}

} // namespace
