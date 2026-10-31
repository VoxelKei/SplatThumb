#include <gtest/gtest.h>

#include <windows.h>

#include <objidl.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "class_factory.h"
#include "clsid.h"
#include "module.h"
#include "ply_builder.h"
#include "splatthumb/limits.h"
#include "stream_byte_source.h"

namespace {

using splatthumb::shellext::get_class_object;
using splatthumb::shellext::kThumbnailProviderClsid;
using splatthumb::shellext::StreamByteSource;
using namespace splatthumb::test;

// Owns one COM reference.
template <typename T>
class Ref {
public:
    Ref() = default;
    ~Ref() { reset(); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    void reset() {
        if (p_ != nullptr) {
            p_->Release();
            p_ = nullptr;
        }
    }

private:
    T* p_ = nullptr;
};

IStream* memory_stream(std::span<const std::byte> bytes) {
    return SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),
                             static_cast<UINT>(bytes.size()));
}

std::vector<std::byte> small_scene() {
    std::vector<RawSplat> splats;
    for (int i = 0; i < 50; ++i) {
        RawSplat s;
        s.x = static_cast<float>(i % 5) * 0.2f;
        s.y = static_cast<float>(i / 5 % 5) * 0.2f;
        s.z = static_cast<float>(i / 25) * 0.2f;
        s.f_dc[0] = 1.0f;
        splats.push_back(s);
    }
    return make_standard_file(splats);
}

// An IStream whose Seek, Read or Stat can be made to fail, or that returns only a few
// bytes per Read.
class TestStream final : public IStream {
public:
    explicit TestStream(std::vector<std::byte> data) : data_(std::move(data)) {}

    bool fail_stat = false;
    bool fail_seek = false;
    bool fail_read = false;
    ULONG max_read = 0xffffffffu;
    std::uint64_t fail_reads_after = UINT64_MAX; // fail once this many bytes were read
    std::uint64_t read_total = 0;

    IFACEMETHODIMP QueryInterface(REFIID iid, void** ppv) override {
        if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IStream)) ||
            IsEqualIID(iid, __uuidof(ISequentialStream))) {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; } // lives on the test's stack
    ULONG refs() const { return refs_; }

    IFACEMETHODIMP Read(void* out, ULONG cb, ULONG* read) override {
        if (fail_read || read_total >= fail_reads_after) {
            return E_FAIL;
        }
        std::size_t n = 0;
        if (pos_ < data_.size()) {
            n = std::min<std::size_t>({cb, max_read, data_.size() - static_cast<std::size_t>(pos_)});
            std::memcpy(out, data_.data() + pos_, n);
        }
        pos_ += n;
        read_total += n;
        if (read != nullptr) {
            *read = static_cast<ULONG>(n);
        }
        return n < cb ? S_FALSE : S_OK;
    }
    IFACEMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* now) override {
        if (fail_seek || origin != STREAM_SEEK_SET || move.QuadPart < 0) {
            return E_FAIL;
        }
        pos_ = static_cast<std::uint64_t>(move.QuadPart);
        if (now != nullptr) {
            now->QuadPart = pos_;
        }
        return S_OK;
    }
    IFACEMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    IFACEMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP Commit(DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP Revert() override { return E_NOTIMPL; }
    IFACEMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }
    IFACEMETHODIMP Stat(STATSTG* stat, DWORD) override {
        if (fail_stat) {
            return E_FAIL;
        }
        *stat = STATSTG{};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = data_.size();
        return S_OK;
    }
    IFACEMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

private:
    std::vector<std::byte> data_;
    std::uint64_t pos_ = 0;
    ULONG refs_ = 1;
};

HRESULT make_provider(Ref<IThumbnailProvider>& out) {
    Ref<IClassFactory> factory;
    HRESULT hr = get_class_object(kThumbnailProviderClsid, __uuidof(IClassFactory),
                                  reinterpret_cast<void**>(factory.put()));
    if (FAILED(hr)) {
        return hr;
    }
    return factory->CreateInstance(nullptr, __uuidof(IThumbnailProvider),
                                   reinterpret_cast<void**>(out.put()));
}

