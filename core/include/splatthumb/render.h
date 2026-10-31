#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "splatthumb/byte_source.h"
#include "splatthumb/camera.h"
#include "splatthumb/deadline.h"
#include "splatthumb/loader.h"
#include "splatthumb/splat.h"
#include "splatthumb/status.h"

namespace splatthumb {

// Square RGBA image, 8 bits per channel, premultiplied alpha, rows top to bottom.
struct Image {
    int size = 0;
    std::vector<std::uint8_t> rgba;
};

enum class Background {
    Transparent,
    Gray,
};

struct RenderOptions {
    int size = 256; // clamped to limits::kMinThumbnailSize..kMaxThumbnailSize
    Background background = Background::Transparent;
};

struct RenderResult {
    Status status = Status::Ok;
    Image image;
    std::size_t splats_drawn = 0;
};

// Draws splats as screen-space elliptical Gaussians (EWA), back to front.
RenderResult render_splats(std::span<const Splat> splats, const Camera& camera,
                           const RenderOptions& options, const Deadline& deadline) noexcept;

// Timings in milliseconds for each step of render_thumbnail.
struct ThumbnailStats {
    double inspect_ms = 0, load_ms = 0, camera_ms = 0, render_ms = 0;
    std::uint64_t vertex_count = 0;
    std::uint64_t splats_loaded = 0;
    std::size_t splats_drawn = 0;
};

// inspect + load_splats + auto_camera + render_splats under one deadline.
RenderResult render_thumbnail(ByteSource& source, const RenderOptions& options,
                              const LoadOptions& load_options, const Deadline& deadline,
                              ThumbnailStats* stats = nullptr) noexcept;

} // namespace splatthumb
