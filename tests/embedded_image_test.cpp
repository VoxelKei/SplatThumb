// Showing the preview image of Spatialograph Maker project files (.spgm): decoding with
// Windows' own codecs, and the thumbnail handler's use of it. The test images are made with
// Windows' own encoders.

#include <gtest/gtest.h>

#include <windows.h>

#include <objidl.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "class_factory.h"
#include "clsid.h"
#include "embedded_image.h"
#include "splatthumb/limits.h"
#include "splatthumb/spgm.h"

namespace {

using splatthumb::EmbeddedImageFormat;
using splatthumb::shellext::decode_embedded_image;

template <typename T> class Ref {
  public:
    Ref() = default;
    ~Ref() {
        if (p_ != nullptr) {
            p_->Release();
        }
    }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    T* get() const {
        return p_;
    }
    T* operator->() const {
        return p_;
    }
    T** put() {
        return &p_;
    }

  private:
    T* p_ = nullptr;
};

class EmbeddedImage : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    }
    void TearDown() override {
        CoUninitialize();
    }
};

struct Bgra {
    std::uint8_t b, g, r, a;
};

// Encodes a width x height image of one colour with Windows' PNG or JPEG encoder.
std::vector<std::byte> encode(UINT width, UINT height, Bgra colour, EmbeddedImageFormat format) {
    std::vector<std::byte> out;
    Ref<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.put())))) {
        return out;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = colour.b;
        pixels[i + 1] = colour.g;
        pixels[i + 2] = colour.r;
        pixels[i + 3] = colour.a;
    }
    Ref<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) {
        return out;
    }
    Ref<IWICBitmapEncoder> encoder;
    const GUID container =
        format == EmbeddedImageFormat::Png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg;
    if (FAILED(factory->CreateEncoder(container, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) {
        return out;
    }
    Ref<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height))) {
        return out;
    }
    WICPixelFormatGUID pixel_format = format == EmbeddedImageFormat::Png
                                          ? GUID_WICPixelFormat32bppBGRA
                                          : GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&pixel_format))) {
        return out;
    }
    if (pixel_format == GUID_WICPixelFormat32bppBGRA) {
        if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()),
                                      pixels.data()))) {
            return out;
        }
    } else if (pixel_format == GUID_WICPixelFormat24bppBGR) {
        std::vector<std::uint8_t> rgb;
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            rgb.insert(rgb.end(), {pixels[i], pixels[i + 1], pixels[i + 2]});
        }
        // Rows of 24-bit pixels padded to 4 bytes.
        const UINT stride = (width * 3 + 3) & ~3u;
        std::vector<std::uint8_t> padded(static_cast<std::size_t>(stride) * height);
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(&padded[static_cast<std::size_t>(y) * stride],
                        &rgb[static_cast<std::size_t>(y) * width * 3], width * 3);
        }
        if (FAILED(frame->WritePixels(height, stride, static_cast<UINT>(padded.size()),
                                      padded.data()))) {
            return out;
        }
    } else {
        return out;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return out;
    }
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) {
        return out;
    }
    out.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    ULONG read = 0;
    if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(out.data(), static_cast<ULONG>(out.size()), &read)) ||
        read != out.size()) {
        out.clear();
    }
    return out;
}

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<Bgra> data;
    Bgra at(int x, int y) const {
        return data[static_cast<std::size_t>(y * width + x)];
    }
};

std::optional<Pixels> read_bitmap(HBITMAP bitmap) {
    DIBSECTION dib{};
    if (GetObjectW(bitmap, sizeof dib, &dib) != sizeof dib || dib.dsBm.bmBitsPixel != 32 ||
        dib.dsBm.bmBits == nullptr) {
        return std::nullopt;
    }
    Pixels p;
    p.width = dib.dsBm.bmWidth;
    p.height = dib.dsBm.bmHeight < 0 ? -dib.dsBm.bmHeight : dib.dsBm.bmHeight;
    p.data.resize(static_cast<std::size_t>(p.width) * static_cast<std::size_t>(p.height));
    std::memcpy(p.data.data(), dib.dsBm.bmBits, p.data.size() * 4);
    return p;
}

bool close_to(std::uint8_t a, int b, int tolerance = 6) {
    return a >= b - tolerance && a <= b + tolerance;
}

TEST_F(EmbeddedImage, DecodesAPngAndScalesItDown) {
    const auto png = encode(512, 512, {0, 0, 255, 255}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = nullptr;
    ASSERT_EQ(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, &bitmap), S_OK);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    EXPECT_EQ(pixels->width, 256);
    EXPECT_EQ(pixels->height, 256);
    const Bgra c = pixels->at(128, 128);
    EXPECT_TRUE(close_to(c.r, 255) && close_to(c.g, 0) && close_to(c.b, 0));
    EXPECT_EQ(c.a, 255);
}

