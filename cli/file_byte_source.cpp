#include "file_byte_source.h"

#include <algorithm>
#include <limits>
#include <system_error>

namespace splatthumb::cli {

bool FileByteSource::open(const std::filesystem::path& path) noexcept {
    try {
        std::error_code ec;
        const std::uintmax_t file_size = std::filesystem::file_size(path, ec);
        if (ec) {
            return false;
        }
        file_.open(path, std::ios::binary | std::ios::in);
        if (!file_) {
            return false;
        }
        size_ = file_size;
        return true;
    } catch (...) {
        return false;
    }
}

std::size_t FileByteSource::read_at(std::uint64_t offset, std::span<std::byte> out) noexcept {
    try {
        if (offset >= size_ || out.empty() ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            return 0;
        }
        constexpr auto kMaxRead =
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
        const std::uint64_t remaining = size_ - offset;
        const auto want = static_cast<std::size_t>(
            std::min({static_cast<std::uint64_t>(out.size()), remaining, kMaxRead}));
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        if (!file_) {
            return 0;
        }
        file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(want));
        const std::streamsize got = file_.gcount();
        return got > 0 ? static_cast<std::size_t>(got) : 0;
    } catch (...) {
        return 0;
    }
}

} // namespace splatthumb::cli
