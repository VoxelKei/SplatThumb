#include "splatthumb/splat.h"

#include <algorithm>
#include <cmath>
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

} // namespace splatthumb
