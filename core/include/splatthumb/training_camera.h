#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>

#include "splatthumb/camera.h"
#include "splatthumb/view.h"

namespace splatthumb {

// One camera from the cameras.json that the original 3D Gaussian Splatting training code
// writes next to its output: position, camera-to-world rotation (columns are the camera's
// right, down and forward axes, OpenCV style) and the vertical focal length in pixels.
struct TrainingCamera {
    Vec3 position{};
    std::array<Vec3, 3> rotation{}; // rows
    float height = 0.0f;
    float fy = 0.0f;
};

// Largest cameras.json prefix that is looked at; only the first camera is used.
inline constexpr std::size_t kMaxCamerasJsonBytes = 1024 * 1024;

// Reads the first camera of a cameras.json text. Only the first top-level object is
// examined, and only its "position", "rotation", "height" and "fy" keys. Returns nullopt
// when any of them is missing or malformed, a number is not finite or out of range, or the
// rotation is not a proper rotation.
std::optional<TrainingCamera> parse_first_training_camera(std::string_view json) noexcept;

// Looks for cameras.json in the PLY's folder and up to three folders above it (the training
// code writes point_cloud/iteration_N/point_cloud.ply below it) and reads its first camera.
// At most kMaxCamerasJsonBytes are read.
std::optional<TrainingCamera> find_training_camera(const std::filesystem::path& ply) noexcept;

// A view from the training camera, looking at the depth of `framing`'s target and framing
// the same radius. Returns nullopt if no valid camera results.
std::optional<View> view_from_training_camera(const TrainingCamera& camera,
                                              const Camera& framing) noexcept;

} // namespace splatthumb
