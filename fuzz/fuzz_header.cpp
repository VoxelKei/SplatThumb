// Feeds arbitrary bytes to parse_header() and classify(), then loads and frames accepted
// standard files. Any crash, sanitizer report, accepted file whose declared data does not
// fit in the input, or non-finite camera is a bug.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "splatthumb/camera.h"
#include "splatthumb/classify.h"
#include "splatthumb/loader.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    splatthumb::MemoryByteSource source(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
    const splatthumb::Classification c = splatthumb::inspect(source);
    if (c.kind != splatthumb::SplatKind::NotSplat) {
        if (c.vertex_count == 0 || c.vertex_data_offset > size ||
            c.vertex_count > (size - c.vertex_data_offset) / c.vertex_stride) {
            __builtin_trap();
        }
    }
    if (c.kind == splatthumb::SplatKind::Standard) {
        splatthumb::LoadOptions options;
        options.max_splats = 1000;
        const auto loaded = splatthumb::load_splats(
            source, c, options, splatthumb::Deadline::after(std::chrono::seconds(1)));
        if (loaded.status == splatthumb::Status::Ok) {
            const auto cam = splatthumb::auto_camera(loaded.splats);
            if (cam.status == splatthumb::Status::Ok &&
                (!std::isfinite(cam.camera.eye[0]) || !std::isfinite(cam.camera.radius))) {
                __builtin_trap();
            }
        }
    }
    return 0;
}
