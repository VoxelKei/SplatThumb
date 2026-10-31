#include "splatthumb/settings.h"

#include "splatthumb/limits.h"

namespace splatthumb {

Settings settings_from_stored(const StoredSettings& stored) noexcept {
    Settings s;
    if (stored.enabled) {
        s.enabled = *stored.enabled != 0;
    }
    if (stored.background) {
        switch (*stored.background) {
        case 0: s.background = Background::Gray; break;
        case 1: s.background = Background::Transparent; break;
        case 2: s.background = Background::White; break;
        case 3: s.background = Background::Black; break;
        default: break;
        }
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
    switch (settings.background) {
    case Background::Gray: out.background = 0u; break;
    case Background::Transparent: out.background = 1u; break;
    case Background::White: out.background = 2u; break;
    case Background::Black: out.background = 3u; break;
    }
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
