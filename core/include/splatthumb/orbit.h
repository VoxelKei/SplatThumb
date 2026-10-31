#pragma once

#include "splatthumb/view.h"

namespace splatthumb {

// Mouse-style changes to a view, for the viewer window. Each returns the view unchanged
// when the result would not make a valid camera (see camera_from_view).

// Turns the camera around the target: yaw about the screen's up axis, then pitch about the
// screen's right axis. There is no fixed world up, so the view can go over the top.
View orbit(const View& view, float yaw_rad, float pitch_rad) noexcept;

// Turns the picture on the screen (rotates up about the viewing direction). Positive turns
// the picture counterclockwise.
View roll(const View& view, float angle_rad) noexcept;

// Moves toward (factor < 1) or away from (factor > 1) the target. The framed radius scales
// with the distance so the near clip stays in front of the subject.
View zoom(const View& view, float factor) noexcept;

// Shifts the camera and target sideways in screen space, by fractions of the framed radius.
View pan(const View& view, float right, float down) noexcept;

} // namespace splatthumb
