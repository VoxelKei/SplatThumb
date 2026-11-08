#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/camera.h"
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
    std::size_t max_splats = limits::kStandardRenderSplats;
    SampleMode mode = SampleMode::Blocks;
    std::size_t block_size = 100;
    // render_thumbnail only: when the file has more splats than max_splats, read on and
    // keep the most important ones (load_important_splats) instead of the even sample.
    bool importance = true;
};

struct LoadResult {
    Status status = Status::Ok;
    std::vector<Splat> splats;
    std::uint64_t records_read = 0;
    std::uint64_t records_skipped = 0; // read but rejected by decode_standard_record
    std::uint64_t read_calls = 0;
};

// Reads up to options.max_splats records of a Standard or Compressed file. Never reads
// outside the vertex and chunk data described by c, and stops with Timeout once the
// deadline passes.
LoadResult load_splats(ByteSource& source, const Classification& c, const LoadOptions& options,
                       const Deadline& deadline) noexcept;

// How much a splat adds to a picture: opacity times the area of its two largest axes.
// Larger, more opaque splats cover more of the image.
float splat_importance(const Splat& s) noexcept;

struct ImportantResult {
    Status status = Status::Ok;
    // At most max_splats splats, the most important of those read, in no particular order.
    std::vector<Splat> splats;
    std::uint64_t records_read = 0;
    std::uint64_t read_calls = 0;
    // True when every record was read before stop.
    bool complete = false;
};

// Reads the records of a Standard or Compressed file and keeps the max_splats most
// important ones (splat_importance). With a camera, splats it cannot see are left out.
// Records are read in pieces of at most limits::kMaxReadChunkBytes, in an order spread
// over the whole file, so stopping early still leaves an even mix. When stop passes,
// reading ends and status is still Ok with complete false. Memory stays at about twice
// max_splats splats. Never reads outside the vertex and chunk data described by c.
ImportantResult load_important_splats(ByteSource& source, const Classification& c,
                                      std::size_t max_splats, const Camera* camera,
                                      const Deadline& stop) noexcept;

// For a file that load_splats sampled (loaded.records_read < c.vertex_count): runs
// load_important_splats until stop or limits::kImportanceReadMs from now, whichever comes
// first, and swaps its pick into loaded.splats when it read the whole file or at least
// twice as many records as the sample. Otherwise (too little time, a read error, out of
// memory) loaded is left as it was. Returns the records the pass read when its pick was
// used, else 0.
std::uint64_t prefer_important_splats(ByteSource& source, const Classification& c,
                                      std::size_t max_splats, const Camera* camera,
                                      LoadResult& loaded, const Deadline& stop) noexcept;

} // namespace splatthumb