HRESULT thumbnail(IStream* stream, UINT cx, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) {
    Ref<IThumbnailProvider> provider;
    HRESULT hr = make_provider(provider);
    if (FAILED(hr)) {
        return hr;
    }
    Ref<IInitializeWithStream> init;
    hr = provider->QueryInterface(__uuidof(IInitializeWithStream),
                                  reinterpret_cast<void**>(init.put()));
    if (FAILED(hr)) {
        return hr;
    }
    hr = init->Initialize(stream, STGM_READ);
    if (FAILED(hr)) {
        return hr;
    }
    return provider->GetThumbnail(cx, bitmap, alpha);
}

int bitmap_side(HBITMAP bitmap) {
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof info, &info) == 0) {
        return -1;
    }
    EXPECT_EQ(info.bmWidth, std::abs(info.bmHeight));
    EXPECT_EQ(info.bmBitsPixel, 32);
    return info.bmWidth;
}

TEST(StreamByteSource, ReadsAtOffsets) {
    const std::vector<std::byte> data = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    Ref<IStream> stream;
    *stream.put() = memory_stream(data);
    ASSERT_NE(stream.get(), nullptr);
    StreamByteSource source(stream.get());
    ASSERT_TRUE(source.ok());
    EXPECT_EQ(source.size(), 4u);
    std::byte out[8]{};
    EXPECT_EQ(source.read_at(1, out), 3u);
    EXPECT_EQ(out[0], std::byte{2});
    EXPECT_EQ(source.read_at(4, out), 0u);
    EXPECT_EQ(source.read_at(UINT64_MAX, out), 0u);
}

TEST(StreamByteSource, ShortReadsAreJoined) {
    TestStream stream(std::vector<std::byte>(1000, std::byte{7}));
    stream.max_read = 3;
    StreamByteSource source(&stream);
    std::vector<std::byte> out(600);
    EXPECT_EQ(source.read_at(100, out), 600u);
    EXPECT_EQ(out[599], std::byte{7});
}

TEST(StreamByteSource, FailuresReadNothing) {
    TestStream stat_fails({std::byte{1}});
    stat_fails.fail_stat = true;
    StreamByteSource a(&stat_fails);
    EXPECT_FALSE(a.ok());
    EXPECT_EQ(a.size(), 0u);

    std::byte out[4]{};
    TestStream seek_fails(std::vector<std::byte>(4));
    seek_fails.fail_seek = true;
    EXPECT_EQ(StreamByteSource(&seek_fails).read_at(0, out), 0u);

    TestStream read_fails(std::vector<std::byte>(4));
    read_fails.fail_read = true;
    EXPECT_EQ(StreamByteSource(&read_fails).read_at(0, out), 0u);

    EXPECT_FALSE(StreamByteSource(nullptr).ok());
}

TEST(StreamByteSource, HoldsAReference) {
    TestStream stream(std::vector<std::byte>(4));
    {
        StreamByteSource source(&stream);
        EXPECT_EQ(stream.refs(), 2u);
    }
    EXPECT_EQ(stream.refs(), 1u);
}

TEST(ThumbnailProvider, DrawsAStandardFile) {
    const std::vector<std::byte> file = small_scene();
    Ref<IStream> stream;
    *stream.put() = memory_stream(file);
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    ASSERT_EQ(thumbnail(stream.get(), 256, &bitmap, &alpha), S_OK);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(alpha, WTSAT_RGB);
    EXPECT_EQ(bitmap_side(bitmap), 256);
    DeleteObject(bitmap);
}

TEST(ThumbnailProvider, SizeIsClamped) {
    const std::vector<std::byte> file = small_scene();
    for (const UINT cx : {0u, 1u, 100000u, 0xffffffffu}) {
        Ref<IStream> stream;
        *stream.put() = memory_stream(file);
        HBITMAP bitmap = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        ASSERT_EQ(thumbnail(stream.get(), cx, &bitmap, &alpha), S_OK) << cx;
        const int side = bitmap_side(bitmap);
        EXPECT_GE(side, splatthumb::limits::kMinThumbnailSize) << cx;
        EXPECT_LE(side, splatthumb::limits::kMaxThumbnailSize) << cx;
        DeleteObject(bitmap);
    }
}

