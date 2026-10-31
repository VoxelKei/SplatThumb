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

private:
    std::byte byte_at(std::uint64_t pos) const {
        if (pos < header_.size()) {
            return static_cast<std::byte>(header_[static_cast<std::size_t>(pos)]);
        }
        const std::uint64_t rel = pos - header_.size();
        RawSplat raw;
        raw.x = static_cast<float>(rel / kStandardRecordSize);
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
    ProceduralSource src(splatthumb::limits::kMaxRenderSplats);
    const LoadResult r = load(src);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.records_read, splatthumb::limits::kMaxRenderSplats);
    EXPECT_EQ(r.splats.back().position[0],
              static_cast<float>(splatthumb::limits::kMaxRenderSplats - 1));
}

TEST(Loader, OneOverLimitIsSampled) {
    ProceduralSource src(splatthumb::limits::kMaxRenderSplats + 1);
    const LoadResult r = load(src);
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

TEST(Loader, RejectsNonStandardKinds) {
    Classification c;
    c.kind = splatthumb::SplatKind::Compressed;
    splatthumb::MemoryByteSource src({});
    EXPECT_EQ(splatthumb::load_splats(src, c, {}, far_future()).status, Status::UnsupportedKind);
    c.kind = splatthumb::SplatKind::NotSplat;
    c.status = Status::HasFaces;
    EXPECT_EQ(splatthumb::load_splats(src, c, {}, far_future()).status, Status::HasFaces);
}

} // namespace
