#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/classify.h"
#include "splatthumb/deadline.h"
#include "splatthumb/limits.h"
#include "splatthumb/splat.h"
#include "splatthumb/status.h"

namespace splatthumb {

enum class SampleMode {
    // Records at i * N / M. No bias, but one read per record.
    Uniform,
    // Evenly spaced runs of consecutive records. Far fewer reads.
    Blocks,
};

struct LoadOptions {
    std::size_t max_splats = limits::kMaxRenderSplats;
    SampleMode mode = SampleMode::Blocks;
    std::size_t block_size = 100;
};

struct LoadResult {
    Status status = Status::Ok;
    std::vector<Splat> splats;
    std::uint64_t records_read = 0;
    std::uint64_t records_skipped = 0; // read but rejected by decode_standard_record
    std::uint64_t read_calls = 0;
};

// Reads up to options.max_splats records of a Standard file. Never reads outside the
// vertex data described by c, and stops with Timeout once the deadline passes.
LoadResult load_splats(ByteSource& source, const Classification& c, const LoadOptions& options,
                       const Deadline& deadline) noexcept;

} // namespace splatthumb
