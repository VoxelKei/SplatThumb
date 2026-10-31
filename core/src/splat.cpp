#include "splatthumb/splat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

// Requires offset + 4 <= record.size(); checked by the caller.
float read_f32(std::span<const std::byte> record, std::size_t offset) noexcept {
    float v = 0.0f;
    std::memcpy(&v, record.data() + offset, sizeof v);
    return v;
}

std::uint32_t read_u32(std::span<const std::byte> record, std::size_t offset) noexcept {
    std::uint32_t v = 0;
    std::memcpy(&v, record.data() + offset, sizeof v);
    return v;
}

bool fits(std::span<const std::byte> record, std::size_t offset) noexcept {
    return record.size() >= 4 && offset <= record.size() - 4;
}

// Low `bits` bits of v as a fraction of their maximum, 0..1.
float unorm(std::uint32_t v, unsigned bits) noexcept {
    const std::uint32_t max = (1u << bits) - 1u;
    return static_cast<float>(v & max) / static_cast<float>(max);
}

constexpr float kShC0 = 0.28209479177387814f;

} // namespace

float sigmoid(float x) noexcept {
    if (x >= 0.0f) {
        return 1.0f / (1.0f + std::exp(-x));
    }
    const float e = std::exp(x);
    return e / (1.0f + e);
}

bool decode_standard_record(std::span<const std::byte> record, const StandardLayout& layout,
                            Splat& out) noexcept {
    // Little-endian floats only; every supported Windows target is little-endian.
    static_assert(sizeof(float) == 4);

    const auto in_record = [&](std::size_t offset) { return offset <= record.size() - 4; };
    if (record.size() < 4) {
        return false;
    }
    const std::size_t offsets[] = {
        layout.x,        layout.y,        layout.z,        layout.f_dc[0], layout.f_dc[1],
        layout.f_dc[2],  layout.opacity,  layout.scale[0], layout.scale[1], layout.scale[2],
        layout.rot[0],   layout.rot[1],   layout.rot[2],   layout.rot[3]};
    float v[14];
    for (std::size_t i = 0; i < 14; ++i) {
        if (!in_record(offsets[i])) {
            return false;
        }
        v[i] = read_f32(record, offsets[i]);
        if (!std::isfinite(v[i])) {
            return false;
        }
    }

    for (std::size_t i = 0; i < 3; ++i) {
        if (std::fabs(v[i]) > limits::kMaxAbsCoordinate) {
            return false;
        }
        out.position[i] = v[i];
        out.color[i] = std::clamp(0.5f + kShC0 * v[3 + i], 0.0f, 1.0f);
        out.scale[i] = std::exp(std::clamp(v[7 + i], limits::kMinLogScale, limits::kMaxLogScale));
    }

    out.opacity = sigmoid(v[6]);
    if (!(out.opacity >= limits::kMinOpacity)) {
        return false;
    }

    // Normalize in double so that tiny or huge (but finite) components cannot overflow.
    const double w = v[10], x = v[11], y = v[12], z = v[13];
    const double len = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(len > 1e-8) || !std::isfinite(len)) {
        return false;
    }
    out.rotation = {static_cast<float>(w / len), static_cast<float>(x / len),
                    static_cast<float>(y / len), static_cast<float>(z / len)};
    return true;
}

bool decode_compressed_record(std::span<const std::byte> record, std::span<const std::byte> chunk,
                              const CompressedLayout& layout, Splat& out) noexcept {
    // Value ranges from the chunk: 6 position, 6 scale, optionally 6 color (min xyz, max xyz).
    float range[18];
    for (std::size_t i = 0; i < 6; ++i) {
        if (!fits(chunk, layout.position[i]) || !fits(chunk, layout.scale[i])) {
            return false;
        }
        range[i] = read_f32(chunk, layout.position[i]);
        range[6 + i] = read_f32(chunk, layout.scale[i]);
        if (layout.has_color_range) {
            if (!fits(chunk, layout.color[i])) {
                return false;
            }
            range[12 + i] = read_f32(chunk, layout.color[i]);
        } else {
            range[12 + i] = i < 3 ? 0.0f : 1.0f;
        }
    }
    for (float r : range) {
        if (!std::isfinite(r)) {
            return false;
        }
    }
    if (!fits(record, layout.packed_position) || !fits(record, layout.packed_rotation) ||
        !fits(record, layout.packed_scale) || !fits(record, layout.packed_color)) {
        return false;
    }
    const std::uint32_t pos = read_u32(record, layout.packed_position);
    const std::uint32_t rot = read_u32(record, layout.packed_rotation);
    const std::uint32_t scl = read_u32(record, layout.packed_scale);
    const std::uint32_t col = read_u32(record, layout.packed_color);

    // Position and log scale: 11, 10 and 11 bits, each a fraction of the chunk's range.
    const float pos_t[3] = {unorm(pos >> 21, 11), unorm(pos >> 11, 10), unorm(pos, 11)};
    const float scl_t[3] = {unorm(scl >> 21, 11), unorm(scl >> 11, 10), unorm(scl, 11)};
    // Color: 8 bits each for r, g, b (fractions of the chunk's range) and opacity.
    const float col_t[3] = {unorm(col >> 24, 8), unorm(col >> 16, 8), unorm(col >> 8, 8)};
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    for (std::size_t i = 0; i < 3; ++i) {
        const float p = lerp(range[i], range[3 + i], pos_t[i]);
        if (!std::isfinite(p) || std::fabs(p) > limits::kMaxAbsCoordinate) {
            return false;
        }
        out.position[i] = p;
        const float s = lerp(range[6 + i], range[9 + i], scl_t[i]);
        if (!std::isfinite(s)) {
            return false;
        }
        out.scale[i] = std::exp(std::clamp(s, limits::kMinLogScale, limits::kMaxLogScale));
        // The stored color is already 0.5 + SH_C0 * f_dc.
        const float c = lerp(range[12 + i], range[15 + i], col_t[i]);
        if (!std::isfinite(c)) {
            return false;
        }
        out.color[i] = std::clamp(c, 0.0f, 1.0f);
    }
    // Stored after the sigmoid.
    out.opacity = unorm(col, 8);
    if (!(out.opacity >= limits::kMinOpacity)) {
        return false;
    }

    // Rotation: the top 2 bits say which of (w, x, y, z) is largest; the other three follow
    // in order as 10-bit values in [-1/sqrt(2), 1/sqrt(2)].
    constexpr float kNorm = 1.41421356237309505f;
    const float small[3] = {(unorm(rot >> 20, 10) - 0.5f) * kNorm,
                            (unorm(rot >> 10, 10) - 0.5f) * kNorm, (unorm(rot, 10) - 0.5f) * kNorm};
    const float sum = small[0] * small[0] + small[1] * small[1] + small[2] * small[2];
    const std::size_t largest = rot >> 30;
    std::array<float, 4> q{};
    for (std::size_t i = 0, k = 0; i < 4; ++i) {
        q[i] = i == largest ? std::sqrt(std::max(0.0f, 1.0f - sum)) : small[k++];
    }
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(len > 1e-8f)) {
        return false;
    }
    out.rotation = {q[0] / len, q[1] / len, q[2] / len, q[3] / len};
    return true;
}

} // namespace splatthumb
