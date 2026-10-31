#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"

namespace {

using splatthumb::MemoryByteSource;

std::vector<std::byte> pattern(std::size_t n, unsigned seed = 1) {
    std::vector<std::byte> v(n);
    std::uint32_t x = seed;
    for (auto& b : v) {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::byte>(x >> 24);
    }
    return v;
}

splatthumb::Fingerprint fp(const std::vector<std::byte>& data) {
    MemoryByteSource src(data);
    const auto r = splatthumb::fingerprint(src);
    EXPECT_TRUE(r.has_value());
    return r.value_or(splatthumb::Fingerprint{});
}

// Counts reads and bytes, to check the read cap.
class CountingSource final : public splatthumb::ByteSource {
public:
    explicit CountingSource(std::uint64_t size) : size_(size) {}
    std::uint64_t size() const noexcept override { return size_; }
    std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override {
        if (offset >= size_) {
            return 0;
        }
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), size_ - offset));
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = static_cast<std::byte>((offset + i) * 31);
        }
        bytes += n;
        max_end = std::max(max_end, offset + n);
        return n;
    }
    std::uint64_t bytes = 0;
    std::uint64_t max_end = 0;

private:
    std::uint64_t size_;
};

TEST(Fingerprint, HexIs32LowercaseDigits) {
    const auto hex = fp(pattern(100)).hex();
    ASSERT_EQ(hex.size(), 32u);
    for (const char c : hex) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << hex;
    }
}

TEST(Fingerprint, SameContentSameFingerprint) {
    EXPECT_EQ(fp(pattern(500'000)), fp(pattern(500'000)));
}

TEST(Fingerprint, EmptyHasNone) {
    std::vector<std::byte> empty;
    MemoryByteSource src(empty);
    EXPECT_FALSE(splatthumb::fingerprint(src).has_value());
}

TEST(Fingerprint, ChangesWithHeadSampleOrSize) {
    const auto base = pattern(500'000);
    const auto ref = fp(base);

    auto head = base;
    head[100] ^= std::byte{1};
    EXPECT_NE(fp(head), ref);

    // The last byte is always inside the last sample.
    auto tail = base;
    tail.back() ^= std::byte{1};
    EXPECT_NE(fp(tail), ref);

    auto longer = base;
    longer.push_back(std::byte{0});
    EXPECT_NE(fp(longer), ref);
}

TEST(Fingerprint, SmallFilesAreHashedWhole) {
    for (const std::size_t n : {1u, 4096u, 65536u, 65537u, 70000u}) {
        const auto base = pattern(n);
        for (const std::size_t at : {std::size_t{0}, n / 2, n - 1}) {
            auto changed = base;
            changed[at] ^= std::byte{0x80};
            EXPECT_NE(fp(changed), fp(base)) << n << " " << at;
        }
    }
}

TEST(Fingerprint, ReadsAtMostTheCapAndStaysInBounds) {
    constexpr std::uint64_t cap = splatthumb::limits::kFingerprintHeadBytes +
                                  splatthumb::limits::kFingerprintSamples *
                                      splatthumb::limits::kFingerprintSampleBytes;
    for (const std::uint64_t size : {std::uint64_t{70'000}, std::uint64_t{10'000'000},
                                     std::uint64_t{20'000'000'000}}) {
        CountingSource src(size);
        ASSERT_TRUE(splatthumb::fingerprint(src).has_value());
        EXPECT_LE(src.bytes, cap) << size;
        EXPECT_EQ(src.max_end, size) << size;
    }
}

TEST(Fingerprint, ReadErrorHasNone) {
    class Failing final : public splatthumb::ByteSource {
    public:
        std::uint64_t size() const noexcept override { return 1'000'000; }
        std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override {
            return offset == 0 ? out.size() : 0;
        }
    } src;
    EXPECT_FALSE(splatthumb::fingerprint(src).has_value());
}

} // namespace
