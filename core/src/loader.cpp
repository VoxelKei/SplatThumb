#include "splatthumb/loader.h"

#include <algorithm>
#include <cstdint>
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
    if (c.kind == SplatKind::NotSplat) {
        result.status = c.status;
        return result;
    }
    const bool compressed = c.kind == SplatKind::Compressed;
    const std::size_t stride = c.vertex_stride;
    const std::size_t chunk_stride = c.compressed.chunk_stride;
    if (stride == 0 || stride > limits::kMaxReadChunkBytes || c.vertex_count == 0 ||
        (compressed && (chunk_stride == 0 || chunk_stride > limits::kMaxReadChunkBytes))) {
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
        const std::size_t records_per_read = limits::kMaxReadChunkBytes / stride;
        std::vector<std::byte> buffer(std::min<std::uint64_t>(records_per_read, m) * stride);
        Status status = Status::Ok;

        // Compressed files: the chunk record of the last vertex decoded. Runs are read in
        // increasing order, so each chunk is read about once.
        std::vector<std::byte> chunk_record(compressed ? chunk_stride : 0);
        std::uint64_t loaded_chunk = UINT64_MAX;
        const auto decode = [&](std::uint64_t index, std::span<const std::byte> record,
                                Splat& s) -> bool {
            if (!compressed) {
                return decode_standard_record(record, c.layout, s);
            }
            const std::uint64_t chunk_index = index / limits::kCompressedChunkSize;
            if (chunk_index != loaded_chunk) {
                // classify() guaranteed chunk_count >= ceil(n / 256) and that every element
                // fits in the file, so this read is in bounds.
                const std::uint64_t at =
                    c.compressed.chunk_data_offset + chunk_index * chunk_stride;
                ++result.read_calls;
                if (source.read_at(at, chunk_record) != chunk_record.size()) {
                    status = Status::ReadError;
                    return false;
                }
                loaded_chunk = chunk_index;
            }
            return decode_compressed_record(record, chunk_record, c.compressed, s);
        };

        for_each_run(n, m, options, [&](Run run) {
            while (run.count > 0) {
                if (deadline.expired()) {
                    status = Status::Timeout;
                    return false;
                }
                const auto k = static_cast<std::size_t>(
                    std::min<std::uint64_t>(run.count, buffer.size() / stride));
                const std::span<std::byte> records(buffer.data(), k * stride);
                // classify() guaranteed offset + n * stride <= file size, so no overflow.
                const std::uint64_t offset = c.vertex_data_offset + run.first * stride;
                ++result.read_calls;
                if (source.read_at(offset, records) != records.size()) {
                    status = Status::ReadError;
                    return false;
                }
                for (std::size_t r = 0; r < k; ++r) {
                    Splat s;
                    if (decode(run.first + r, records.subspan(r * stride, stride), s)) {
                        result.splats.push_back(s);
                    } else if (status != Status::Ok) {
                        return false;
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
