#include "thumbnail_provider.h"

#include <chrono>
#include <climits>
#include <filesystem>
#include <new>
#include <optional>
#include <system_error>

#include "bitmap.h"
#include "embedded_image.h"
#include "module.h"
#include "splatthumb/deadline.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "splatthumb/settings.h"
#include "splatthumb/spgm.h"
#include "splatthumb/view_store.h"
#include "stream_byte_source.h"
#include "user_store.h"

namespace splatthumb::shellext {

HRESULT ThumbnailProvider::create(REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;
    auto* provider = new (std::nothrow) ThumbnailProvider();
    if (provider == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = provider->QueryInterface(iid, ppv);
    provider->Release();
    return hr;
}

ThumbnailProvider::ThumbnailProvider() noexcept {
    module_add_ref();
}

ThumbnailProvider::~ThumbnailProvider() {
    if (stream_ != nullptr) {
        stream_->Release();
    }
    module_release();
}

IFACEMETHODIMP ThumbnailProvider::QueryInterface(REFIID iid, void** ppv) noexcept {
    if (ppv == nullptr) {
        return E_POINTER;
    }
    if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IInitializeWithStream))) {
        *ppv = static_cast<IInitializeWithStream*>(this);
    } else if (IsEqualIID(iid, __uuidof(IThumbnailProvider))) {
        *ppv = static_cast<IThumbnailProvider*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::AddRef() noexcept {
    return refs_.fetch_add(1, std::memory_order_relaxed) + 1;
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::Release() noexcept {
    const ULONG left = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (left == 0) {
        delete this;
    }
    return left;
}

IFACEMETHODIMP ThumbnailProvider::Initialize(IStream* stream, DWORD /*mode*/) noexcept {
    if (stream == nullptr) {
        return E_INVALIDARG;
    }
    if (stream_ != nullptr) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    stream->AddRef();
    stream_ = stream;
    return S_OK;
}

IFACEMETHODIMP ThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* bitmap,
                                              WTS_ALPHATYPE* alpha) noexcept {
    if (bitmap == nullptr || alpha == nullptr) {
        return E_POINTER;
    }
    *bitmap = nullptr;
    *alpha = WTSAT_UNKNOWN;
    if (stream_ == nullptr) {
        return E_UNEXPECTED;
    }

    const Deadline deadline =
        Deadline::after(std::chrono::milliseconds(limits::kRenderTimeLimitMs));
    StreamByteSource source(stream_);
    if (!source.ok()) {
        return E_FAIL;
    }

    const Settings settings = read_settings();
    if (!settings.enabled) {
        // Turned off in the settings: Windows shows its own icon.
        return E_FAIL;
    }

    // A Spatialograph Maker project (.spgm) carries its own preview image; show that and
    // read nothing else. Projects without one get Windows' own icon.
    if (const SpgmResult spgm = probe_spgm(source); spgm.probe != SpgmProbe::NotSpgm) {
        if (spgm.probe != SpgmProbe::Ok) {
            return E_FAIL;
        }
        const auto image = read_spgm_image(source, spgm.image);
        if (!image) {
            return E_FAIL;
        }
        const UINT side =
            cx < static_cast<UINT>(limits::kMinThumbnailSize)   ? limits::kMinThumbnailSize
            : cx > static_cast<UINT>(limits::kMaxThumbnailSize) ? limits::kMaxThumbnailSize
                                                                : cx;
        HBITMAP made = nullptr;
        const HRESULT hr = decode_embedded_image(*image, spgm.image.format, side, &made);
        if (FAILED(hr)) {
            return hr == E_OUTOFMEMORY ? E_OUTOFMEMORY : E_FAIL;
        }
        *bitmap = made;
        *alpha = WTSAT_RGB; // laid over the gray background, so opaque
        return S_OK;
    }

    // A saved view is looked up by the file's fingerprint. Without a views folder nothing
    // was ever saved, so the fingerprint is not computed at all.
    std::optional<View> view;
    if (const auto root = views_root()) {
        std::error_code ec;
        if (std::filesystem::is_directory(*root, ec)) {
            if (const auto fp = fingerprint(source)) {
                view = load_view(*root, *fp);
            }
        }
    }

    RenderOptions options;
    // render_thumbnail clamps the size; this only keeps the conversion to int safe.
    options.size = cx > static_cast<UINT>(INT_MAX) ? INT_MAX : static_cast<int>(cx);
    options.background = Background::Gray;
    LoadOptions load;
    load.max_splats = max_splats_for(settings.quality);
    const RenderResult result = render_thumbnail(source, options, load, deadline, nullptr,
                                                 view ? &*view : nullptr);
    switch (result.status) {
    case Status::Ok:
        break;
    case Status::OutOfMemory:
        return E_OUTOFMEMORY;
    default:
        // Not a 3DGS file, broken, too slow or nothing visible: Windows shows its own icon.
        return E_FAIL;
    }

    HBITMAP made = make_bitmap(result.image);
    if (made == nullptr) {
        return E_OUTOFMEMORY;
    }
    *bitmap = made;
    // The gray background is opaque, so there is no alpha to interpret.
    *alpha = WTSAT_RGB;
    return S_OK;
}

} // namespace splatthumb::shellext
