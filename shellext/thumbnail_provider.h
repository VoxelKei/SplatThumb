#pragma once

#include <windows.h>

#include <propsys.h>
#include <thumbcache.h>

#include <atomic>

namespace splatthumb::shellext {

// The thumbnail handler Explorer creates for each .ply file. Explorer runs it in an
// isolated surrogate process, so it only reads the stream it is given.
class ThumbnailProvider final : public IInitializeWithStream, public IThumbnailProvider {
public:
    // Creates an object with one reference and returns the requested interface.
    static HRESULT create(REFIID iid, void** ppv) noexcept;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID iid, void** ppv) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
    IFACEMETHODIMP_(ULONG) Release() noexcept override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD mode) noexcept override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) noexcept override;

private:
    ThumbnailProvider() noexcept;
    ~ThumbnailProvider();
    ThumbnailProvider(const ThumbnailProvider&) = delete;
    ThumbnailProvider& operator=(const ThumbnailProvider&) = delete;

    std::atomic<ULONG> refs_{1};
    IStream* stream_ = nullptr;
};

} // namespace splatthumb::shellext
