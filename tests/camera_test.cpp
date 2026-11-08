#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "splatthumb/camera.h"

namespace {

using splatthumb::Splat;
using splatthumb::Status;

Splat at(float x, float y, float z) {
    Splat s;
    s.position = {x, y, z};
    return s;
}

float dot(const splatthumb::Vec3& a, const splatthumb::Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

TEST(AutoCamera, EmptyMeansNothingToDraw) {
    EXPECT_EQ(splatthumb::auto_camera({}).status, Status::NothingToDraw);
}

TEST(AutoCamera, CubeIsCenteredAndFramed) {
    std::vector<Splat> splats;
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            for (int z = -10; z <= 10; ++z) {
                splats.push_back(at(static_cast<float>(x) + 5.0f, static_cast<float>(y), static_cast<float>(z)));
            }
        }
    }
    const auto r = splatthumb::auto_camera(splats);
    ASSERT_EQ(r.status, Status::Ok);
    const auto& c = r.camera;
    EXPECT_NEAR(c.target[0], 5.0f, 1e-4f);
    EXPECT_NEAR(c.target[1], 0.0f, 1e-4f);
    // 1st..99th percentile of -10..10 is -10..10 (rounded), so radius ~ sqrt(3) * 10.
    EXPECT_NEAR(c.radius, std::sqrt(300.0f), 0.5f);
    // The whole sphere fits in the vertical field of view.
    const float dist = std::sqrt(dot({c.eye[0] - c.target[0], c.eye[1] - c.target[1],
                                      c.eye[2] - c.target[2]},
                                     {c.eye[0] - c.target[0], c.eye[1] - c.target[1],
                                      c.eye[2] - c.target[2]}));
    EXPECT_NEAR(std::asin(c.radius / dist) * 2.0f, c.fov_y, 1e-4f);
}

TEST(AutoCamera, LooksDownWithMinusYUp) {
    const std::vector<Splat> splats = {at(-1, 0, -1), at(1, 0, 1)};
    const auto c = splatthumb::auto_camera(splats).camera;
    // Eye is above the target in world terms (smaller y).
    EXPECT_LT(c.eye[1], c.target[1]);
    // Screen up points roughly to world -Y.
    EXPECT_LT(c.up[1], 0.0f);
    // Basis is orthonormal.
    EXPECT_NEAR(dot(c.forward, c.right), 0.0f, 1e-5f);
    EXPECT_NEAR(dot(c.forward, c.up), 0.0f, 1e-5f);
    EXPECT_NEAR(dot(c.up, c.up), 1.0f, 1e-5f);
}

TEST(AutoCamera, LooksFromMinusZTowardPlusZ) {
    const std::vector<Splat> splats = {at(-1, 0, -1), at(1, 0, 1)};
    const auto c = splatthumb::auto_camera(splats).camera;
    EXPECT_LT(c.eye[2], c.target[2]);
    EXPECT_GT(c.forward[2], 0.0f);
}

TEST(AutoCamera, IgnoresFloaters) {
    std::vector<Splat> splats;
    for (int i = 0; i < 1000; ++i) {
        splats.push_back(at(static_cast<float>(i % 10), 0, 0));
    }
    splats.push_back(at(1e5f, 0, 0)); // a single far-away floater
    const auto r = splatthumb::auto_camera(splats);
    EXPECT_LT(r.camera.radius, 10.0f);
}

Splat solid(float x, float y, float z, float size = 0.01f) {
    Splat s = at(x, y, z);
    s.opacity = 0.9f;
    s.scale = {size, size, size};
    return s;
}

TEST(AutoCamera, FramesTheSolidCoreNotBigFaintSplats) {
    std::vector<Splat> splats;
    for (int i = 0; i < 1000; ++i) {
        splats.push_back(solid(static_cast<float>(i % 10) * 0.1f, 0, 0));
    }
    // Many large, faint floaters off to one side (more than 1% of the splats).
    for (int i = 0; i < 300; ++i) {
        Splat f = at(50.0f + static_cast<float>(i), 0, 0);
        f.opacity = 0.1f;
        f.scale = {5, 5, 5};
        splats.push_back(f);
    }
    const auto r = splatthumb::auto_camera(splats);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_NEAR(r.camera.target[0], 0.45f, 0.11f);
    EXPECT_LT(r.camera.radius, 1.0f);
}

