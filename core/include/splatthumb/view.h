#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "splatthumb/camera.h"
#include "splatthumb/settings.h"

namespace splatthumb {

// A saved viewpoint ("recipe") for one file: where the camera is, what it looks at, and
// how much it frames. Only these numbers are stored, never an image.
struct View {
    Vec3 eye{};
    Vec3 target{};
    Vec3 up{};
    float fov_y_deg = 45.0f;
    float radius = 1.0f;
    // Quality for this file only; nullopt follows the global setting.
    std::optional<Quality> quality;

    friend bool operator==(const View&, const View&) = default;
};

// The view that reproduces camera.
View view_from_camera(const Camera& camera) noexcept;

// The camera for a view; fails like look_at().
CameraResult camera_from_view(const View& view) noexcept;

// Text form of a view file:
//   splatthumb-view 1
//   eye <x> <y> <z>
//   target <x> <y> <z>
//   up <x> <y> <z>
//   fov <degrees>
//   radius <r>
//   quality <0|1|2>     (optional: 0 fast, 1 standard, 2 high)
std::string format_view(const View& view);

// Parses the text form. Returns nullopt for anything else: wrong first line, missing or
// repeated keys, numbers that are not finite or beyond limits::kMaxAbsCoordinate, a view
// camera_from_view() would reject, or more than limits::kMaxViewFileBytes of text. Unknown
// keys are ignored so later versions can add some, and so is a quality number this version
// does not know (the view itself is kept); a quality that is not a plain number, or appears
// twice, rejects the file like any other malformed line.
std::optional<View> parse_view(std::string_view text) noexcept;

} // namespace splatthumb
