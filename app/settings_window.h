#pragma once

namespace splatthumb::app {

// The settings window's class, so that other windows can find an open one.
inline constexpr wchar_t kSettingsWindowClass[] = L"SplatThumbSettings";

// Opens the settings window (from the Start menu) and runs until it is closed. Returns the
// process exit code.
int run_settings(int show) noexcept;

} // namespace splatthumb::app
