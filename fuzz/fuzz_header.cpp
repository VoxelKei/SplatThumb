// Feeds arbitrary bytes to parse_header() and classify(). Any crash, sanitizer report or
// accepted file whose declared data does not fit in the input is a bug.

#include <cstddef>
#include <cstdint>
#include <span>

#include "splatthumb/classify.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    splatthumb::MemoryByteSource source(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
    const splatthumb::Classification c = splatthumb::inspect(source);
    if (c.kind != splatthumb::SplatKind::NotSplat) {
        if (c.vertex_count == 0 || c.vertex_data_offset > size ||
            c.vertex_count > (size - c.vertex_data_offset) / c.vertex_stride) {
            __builtin_trap();
        }
    }
    return 0;
}
