#include "splatthumb/loader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <new>
#include <numeric>
#include <span>
#include <utility>

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

// False when c cannot be read: no records, or a record or chunk larger than one read.
bool readable(const Classification& c) noexcept {
    const bool compressed = c.kind == SplatKind::Compressed;
    const std::size_t stride = c.vertex_stride;
    const std::size_t chunk_stride = c.compressed.chunk_stride;
    return c.kind != SplatKind::NotSplat && stride != 0 &&
           stride <= limits::kMaxReadChunkBytes && c.vertex_count != 0 &&
           (!compressed || (chunk_stride != 0 && chunk_stride <= limits::kMaxReadChunkBytes));
}

// Reads runs of records and decodes them. For compressed files it keeps the chunk record
// of the last vertex decoded, so runs read in increasing order read each chunk about once.
class RecordReader {
public:
    // c must pass readable(). Throws std::bad_alloc.
    RecordReader(ByteSource& source, const Classification& c)
        : source_(source), c_(c), compressed_(c.kind == SplatKind::Compressed),
          stride_(c.vertex_stride),
          chunk_record_(compressed_ ? c.compressed.chunk_stride : 0) {}

    // Largest number of records one read() may ask for.
    std::size_t max_records() const noexcept { return limits::kMaxReadChunkBytes / stride_; }

    // Reads records [first, first + count) into buffer (count <= max_records(), first +
    // count <= vertex_count) and calls on_splat(splat) for each record that decodes.
    // Returns Ok, or ReadError when the source comes up short.
    template <typename F>
    Status read(std::uint64_t first, std::size_t count, std::vector<std::byte>& buffer,
                std::uint64_t& skipped, F&& on_splat) {
        buffer.resize(count * stride_);
        const std::span<std::byte> records(buffer.data(), count * stride_);
        // classify() guaranteed offset + n * stride <= file size, so no overflow.
        const std::uint64_t offset = c_.vertex_data_offset + first * stride_;
        ++read_calls;
        if (source_.read_at(offset, records) != records.size()) {
            return Status::ReadError;
        }
        for (std::size_t r = 0; r < count; ++r) {
            Splat s;
            Status status = Status::Ok;
            if (decode(first + r, records.subspan(r * stride_, stride_), s, status)) {
                on_splat(s);
            } else if (status != Status::Ok) {
                return status;
            } else {
                ++skipped;
            }
        }
        return Status::Ok;
    }

    std::uint64_t read_calls = 0;

private:
    bool decode(std::uint64_t index, std::span<const std::byte> record, Splat& s,
                Status& status) {
        if (!compressed_) {
            return decode_standard_record(record, c_.layout, s);
        }
        const std::uint64_t chunk_index = index / limits::kCompressedChunkSize;
        if (chunk_index != loaded_chunk_) {
            // classify() guaranteed chunk_count >= ceil(n / 256) and that every element
            // fits in the file, so this read is in bounds.
            const std::uint64_t at =
                c_.compressed.chunk_data_offset + chunk_index * c_.compressed.chunk_stride;
            ++read_calls;
            if (source_.read_at(at, chunk_record_) != chunk_record_.size()) {
                status = Status::ReadError;
                return false;
            }
            loaded_chunk_ = chunk_index;
        }
        return decode_compressed_record(record, chunk_record_, c_.compressed, s);
    }

    ByteSource& source_;
    const Classification& c_;
    bool compressed_;
    std::size_t stride_;
    std::vector<std::byte> chunk_record_;
    std::uint64_t loaded_chunk_ = UINT64_MAX;
};

// A step through 0..count-1 (count > 0) that is coprime with count, near count / phi,
// so piece i * step % count visits every piece once and early pieces are spread out.
std::uint64_t spread_step(std::uint64_t count) noexcept {
    std::uint64_t step = std::max<std::uint64_t>(1, count * 618 / 1000);
    // Coprime numbers are dense; this ends within a few dozen tries for any count.
    while (std::gcd(step, count) != 1) {
        ++step;
    }
    return step;
}

} // namespace

