#pragma once

namespace splatthumb::app {

// Opens the settings window (from the Start menu) and runs until it is closed. Returns the
// process exit code.
int run_settings(int show) noexcept;

} // namespace splatthumb::app