TEST_F(EmbeddedImage, KeepsTheAspectRatio) {
    const auto png = encode(400, 200, {255, 0, 0, 255}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = nullptr;
    ASSERT_EQ(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, &bitmap), S_OK);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    EXPECT_EQ(pixels->width, 256);
    EXPECT_EQ(pixels->height, 128);
}

TEST_F(EmbeddedImage, DoesNotEnlarge) {
    const auto png = encode(64, 48, {0, 255, 0, 255}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = nullptr;
    ASSERT_EQ(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, &bitmap), S_OK);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    EXPECT_EQ(pixels->width, 64);
    EXPECT_EQ(pixels->height, 48);
}

TEST_F(EmbeddedImage, TransparencyBecomesGray) {
    const auto png = encode(32, 32, {0, 0, 0, 0}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = nullptr;
    ASSERT_EQ(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, &bitmap), S_OK);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    const Bgra c = pixels->at(16, 16);
    EXPECT_TRUE(close_to(c.r, 46, 1) && close_to(c.g, 46, 1) && close_to(c.b, 46, 1));
    EXPECT_EQ(c.a, 255);
}

TEST_F(EmbeddedImage, DecodesAJpeg) {
    const auto jpeg = encode(300, 300, {255, 0, 0, 255}, EmbeddedImageFormat::Jpeg);
    ASSERT_FALSE(jpeg.empty());
    ASSERT_EQ(static_cast<unsigned>(jpeg[0]), 0xFFu);
    HBITMAP bitmap = nullptr;
    ASSERT_EQ(decode_embedded_image(jpeg, EmbeddedImageFormat::Jpeg, 100, &bitmap), S_OK);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    EXPECT_EQ(pixels->width, 100);
    EXPECT_EQ(pixels->height, 100);
    const Bgra c = pixels->at(50, 50);
    EXPECT_TRUE(close_to(c.b, 255, 20) && close_to(c.r, 0, 20));
}

TEST_F(EmbeddedImage, RefusesImagesThatAreTooBig) {
    const UINT too_wide = splatthumb::limits::kMaxEmbeddedImageSide + 1;
    const auto png = encode(too_wide, 4, {0, 0, 0, 255}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(1);
    EXPECT_TRUE(FAILED(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, &bitmap)));
    EXPECT_EQ(bitmap, nullptr);
}

TEST_F(EmbeddedImage, RefusesBrokenAndMislabelledData) {
    auto png = encode(64, 64, {0, 0, 255, 255}, EmbeddedImageFormat::Png);
    ASSERT_FALSE(png.empty());
    HBITMAP bitmap = nullptr;
    // A PNG handed to the JPEG decoder.
    EXPECT_TRUE(FAILED(decode_embedded_image(png, EmbeddedImageFormat::Jpeg, 256, &bitmap)));
    EXPECT_EQ(bitmap, nullptr);
    // Only the first half of the PNG. Windows' decoder fills in the missing rows and Wine's
    // refuses it; either is fine, as long as a result has the image's full size.
    const std::span<const std::byte> half(png.data(), png.size() / 2);
    if (SUCCEEDED(decode_embedded_image(half, EmbeddedImageFormat::Png, 256, &bitmap))) {
        const auto pixels = read_bitmap(bitmap);
        DeleteObject(bitmap);
        ASSERT_TRUE(pixels);
        EXPECT_EQ(pixels->width, 64);
        EXPECT_EQ(pixels->height, 64);
    } else {
        EXPECT_EQ(bitmap, nullptr);
    }
    bitmap = nullptr;
    // Noise after the signature.
    std::vector<std::byte> noise(png.begin(), png.begin() + 8);
    for (int i = 0; i < 500; ++i) {
        noise.push_back(static_cast<std::byte>(i * 37 + 11));
    }
    EXPECT_TRUE(FAILED(decode_embedded_image(noise, EmbeddedImageFormat::Png, 256, &bitmap)));
    EXPECT_EQ(bitmap, nullptr);
    // Nothing, and no output pointer.
    EXPECT_TRUE(FAILED(decode_embedded_image({}, EmbeddedImageFormat::Png, 256, &bitmap)));
    EXPECT_EQ(decode_embedded_image(png, EmbeddedImageFormat::Png, 256, nullptr), E_POINTER);
}

// --- The thumbnail handler ---------------------------------------------------------------

