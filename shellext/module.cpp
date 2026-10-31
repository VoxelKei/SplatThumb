#include "module.h"

#include <atomic>

namespace splatthumb::shellext {

namespace {
std::atomic<long> g_count{0};
} // namespace

void module_add_ref() noexcept {
    g_count.fetch_add(1, std::memory_order_relaxed);
}

void module_release() noexcept {
    g_count.fetch_sub(1, std::memory_order_relaxed);
}

bool module_in_use() noexcept {
    return g_count.load(std::memory_order_relaxed) != 0;
}

} // namespace splatthumb::shellext
