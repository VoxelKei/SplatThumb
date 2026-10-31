#include "stream_byte_source.h"

#include <algorithm>
#include <limits>

namespace splatthumb::shellext {

StreamByteSource::StreamByteSource(IStream* stream) noexcept : stream_(stream) {
    if (stream_ == nullptr) {
        return;
    }
    stream_->AddRef();
    STATSTG stat{};
    if (SUCCEEDED(stream_->Stat(&stat, STATFLAG_NONAME))) {
        size_ = stat.cbSize.QuadPart;
        ok_ = true;
    }
}

StreamByteSource::~StreamByteSource() {
    if (stream_ != nullptr) {
        stream_->Release();
    }
}

std::size_t StreamByteSource::read_at(std::uint64_t offset, std::span<std::byte> out) noexcept {
    if (!ok_ || offset >= size_ || out.empty() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        return 0;
    }
    const std::uint64_t available = size_ - offset;
    std::size_t wanted = out.size();
    if (available < wanted) {
        wanted = static_cast<std::size_t>(available);
    }

    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (FAILED(stream_->Seek(position, STREAM_SEEK_SET, nullptr))) {
        return 0;
    }

    // A stream may return fewer bytes than asked; keep reading until done or stuck.
    std::size_t done = 0;
    while (done < wanted) {
        const std::size_t chunk_size =
            std::min<std::size_t>(wanted - done, std::numeric_limits<ULONG>::max());
        ULONG got = 0;
        const HRESULT hr = stream_->Read(out.data() + done, static_cast<ULONG>(chunk_size), &got);
        if (FAILED(hr)) {
            return 0;
        }
        if (got == 0) {
            break;
        }
        done += std::min<std::size_t>(got, chunk_size);
    }
    return done;
}

} // namespace splatthumb::shellext
