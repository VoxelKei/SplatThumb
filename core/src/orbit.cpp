#include "splatthumb/orbit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 scale(const Vec3& a, float s) noexcept {
    return {a[0] * s, a[1] * s, a[2] * s};
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float dot(const Vec3& a, const Vec3& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Rotates v about the unit axis by angle (right-handed, Rodrigues' formula).
Vec3 rotate(const Vec3& v, const Vec3& axis, float angle) noexcept {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1.0f - c)));
}

// The view if it makes a camera and stays within the coordinate limit, otherwise fallback.
View checked(const View& candidate, const View& fallback) noexcept {
    for (const Vec3* v : {&candidate.eye, &candidate.target}) {
        for (const float x : *v) {
            if (!std::isfinite(x) || std::fabs(x) > limits::kMaxAbsCoordinate) {
                return fallback;
            }
        }
    }
    if (!(candidate.radius <= limits::kMaxAbsCoordinate)) {
        return fallback;
    }
    return camera_from_view(candidate).status == Status::Ok ? candidate : fallback;
}

} // namespace

View orbit(const View& view, float yaw_rad, float pitch_rad) noexcept {
    const CameraResult cam = camera_from_view(view);
    if (cam.status != Status::Ok || !std::isfinite(yaw_rad) || !std::isfinite(pitch_rad)) {
        return view;
    }
    const Camera& c = cam.camera;
    // The fixed axis: file Y, pointing the way the screen's up mostly points (-Y for files
    // shown upright, as COLMAP-style data is).
    const Vec3 axis{0.0f, c.up[1] <= 0.0f ? -1.0f : 1.0f, 0.0f};

    // Yaw about the axis: the eye keeps its height along Y.
    Vec3 offset = rotate(sub(view.eye, view.target), axis, yaw_rad);
    Vec3 right = rotate(c.right, axis, yaw_rad);
    Vec3 up = rotate(c.up, axis, yaw_rad);

    // Pitch about the yawed screen right, limited so the eye never reaches the axis.
    const float length = std::sqrt(dot(offset, offset));
    if (!(length > 0.0f)) {
        return view;
    }
    const float height = std::asin(std::clamp(dot(offset, axis) / length, -1.0f, 1.0f));
    const float limit = kOrbitPitchLimitDeg * std::numbers::pi_v<float> / 180.0f;
    const float pitch = std::clamp(height + pitch_rad, -limit, limit) - height;
    // Moving the mouse up should show more of the top: the eye goes up, i.e. turns about
    // right by a negative angle in this right-handed setup.
    offset = rotate(offset, right, -pitch);
    up = rotate(up, right, -pitch);

    View out = view;
    out.eye = add(view.target, offset);
    out.up = up;
    return checked(out, view);
}

View roll(const View& view, float angle_rad) noexcept {
    const CameraResult cam = camera_from_view(view);
    if (cam.status != Status::Ok) {
        return view;
    }
    View out = view;
    out.up = rotate(cam.camera.up, cam.camera.forward, angle_rad);
    return checked(out, view);
}

View zoom(const View& view, float factor) noexcept {
    if (!std::isfinite(factor) || !(factor > 0.0f)) {
        return view;
    }
    View out = view;
    out.eye = add(view.target, scale(sub(view.eye, view.target), factor));
    out.radius = view.radius * factor;
    return checked(out, view);
}

View pan(const View& view, float right, float down) noexcept {
    const CameraResult cam = camera_from_view(view);
    if (cam.status != Status::Ok || !std::isfinite(right) || !std::isfinite(down)) {
        return view;
    }
    const Camera& c = cam.camera;
    const Vec3 shift = add(scale(c.right, right * view.radius), scale(c.up, -down * view.radius));
    View out = view;
    out.eye = add(view.eye, shift);
    out.target = add(view.target, shift);
    return checked(out, view);
}

} // namespace splatthumb
