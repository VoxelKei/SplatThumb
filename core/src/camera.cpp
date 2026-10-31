#include "splatthumb/camera.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <numbers>
#include <vector>

namespace splatthumb {

namespace {

constexpr float kMinRadius = 1e-3f;

float radians(float deg) noexcept {
    return deg * std::numbers::pi_v<float> / 180.0f;
}

Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 normalize(const Vec3& v) noexcept {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return {v[0] / len, v[1] / len, v[2] / len};
}

// Value at fraction p (0..1) of the sorted values; reorders values.
float percentile(std::vector<float>& values, float p) noexcept {
    const auto last = static_cast<float>(values.size() - 1);
    const auto k = static_cast<std::size_t>(std::clamp(p, 0.0f, 1.0f) * last + 0.5f);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k),
                     values.end());
    return values[k];
}

} // namespace

CameraResult auto_camera(std::span<const Splat> splats, const AutoCameraOptions& options) noexcept {
    CameraResult result;
    if (splats.empty()) {
        result.status = Status::NothingToDraw;
        return result;
    }
    try {
        std::vector<float> axis(splats.size());
        Vec3 lo{}, hi{}, mid{};
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t i = 0; i < splats.size(); ++i) {
                axis[i] = splats[i].position[a];
            }
            lo[a] = percentile(axis, options.low_percentile);
            hi[a] = percentile(axis, options.high_percentile);
            mid[a] = percentile(axis, 0.5f);
        }

        // Frame the box around the median target. Positions are bounded by
        // limits::kMaxAbsCoordinate, so these sums cannot overflow.
        float r2 = 0.0f;
        for (std::size_t a = 0; a < 3; ++a) {
            const float d = std::max(mid[a] - lo[a], hi[a] - mid[a]);
            r2 += d * d;
        }
        const float radius = std::max(std::sqrt(r2), kMinRadius);

        const float az = radians(options.azimuth_deg);
        const float el = radians(std::clamp(options.elevation_deg, -80.0f, 80.0f));
        const float fov = radians(std::clamp(options.fov_y_deg, 1.0f, 170.0f));
        // World up is -Y. Elevation moves the eye toward world up.
        const Vec3 world_up{0.0f, -1.0f, 0.0f};
        const Vec3 dir{std::cos(el) * std::sin(az), -std::sin(el), std::cos(el) * std::cos(az)};
        const float distance = radius / std::sin(fov * 0.5f);

        Camera& cam = result.camera;
        cam.target = mid;
        cam.eye = {mid[0] + dir[0] * distance, mid[1] + dir[1] * distance,
                   mid[2] + dir[2] * distance};
        cam.forward = normalize(sub(cam.target, cam.eye));
        cam.right = normalize(cross(cam.forward, world_up));
        cam.up = cross(cam.right, cam.forward);
        cam.fov_y = fov;
        cam.radius = radius;
    } catch (const std::bad_alloc&) {
        result.status = Status::OutOfMemory;
    }
    return result;
}

} // namespace splatthumb
