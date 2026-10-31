#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "ply_builder.h"
#include "splatthumb/limits.h"
#include "splatthumb/splat.h"

namespace {

using splatthumb::Splat;
using namespace splatthumb::test;

splatthumb::StandardLayout layout() {
    splatthumb::StandardLayout l;
    l.x = 0; l.y = 4; l.z = 8;
    l.f_dc = {12, 16, 20};
    l.opacity = 24;
    l.scale = {28, 32, 36};
    l.rot = {40, 44, 48, 52};
    return l;
}

bool decode(const RawSplat& raw, Splat& out) {
    std::array<std::byte, kStandardRecordSize> rec{};
    write_record(raw, rec.data());
    return splatthumb::decode_standard_record(rec, layout(), out);
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

TEST(Sigmoid, IsSafeAtExtremes) {
    EXPECT_FLOAT_EQ(splatthumb::sigmoid(0.0f), 0.5f);
    EXPECT_FLOAT_EQ(splatthumb::sigmoid(1000.0f), 1.0f);
    EXPECT_FLOAT_EQ(splatthumb::sigmoid(-1000.0f), 0.0f);
    EXPECT_FALSE(std::isnan(splatthumb::sigmoid(-std::numeric_limits<float>::max())));
}

TEST(Decode, ConvertsValues) {
    RawSplat raw;
    raw.x = 1; raw.y = 2; raw.z = 3;
    raw.f_dc[0] = 0; raw.f_dc[1] = 1; raw.f_dc[2] = -10;
    raw.opacity = 0;
    raw.scale[0] = 0; raw.scale[1] = -1; raw.scale[2] = 1;
    raw.rot[0] = 2; raw.rot[1] = 0; raw.rot[2] = 0; raw.rot[3] = 0;
    Splat s;
    ASSERT_TRUE(decode(raw, s));
    EXPECT_EQ(s.position, (std::array<float, 3>{1, 2, 3}));
    EXPECT_FLOAT_EQ(s.color[0], 0.5f);
    EXPECT_NEAR(s.color[1], 0.5f + 0.28209479f, 1e-6f);
    EXPECT_FLOAT_EQ(s.color[2], 0.0f); // clamped
    EXPECT_FLOAT_EQ(s.opacity, 0.5f);
    EXPECT_FLOAT_EQ(s.scale[0], 1.0f);
    EXPECT_NEAR(s.scale[1], std::exp(-1.0f), 1e-6f);
    EXPECT_FLOAT_EQ(s.rotation[0], 1.0f); // normalized
}

TEST(Decode, RejectsNonFiniteValues) {
    for (int field = 0; field < 14; ++field) {
        for (const float bad : {kNaN, kInf, -kInf}) {
            RawSplat raw;
            float* fields[14] = {&raw.x,        &raw.y,        &raw.z,        &raw.f_dc[0],
                                 &raw.f_dc[1],  &raw.f_dc[2],  &raw.opacity,  &raw.scale[0],
                                 &raw.scale[1], &raw.scale[2], &raw.rot[0],   &raw.rot[1],
                                 &raw.rot[2],   &raw.rot[3]};
            *fields[field] = bad;
            Splat s;
            EXPECT_FALSE(decode(raw, s)) << "field " << field << " value " << bad;
        }
    }
}

TEST(Decode, RejectsFarAwayPositions) {
    RawSplat raw;
    raw.y = splatthumb::limits::kMaxAbsCoordinate * 2;
    Splat s;
    EXPECT_FALSE(decode(raw, s));
}

TEST(Decode, ClampsHugeScales) {
    RawSplat raw;
    raw.scale[0] = 1e30f;
    raw.scale[1] = -1e30f;
    Splat s;
    ASSERT_TRUE(decode(raw, s));
    EXPECT_TRUE(std::isfinite(s.scale[0]));
    EXPECT_FLOAT_EQ(s.scale[0], std::exp(splatthumb::limits::kMaxLogScale));
    EXPECT_GT(s.scale[1], 0.0f);
}

TEST(Decode, RejectsZeroRotation) {
    RawSplat raw;
    raw.rot[0] = raw.rot[1] = raw.rot[2] = raw.rot[3] = 0.0f;
    Splat s;
    EXPECT_FALSE(decode(raw, s));
}

TEST(Decode, NormalizesExtremeRotation) {
    RawSplat raw;
    raw.rot[0] = std::numeric_limits<float>::max();
    raw.rot[1] = std::numeric_limits<float>::max();
    Splat s;
    ASSERT_TRUE(decode(raw, s));
    EXPECT_NEAR(s.rotation[0], std::sqrt(0.5f), 1e-6f);
}

TEST(Decode, SkipsNearlyTransparent) {
    RawSplat raw;
    raw.opacity = -10.0f; // sigmoid ~ 4.5e-5
    Splat s;
    EXPECT_FALSE(decode(raw, s));
}

TEST(Decode, RejectsShortRecord) {
    std::array<std::byte, 8> rec{};
    Splat s;
    EXPECT_FALSE(splatthumb::decode_standard_record(rec, layout(), s));
}

} // namespace