void put_u32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>(v >> (8 * i)));
    }
}

std::vector<std::byte> make_spgm(std::uint32_t version, const std::vector<std::byte>& image) {
    std::vector<std::byte> out = {std::byte{'S'}, std::byte{'G'}, std::byte{'P'}, std::byte{0}};
    put_u32(out, version);
    if (version >= 3) {
        put_u32(out, static_cast<std::uint32_t>(image.size()));
        out.insert(out.end(), image.begin(), image.end());
    }
    const std::string json = R"({"version":3,"hasSplatData":false})";
    put_u32(out, static_cast<std::uint32_t>(json.size()));
    for (const char c : json) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

HRESULT thumbnail(const std::vector<std::byte>& file, UINT cx, HBITMAP* bitmap,
                  WTS_ALPHATYPE* alpha) {
    Ref<IStream> stream;
    *stream.put() = SHCreateMemStream(reinterpret_cast<const BYTE*>(file.data()),
                                      static_cast<UINT>(file.size()));
    Ref<IClassFactory> factory;
    HRESULT hr = splatthumb::shellext::get_class_object(
        splatthumb::shellext::kThumbnailProviderClsid, __uuidof(IClassFactory),
        reinterpret_cast<void**>(factory.put()));
    if (FAILED(hr)) {
        return hr;
    }
    Ref<IInitializeWithStream> init;
    if (FAILED(hr = factory->CreateInstance(nullptr, __uuidof(IInitializeWithStream),
                                            reinterpret_cast<void**>(init.put())))) {
        return hr;
    }
    if (FAILED(hr = init->Initialize(stream.get(), STGM_READ))) {
        return hr;
    }
    Ref<IThumbnailProvider> provider;
    if (FAILED(hr = init->QueryInterface(__uuidof(IThumbnailProvider),
                                         reinterpret_cast<void**>(provider.put())))) {
        return hr;
    }
    return provider->GetThumbnail(cx, bitmap, alpha);
}

TEST_F(EmbeddedImage, HandlerShowsTheProjectImage) {
    const auto file = make_spgm(3, encode(512, 256, {0, 0, 255, 255}, EmbeddedImageFormat::Png));
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    ASSERT_EQ(thumbnail(file, 256, &bitmap, &alpha), S_OK);
    EXPECT_EQ(alpha, WTSAT_RGB);
    const auto pixels = read_bitmap(bitmap);
    DeleteObject(bitmap);
    ASSERT_TRUE(pixels);
    EXPECT_EQ(pixels->width, 256);
    EXPECT_EQ(pixels->height, 128);
    EXPECT_TRUE(close_to(pixels->at(128, 64).r, 255));
}

TEST_F(EmbeddedImage, HandlerClampsTheSize) {
    const auto file = make_spgm(3, encode(2048, 2048, {0, 255, 0, 255}, EmbeddedImageFormat::Png));
    for (const UINT cx : {0u, 1u, 100000u, 0xffffffffu}) {
        HBITMAP bitmap = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        ASSERT_EQ(thumbnail(file, cx, &bitmap, &alpha), S_OK) << cx;
        const auto pixels = read_bitmap(bitmap);
        DeleteObject(bitmap);
        ASSERT_TRUE(pixels);
        EXPECT_GE(pixels->width, splatthumb::limits::kMinThumbnailSize) << cx;
        EXPECT_LE(pixels->width, splatthumb::limits::kMaxThumbnailSize) << cx;
    }
}

TEST_F(EmbeddedImage, HandlerFailsWithoutAUsableImage) {
    const auto png = encode(64, 64, {0, 0, 255, 255}, EmbeddedImageFormat::Png);
    // The PNG signature followed by noise.
    std::vector<std::byte> broken(png.begin(), png.begin() + 8);
    for (int i = 0; i < 500; ++i) {
        broken.push_back(static_cast<std::byte>(i * 37 + 11));
    }
    std::vector<std::byte> cut = make_spgm(3, png);
    cut.resize(20);
    const std::vector<std::vector<std::byte>> files = {
        make_spgm(2, {}),     // an older project: no image
        make_spgm(4, png),    // a newer version this handler does not know
        make_spgm(3, {}),     // no image
        make_spgm(3, broken), // the image is broken
        cut,                  // the file ends inside the image
    };
    for (std::size_t i = 0; i < files.size(); ++i) {
        HBITMAP bitmap = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        EXPECT_EQ(thumbnail(files[i], 256, &bitmap, &alpha), E_FAIL) << i;
        EXPECT_EQ(bitmap, nullptr) << i;
    }
}

} // namespace
