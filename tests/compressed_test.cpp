// PlayCanvas compressed PLY: classification, record decoding, loading and rendering.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/classify.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "splatthumb/splat.h"

namespace {

using splatthumb::Classification;
using splatthumb::Deadline;
using splatthumb::LoadResult;
using splatthumb::SplatKind;
using splatthumb::Status;
using namespace splatthumb::test;

Deadline far_future() {
    return Deadline::after(std::chrono::hours(1));
}

Classification inspect(const std::vector<std::byte>& bytes) {
    splatthumb::MemoryByteSource src(bytes);
    return splatthumb::inspect(src);
}

LoadResult load(const std::vector<std::byte>& bytes) {
    splatthumb::MemoryByteSource src(bytes);
    const Classification c = splatthumb::inspect(src);
    return splatthumb::load_splats(src, c, {}, far_future());
}

// Decodes one record of a one-chunk file.
bool decode(const RawChunk& chunk, const PackedSplat& packed, splatthumb::Splat& out,
            bool color_range = true) {
    const auto bytes = make_compressed_file({&chunk, 1}, {&packed, 1}, color_range);
    const Classification c = inspect(bytes);
    if (c.kind != SplatKind::Compressed) {
        return false;
    }
    const std::span<const std::byte> all(bytes);
    return splatthumb::decode_compressed_record(
        all.subspan(c.vertex_data_offset, c.vertex_stride),
        all.subspan(c.compressed.chunk_data_offset, c.compressed.chunk_stride), c.compressed,
        out);
}

// ---- Classification ----

TEST(Compressed, Classified) {
    const RawChunk chunk;
    const std::vector<PackedSplat> splats(256);
    const auto bytes = make_compressed_file({&chunk, 1}, splats);
    const Classification c = inspect(bytes);
    ASSERT_EQ(c.kind, SplatKind::Compressed);
    EXPECT_EQ(c.vertex_count, 256u);
    EXPECT_EQ(c.vertex_stride, 16u);
    EXPECT_EQ(c.compressed.chunk_count, 1u);
    EXPECT_EQ(c.compressed.chunk_stride, 72u);
    EXPECT_EQ(c.compressed.chunk_data_offset, compressed_header(1, 256).size());
    EXPECT_EQ(c.vertex_data_offset, c.compressed.chunk_data_offset + 72);
    EXPECT_TRUE(c.compressed.has_color_range);
}

TEST(Compressed, OlderFilesWithoutColorRange) {
    const RawChunk chunk;
    const PackedSplat splat;
    const Classification c = inspect(make_compressed_file({&chunk, 1}, {&splat, 1}, false));
    ASSERT_EQ(c.kind, SplatKind::Compressed);
    EXPECT_FALSE(c.compressed.has_color_range);
    EXPECT_EQ(c.compressed.chunk_stride, 48u);
}

TEST(Compressed, TooFewChunks) {
    const RawChunk chunk;
    const std::vector<PackedSplat> splats(257); // needs 2 chunks
    const Classification c = inspect(make_compressed_file({&chunk, 1}, splats));
    EXPECT_EQ(c.kind, SplatKind::NotSplat);
    EXPECT_EQ(c.status, Status::ChunkCountMismatch);
}

TEST(Compressed, MissingChunkRangeIsNotASplat) {
    std::string h = compressed_header(1, 1);
    h.replace(h.find("property float max_scale_z\n"), 27, "");
    const auto bytes = make_file(h, 68 + 16);
    const Classification c = inspect(bytes);
    EXPECT_EQ(c.kind, SplatKind::NotSplat);
    EXPECT_EQ(c.status, Status::MissingSplatProperties);
}

TEST(Compressed, ChunkDataMustFitInFile) {
    const RawChunk chunk;
    const PackedSplat splat;
    auto bytes = make_compressed_file({&chunk, 1}, {&splat, 1});
    bytes.pop_back();
    EXPECT_EQ(inspect(bytes).status, Status::FileTooSmall);
}

// ---- Decoding ----

TEST(Compressed, DecodesRangesAndPacking) {
    RawChunk chunk;
    chunk.min_pos[0] = 10;
    chunk.max_pos[0] = 20;
    chunk.min_color[1] = 0.2f;
    chunk.max_color[1] = 0.6f;
    PackedSplat p;
    p.position = pack_11_10_11(0.0f, 1.0f, 0.5f);
    p.scale = pack_11_10_11(0.0f, 1.0f, 0.0f);
    p.color = pack_color(1.0f, 0.5f, 0.0f, 0.8f);
    splatthumb::Splat s;
    ASSERT_TRUE(decode(chunk, p, s));
    EXPECT_NEAR(s.position[0], 10.0f, 1e-4f);
    EXPECT_NEAR(s.position[1], 1.0f, 1e-4f);
    EXPECT_NEAR(s.position[2], 0.0f, 2e-3f);
    EXPECT_NEAR(s.scale[0], std::exp(-4.0f), 1e-5f);
    EXPECT_NEAR(s.scale[1], std::exp(-2.0f), 1e-5f);
    EXPECT_NEAR(s.color[0], 1.0f, 1e-5f);
    EXPECT_NEAR(s.color[1], 0.4f, 3e-3f); // halfway between 0.2 and 0.6
    EXPECT_NEAR(s.color[2], 0.0f, 1e-5f);
    EXPECT_NEAR(s.opacity, 0.8f, 3e-3f);
}

TEST(Compressed, DecodesRotation) {
    const float h = std::sqrt(0.5f);
    const float quats[][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {h, 0, h, 0},
                              {0.5f, -0.5f, 0.5f, 0.5f}};
    for (const auto& q : quats) {
        PackedSplat p;
        p.rotation = pack_rotation(q[0], q[1], q[2], q[3]);
        splatthumb::Splat s;
        ASSERT_TRUE(decode(RawChunk{}, p, s));
        // q and -q are the same rotation.
        const float dot = s.rotation[0] * q[0] + s.rotation[1] * q[1] + s.rotation[2] * q[2] +
                          s.rotation[3] * q[3];
        EXPECT_NEAR(std::fabs(dot), 1.0f, 2e-3f) << q[0] << " " << q[1] << " " << q[2] << " "
                                                 << q[3];
    }
}

TEST(Compressed, WithoutColorRangeColorIsStoredDirectly) {
    PackedSplat p;
    p.color = pack_color(0.25f, 0.5f, 0.75f, 1.0f);
    splatthumb::Splat s;
    ASSERT_TRUE(decode(RawChunk{}, p, s, false));
    EXPECT_NEAR(s.color[0], 0.25f, 3e-3f);
    EXPECT_NEAR(s.color[2], 0.75f, 3e-3f);
}

TEST(Compressed, SkipsTransparentAndBrokenChunks) {
    PackedSplat transparent;
    transparent.color = pack_color(0.5f, 0.5f, 0.5f, 0.0f);
    splatthumb::Splat s;
    EXPECT_FALSE(decode(RawChunk{}, transparent, s));

    const float bad[] = {std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity()};
    for (float v : bad) {
        RawChunk chunk;
        chunk.max_scale[1] = v;
        EXPECT_FALSE(decode(chunk, PackedSplat{}, s));
        RawChunk far;
        far.min_pos[2] = v;
        EXPECT_FALSE(decode(far, PackedSplat{}, s));
    }

    RawChunk huge;
    huge.max_pos[0] = 1e30f;
    PackedSplat at_max;
    at_max.position = pack_11_10_11(1.0f, 0.5f, 0.5f);
    EXPECT_FALSE(decode(huge, at_max, s));
}

TEST(Compressed, ShortRecordsAreSkipped) {
    splatthumb::CompressedLayout layout;
    layout.packed_color = 12;
    layout.position = {0, 4, 8, 12, 16, 20};
    layout.scale = {24, 28, 32, 36, 40, 44};
    std::vector<std::byte> chunk(48), record(12);
    splatthumb::Splat s;
    EXPECT_FALSE(splatthumb::decode_compressed_record(record, chunk, layout, s));
    layout.packed_color = 0;
    layout.scale[5] = 46; // past the end of the chunk record
    EXPECT_FALSE(splatthumb::decode_compressed_record(record, chunk, layout, s));
}

// ---- Loading and rendering ----

TEST(Compressed, EachVertexUsesItsOwnChunk) {
    std::vector<RawChunk> chunks(2);
    chunks[1].min_pos[0] = 100;
    chunks[1].max_pos[0] = 102;
    const std::vector<PackedSplat> splats(300);
    const LoadResult r = load(make_compressed_file(chunks, splats));
    ASSERT_EQ(r.status, Status::Ok);
    ASSERT_EQ(r.splats.size(), 300u);
    EXPECT_NEAR(r.splats[255].position[0], 0.0f, 1e-3f);
    EXPECT_NEAR(r.splats[256].position[0], 101.0f, 1e-3f);
    EXPECT_NEAR(r.splats[299].position[0], 101.0f, 1e-3f);
}

TEST(Compressed, SampledLoadStaysInBounds) {
    const std::vector<RawChunk> chunks(40);
    const std::vector<PackedSplat> splats(10000);
    const auto bytes = make_compressed_file(chunks, splats);
    splatthumb::MemoryByteSource src(bytes);
    const Classification c = splatthumb::inspect(src);
    for (auto mode : {splatthumb::SampleMode::Blocks, splatthumb::SampleMode::Uniform}) {
        splatthumb::LoadOptions options;
        options.max_splats = 777;
        options.mode = mode;
        const LoadResult r = splatthumb::load_splats(src, c, options, far_future());
        ASSERT_EQ(r.status, Status::Ok);
        EXPECT_EQ(r.splats.size(), 777u);
    }
}

TEST(Compressed, RendersThumbnail) {
    std::vector<PackedSplat> splats;
    for (int i = 0; i < 200; ++i) {
        PackedSplat p;
        const float t = static_cast<float>(i) / 199.0f;
        p.position = pack_11_10_11(t, 0.5f + 0.4f * std::sin(t * 6.0f), 0.5f);
        p.color = pack_color(t, 0.5f, 1.0f - t, 1.0f);
        splats.push_back(p);
    }
    const RawChunk chunk;
    const auto bytes = make_compressed_file({&chunk, 1}, splats);
    splatthumb::MemoryByteSource src(bytes);
    const auto r = splatthumb::render_thumbnail(src, {}, {}, far_future());
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.image.size, 256);
}

} // namespace
