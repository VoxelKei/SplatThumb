#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"

namespace {

using splatthumb::Classification;
using splatthumb::Deadline;
using splatthumb::LoadOptions;
using splatthumb::LoadResult;
using splatthumb::SampleMode;
using splatthumb::Status;
using namespace splatthumb::test;

Deadline far_future() {
    return Deadline::after(std::chrono::hours(1));
}

// A standard file of n records generated on the fly: record i has x = i. Counts reads.
class ProceduralSource final : public splatthumb::ByteSource {
public:
    explicit ProceduralSource(std::uint64_t n) : header_(standard_header(n)), n_(n) {}

    std::uint64_t size() const noexcept override {
        return header_.size() + n_ * kStandardRecordSize;
    }

    std::size_t read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override {
        ++reads;
        offsets.push_back(offset);
        if (fail_after >= 0 && reads > static_cast<std::uint64_t>(fail_after)) {
            return 0;
        }
        const std::uint64_t end = std::min<std::uint64_t>(offset + out.size(), size());
        for (std::uint64_t pos = offset; pos < end; ++pos) {
            out[static_cast<std::size_t>(pos - offset)] = byte_at(pos);
        }
        return end > offset ? static_cast<std::size_t>(end - offset) : 0;
    }

    std::uint64_t reads = 0;
    long long fail_after = -1;
    std::vector<std::uint64_t> offsets; // every read_at offset, in order

private:
    std::byte byte_at(std::uint64_t pos) const {
        if (pos < header_.size()) {
            return static_cast<std::byte>(header_[static_cast<std::size_t>(pos)]);
        }
        const std::uint64_t rel = pos - header_.size();
        RawSplat raw;
        const std::uint64_t index = rel / kStandardRecordSize;
        raw.x = static_cast<float>(index);
        // Sizes vary with the index, so the most important records are known: those
        // with index % 1000 == 999 are the largest.
        raw.scale[0] = raw.scale[1] = -6.0f + 0.005f * static_cast<float>(index % 1000);
        std::byte rec[kStandardRecordSize];
        write_record(raw, rec);
        return rec[rel % kStandardRecordSize];
    }

    std::string header_;
    std::uint64_t n_;
};

LoadResult load(splatthumb::ByteSource& src, const LoadOptions& options = {},
                Deadline deadline = far_future()) {
    const Classification c = splatthumb::inspect(src);
    return splatthumb::load_splats(src, c, options, deadline);
}

TEST(Loader, ReadsAllWhenSmall) {
    std::vector<RawSplat> raw(10);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i].x = static_cast<float>(i);
    }
    const auto bytes = make_standard_file(raw);
    splatthumb::MemoryByteSource src(bytes);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    ASSERT_EQ(r.splats.size(), 10u);
    EXPECT_EQ(r.splats[9].position[0], 9.0f);
    EXPECT_EQ(r.read_calls, 1u);
}

TEST(Loader, SkipsInvalidRecordsAndCountsThem) {
    std::vector<RawSplat> raw(3);
    raw[1].opacity = std::numeric_limits<float>::quiet_NaN();
    const auto bytes = make_standard_file(raw);
    splatthumb::MemoryByteSource src(bytes);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.splats.size(), 2u);
    EXPECT_EQ(r.records_skipped, 1u);
}

TEST(Loader, AllInvalidMeansNothingToDraw) {
    std::vector<RawSplat> raw(3);
    for (auto& s : raw) {
        s.opacity = -100.0f;
    }
    const auto bytes = make_standard_file(raw);
    splatthumb::MemoryByteSource src(bytes);
    EXPECT_EQ(load(src).status, Status::NothingToDraw);
}

TEST(Loader, SingleRecord) {
    ProceduralSource src(1);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.splats.size(), 1u);
}

TEST(Loader, ExactlyAtLimitReadsEverything) {
    constexpr std::size_t n = splatthumb::limits::kStandardRenderSplats;
    ProceduralSource src(n);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.records_read, n);
    EXPECT_EQ(r.splats.back().position[0], static_cast<float>(n - 1));
}

TEST(Loader, OneOverLimitIsSampled) {
    ProceduralSource src(splatthumb::limits::kStandardRenderSplats + 1);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.records_read, splatthumb::limits::kStandardRenderSplats);
}

