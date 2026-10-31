#include "splatthumb/settings.h"

#include "splatthumb/limits.h"

namespace splatthumb {

Settings settings_from_stored(const StoredSettings& stored) noexcept {
    Settings s;
    if (stored.enabled) {
        s.enabled = *stored.enabled != 0;
    }
    if (stored.quality) {
        switch (*stored.quality) {
        case 0: s.quality = Quality::Fast; break;
        case 1: s.quality = Quality::Standard; break;
        case 2: s.quality = Quality::High; break;
        default: break;
        }
    }
    return s;
}

StoredSettings stored_from_settings(const Settings& settings) noexcept {
    StoredSettings out;
    out.enabled = settings.enabled ? 1u : 0u;
    switch (settings.quality) {
    case Quality::Fast: out.quality = 0u; break;
    case Quality::Standard: out.quality = 1u; break;
    case Quality::High: out.quality = 2u; break;
    }
    return out;
}

std::size_t max_splats_for(Quality quality) noexcept {
    switch (quality) {
    case Quality::Fast: return limits::kFastRenderSplats;
    case Quality::Standard: return limits::kStandardRenderSplats;
    case Quality::High: return limits::kMaxRenderSplats;
    }
    return limits::kStandardRenderSplats;
}

} // namespace splatthumb
