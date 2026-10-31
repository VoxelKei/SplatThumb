#pragma once

#include <windows.h>

namespace splatthumb::shellext {

// Class ID of the thumbnail handler. Never change it: installers and registrations
// written by earlier versions refer to it.
// {09C68D16-D21F-4B95-97A1-29B45AEBF4CC}
inline constexpr CLSID kThumbnailProviderClsid = {
    0x09c68d16, 0xd21f, 0x4b95, {0x97, 0xa1, 0x29, 0xb4, 0x5a, 0xeb, 0xf4, 0xcc}};

} // namespace splatthumb::shellext
