// Fuzzes the small text parsers the viewer uses: saved views and the first camera of a
// cameras.json. Nothing may crash; an accepted view must make a usable camera and survive a
// format/parse round trip unchanged.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "splatthumb/training_camera.h"
#include "splatthumb/view.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (const auto cam = splatthumb::parse_first_training_camera(text)) {
        splatthumb::View v;
        v.eye = {0, 0, -10};
        v.up = {0, -1, 0};
        v.radius = 2;
        const auto framing = splatthumb::camera_from_view(v);
        if (const auto tv = splatthumb::view_from_training_camera(*cam, framing.camera)) {
            if (splatthumb::camera_from_view(*tv).status != splatthumb::Status::Ok) {
                std::abort();
            }
        }
    }
    const auto view = splatthumb::parse_view(text);
    if (!view) {
        return 0;
    }
    if (splatthumb::camera_from_view(*view).status != splatthumb::Status::Ok) {
        std::abort();
    }
    const auto again = splatthumb::parse_view(splatthumb::format_view(*view));
    if (!again || !(*again == *view)) {
        std::abort();
    }
    return 0;
}
