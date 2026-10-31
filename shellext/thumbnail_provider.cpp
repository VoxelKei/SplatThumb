#include "thumbnail_provider.h"

#include <chrono>
#include <climits>
#include <new>

#include "bitmap.h"
#include "module.h"
#include "splatthumb/deadline.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "stream_byte_source.h"

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

    RenderOptions options;
    // render_thumbnail clamps the size; this only keeps the conversion to int safe.
    options.size = cx > static_cast<UINT>(INT_MAX) ? INT_MAX : static_cast<int>(cx);
    options.background = Background::Gray;
    const RenderResult result = render_thumbnail(source, options, LoadOptions{}, deadline);
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
