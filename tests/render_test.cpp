#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/limits.h"
#include "splatthumb/render.h"

namespace {

using splatthumb::Background;
using splatthumb::Camera;
using splatthumb::Deadline;
using splatthumb::Image;
using splatthumb::RenderOptions;
using splatthumb::RenderResult;
using splatthumb::Splat;
using splatthumb::Status;
using namespace splatthumb::test;

Deadline far_future() {
    return Deadline::after(std::chrono::hours(1));
}

// Camera at z = -10 looking toward +z, screen up = world -y.
Camera front_camera() {
    Camera c;
    c.eye = {0, 0, -10};
    c.target = {0, 0, 0};
    c.forward = {0, 0, 1};
    c.right = {-1, 0, 0};
    c.up = {0, -1, 0};
    c.fov_y = 0.785398f;
    c.radius = 2.0f;
    return c;
}

Splat splat(float x, float y, float z, float r, float g, float b, float size = 0.2f,
            float opacity = 0.99f) {
    Splat s;
    s.position = {x, y, z};
    s.color = {r, g, b};
    s.opacity = opacity;
    s.scale = {size, size, size};
    s.rotation = {1, 0, 0, 0};
    return s;
}

const std::uint8_t* pixel(const Image& img, int x, int y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.size) +
                              static_cast<std::size_t>(x)) * 4;
}

RenderResult render(const std::vector<Splat>& splats, RenderOptions o = {}) {
    return splatthumb::render_splats(splats, front_camera(), o, far_future());
}

TEST(Render, SingleSplatColorsTheCenter) {
    const RenderResult r = render({splat(0, 0, 0, 1, 0, 0)});
    ASSERT_EQ(r.status, Status::Ok);
    ASSERT_EQ(r.image.size, 256);
    ASSERT_EQ(r.image.rgba.size(), 256u * 256u * 4u);
    const std::uint8_t* c = pixel(r.image, 128, 128);
    EXPECT_GT(c[3], 230);
    EXPECT_GT(c[0], 230);
    EXPECT_LT(c[1], 5);
    // Corners stay transparent.
    EXPECT_EQ(pixel(r.image, 0, 0)[3], 0);
}

TEST(Render, OutputIsPremultiplied) {
    const RenderResult r = render({splat(0, 0, 0, 1, 1, 1, 0.5f, 0.5f)});
    ASSERT_EQ(r.status, Status::Ok);
    for (std::size_t i = 0; i < r.image.rgba.size(); i += 4) {
        ASSERT_LE(r.image.rgba[i], r.image.rgba[i + 3]);
        ASSERT_LE(r.image.rgba[i + 1], r.image.rgba[i + 3]);
        ASSERT_LE(r.image.rgba[i + 2], r.image.rgba[i + 3]);
    }
}

TEST(Render, NearSplatCoversFarSplat) {
    // Red is nearer the camera (smaller z) than blue, regardless of input order.
    for (int order = 0; order < 2; ++order) {
        std::vector<Splat> splats = {splat(0, 0, 1, 0, 0, 1), splat(0, 0, -1, 1, 0, 0)};
        if (order == 1) {
            std::swap(splats[0], splats[1]);
        }
        const RenderResult r = render(splats);
        ASSERT_EQ(r.status, Status::Ok);
        const std::uint8_t* c = pixel(r.image, 128, 128);
        EXPECT_GT(c[0], c[2]) << "order " << order;
    }
}

TEST(Render, ScreenUpIsWorldMinusY) {
    // A splat at world -y should appear in the upper half.
    const RenderResult r = render({splat(0, -2, 0, 1, 1, 1)});
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_GT(pixel(r.image, 128, 80)[3], pixel(r.image, 128, 176)[3]);
}

TEST(Render, GrayBackgroundIsOpaque) {
    RenderOptions o;
    o.background = Background::Gray;
    const RenderResult r = render({splat(0, 0, 0, 1, 0, 0)}, o);
    ASSERT_EQ(r.status, Status::Ok);
    const std::uint8_t* corner = pixel(r.image, 0, 0);
    EXPECT_EQ(corner[3], 255);
    EXPECT_NEAR(corner[0], 46, 1);
}

TEST(Render, SplatsBehindOrOffScreenAreNothingToDraw) {
    EXPECT_EQ(render({splat(0, 0, -20, 1, 1, 1)}).status, Status::NothingToDraw);
    EXPECT_EQ(render({splat(1000, 0, 0, 1, 1, 1)}).status, Status::NothingToDraw);
    EXPECT_EQ(render({}).status, Status::NothingToDraw);
}

TEST(Render, HugeSplatIsBounded) {
    const RenderResult r = render({splat(0, 0, 0, 1, 1, 1, 22026.0f)});
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.splats_drawn, 1u);
}

TEST(Render, SizeIsClamped) {
    RenderOptions o;
    o.size = 100000;
    EXPECT_EQ(render({splat(0, 0, 0, 1, 1, 1)}, o).image.size,
              splatthumb::limits::kMaxThumbnailSize);
    o.size = -5;
    EXPECT_EQ(render({splat(0, 0, 0, 1, 1, 1)}, o).image.size,
              splatthumb::limits::kMinThumbnailSize);
}

TEST(Render, StopsAtDeadline) {
    std::vector<Splat> many(5000, splat(0, 0, 0, 1, 1, 1, 1.0f));
    const RenderResult r = splatthumb::render_splats(
        many, front_camera(), {}, Deadline(Deadline::Clock::now() - std::chrono::seconds(1)));
    EXPECT_EQ(r.status, Status::Timeout);
}

TEST(RenderThumbnail, EndToEndFromFile) {
    std::vector<RawSplat> raw(50);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i].x = static_cast<float>(i % 10);
        raw[i].z = static_cast<float>(i / 10);
        raw[i].f_dc[0] = 1.0f;
    }
    const auto bytes = make_standard_file(raw);
    splatthumb::MemoryByteSource src(bytes);
    splatthumb::ThumbnailStats stats;
    const RenderResult r =
        splatthumb::render_thumbnail(src, {}, {}, far_future(), &stats);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(stats.vertex_count, 50u);
    EXPECT_EQ(stats.splats_loaded, 50u);
    EXPECT_GT(stats.splats_drawn, 0u);
}

TEST(RenderThumbnail, ReportsWhyNotDrawn) {
    const auto bytes = make_file("ply\nformat ascii 1.0\nend_header\n", 0);
    splatthumb::MemoryByteSource src(bytes);
    EXPECT_EQ(splatthumb::render_thumbnail(src, {}, {}, far_future()).status,
              Status::UnsupportedFormat);
}

} // namespace
