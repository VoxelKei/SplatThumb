#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "splatthumb/byte_source.h"

namespace splatthumb {

// Identifies a file by its contents, so a saved view follows the file when it is moved or
// renamed. Computed from the size, the first bytes and evenly spaced samples (see limits),
// so files that differ only between the samples share a fingerprint.
struct Fingerprint {
    std::array<std::uint8_t, 16> bytes{};

    // 32 lowercase hex digits.
    std::string hex() const;

    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
};

// Returns nullopt when the source is empty or a read fails.
std::optional<Fingerprint> fingerprint(ByteSource& source) noexcept;

} // namespace splatthumb
