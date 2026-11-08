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
#include "splatthumb/view.h"

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
    Background background = Background::Gray;
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
    double inspect_ms = 0, load_ms = 0, camera_ms = 0, importance_ms = 0, render_ms = 0;
    std::uint64_t vertex_count = 0;
    std::uint64_t splats_loaded = 0;
    // Records looked at to pick the most important splats; 0 when the sample was drawn.
    std::uint64_t importance_records_read = 0;
    std::size_t splats_drawn = 0;
};

// inspect + load_splats + auto_camera + render_splats under one deadline. A given view
// replaces auto_camera; if it cannot make a camera, auto_camera is used instead. When the
// file has more splats than load_options.max_splats and load_options.importance is set,
// up to limits::kImportanceReadMs more is spent in load_important_splats, and its splats
// are drawn if it read at least twice as many records as the sample.
RenderResult render_thumbnail(ByteSource& source, const RenderOptions& options,
                              const LoadOptions& load_options, const Deadline& deadline,
                              ThumbnailStats* stats = nullptr,
                              const View* view = nullptr) noexcept;

} // namespace splatthumb
