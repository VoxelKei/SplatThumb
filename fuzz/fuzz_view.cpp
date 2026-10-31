// Fuzzes the saved-view parser: arbitrary text must never crash parse_view, and anything it
// accepts must make a usable camera and survive a format/parse round trip unchanged.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "splatthumb/view.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
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
