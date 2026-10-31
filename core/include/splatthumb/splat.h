#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "splatthumb/classify.h"

namespace splatthumb {

// One Gaussian, already converted from file values to rendering values.
struct Splat {
    std::array<float, 3> position{};
    std::array<float, 3> color{};    // linear 0..1
    float opacity = 0.0f;            // 0..1
    std::array<float, 3> scale{};    // standard deviations, > 0
    std::array<float, 4> rotation{}; // unit quaternion (w, x, y, z)
};

// Converts one standard-layout vertex record. Returns false (and leaves out unspecified)
// when the record must be skipped: non-finite values, out-of-range position, near-zero
// rotation, or opacity below limits::kMinOpacity.
bool decode_standard_record(std::span<const std::byte> record, const StandardLayout& layout,
                            Splat& out) noexcept;

// Numerically safe logistic function.
float sigmoid(float x) noexcept;

} // namespace splatthumb
