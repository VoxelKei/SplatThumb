#pragma once

#include <filesystem>

namespace splatthumb::app {

// Asks Windows to make the file's thumbnail again at the common sizes and tells Explorer
// the file changed, so a new view or setting shows without pressing F5. Only the
// thumbnail cache entries for this file are replaced. Returns false if Windows refused.
// If given, on_step(context, done, total) is called after each size is made.
using RefreshStep = void (*)(void* context, unsigned done, unsigned total) noexcept;
bool refresh_thumbnail(const std::filesystem::path& file, RefreshStep on_step = nullptr,
                       void* context = nullptr) noexcept;

// Whether Windows' thumbnail cache already holds a thumbnail of the file at one of the common
// sizes. Only reads the cache; never makes a thumbnail.
bool has_cached_thumbnail(const std::filesystem::path& file) noexcept;

} // namespace splatthumb::app
