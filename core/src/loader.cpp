#include "splatthumb/loader.h"

#include <algorithm>
#include <new>
#include <span>

namespace splatthumb {

namespace {

struct Run {
    std::uint64_t first = 0;
    std::uint64_t count = 0;
};

// Calls f(run) for each run of record indices to read, in increasing order.
// Returns false if f does.
template <typename F>
bool for_each_run(std::uint64_t n, std::uint64_t m, const LoadOptions& options, F&& f) {
    if (m == n) {
        return f(Run{0, n});
    }
    if (options.mode == SampleMode::Uniform || options.block_size <= 1) {
        // m < n <= kMaxVertexCount, so i * n fits easily in 64 bits.
        for (std::uint64_t i = 0; i < m; ++i) {
            if (!f(Run{i * n / m, 1})) {
                return false;
            }
        }
        return true;
    }
    const std::uint64_t block = options.block_size;
    const std::uint64_t blocks = (m + block - 1) / block;
    for (std::uint64_t j = 0; j < blocks; ++j) {
        // Block starts are at least n / blocks > block apart, so runs never overlap.
        const std::uint64_t first = j * n / blocks;
        const std::uint64_t count = std::min({block, m - j * block, n - first});
        if (!f(Run{first, count})) {
            return false;
        }
    }
    return true;
}

} // namespace

LoadResult load_splats(ByteSource& source, const Classification& c, const LoadOptions& options,
                       const Deadline& deadline) noexcept {
    LoadResult result;
    if (c.kind != SplatKind::Standard) {
        result.status = c.kind == SplatKind::NotSplat ? c.status : Status::UnsupportedKind;
        return result;
    }
    const std::size_t stride = c.vertex_stride;
    if (stride == 0 || stride > limits::kMaxReadChunkBytes || c.vertex_count == 0) {
        result.status = Status::NothingToDraw;
        return result;
    }
    const std::uint64_t n = c.vertex_count;
    const std::uint64_t m = std::min<std::uint64_t>(n, std::min(options.max_splats,
                                                                limits::kMaxRenderSplats));
    if (m == 0) {
        result.status = Status::NothingToDraw;
        return result;
    }

    try {
        result.splats.reserve(static_cast<std::size_t>(m));
        const std::size_t records_per_chunk = limits::kMaxReadChunkBytes / stride;
        std::vector<std::byte> buffer(std::min<std::uint64_t>(records_per_chunk, m) * stride);
        Status status = Status::Ok;

        for_each_run(n, m, options, [&](Run run) {
            while (run.count > 0) {
                if (deadline.expired()) {
                    status = Status::Timeout;
                    return false;
                }
                const auto k = static_cast<std::size_t>(
                    std::min<std::uint64_t>(run.count, buffer.size() / stride));
                const std::span<std::byte> chunk(buffer.data(), k * stride);
                // classify() guaranteed offset + n * stride <= file size, so no overflow.
                const std::uint64_t offset = c.vertex_data_offset + run.first * stride;
                ++result.read_calls;
                if (source.read_at(offset, chunk) != chunk.size()) {
                    status = Status::ReadError;
                    return false;
                }
                for (std::size_t r = 0; r < k; ++r) {
                    Splat s;
                    if (decode_standard_record(chunk.subspan(r * stride, stride), c.layout, s)) {
                        result.splats.push_back(s);
                    } else {
                        ++result.records_skipped;
                    }
                }
                result.records_read += k;
                run.first += k;
                run.count -= k;
            }
            return true;
        });

        if (status == Status::Ok && result.splats.empty()) {
            status = Status::NothingToDraw;
        }
        result.status = status;
    } catch (const std::bad_alloc&) {
        result.status = Status::OutOfMemory;
    }
    if (result.status != Status::Ok) {
        result.splats.clear();
    }
    return result;
}

} // namespace splatthumb
