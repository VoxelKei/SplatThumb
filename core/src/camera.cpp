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
constexpr float kCoreOpacity = 0.5f;
constexpr float kLargeScalePercentile = 0.9f;
constexpr std::size_t kMinCoreSplats = 100;
constexpr float kNearDistancePercentile = 0.9f;
constexpr float kNearDistanceMargin = 2.0f;
constexpr float kNearClipRadii = 1.5f;

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
        // Framing uses the solid core of the scene: opaque splats that are not among the
        // largest 10%. Big, faint splats are mostly floaters. Too few core splats means a
        // tiny or unusual file, so fall back to all of them.
        std::vector<float> values(splats.size());
        for (std::size_t i = 0; i < splats.size(); ++i) {
            const auto& sc = splats[i].scale;
            values[i] = std::max({sc[0], sc[1], sc[2]});
        }
        const float big_scale = percentile(values, kLargeScalePercentile);
        std::vector<const Splat*> core;
        core.reserve(splats.size());
        for (const Splat& s : splats) {
            const auto& sc = s.scale;
            if (s.opacity >= kCoreOpacity && std::max({sc[0], sc[1], sc[2]}) <= big_scale) {
                core.push_back(&s);
            }
        }
        if (core.size() < kMinCoreSplats) {
            core.clear();
            for (const Splat& s : splats) {
                core.push_back(&s);
            }
        }

        values.resize(core.size());
        Vec3 lo{}, hi{}, mid{};
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t i = 0; i < core.size(); ++i) {
                values[i] = core[i]->position[a];
            }
            lo[a] = percentile(values, options.low_percentile);
            hi[a] = percentile(values, options.high_percentile);
            mid[a] = percentile(values, 0.5f);
        }

        // Frame the box around the median target. Positions are bounded by
        // limits::kMaxAbsCoordinate, so these sums cannot overflow.
        float r2 = 0.0f;
        for (std::size_t a = 0; a < 3; ++a) {
            const float d = std::max(mid[a] - lo[a], hi[a] - mid[a]);
            r2 += d * d;
        }
        float radius = std::sqrt(r2);

        // A scene spreads thinly far beyond its dense middle, which the box alone would
        // shrink to a speck. Cap the radius by how far most core splats are from the target.
        for (std::size_t i = 0; i < core.size(); ++i) {
            const Vec3 d = sub(core[i]->position, mid);
            values[i] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }
        const float near_distance = percentile(values, kNearDistancePercentile);
        radius = std::min(radius, kNearDistanceMargin * near_distance);
        radius = std::max(radius, kMinRadius);

        const float az = radians(options.azimuth_deg);
        const float el = radians(std::clamp(options.elevation_deg, -80.0f, 80.0f));
        const float fov = radians(std::clamp(options.fov_y_deg, 1.0f, 170.0f));
        // World up is -Y. Elevation moves the eye toward world up. Azimuth 0 puts the eye
        // on the -Z side looking toward +Z, the way COLMAP capture cameras face the subject.
        const Vec3 world_up{0.0f, -1.0f, 0.0f};
        const Vec3 dir{std::cos(el) * std::sin(az), -std::sin(el), -std::cos(el) * std::cos(az)};
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
        // Leave out what lies well in front of the framed sphere: in a scene that is
        // foreground clutter right in front of the lens, and an object has nothing there.
        cam.near_clip = distance - kNearClipRadii * radius;
    } catch (const std::bad_alloc&) {
        result.status = Status::OutOfMemory;
    }
    return result;
}

} // namespace splatthumb