LoadResult load_splats(ByteSource& source, const Classification& c, const LoadOptions& options,
                       const Deadline& deadline) noexcept {
    LoadResult result;
    if (c.kind == SplatKind::NotSplat) {
        result.status = c.status;
        return result;
    }
    if (!readable(c)) {
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
        RecordReader reader(source, c);
        std::vector<std::byte> buffer;
        Status status = Status::Ok;
        for_each_run(n, m, options, [&](Run run) {
            while (run.count > 0) {
                if (deadline.expired()) {
                    status = Status::Timeout;
                    return false;
                }
                const auto k = static_cast<std::size_t>(
                    std::min<std::uint64_t>(run.count, reader.max_records()));
                status = reader.read(run.first, k, buffer, result.records_skipped,
                                     [&](const Splat& s) { result.splats.push_back(s); });
                if (status != Status::Ok) {
                    return false;
                }
                result.records_read += k;
                run.first += k;
                run.count -= k;
            }
            return true;
        });
        result.read_calls = reader.read_calls;

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

float splat_importance(const Splat& s) noexcept {
    float a = s.scale[0], b = s.scale[1], c = s.scale[2];
    // Drop the smallest axis; what is left is the area the splat shows from most sides.
    const float smallest = std::min({a, b, c});
    const float area = smallest == a ? b * c : smallest == b ? a * c : a * b;
    return s.opacity * area;
}

ImportantResult load_important_splats(ByteSource& source, const Classification& c,
                                      std::size_t max_splats, const Camera* camera,
                                      const Deadline& stop) noexcept {
    ImportantResult result;
    if (c.kind == SplatKind::NotSplat) {
        result.status = c.status;
        return result;
    }
    const std::size_t m = std::min(max_splats, limits::kMaxRenderSplats);
    if (!readable(c) || m == 0) {
        result.status = Status::NothingToDraw;
        return result;
    }

    struct Scored {
        float score;
        Splat splat;
    };
    const auto higher = [](const Scored& l, const Scored& r) { return l.score > r.score; };

    try {
        RecordReader reader(source, c);
        // Up to 2m candidates are kept. When full, the best m stay and anything not above
        // the weakest of them is turned away from then on.
        std::vector<Scored> kept;
        kept.reserve(2 * m);
        float threshold = 0.0f;
        bool have_threshold = false;
        const auto shrink = [&] {
            std::nth_element(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(m - 1),
                             kept.end(), higher);
            kept.resize(m);
            threshold = kept[m - 1].score;
            have_threshold = true;
        };
        const auto offer = [&](const Splat& s) {
            if (camera != nullptr && !may_be_visible(s, *camera)) {
                return;
            }
            const float score = splat_importance(s);
            // decode_* guarantee finite, positive scale and opacity, so score is not NaN.
            if (have_threshold && !(score > threshold)) {
                return;
            }
            kept.push_back(Scored{score, s});
            if (kept.size() == 2 * m) {
                shrink();
            }
        };

        const std::uint64_t n = c.vertex_count;
        const std::uint64_t per_piece = reader.max_records();
        const std::uint64_t pieces = (n + per_piece - 1) / per_piece;
        const std::uint64_t step = spread_step(pieces);
        std::vector<std::byte> buffer;
        std::uint64_t skipped = 0;
        std::uint64_t piece = 0;
        std::uint64_t done = 0;
        for (; done < pieces; ++done) {
            if (stop.expired()) {
                break;
            }
            const std::uint64_t first = piece * per_piece;
            const auto count = static_cast<std::size_t>(std::min(per_piece, n - first));
            const Status status = reader.read(first, count, buffer, skipped, offer);
            if (status != Status::Ok) {
                result.status = status;
                result.read_calls = reader.read_calls;
                return result;
            }
            result.records_read += count;
            // piece < pieces and step < pieces <= 50M, so this does not overflow.
            piece = (piece + step) % pieces;
        }
        result.complete = done == pieces;
        result.read_calls = reader.read_calls;
        if (kept.size() > m) {
            shrink();
        }
        result.splats.reserve(kept.size());
        for (const Scored& k : kept) {
            result.splats.push_back(k.splat);
        }
        if (result.complete && result.splats.empty()) {
            result.status = Status::NothingToDraw;
        }
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Status::OutOfMemory;
    }
    return result;
}

std::uint64_t prefer_important_splats(ByteSource& source, const Classification& c,
                                      std::size_t max_splats, const Camera* camera,
                                      LoadResult& loaded, const Deadline& stop) noexcept {
    if (loaded.status != Status::Ok || loaded.records_read >= c.vertex_count) {
        return 0;
    }
    const Deadline budget = std::min(
        stop, Deadline::after(std::chrono::milliseconds(limits::kImportanceReadMs)));
    ImportantResult important = load_important_splats(source, c, max_splats, camera, budget);
    if (important.status != Status::Ok || important.splats.empty() ||
        !(important.complete || important.records_read >= 2 * loaded.records_read)) {
        return 0;
    }
    loaded.splats = std::move(important.splats);
    return important.records_read;
}

} // namespace splatthumb
