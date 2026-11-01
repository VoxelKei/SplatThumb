// Finding the preview image in Spatialograph Maker project files (.spgm).

#include "splatthumb/spgm.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "splatthumb/limits.h"

namespace {

using splatthumb::EmbeddedImageFormat;
using splatthumb::MemoryByteSource;
using splatthumb::SpgmProbe;

const std::vector<std::uint8_t> kPngStart = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
const std::vector<std::uint8_t> kJpegStart = {0xFF, 0xD8, 0xFF, 0xE0};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

// "SGP\0", version, image size, image, JSON size, JSON, splat bytes.
std::vector<std::uint8_t> make_spgm(std::uint32_t version, const std::vector<std::uint8_t>& image,
                                    std::uint32_t declared_size) {
    std::vector<std::uint8_t> out = {'S', 'G', 'P', 0};
    put_u32(out, version);
    put_u32(out, declared_size);
    out.insert(out.end(), image.begin(), image.end());
    const std::string json = R"({"version":3,"hasSplatData":false})";
    put_u32(out, static_cast<std::uint32_t>(json.size()));
    out.insert(out.end(), json.begin(), json.end());
    return out;
}

std::vector<std::uint8_t> make_spgm(std::uint32_t version, const std::vector<std::uint8_t>& image) {
    return make_spgm(version, image, static_cast<std::uint32_t>(image.size()));
}

std::vector<std::uint8_t> png_like(std::size_t size) {
    std::vector<std::uint8_t> image = kPngStart;
    image.resize(size, 0x42);
    return image;
}

splatthumb::SpgmResult probe(const std::vector<std::uint8_t>& bytes) {
    MemoryByteSource source(std::as_bytes(std::span(bytes)));
    return splatthumb::probe_spgm(source);
}

TEST(Spgm, FindsAPngImage) {
    const auto file = make_spgm(3, png_like(100));
    const auto result = probe(file);
    ASSERT_EQ(result.probe, SpgmProbe::Ok);
    EXPECT_EQ(result.image.version, 3u);
    EXPECT_EQ(result.image.offset, 12u);
    EXPECT_EQ(result.image.size, 100u);
    EXPECT_EQ(result.image.format, EmbeddedImageFormat::Png);

    MemoryByteSource source(std::as_bytes(std::span(file)));
    const auto bytes = splatthumb::read_spgm_image(source, result.image);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(bytes->size(), 100u);
    EXPECT_EQ((*bytes)[0], std::byte{0x89});
    EXPECT_EQ((*bytes)[99], std::byte{0x42});
}

TEST(Spgm, FindsAJpegImage) {
    std::vector<std::uint8_t> image = kJpegStart;
    image.resize(50, 0);
    const auto result = probe(make_spgm(3, image));
    ASSERT_EQ(result.probe, SpgmProbe::Ok);
    EXPECT_EQ(result.image.format, EmbeddedImageFormat::Jpeg);
}

TEST(Spgm, ImageThatEndsExactlyAtTheEndOfTheFile) {
    std::vector<std::uint8_t> file = {'S', 'G', 'P', 0};
    put_u32(file, 3);
    put_u32(file, 8);
    file.insert(file.end(), kPngStart.begin(), kPngStart.end());
    EXPECT_EQ(probe(file).probe, SpgmProbe::Ok);
}

TEST(Spgm, RejectsOtherFiles) {
    EXPECT_EQ(probe({}).probe, SpgmProbe::NotSpgm);
    EXPECT_EQ(probe({'S', 'G', 'P', 0, 3, 0, 0}).probe, SpgmProbe::NotSpgm); // too short
    auto file = make_spgm(3, png_like(20));
    file[3] = 'X';
    EXPECT_EQ(probe(file).probe, SpgmProbe::NotSpgm);
    const std::vector<std::uint8_t> ply(64, 'p');
    EXPECT_EQ(probe(ply).probe, SpgmProbe::NotSpgm);
}

TEST(Spgm, OlderAndNewerVersions) {
    // Versions 1 and 2: magic, version, JSON size, JSON, splats. No image.
    std::vector<std::uint8_t> v2 = {'S', 'G', 'P', 0};
    put_u32(v2, 2);
    put_u32(v2, 2);
    v2.push_back('{');
    v2.push_back('}');
    EXPECT_EQ(probe(v2).probe, SpgmProbe::OldVersion);
    v2[4] = 1;
    EXPECT_EQ(probe(v2).probe, SpgmProbe::OldVersion);
    v2[4] = 0;
    EXPECT_EQ(probe(v2).probe, SpgmProbe::OldVersion);
    EXPECT_EQ(probe(make_spgm(4, png_like(20))).probe, SpgmProbe::UnknownVersion);
    EXPECT_EQ(probe(make_spgm(0xFFFFFFFFu, png_like(20))).probe, SpgmProbe::UnknownVersion);
}

TEST(Spgm, NoImage) {
    EXPECT_EQ(probe(make_spgm(3, {})).probe, SpgmProbe::NoImage);
}

TEST(Spgm, ImageSizesThatDoNotFit) {
    EXPECT_EQ(probe(make_spgm(3, png_like(20), 1000)).probe, SpgmProbe::Truncated);
    EXPECT_EQ(probe(make_spgm(3, png_like(20), 0xFFFFFFFFu)).probe, SpgmProbe::ImageTooLarge);
    EXPECT_EQ(
        probe(make_spgm(3, png_like(20), splatthumb::limits::kMaxEmbeddedImageBytes + 1)).probe,
        SpgmProbe::ImageTooLarge);
}

TEST(Spgm, UnknownImageFormats) {
    EXPECT_EQ(probe(make_spgm(3, std::vector<std::uint8_t>(40, 0))).probe,
              SpgmProbe::UnknownImageFormat);
    // Shorter than a PNG signature, and only part of one.
    EXPECT_EQ(probe(make_spgm(3, {0x89, 'P', 'N'})).probe, SpgmProbe::UnknownImageFormat);
    // GIF and BMP are not accepted, even though Windows could read them.
    EXPECT_EQ(probe(make_spgm(3, {'G', 'I', 'F', '8', '9', 'a', 0, 0})).probe,
              SpgmProbe::UnknownImageFormat);
    EXPECT_EQ(probe(make_spgm(3, {'B', 'M', 0, 0, 0, 0, 0, 0})).probe,
              SpgmProbe::UnknownImageFormat);
}

TEST(Spgm, ReadImageRejectsBadRequests) {
    const auto file = make_spgm(3, png_like(30));
    MemoryByteSource source(std::as_bytes(std::span(file)));
    splatthumb::SpgmImage image;
    image.offset = 12;
    image.size = 0;
    EXPECT_FALSE(splatthumb::read_spgm_image(source, image).has_value());
    image.size = static_cast<std::uint32_t>(file.size()); // runs past the end
    EXPECT_FALSE(splatthumb::read_spgm_image(source, image).has_value());
    image.size = splatthumb::limits::kMaxEmbeddedImageBytes + 1;
    EXPECT_FALSE(splatthumb::read_spgm_image(source, image).has_value());
}

TEST(Spgm, EveryProbeHasText) {
    for (const auto p :
         {SpgmProbe::Ok, SpgmProbe::NotSpgm, SpgmProbe::OldVersion, SpgmProbe::UnknownVersion,
          SpgmProbe::NoImage, SpgmProbe::ImageTooLarge, SpgmProbe::Truncated,
          SpgmProbe::UnknownImageFormat, SpgmProbe::ReadError}) {
        EXPECT_NE(splatthumb::to_string(p), "unknown");
    }
}

} // namespace
