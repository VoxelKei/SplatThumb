#include "splatthumb/version.h"

namespace splatthumb {

std::string_view version() noexcept {
    return SPLATTHUMB_VERSION_STRING;
}

} // namespace splatthumb
