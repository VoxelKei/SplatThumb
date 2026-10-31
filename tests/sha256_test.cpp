#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "splatthumb/sha256.h"

namespace {

std::string hex(const splatthumb::Sha256::Digest& d) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    for (const auto b : d) {
        s += kDigits[b >> 4];
        s += kDigits[b & 0xf];
    }
    return s;
}

std::string sha(std::string_view text) {
    splatthumb::Sha256 h;
    h.update(std::as_bytes(std::span(text.data(), text.size())));
    return hex(h.finish());
}

// Test vectors from FIPS 180-4 examples and NIST CAVP.
TEST(Sha256, KnownVectors) {
    EXPECT_EQ(sha(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha(std::string(1'000'000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, SplitUpdatesMatchOneUpdate) {
    std::vector<std::byte> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i * 7 + 3);
    }
    splatthumb::Sha256 whole;
    whole.update(data);
    const auto expected = whole.finish();
    for (const std::size_t split : {1u, 55u, 56u, 63u, 64u, 65u, 500u}) {
        splatthumb::Sha256 parts;
        parts.update(std::span(data).first(split));
        parts.update(std::span(data).subspan(split));
        EXPECT_EQ(parts.finish(), expected) << split;
    }
}

} // namespace
