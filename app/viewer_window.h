#pragma once

#include <filesystem>

namespace splatthumb::app {

// Opens the viewpoint window for one PLY file and runs until it is closed. Shows a message
// instead when the file cannot be opened or is not a 3DGS PLY. Returns the process exit code.
int run_viewer(const std::filesystem::path& file, int show) noexcept;

} // namespace splatthumb::app
