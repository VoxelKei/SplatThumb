#include "thumbnail_refresh.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <thumbcache.h>

namespace splatthumb::app {

namespace {

// CLSID_LocalThumbnailCache; defined here because not every SDK exports it.
constexpr CLSID kLocalThumbnailCache = {
    0x50EF4544, 0xAC9F, 0x4A8E, {0xB2, 0x1B, 0x8A, 0x26, 0x18, 0x0D, 0xB1, 0x3F}};

// Explorer's cache sizes that SplatThumb thumbnails are used for.
constexpr UINT kSizes[] = {48, 96, 256, 768};

} // namespace

bool refresh_thumbnail(const std::filesystem::path& file) noexcept {
    bool ok = false;
    IShellItem* item = nullptr;
    IThumbnailCache* cache = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(CoCreateInstance(kLocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&cache)))) {
        ok = true;
        for (const UINT size : kSizes) {
            ISharedBitmap* bitmap = nullptr;
            const HRESULT hr = cache->GetThumbnail(item, size, WTS_EXTRACT | WTS_FORCEEXTRACTION,
                                                   &bitmap, nullptr, nullptr);
            if (bitmap != nullptr) {
                bitmap->Release();
            }
            ok = ok && SUCCEEDED(hr);
        }
    }
    if (cache != nullptr) {
        cache->Release();
    }
    if (item != nullptr) {
        item->Release();
    }
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, file.c_str(), nullptr);
    return ok;
}

bool has_cached_thumbnail(const std::filesystem::path& file) noexcept {
    bool cached = false;
    IShellItem* item = nullptr;
    IThumbnailCache* cache = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(CoCreateInstance(kLocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&cache)))) {
        for (const UINT size : kSizes) {
            ISharedBitmap* bitmap = nullptr;
            const HRESULT hr =
                cache->GetThumbnail(item, size, WTS_INCACHEONLY, &bitmap, nullptr, nullptr);
            if (bitmap != nullptr) {
                bitmap->Release();
                cached = cached || hr == S_OK;
            }
            if (cached) {
                break;
            }
        }
    }
    if (cache != nullptr) {
        cache->Release();
    }
    if (item != nullptr) {
        item->Release();
    }
    return cached;
}

} // namespace splatthumb::app
