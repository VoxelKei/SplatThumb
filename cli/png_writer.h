#pragma once

#include <filesystem>

#include "splatthumb/render.h"

namespace splatthumb::cli {

// Writes img (premultiplied RGBA) as a straight-alpha PNG. Returns false on failure.
bool write_png(const std::filesystem::path& path, const Image& img) noexcept;

} // namespace splatthumb::cli
