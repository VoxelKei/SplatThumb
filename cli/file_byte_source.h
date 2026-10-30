#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

#include "splatthumb/byte_source.h"

namespace splatthumb::cli {

// Read-only ByteSource over a file. The file is never written.
class FileByteSource final : public ByteSource {
public:
    // Returns false if the file cannot be opened or its size cannot be determined.
    bool open(const std::filesystem::path& path) noexcept;

    std::uint64_t size() const noexcept override { return size_; }
    std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override;

private:
    std::ifstream file_;
    std::uint64_t size_ = 0;
};

} // namespace splatthumb::cli
