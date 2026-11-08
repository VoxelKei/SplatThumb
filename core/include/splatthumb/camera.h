#pragma once

#include <array>
#include <span>

#include "splatthumb/splat.h"
#include "splatthumb/status.h"

namespace splatthumb {

using Vec3 = std::array<float, 3>;

struct Camera {
    Vec3 eye{};
    Vec3 target{};
    // Orthonormal basis: forward points from eye to target; up is the screen's up.
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    float fov_y = 0.0f; // radians
    // Radius of the sphere around target that the view is framed to contain.
    float radius = 0.0f;
    // Splats nearer to the eye than this (along forward) are not drawn. Zero means only
    // the renderer's minimum applies.
    float near_clip = 0.0f;
};

struct CameraResult {
    Status status = Status::Ok;
    Camera camera;
};

// View settings for the automatic camera.
struct AutoCameraOptions {
    float azimuth_deg = 0.0f; // straight in front (eye on the -Z side, x = target x)
    float elevation_deg = 20.0f;
    float fov_y_deg = 45.0f;
    // Positions outside these percentiles (per axis) are ignored as floaters.
    float low_percentile = 0.01f;
    float high_percentile = 0.99f;
};

// Builds a camera at eye looking at target, with up_hint giving the screen's up direction
// (it need not be exactly perpendicular). radius frames a sphere around target; splats well
// in front of that sphere are clipped. Fails with NothingToDraw when the inputs are not
// finite, eye and target coincide, up_hint is parallel to the view direction, or radius
// or fov_y_deg is out of range.
CameraResult look_at(const Vec3& eye, const Vec3& target, const Vec3& up_hint, float fov_y_deg,
                     float radius) noexcept;

// Chooses a view of the splats: target at the per-axis median, framed to fit the
// low..high percentile box, looking down from above with -Y as up (COLMAP convention).
CameraResult auto_camera(std::span<const Splat> splats,
                         const AutoCameraOptions& options = {}) noexcept;

// Distance along forward below which splats are not drawn.
float near_distance(const Camera& camera) noexcept;

// False when the splat cannot show up in a square image from this camera: it is not
// beyond near_distance, or it lies (with its extent) entirely outside the field of view.
// True for anything that might show, so it never drops a visible splat.
bool may_be_visible(const Splat& splat, const Camera& camera) noexcept;

} // namespace splatthumb