TEST(ThumbnailProvider, OtherFilesFail) {
    const std::string mesh_header =
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty float x\n"
        "property float y\nproperty float z\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n";
    std::vector<std::vector<std::byte>> files = {
        make_file(mesh_header, 3 * 12 + 13),
        make_file("ply\nformat ascii 1.0\nend_header\n", 0),
        make_file(standard_header(1000), 10), // too short for its vertex count
        {},
    };
    std::vector<std::byte> garbage(4096);
    for (std::size_t i = 0; i < garbage.size(); ++i) {
        garbage[i] = static_cast<std::byte>(i * 131 + 7);
    }
    files.push_back(garbage);

    for (std::size_t i = 0; i < files.size(); ++i) {
        Ref<IStream> stream;
        *stream.put() = memory_stream(files[i]);
        HBITMAP bitmap = reinterpret_cast<HBITMAP>(1);
        WTS_ALPHATYPE alpha = WTSAT_ARGB;
        EXPECT_TRUE(FAILED(thumbnail(stream.get(), 256, &bitmap, &alpha))) << i;
        EXPECT_EQ(bitmap, nullptr) << i;
    }
}

TEST(ThumbnailProvider, BrokenStreamsFail) {
    const std::vector<std::byte> file = small_scene();
    for (int mode = 0; mode < 4; ++mode) {
        TestStream stream(file);
        stream.fail_stat = mode == 0;
        stream.fail_seek = mode == 1;
        stream.fail_read = mode == 2;
        stream.fail_reads_after = mode == 3 ? 1 : UINT64_MAX; // first read ok, later ones fail
        HBITMAP bitmap = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        EXPECT_TRUE(FAILED(thumbnail(&stream, 256, &bitmap, &alpha))) << mode;
        EXPECT_EQ(bitmap, nullptr) << mode;
        EXPECT_EQ(stream.refs(), 1u) << mode; // the provider released the stream
    }
}

TEST(ThumbnailProvider, InitializeRules) {
    Ref<IThumbnailProvider> provider;
    ASSERT_EQ(make_provider(provider), S_OK);
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    EXPECT_EQ(provider->GetThumbnail(256, &bitmap, &alpha), E_UNEXPECTED);
    EXPECT_EQ(provider->GetThumbnail(256, nullptr, &alpha), E_POINTER);

    Ref<IInitializeWithStream> init;
    ASSERT_EQ(provider->QueryInterface(__uuidof(IInitializeWithStream),
                                       reinterpret_cast<void**>(init.put())),
              S_OK);
    EXPECT_EQ(init->Initialize(nullptr, STGM_READ), E_INVALIDARG);
    TestStream stream(std::vector<std::byte>(4));
    EXPECT_EQ(init->Initialize(&stream, STGM_READ), S_OK);
    EXPECT_EQ(init->Initialize(&stream, STGM_READ), HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED));
    EXPECT_EQ(stream.refs(), 2u);
    init.reset();
    provider.reset();
    EXPECT_EQ(stream.refs(), 1u);
}

TEST(ThumbnailProvider, QueryInterfaceAndFactory) {
    Ref<IThumbnailProvider> provider;
    ASSERT_EQ(make_provider(provider), S_OK);
    void* unknown = reinterpret_cast<void*>(1);
    EXPECT_EQ(provider->QueryInterface(__uuidof(IStream), &unknown), E_NOINTERFACE);
    EXPECT_EQ(unknown, nullptr);
    EXPECT_EQ(provider->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);

    void* object = nullptr;
    EXPECT_EQ(get_class_object(__uuidof(IUnknown), __uuidof(IClassFactory), &object),
              CLASS_E_CLASSNOTAVAILABLE);
    EXPECT_EQ(object, nullptr);

    Ref<IClassFactory> factory;
    ASSERT_EQ(get_class_object(kThumbnailProviderClsid, __uuidof(IClassFactory),
                               reinterpret_cast<void**>(factory.put())),
              S_OK);
    EXPECT_EQ(factory->CreateInstance(provider.get(), __uuidof(IUnknown), &object), CLASS_E_NOAGGREGATION);
    EXPECT_EQ(factory->LockServer(TRUE), S_OK);
    EXPECT_TRUE(splatthumb::shellext::module_in_use());
    EXPECT_EQ(factory->LockServer(FALSE), S_OK);
}

TEST(ThumbnailProvider, ModuleIsUnusedWhenAllReleased) {
    {
        Ref<IThumbnailProvider> provider;
        ASSERT_EQ(make_provider(provider), S_OK);
        EXPECT_TRUE(splatthumb::shellext::module_in_use());
    }
    EXPECT_FALSE(splatthumb::shellext::module_in_use());
}

} // namespace
