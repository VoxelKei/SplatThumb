#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/classify.h"
#include "splatthumb/limits.h"

namespace {

using splatthumb::Classification;
using splatthumb::MemoryByteSource;
using splatthumb::SplatKind;
using splatthumb::Status;
using namespace splatthumb::test;

Classification inspect_bytes(const std::vector<std::byte>& bytes) {
    MemoryByteSource src(bytes);
    return splatthumb::inspect(src);
}

Classification inspect_header(const std::string& header, std::size_t data_bytes) {
    return inspect_bytes(make_file(header, data_bytes));
}

void expect_rejected(const Classification& c, Status status) {
    EXPECT_EQ(c.kind, SplatKind::NotSplat);
    EXPECT_EQ(c.status, status) << splatthumb::to_string(c.status);
}

// ---- Accepted ----

TEST(Classify, MinimalStandard) {
    const std::string h = standard_header(3);
    const Classification c = inspect_header(h, 3 * kStandardRecordSize);
    EXPECT_EQ(c.kind, SplatKind::Standard);
    EXPECT_EQ(c.status, Status::Ok);
    EXPECT_EQ(c.vertex_count, 3u);
    EXPECT_EQ(c.vertex_data_offset, h.size());
    EXPECT_EQ(c.vertex_stride, kStandardRecordSize);
    EXPECT_EQ(c.sh_rest_count, 0u);
    EXPECT_EQ(c.layout.x, 0u);
    EXPECT_EQ(c.layout.opacity, 24u);
    EXPECT_EQ(c.layout.rot[3], 52u);
}

// Same layout as the real exports we checked: normals between position and color.
TEST(Classify, StandardWithNormals) {
    const std::string props =
        "property float x\nproperty float y\nproperty float z\n"
        "property float nx\nproperty float ny\nproperty float nz\n"
        "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
        "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n";
    const Classification c = inspect_header(standard_header(2, props), 2 * 68);
    EXPECT_EQ(c.kind, SplatKind::Standard);
    EXPECT_EQ(c.vertex_stride, 68u);
    EXPECT_EQ(c.layout.f_dc[0], 24u);
}

TEST(Classify, StandardWithShDegree3) {
    std::string props(kStandardVertexProperties);
    for (int i = 0; i < 45; ++i) {
        props += "property float f_rest_" + std::to_string(i) + "\n";
    }
    const Classification c = inspect_header(standard_header(1, props), (14 + 45) * 4);
    EXPECT_EQ(c.kind, SplatKind::Standard);
    EXPECT_EQ(c.sh_rest_count, 45u);
}

TEST(Classify, CrlfAndComments) {
    std::string h = "ply\r\nformat binary_little_endian 1.0\r\ncomment made by test\r\n"
                    "obj_info x\r\nelement vertex 1\r\n";
    std::string_view props = kStandardVertexProperties;
    while (!props.empty()) {
        const auto nl = props.find('\n');
        h += std::string(props.substr(0, nl)) + "\r\n";
        props.remove_prefix(nl + 1);
    }
    h += "end_header\r\n";
    EXPECT_EQ(inspect_header(h, kStandardRecordSize).kind, SplatKind::Standard);
}

TEST(Classify, TrailingBytesAreAllowed) {
    EXPECT_EQ(inspect_header(standard_header(1), kStandardRecordSize + 100).kind,
              SplatKind::Standard);
}

TEST(Classify, Compressed) {
    const std::string h = compressed_header(1, 256);
    const Classification c = inspect_header(h, 72 + 256 * 16);
    EXPECT_EQ(c.kind, SplatKind::Compressed);
    EXPECT_EQ(c.vertex_data_offset, h.size() + 72);
    EXPECT_EQ(c.vertex_stride, 16u);
}

// ---- Not a splat ----

TEST(Classify, MeshIsRejected) {
    const std::string mesh = "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
                             "property float x\nproperty float y\nproperty float z\n"
                             "element face 1\nproperty list uchar int vertex_indices\n"
                             "end_header\n";
    expect_rejected(inspect_header(mesh, 3 * 12 + 13), Status::HasFaces);
}

TEST(Classify, SplatPropertiesWithFacesIsStillRejected) {
    const std::string h = "ply\nformat binary_little_endian 1.0\nelement vertex 1\n" +
                          std::string(kStandardVertexProperties) +
                          "element face 0\nproperty list uchar int vertex_indices\nend_header\n";
    expect_rejected(inspect_header(h, kStandardRecordSize), Status::HasFaces);
}

TEST(Classify, PointCloudIsRejected) {
    const std::string props = "property float x\nproperty float y\nproperty float z\n"
                              "property uchar red\nproperty uchar green\nproperty uchar blue\n";
    expect_rejected(inspect_header(standard_header(10, props), 10 * 15),
                    Status::MissingSplatProperties);
}

TEST(Classify, DoublePrecisionSplatIsRejected) {
    std::string props(kStandardVertexProperties);
    props.replace(props.find("float opacity"), 5, "double");
    expect_rejected(inspect_header(standard_header(1, props), 60), Status::MissingSplatProperties);
}

TEST(Classify, ListInVertexIsRejected) {
    const std::string props =
        std::string(kStandardVertexProperties) + "property list uchar float extra\n";
    expect_rejected(inspect_header(standard_header(1, props), 100), Status::HasListProperty);
}

TEST(Classify, NoVertexElement) {
    const std::string h = "ply\nformat binary_little_endian 1.0\nelement chunk 1\n"
                          "property float a\nend_header\n";
    expect_rejected(inspect_header(h, 4), Status::MissingVertexElement);
}

TEST(Classify, EmptyVertexElement) {
    expect_rejected(inspect_header(standard_header(0), 0), Status::EmptyVertexElement);
}

// ---- Size checks ----

TEST(Classify, OneRecordShort) {
    expect_rejected(inspect_header(standard_header(4), 3 * kStandardRecordSize),
                    Status::FileTooSmall);
}

TEST(Classify, HeaderOnly) {
    expect_rejected(inspect_header(standard_header(1), 0), Status::FileTooSmall);
}

TEST(Classify, TooManyVertices) {
    // The size check would also fail; the count limit must be reported regardless.
    expect_rejected(inspect_header(standard_header(splatthumb::limits::kMaxVertexCount + 1), 0),
                    Status::TooManyVertices);
}

TEST(Classify, CountTimesRecordSizeOverflows) {
    expect_rejected(inspect_header(standard_header(UINT64_MAX / 8), 0), Status::SizeOverflow);
}

TEST(Classify, SumOfElementsOverflows) {
    const std::string h = "ply\nformat binary_little_endian 1.0\n"
                          "element chunk 18446744073709551615\nproperty uchar a\n"
                          "element vertex 1\n" +
                          std::string(kStandardVertexProperties) + "end_header\n";
    expect_rejected(inspect_header(h, kStandardRecordSize), Status::SizeOverflow);
}

} // namespace
