// Feeds arbitrary bytes to parse_header() and classify(), then loads and frames accepted
// standard and compressed files, and renders them at the smallest size. Any crash, sanitizer report,
// accepted file whose declared data does not fit in the input, non-finite camera or
// wrongly sized image is a bug.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "splatthumb/camera.h"
#include "splatthumb/classify.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"

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
    if (c.kind == splatthumb::SplatKind::Compressed) {
        const auto& l = c.compressed;
        if (l.chunk_count * splatthumb::limits::kCompressedChunkSize < c.vertex_count ||
            l.chunk_data_offset > size || l.chunk_stride == 0 ||
            l.chunk_count > (size - l.chunk_data_offset) / l.chunk_stride) {
            __builtin_trap();
        }
    }
    if (c.kind != splatthumb::SplatKind::NotSplat) {
        splatthumb::LoadOptions options;
        options.max_splats = 1000;
        const auto loaded = splatthumb::load_splats(
            source, c, options, splatthumb::Deadline::after(std::chrono::seconds(1)));
        if (loaded.status == splatthumb::Status::Ok) {
            const auto cam = splatthumb::auto_camera(loaded.splats);
            if (cam.status == splatthumb::Status::Ok) {
                if (!std::isfinite(cam.camera.eye[0]) || !std::isfinite(cam.camera.radius)) {
                    __builtin_trap();
                }
                splatthumb::RenderOptions render_options;
                render_options.size = splatthumb::limits::kMinThumbnailSize;
                const auto img = splatthumb::render_splats(
                    loaded.splats, cam.camera, render_options,
                    splatthumb::Deadline::after(std::chrono::seconds(1)));
                if (img.status == splatthumb::Status::Ok &&
                    img.image.rgba.size() != static_cast<std::size_t>(img.image.size) *
                                                 static_cast<std::size_t>(img.image.size) * 4) {
                    __builtin_trap();
                }
            }
        }
    }
    return 0;
}
