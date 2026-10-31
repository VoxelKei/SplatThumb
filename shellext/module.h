#pragma once

namespace splatthumb::shellext {

// Counts live COM objects and server locks so DllCanUnloadNow can answer.
void module_add_ref() noexcept;
void module_release() noexcept;
bool module_in_use() noexcept;

} // namespace splatthumb::shellext
