#pragma once

#include <windows.h>

#include <objidl.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "splatthumb/byte_source.h"

namespace splatthumb::shellext {

// Reads an IStream through the ByteSource interface. Holds a reference to the stream.
// The size is taken once, when the source is created.
class StreamByteSource final : public ByteSource {
public:
    explicit StreamByteSource(IStream* stream) noexcept;
    ~StreamByteSource() override;
    StreamByteSource(const StreamByteSource&) = delete;
    StreamByteSource& operator=(const StreamByteSource&) = delete;

    // False when the stream size could not be read; the source then has size 0.
    bool ok() const noexcept { return ok_; }

    std::uint64_t size() const noexcept override { return size_; }
    std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override;

private:
    IStream* stream_;
    std::uint64_t size_ = 0;
    bool ok_ = false;
};

} // namespace splatthumb::shellext
