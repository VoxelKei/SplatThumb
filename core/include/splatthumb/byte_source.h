#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace splatthumb {

// The only way core reads file contents. Implementations wrap a memory buffer,
// a file (cli) or an IStream (shellext).
class ByteSource {
public:
    virtual ~ByteSource() = default;

    // Total size of the source in bytes.
    virtual std::uint64_t size() const noexcept = 0;

    // Reads up to out.size() bytes starting at offset and returns how many were read.
    // Returns 0 when offset is at or past the end, or on a read error.
    virtual std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept = 0;
};

// A ByteSource over a caller-owned buffer, which must outlive it.
class MemoryByteSource final : public ByteSource {
public:
    explicit MemoryByteSource(std::span<const std::byte> data) noexcept : data_(data) {}

    std::uint64_t size() const noexcept override;
    std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override;

private:
    std::span<const std::byte> data_;
};

} // namespace splatthumb