TEST(Loader, MaxSplatsAboveTheHardCapIsCapped) {
    ProceduralSource src(splatthumb::limits::kMaxRenderSplats + 1);
    LoadOptions o;
    o.max_splats = splatthumb::limits::kMaxRenderSplats * 2;
    const LoadResult r = load(src, o);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.records_read, splatthumb::limits::kMaxRenderSplats);
}

TEST(Loader, UniformSamplingSpansTheFile) {
    ProceduralSource src(1000);
    LoadOptions o;
    o.max_splats = 10;
    o.mode = SampleMode::Uniform;
    const LoadResult r = load(src, o);
    ASSERT_EQ(r.splats.size(), 10u);
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(r.splats[i].position[0], static_cast<float>(i * 100));
    }
    EXPECT_EQ(r.read_calls, 10u);
}

TEST(Loader, BlockSamplingReadsRuns) {
    ProceduralSource src(10000);
    LoadOptions o;
    o.max_splats = 250;
    o.block_size = 100;
    const LoadResult r = load(src, o);
    ASSERT_EQ(r.splats.size(), 250u);
    // Three blocks: starts at 0, 3333, 6666; the last one is 50 long.
    EXPECT_EQ(r.read_calls, 3u);
    EXPECT_EQ(r.splats[0].position[0], 0.0f);
    EXPECT_EQ(r.splats[99].position[0], 99.0f);
    EXPECT_EQ(r.splats[100].position[0], 3333.0f);
    EXPECT_EQ(r.splats[249].position[0], 6715.0f);
}

TEST(Loader, LargestFileReadsOnlyTheSample) {
    ProceduralSource src(splatthumb::limits::kMaxVertexCount);
    LoadOptions o;
    o.max_splats = 1000; // keep the test fast; the read pattern is what matters
    const LoadResult r = load(src, o);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.records_read, 1000u);
    EXPECT_EQ(r.read_calls, 10u);
}

TEST(Loader, ReadFailureIsReported) {
    ProceduralSource src(1000);
    src.fail_after = 1; // the header read succeeds, the first data read fails
    const LoadResult r = load(src);
    EXPECT_EQ(r.status, Status::ReadError);
    EXPECT_TRUE(r.splats.empty());
}

TEST(Loader, StopsAtDeadline) {
    ProceduralSource src(1000);
    const LoadResult r = load(src, {}, Deadline(Deadline::Clock::now() - std::chrono::seconds(1)));
    EXPECT_EQ(r.status, Status::Timeout);
}

TEST(Loader, PassesOnWhyNotASplat) {
    Classification c;
    splatthumb::MemoryByteSource src({});
    c.kind = splatthumb::SplatKind::NotSplat;
    c.status = Status::HasFaces;
    EXPECT_EQ(splatthumb::load_splats(src, c, {}, far_future()).status, Status::HasFaces);
}

// ---- load_important_splats ----

using splatthumb::ImportantResult;

ImportantResult important(splatthumb::ByteSource& src, std::size_t max_splats,
                          const splatthumb::Camera* camera = nullptr,
                          Deadline stop = far_future()) {
    const Classification c = splatthumb::inspect(src);
    return splatthumb::load_important_splats(src, c, max_splats, camera, stop);
}

splatthumb::Splat splat_with_scale(float a, float b, float c, float opacity = 1.0f) {
    splatthumb::Splat s;
    s.scale = {a, b, c};
    s.opacity = opacity;
    s.rotation = {1, 0, 0, 0};
    return s;
}

TEST(Importance, IsOpacityTimesTheTwoLargestAxes) {
    EXPECT_FLOAT_EQ(splatthumb::splat_importance(splat_with_scale(1, 2, 3)), 6.0f);
    EXPECT_FLOAT_EQ(splatthumb::splat_importance(splat_with_scale(3, 1, 2)), 6.0f);
    EXPECT_FLOAT_EQ(splatthumb::splat_importance(splat_with_scale(2, 3, 1, 0.5f)), 3.0f);
    EXPECT_FLOAT_EQ(splatthumb::splat_importance(splat_with_scale(2, 2, 2)), 4.0f);
}

TEST(Importance, KeepsTheLargestSplats) {
    ProceduralSource src(10000);
    const ImportantResult r = important(src, 10);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.records_read, 10000u);
    ASSERT_EQ(r.splats.size(), 10u);
    for (const auto& s : r.splats) {
        // Records 999, 1999, ..., 9999.
        EXPECT_EQ(static_cast<int>(s.position[0]) % 1000, 999);
    }
}

