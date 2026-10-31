// Randomly corrupts a valid file many times and checks that inspect() always returns
// without crashing. Run under ASan/UBSan in CI. Fixed seed so failures reproduce.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/classify.h"

namespace {

using namespace splatthumb::test;

std::vector<std::byte> mutate(std::vector<std::byte> bytes, std::mt19937& rng) {
    std::uniform_int_distribution<int> op_dist(0, 4);
    const int ops = std::uniform_int_distribution<int>(1, 4)(rng);
    for (int i = 0; i < ops; ++i) {
        if (bytes.empty()) {
            bytes.push_back(std::byte{'p'});
        }
        const std::size_t pos = std::uniform_int_distribution<std::size_t>(0, bytes.size() - 1)(rng);
        const auto value = static_cast<std::byte>(std::uniform_int_distribution<int>(0, 255)(rng));
        switch (op_dist(rng)) {
        case 0: bytes[pos] = value; break;
        case 1: bytes[pos] ^= std::byte{1} << std::uniform_int_distribution<int>(0, 7)(rng); break;
        case 2: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(pos), value); break;
        case 3: bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(pos)); break;
        case 4: bytes.resize(pos); break;
        }
    }
    return bytes;
}

TEST(Mutation, CorruptedFilesNeverCrash) {
    const std::vector<std::byte> original =
        make_file(standard_header(4), 4 * kStandardRecordSize);
    std::mt19937 rng(20261030);
    int accepted = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::vector<std::byte> bytes = mutate(original, rng);
        splatthumb::MemoryByteSource src(bytes);
        const splatthumb::Classification c = splatthumb::inspect(src);
        if (c.kind != splatthumb::SplatKind::NotSplat) {
            ++accepted;
            // Anything accepted must fit in the file.
            ASSERT_LE(c.vertex_data_offset + c.vertex_count * c.vertex_stride, bytes.size());
        }
    }
    // Some mutations (e.g. in the data section) leave the file valid.
    EXPECT_GT(accepted, 0);
}

} // namespace
