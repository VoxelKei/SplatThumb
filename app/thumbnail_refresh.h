#pragma once

#include <filesystem>

namespace splatthumb::app {

// Asks Windows to make the file's thumbnail again at the common sizes and tells Explorer
// the file changed, so a new view or setting shows without pressing F5. Only the
// thumbnail cache entries for this file are replaced. Returns false if Windows refused.
bool refresh_thumbnail(const std::filesystem::path& file) noexcept;

} // namespace splatthumb::app
