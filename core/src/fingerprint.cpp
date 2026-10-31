#include "splatthumb/fingerprint.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#include "splatthumb/limits.h"
#include "splatthumb/sha256.h"

namespace splatthumb {

namespace {

// Changing what is hashed must change this tag, so old saved views stop matching
// instead of matching the wrong files.
constexpr std::string_view kTag = "splatthumb-fingerprint-1";

bool hash_range(ByteSource& source, std::uint64_t offset, std::size_t length,
                std::vector<std::byte>& buffer, Sha256& sha) noexcept {
    const std::span<std::byte> out(buffer.data(), length);
    if (source.read_at(offset, out) != length) {
        return false;
    }
    sha.update(out);
    return true;
}

} // namespace

std::string Fingerprint::hex() const {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        s.push_back(kDigits[b >> 4]);
        s.push_back(kDigits[b & 0xf]);
    }
    return s;
}

std::optional<Fingerprint> fingerprint(ByteSource& source) noexcept {
    const std::uint64_t size = source.size();
    if (size == 0) {
        return std::nullopt;
    }
    try {
        Sha256 sha;
        sha.update(std::as_bytes(std::span(kTag.data(), kTag.size())));
        std::array<std::byte, 8> size_le{};
        for (std::size_t i = 0; i < size_le.size(); ++i) {
            size_le[i] = static_cast<std::byte>(size >> (8 * i));
        }
        sha.update(size_le);

        std::vector<std::byte> buffer(limits::kFingerprintHeadBytes);
        const auto head = static_cast<std::size_t>(
            std::min<std::uint64_t>(size, limits::kFingerprintHeadBytes));
        if (!hash_range(source, 0, head, buffer, sha)) {
            return std::nullopt;
        }
        if (size > limits::kFingerprintHeadBytes) {
            // Samples from just after the head to the very end of the file.
            constexpr std::uint64_t piece = limits::kFingerprintSampleBytes;
            constexpr std::uint64_t count = limits::kFingerprintSamples;
            const std::uint64_t first = limits::kFingerprintHeadBytes;
            const std::uint64_t span = size - first;
            const auto length = static_cast<std::size_t>(std::min(span, piece));
            const std::uint64_t last = size - length;
            for (std::uint64_t i = 0; i < count; ++i) {
                // first + (last - first) * i / (count - 1); the product stays far below
                // 2^64 for any real file size.
                const std::uint64_t at = first + (last - first) / (count - 1) * i +
                                         (last - first) % (count - 1) * i / (count - 1);
                if (!hash_range(source, at, length, buffer, sha)) {
                    return std::nullopt;
                }
            }
        }
        const Sha256::Digest digest = sha.finish();
        Fingerprint fp;
        std::copy_n(digest.begin(), fp.bytes.size(), fp.bytes.begin());
        return fp;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

} // namespace splatthumb