TEST(Importance, KeepsTheLargestSplatsWhenTheBufferFillsManyTimes) {
    ProceduralSource src(100000);
    const ImportantResult r = important(src, 100);
    ASSERT_EQ(r.status, Status::Ok);
    ASSERT_EQ(r.splats.size(), 100u);
    for (const auto& s : r.splats) {
        EXPECT_EQ(static_cast<int>(s.position[0]) % 1000, 999);
    }
}

TEST(Importance, ReadsEveryRecordOnceInSpreadPieces) {
    // 56-byte records: 18724 per 1 MiB piece, so 6 pieces.
    constexpr std::uint64_t n = 100000;
    ProceduralSource src(n);
    const ImportantResult r = important(src, splatthumb::limits::kStandardRenderSplats);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_TRUE(r.complete);
    ASSERT_EQ(r.splats.size(), n);
    std::vector<bool> seen(n, false);
    for (const auto& s : r.splats) {
        const auto i = static_cast<std::size_t>(s.position[0]);
        ASSERT_LT(i, n);
        EXPECT_FALSE(seen[i]);
        seen[i] = true;
    }
    // Header read, then one read per piece. The second piece is not next to the first.
    ASSERT_EQ(src.offsets.size(), 1u + 6u);
    const std::uint64_t piece_bytes = (splatthumb::limits::kMaxReadChunkBytes /
                                       kStandardRecordSize) * kStandardRecordSize;
    EXPECT_GT(src.offsets[2] - src.offsets[1], piece_bytes);
}

TEST(Importance, MaxSplatsIsCapped) {
    ProceduralSource src(splatthumb::limits::kMaxRenderSplats + 10);
    const ImportantResult r = important(src, splatthumb::limits::kMaxRenderSplats * 2);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.splats.size(), splatthumb::limits::kMaxRenderSplats);
}

TEST(Importance, StopsAtTheStopTimeWithoutFailing) {
    ProceduralSource src(100000);
    const ImportantResult r =
        important(src, 100, nullptr, Deadline(Deadline::Clock::now() - std::chrono::seconds(1)));
    EXPECT_EQ(r.status, Status::Ok);
    EXPECT_FALSE(r.complete);
    EXPECT_EQ(r.records_read, 0u);
    EXPECT_TRUE(r.splats.empty());
}

TEST(Importance, ReadFailureIsReported) {
    ProceduralSource src(100000);
    src.fail_after = 2;
    const ImportantResult r = important(src, 100);
    EXPECT_EQ(r.status, Status::ReadError);
    EXPECT_TRUE(r.splats.empty());
}

TEST(Importance, CameraLeavesOutWhatItCannotSee) {
    // Records sit on the x axis at x = index. A camera at x = 500 looking toward +x sees
    // only the ones beyond it.
    ProceduralSource src(1000);
    const auto cam = splatthumb::look_at({500, 0, 0}, {600, 0, 0}, {0, -1, 0}, 45.0f, 10.0f);
    ASSERT_EQ(cam.status, Status::Ok);
    const ImportantResult r = important(src, 1000, &cam.camera);
    ASSERT_EQ(r.status, Status::Ok);
    ASSERT_FALSE(r.splats.empty());
    for (const auto& s : r.splats) {
        EXPECT_GT(s.position[0], 500.0f);
    }
}

TEST(Importance, NotASplatIsPassedOn) {
    Classification c;
    splatthumb::MemoryByteSource src({});
    c.kind = splatthumb::SplatKind::NotSplat;
    c.status = Status::HasFaces;
    EXPECT_EQ(splatthumb::load_important_splats(src, c, 10, nullptr, far_future()).status,
              Status::HasFaces);
}

TEST(Importance, ThumbnailUsesItOnlyForSampledFiles) {
    splatthumb::RenderOptions options;
    options.size = 32;
    LoadOptions load;
    load.max_splats = 500;
    for (const bool on : {true, false}) {
        load.importance = on;
        ProceduralSource big(5000);
        splatthumb::ThumbnailStats stats;
        const auto r = splatthumb::render_thumbnail(big, options, load, far_future(), &stats);
        ASSERT_EQ(r.status, Status::Ok);
        EXPECT_EQ(stats.importance_records_read, on ? 5000u : 0u);
    }
    load.importance = true;
    ProceduralSource small(400);
    splatthumb::ThumbnailStats stats;
    const auto r = splatthumb::render_thumbnail(small, options, load, far_future(), &stats);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(stats.importance_records_read, 0u);
}

} // namespace
