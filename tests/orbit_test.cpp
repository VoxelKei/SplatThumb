#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

#include "splatthumb/orbit.h"

namespace {

using splatthumb::Camera;
using splatthumb::Vec3;
using splatthumb::View;

constexpr float kPi = std::numbers::pi_v<float>;

View front() {
    View v;
    v.eye = {0, 0, -10};
    v.target = {0, 0, 0};
    v.up = {0, -1, 0};
    v.fov_y_deg = 45;
    v.radius = 2;
    return v;
}

Camera cam(const View& v) {
    const auto r = splatthumb::camera_from_view(v);
    EXPECT_EQ(r.status, splatthumb::Status::Ok);
    return r.camera;
}

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

float distance(const View& v) {
    const Vec3 d = sub(v.eye, v.target);
    return std::sqrt(dot(d, d));
}

// Screen position of world point p: x to the right, y up.
std::pair<float, float> screen(const View& v, const Vec3& p) {
    const Camera c = cam(v);
    const Vec3 d = sub(p, c.eye);
    return {dot(d, c.right), dot(d, c.up)};
}

void expect_near(const Vec3& a, const Vec3& b, float tol = 1e-4f) {
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(a[i], b[i], tol) << i;
    }
}

TEST(Orbit, ZeroIsIdentity) {
    const View v = splatthumb::orbit(front(), 0, 0);
    expect_near(v.eye, front().eye);
    expect_near(v.up, front().up);
}

TEST(Orbit, KeepsDistanceAndTarget) {
    View v = front();
    for (int i = 0; i < 50; ++i) {
        v = splatthumb::orbit(v, 0.37f, -0.21f);
    }
    EXPECT_NEAR(distance(v), 10.0f, 1e-3f);
    expect_near(v.target, front().target);
}

TEST(Orbit, FullTurnReturns) {
    View v = front();
    for (int i = 0; i < 8; ++i) {
        v = splatthumb::orbit(v, kPi / 4, 0);
    }
    expect_near(v.eye, front().eye, 1e-3f);
}

TEST(Orbit, PositiveYawMovesEyeRightAndPitchMovesItUp) {
    const Camera c = cam(front());
    const View yawed = splatthumb::orbit(front(), 0.3f, 0);
    EXPECT_GT(dot(sub(yawed.eye, front().eye), c.right), 0.0f);
    const View pitched = splatthumb::orbit(front(), 0, 0.3f);
    EXPECT_GT(dot(sub(pitched.eye, front().eye), c.up), 0.0f);
}

TEST(Orbit, YawTurnsAboutYAndKeepsThePictureLevel) {
    View v = splatthumb::orbit(front(), 0, 0.4f); // look from a little above
    const float height = v.eye[1];
    for (int i = 0; i < 7; ++i) {
        v = splatthumb::orbit(v, 0.9f, 0);
        EXPECT_NEAR(v.eye[1], height, 1e-3f);
        // Screen right stays horizontal: no tilt builds up.
        EXPECT_NEAR(cam(v).right[1], 0.0f, 1e-4f);
    }
}

TEST(Orbit, MixedDragsDoNotTiltThePicture) {
    View v = front();
    for (int i = 0; i < 200; ++i) {
        v = splatthumb::orbit(v, 0.37f * static_cast<float>((i % 3) - 1), 0.21f * static_cast<float>((i % 5) - 2));
        EXPECT_NEAR(cam(v).right[1], 0.0f, 1e-3f) << i;
    }
}

TEST(Orbit, StopsShortOfTheTop) {
    View v = front();
    for (int i = 0; i < 8; ++i) {
        v = splatthumb::orbit(v, 0, kPi / 4); // asks for 360 degrees in total
    }
    const Vec3 d = sub(v.eye, v.target);
    const float height_deg = std::asin(-d[1] / distance(v)) * 180.0f / kPi;
    EXPECT_NEAR(height_deg, splatthumb::kOrbitPitchLimitDeg, 1e-2f);
    EXPECT_LT(v.eye[2], 0.0f); // still on the front side, not flipped over
    EXPECT_LT(v.up[1], 0.0f);  // still upright
    // And the same going down.
    for (int i = 0; i < 8; ++i) {
        v = splatthumb::orbit(v, 0, -kPi / 4);
    }
    const Vec3 d2 = sub(v.eye, v.target);
    EXPECT_NEAR(std::asin(-d2[1] / distance(v)) * 180.0f / kPi, -splatthumb::kOrbitPitchLimitDeg,
                1e-2f);
}

TEST(Orbit, UpsideDownViewStillFollowsTheMouse) {
    View v = front();
    v.up = {0, 1, 0};
    const Camera c = cam(v);
    const View yawed = splatthumb::orbit(v, 0.3f, 0);
    EXPECT_GT(dot(sub(yawed.eye, v.eye), c.right), 0.0f);
    const View pitched = splatthumb::orbit(v, 0, 0.3f);
    EXPECT_GT(dot(sub(pitched.eye, v.eye), c.up), 0.0f);
}

TEST(Roll, PositiveTurnsThePictureCounterclockwise) {
    const Vec3 top = {0, -1, 0}; // world point drawn above the target
    const auto before = screen(front(), top);
    EXPECT_GT(before.second, 0.5f);
    const View rolled = splatthumb::roll(front(), kPi / 2);
    const auto after = screen(rolled, top);
    EXPECT_LT(after.first, -0.5f); // moved to the left
    EXPECT_NEAR(after.second, 0.0f, 1e-4f);
    expect_near(rolled.eye, front().eye);
}

TEST(Roll, FourQuarterTurnsReturn) {
    View v = front();
    for (int i = 0; i < 4; ++i) {
        v = splatthumb::roll(v, kPi / 2);
    }
    expect_near(v.up, front().up);
}

TEST(Zoom, ScalesDistanceAndRadius) {
    const View v = splatthumb::zoom(front(), 0.5f);
    EXPECT_NEAR(distance(v), 5.0f, 1e-4f);
    EXPECT_NEAR(v.radius, 1.0f, 1e-6f);
    // Near clip stays in front of the framed sphere.
    EXPECT_NEAR(cam(v).near_clip, cam(front()).near_clip * 0.5f, 1e-4f);
}

TEST(Zoom, InvalidFactorsChangeNothing) {
    for (const float f : {0.0f, -1.0f, std::nanf(""), INFINITY, 1e30f}) {
        EXPECT_EQ(splatthumb::zoom(front(), f), front()) << f;
    }
}

TEST(Pan, ShiftsEyeAndTargetTogether) {
    const View v = splatthumb::pan(front(), 0.5f, 0.0f);
    const Camera c = cam(front());
    EXPECT_NEAR(dot(sub(v.target, front().target), c.right), 1.0f, 1e-5f);
    EXPECT_NEAR(distance(v), 10.0f, 1e-4f);
    const View down = splatthumb::pan(front(), 0.0f, 0.5f);
    EXPECT_NEAR(dot(sub(down.target, front().target), c.up), -1.0f, 1e-5f);
}

TEST(OrbitAll, BrokenViewsAreReturnedUnchanged) {
    View broken = front();
    broken.eye = broken.target;
    EXPECT_EQ(splatthumb::orbit(broken, 0.1f, 0.1f), broken);
    EXPECT_EQ(splatthumb::roll(broken, 0.1f), broken);
    EXPECT_EQ(splatthumb::pan(broken, 0.1f, 0.1f), broken);
    EXPECT_EQ(splatthumb::pan(front(), 1e30f, 0.0f), front());
}

} // namespace