TEST(AutoCamera, ThinSpreadAroundADenseMiddleIsCropped) {
    std::vector<Splat> splats;
    for (int i = 0; i < 9000; ++i) {
        splats.push_back(solid(static_cast<float>(i % 21) * 0.1f - 1.0f, 0, 0));
    }
    // Solid but sparse surroundings reaching far out, as in a captured scene.
    for (int i = 0; i < 1000; ++i) {
        const float x = 2.0f + static_cast<float>(i) * 0.05f;
        splats.push_back(solid(i % 2 == 0 ? x : -x, 0, 0));
    }
    const auto r = splatthumb::auto_camera(splats);
    ASSERT_EQ(r.status, Status::Ok);
    // The 1..99 box alone would give a radius near 50.
    EXPECT_LT(r.camera.radius, 10.0f);
    EXPECT_GT(r.camera.radius, 1.0f);
}

TEST(AutoCamera, NearClipKeepsTheFramedSphere) {
    const std::vector<Splat> splats = {at(-1, 0, -1), at(1, 0, 1)};
    const auto c = splatthumb::auto_camera(splats).camera;
    const splatthumb::Vec3 d{c.target[0] - c.eye[0], c.target[1] - c.eye[1],
                             c.target[2] - c.eye[2]};
    const float distance = std::sqrt(dot(d, d));
    EXPECT_GT(c.near_clip, 0.0f);
    EXPECT_LT(c.near_clip, distance - c.radius);
}

TEST(AutoCamera, SinglePointHasMinimumRadius) {
    const std::vector<Splat> splats = {at(3, 4, 5)};
    const auto r = splatthumb::auto_camera(splats);
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_GT(r.camera.radius, 0.0f);
    EXPECT_TRUE(std::isfinite(r.camera.eye[0]));
    EXPECT_EQ(r.camera.target, (splatthumb::Vec3{3, 4, 5}));
}

TEST(AutoCamera, ExtremeOptionsStayFinite) {
    const std::vector<Splat> splats = {at(0, 0, 0), at(1, 1, 1)};
    splatthumb::AutoCameraOptions o;
    o.elevation_deg = 90.0f; // would make forward parallel to up
    o.fov_y_deg = 0.0f;
    const auto c = splatthumb::auto_camera(splats, o).camera;
    for (float v : c.right) {
        EXPECT_TRUE(std::isfinite(v));
    }
    EXPECT_TRUE(std::isfinite(c.eye[2]));
}

TEST(MayBeVisible, DropsOnlyWhatCannotShow) {
    const auto cam = splatthumb::look_at({0, 0, -10}, {0, 0, 0}, {0, -1, 0}, 90.0f, 1.0f);
    ASSERT_EQ(cam.status, Status::Ok);
    Splat s;
    s.scale = {0.01f, 0.01f, 0.01f};
    s.position = {0, 0, 0};
    EXPECT_TRUE(splatthumb::may_be_visible(s, cam.camera));
    s.position = {0, 0, -20}; // behind the eye
    EXPECT_FALSE(splatthumb::may_be_visible(s, cam.camera));
    s.position = {30, 0, 0}; // 90 degrees: half side at depth 10 is 10
    EXPECT_FALSE(splatthumb::may_be_visible(s, cam.camera));
    s.scale = {10, 10, 10}; // ...unless it is big enough to reach in
    EXPECT_TRUE(splatthumb::may_be_visible(s, cam.camera));
    s.scale = {0.01f, 0.01f, 0.01f};
    s.position = {0, 9.9f, 0}; // just inside the top edge
    EXPECT_TRUE(splatthumb::may_be_visible(s, cam.camera));
}

} // namespace
