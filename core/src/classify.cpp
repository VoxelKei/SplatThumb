#include "splatthumb/classify.h"

#include <limits>
#include <string>
#include <string_view>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
    if (a != 0 && b > kU64Max / a) {
        return false;
    }
    out = a * b;
    return true;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
    if (b > kU64Max - a) {
        return false;
    }
    out = a + b;
    return true;
}

Classification reject(Status status) noexcept {
    Classification c;
    c.status = status;
    return c;
}

bool float_offset(const PlyElement& e, std::string_view name, std::size_t& out) noexcept {
    const PlyProperty* p = e.find(name);
    if (p == nullptr || p->is_list || p->type != ScalarType::Float32) {
        return false;
    }
    out = p->offset;
    return true;
}

bool uint32_offset(const PlyElement& e, std::string_view name, std::size_t& out) noexcept {
    const PlyProperty* p = e.find(name);
    if (p == nullptr || p->is_list || p->type != ScalarType::UInt32) {
        return false;
    }
    out = p->offset;
    return true;
}

template <std::size_t N>
bool float_offsets(const PlyElement& e, const std::array<std::string_view, N>& names,
                   std::array<std::size_t, N>& out) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (!float_offset(e, names[i], out[i])) {
            return false;
        }
    }
    return true;
}

// Fills everything but the chunk position and count, which classify() knows.
bool fill_compressed_layout(const PlyElement& chunk, const PlyElement& v,
                            CompressedLayout& l) noexcept {
    static constexpr std::array<std::string_view, 6> kPosition = {
        "min_x", "min_y", "min_z", "max_x", "max_y", "max_z"};
    static constexpr std::array<std::string_view, 6> kScale = {
        "min_scale_x", "min_scale_y", "min_scale_z", "max_scale_x", "max_scale_y", "max_scale_z"};
    static constexpr std::array<std::string_view, 6> kColor = {"min_r", "min_g", "min_b",
                                                               "max_r", "max_g", "max_b"};
    if (!uint32_offset(v, "packed_position", l.packed_position) ||
        !uint32_offset(v, "packed_rotation", l.packed_rotation) ||
        !uint32_offset(v, "packed_scale", l.packed_scale) ||
        !uint32_offset(v, "packed_color", l.packed_color) ||
        !float_offsets(chunk, kPosition, l.position) || !float_offsets(chunk, kScale, l.scale)) {
        return false;
    }
    l.has_color_range = float_offsets(chunk, kColor, l.color);
    l.chunk_stride = chunk.record_size;
    return true;
}

bool fill_standard_layout(const PlyElement& v, StandardLayout& l) noexcept {
    bool ok = float_offset(v, "x", l.x) && float_offset(v, "y", l.y) && float_offset(v, "z", l.z) &&
              float_offset(v, "opacity", l.opacity);
    static constexpr std::array<std::string_view, 3> kDc = {"f_dc_0", "f_dc_1", "f_dc_2"};
    static constexpr std::array<std::string_view, 3> kScale = {"scale_0", "scale_1", "scale_2"};
    static constexpr std::array<std::string_view, 4> kRot = {"rot_0", "rot_1", "rot_2", "rot_3"};
    for (std::size_t i = 0; ok && i < kDc.size(); ++i) {
        ok = float_offset(v, kDc[i], l.f_dc[i]);
    }
    for (std::size_t i = 0; ok && i < kScale.size(); ++i) {
        ok = float_offset(v, kScale[i], l.scale[i]);
    }
    for (std::size_t i = 0; ok && i < kRot.size(); ++i) {
        ok = float_offset(v, kRot[i], l.rot[i]);
    }
    return ok;
}

std::size_t count_sh_rest(const PlyElement& v) noexcept {
    std::size_t n = 0;
    for (const PlyProperty& p : v.properties) {
        if (std::string_view(p.name).starts_with("f_rest_")) {
            ++n;
        }
    }
    return n;
}

} // namespace

Classification classify(const PlyHeader& header, std::uint64_t file_size) noexcept {
    if (header.find("face") != nullptr) {
        return reject(Status::HasFaces);
    }
    const PlyElement* vertex = header.find("vertex");
    if (vertex == nullptr) {
        return reject(Status::MissingVertexElement);
    }

    // Every element must have fixed-size records so that offsets and the total size are known.
    const PlyElement* chunk = header.find("chunk");
    std::uint64_t offset = header.header_size;
    std::uint64_t vertex_offset = 0;
    std::uint64_t chunk_offset = 0;
    for (const PlyElement& e : header.elements) {
        if (e.has_list) {
            return reject(Status::HasListProperty);
        }
        if (&e == vertex) {
            vertex_offset = offset;
        }
        if (&e == chunk) {
            chunk_offset = offset;
        }
        std::uint64_t bytes = 0;
        if (!checked_mul(e.count, e.record_size, bytes) || !checked_add(offset, bytes, offset)) {
            return reject(Status::SizeOverflow);
        }
    }

    Classification c;
    if (fill_standard_layout(*vertex, c.layout)) {
        c.kind = SplatKind::Standard;
        c.sh_rest_count = count_sh_rest(*vertex);
    } else if (chunk != nullptr && fill_compressed_layout(*chunk, *vertex, c.compressed)) {
        c.kind = SplatKind::Compressed;
        c.compressed.chunk_data_offset = chunk_offset;
        c.compressed.chunk_count = chunk->count;
    } else {
        return reject(Status::MissingSplatProperties);
    }

    if (vertex->count == 0) {
        return reject(Status::EmptyVertexElement);
    }
    if (vertex->count > limits::kMaxVertexCount) {
        return reject(Status::TooManyVertices);
    }
    if (offset > file_size) {
        return reject(Status::FileTooSmall);
    }
    // Every vertex needs its chunk; vertex->count <= kMaxVertexCount, so no overflow.
    if (c.kind == SplatKind::Compressed &&
        c.compressed.chunk_count <
            (vertex->count + limits::kCompressedChunkSize - 1) / limits::kCompressedChunkSize) {
        return reject(Status::ChunkCountMismatch);
    }

    c.status = Status::Ok;
    c.vertex_count = vertex->count;
    c.vertex_data_offset = vertex_offset;
    c.vertex_stride = vertex->record_size;
    return c;
}

Classification inspect(ByteSource& source) noexcept {
    const ParseResult parsed = parse_header(source);
    if (parsed.status != Status::Ok) {
        return reject(parsed.status);
    }
    return classify(parsed.header, source.size());
}

} // namespace splatthumb
