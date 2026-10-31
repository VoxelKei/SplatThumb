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
    float azimuth_deg = 45.0f;
    float elevation_deg = 20.0f;
    float fov_y_deg = 45.0f;
    // Positions outside these percentiles (per axis) are ignored as floaters.
    float low_percentile = 0.01f;
    float high_percentile = 0.99f;
};

// Chooses a view of the splats: target at the per-axis median, framed to fit the
// low..high percentile box, looking down from above with -Y as up (COLMAP convention).
CameraResult auto_camera(std::span<const Splat> splats,
                         const AutoCameraOptions& options = {}) noexcept;

} // namespace splatthumb
