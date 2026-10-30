#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "splatthumb/byte_source.h"

namespace {

using splatthumb::MemoryByteSource;

TEST(MemoryByteSource, ReadsWithinBounds) {
    const std::vector<std::byte> data = {std::byte{1}, std::byte{2}, std::byte{3}};
    MemoryByteSource src(data);
    std::array<std::byte, 2> out{};
    EXPECT_EQ(src.size(), 3u);
    EXPECT_EQ(src.read_at(1, out), 2u);
    EXPECT_EQ(out[0], std::byte{2});
    EXPECT_EQ(out[1], std::byte{3});
}

TEST(MemoryByteSource, ShortReadAtEnd) {
    const std::vector<std::byte> data = {std::byte{1}, std::byte{2}, std::byte{3}};
    MemoryByteSource src(data);
    std::array<std::byte, 8> out{};
    EXPECT_EQ(src.read_at(2, out), 1u);
}

TEST(MemoryByteSource, OffsetPastEndReadsNothing) {
    const std::vector<std::byte> data = {std::byte{1}};
    MemoryByteSource src(data);
    std::array<std::byte, 4> out{};
    EXPECT_EQ(src.read_at(1, out), 0u);
    EXPECT_EQ(src.read_at(UINT64_MAX, out), 0u);
}

TEST(MemoryByteSource, EmptySource) {
    MemoryByteSource src({});
    std::array<std::byte, 4> out{};
    EXPECT_EQ(src.size(), 0u);
    EXPECT_EQ(src.read_at(0, out), 0u);
}

} // namespace
