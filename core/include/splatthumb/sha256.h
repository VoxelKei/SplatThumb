#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace splatthumb {

// SHA-256 (FIPS 180-4). Used for content fingerprints, not for security decisions.
class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() noexcept;
    void update(std::span<const std::byte> data) noexcept;
    Digest finish() noexcept;

private:
    void block(const std::uint8_t* p) noexcept;

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0; // bytes
};

} // namespace splatthumb
