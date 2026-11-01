#include "embedded_image.h"

#include <wincodec.h>

#include <cstdint>
#include <new>
#include <vector>

#include "splatthumb/limits.h"

namespace splatthumb::shellext {

namespace {

// The thumbnail's dark gray, as in core/src/render.cpp (0.18 of full scale).
constexpr std::uint32_t kGray = 46;

// Releases a COM pointer when it goes out of scope.
template <typename T> class Com {
  public:
    Com() noexcept = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p_ != nullptr) {
            p_->Release();
        }
    }
    T* get() const noexcept {
        return p_;
    }
    T* operator->() const noexcept {
        return p_;
    }
    T** put() noexcept {
        return &p_;
    }

  private:
    T* p_ = nullptr;
};

} // namespace

HRESULT decode_embedded_image(std::span<const std::byte> data, EmbeddedImageFormat format,
                              UINT max_side, HBITMAP* bitmap) noexcept {
    if (bitmap == nullptr) {
        return E_POINTER;
    }
    *bitmap = nullptr;
    if (data.empty() || data.size() > limits::kMaxEmbeddedImageBytes || max_side == 0) {
        return E_INVALIDARG;
    }

    Com<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.put()));
    if (FAILED(hr)) {
        return hr;
    }
    Com<IWICStream> stream;
    if (FAILED(hr = factory->CreateStream(stream.put()))) {
        return hr;
    }
    // The bytes are only read, but the API takes a non-const pointer.
    auto* bytes = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data.data()));
    if (FAILED(hr = stream->InitializeFromMemory(bytes, static_cast<DWORD>(data.size())))) {
        return hr;
    }

    // Windows' own decoder for exactly this format, so no other installed codec is ever
    // asked to read the data.
    const CLSID& decoder_clsid =
        format == EmbeddedImageFormat::Png ? CLSID_WICPngDecoder : CLSID_WICJpegDecoder;
    Com<IWICBitmapDecoder> decoder;
    if (FAILED(hr = CoCreateInstance(decoder_clsid, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(decoder.put())))) {
        return hr;
    }
    if (FAILED(hr = decoder->Initialize(stream.get(), WICDecodeMetadataCacheOnDemand))) {
        return hr;
    }
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(hr = decoder->GetFrame(0, frame.put()))) {
        return hr;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(hr = frame->GetSize(&width, &height))) {
        return hr;
    }
    if (width == 0 || height == 0 || width > limits::kMaxEmbeddedImageSide ||
        height > limits::kMaxEmbeddedImageSide) {
        return E_FAIL;
    }

    // Keep the aspect ratio; never enlarge.
    UINT out_w = width;
    UINT out_h = height;
    const UINT longer = width > height ? width : height;
    if (longer > max_side) {
        out_w =
            static_cast<UINT>((static_cast<std::uint64_t>(width) * max_side + longer / 2) / longer);
        out_h = static_cast<UINT>((static_cast<std::uint64_t>(height) * max_side + longer / 2) /
                                  longer);
        out_w = out_w == 0 ? 1 : out_w;
        out_h = out_h == 0 ? 1 : out_h;
    }

    Com<IWICBitmapScaler> scaler;
    if (FAILED(hr = factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(hr =
                   scaler->Initialize(frame.get(), out_w, out_h, WICBitmapInterpolationModeFant))) {
        return hr;
    }
    Com<IWICFormatConverter> converter;
    if (FAILED(hr = factory->CreateFormatConverter(converter.put())) ||
        FAILED(hr = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA,
                                          WICBitmapDitherTypeNone, nullptr, 0.0,
                                          WICBitmapPaletteTypeCustom))) {
        return hr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(out_w);
    info.bmiHeader.biHeight = -static_cast<LONG>(out_h); // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP made = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (made == nullptr || bits == nullptr) {
        if (made != nullptr) {
            DeleteObject(made);
        }
        return E_OUTOFMEMORY;
    }
    // Both sides are at most kMaxEmbeddedImageSide, so these cannot overflow.
    const UINT stride = out_w * 4;
    const UINT size = stride * out_h;
    if (FAILED(hr = converter->CopyPixels(nullptr, stride, size, static_cast<BYTE*>(bits)))) {
        DeleteObject(made);
        return hr;
    }

    // Lay premultiplied pixels over the gray background and make them opaque.
    auto* px = static_cast<std::uint8_t*>(bits);
    for (UINT i = 0; i < size; i += 4) {
        const std::uint32_t a = px[i + 3];
        if (a != 255) {
            for (UINT c = 0; c < 3; ++c) {
                const std::uint32_t v = px[i + c] + (kGray * (255 - a) + 127) / 255;
                px[i + c] = static_cast<std::uint8_t>(v > 255 ? 255 : v);
            }
            px[i + 3] = 255;
        }
    }
    *bitmap = made;
    return S_OK;
}

} // namespace splatthumb::shellext
