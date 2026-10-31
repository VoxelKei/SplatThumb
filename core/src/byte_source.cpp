#include "splatthumb/byte_source.h"

#include <algorithm>
#include <cstring>

namespace splatthumb {

std::uint64_t MemoryByteSource::size() const noexcept {
    return data_.size();
}

std::size_t MemoryByteSource::read_at(std::uint64_t offset, std::span<std::byte> out) noexcept {
    if (offset >= data_.size()) {
        return 0;
    }
    const auto start = static_cast<std::size_t>(offset);
    const std::size_t n = std::min(out.size(), data_.size() - start);
    if (n > 0) {
        std::memcpy(out.data(), data_.data() + start, n);
    }
    return n;
}

} // namespace splatthumb
