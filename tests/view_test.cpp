#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ply_builder.h"
#include "splatthumb/byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"
#include "splatthumb/render.h"
#include "splatthumb/settings.h"
#include "splatthumb/view.h"
#include "splatthumb/view_store.h"

namespace {

using splatthumb::Fingerprint;
using splatthumb::Quality;
using splatthumb::Settings;
using splatthumb::Status;
using splatthumb::StoredSettings;
using splatthumb::View;
using namespace splatthumb::test;

View sample_view() {
    View v;
    v.eye = {1.5f, -2.25f, -10.0f};
    v.target = {0.125f, 0.0f, 3.0f};
    v.up = {0.0f, -1.0f, 0.0f};
    v.fov_y_deg = 40.0f;
    v.radius = 2.5f;
    return v;
}

const std::string kGood = "splatthumb-view 1\n"
                          "eye 0 0 -10\n"
                          "target 0 0 0\n"
                          "up 0 -1 0\n"
                          "fov 45\n"
                          "radius 2\n";

// ---- Text form ----

TEST(View, FormatThenParseIsExact) {
    const View v = sample_view();
    const auto parsed = splatthumb::parse_view(splatthumb::format_view(v));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, v);
}

TEST(View, QualityRoundTripsAndIsOptional) {
    EXPECT_FALSE(splatthumb::parse_view(kGood)->quality.has_value());
    EXPECT_EQ(splatthumb::format_view(sample_view()).find("quality"), std::string::npos);
    for (const auto q : {splatthumb::Quality::Fast, splatthumb::Quality::Standard,
                         splatthumb::Quality::High}) {
        View v = sample_view();
        v.quality = q;
        const auto parsed = splatthumb::parse_view(splatthumb::format_view(v));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, v);
    }
    const auto high = splatthumb::parse_view(kGood + "quality 2\n");
    ASSERT_TRUE(high.has_value());
    EXPECT_EQ(high->quality, splatthumb::Quality::High);
}

TEST(View, UnknownQualityNumberKeepsTheView) {
    // A later version may add levels; this one keeps the view and uses the global quality.
    const auto v = splatthumb::parse_view(kGood + "quality 7\n");
    ASSERT_TRUE(v.has_value());
    EXPECT_FALSE(v->quality.has_value());
}

TEST(View, ParsesGoodText) {
    const auto v = splatthumb::parse_view(kGood);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->eye[2], -10.0f);
    EXPECT_EQ(v->fov_y_deg, 45.0f);
}

TEST(View, AcceptsCrLfBlankLinesAndUnknownKeys) {
    std::string text = kGood;
    for (std::size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 2)) {
        text.insert(at, "\r");
    }
    text += "\r\nlater-feature 1 2 3\r\n";
    EXPECT_TRUE(splatthumb::parse_view(text).has_value());
}

TEST(View, RejectsBrokenText) {
    const auto bad = [](std::string text) {
        EXPECT_FALSE(splatthumb::parse_view(text).has_value()) << text;
    };
    const auto replace = [](std::string from, std::string to) {
        std::string t = kGood;
        t.replace(t.find(from), from.size(), to);
        return t;
    };
    bad("");
    bad("splatthumb-view 2\n" + kGood.substr(kGood.find('\n') + 1));
    bad(kGood.substr(kGood.find('\n') + 1));           // no first line
    bad(replace("fov 45\n", ""));                      // missing key
    bad(kGood + "fov 45\n");                           // repeated key
    bad(replace("eye 0 0 -10", "eye 0 0"));            // too few numbers
    bad(replace("eye 0 0 -10", "eye 0 0 -10 4"));      // too many numbers
    bad(replace("eye 0 0 -10", "eye 0 0 x"));          // not a number
    bad(replace("eye 0 0 -10", "eye 0 0 -10x"));       // trailing junk
    bad(replace("eye 0 0 -10", "eye 0 0 nan"));
    bad(replace("eye 0 0 -10", "eye 0 0 inf"));
    bad(replace("eye 0 0 -10", "eye 0 0 1e7"));        // beyond kMaxAbsCoordinate
    bad(replace("eye 0 0 -10", "eye 0 0 0"));          // eye == target
    bad(replace("up 0 -1 0", "up 0 0 1"));             // up parallel to view direction
    bad(replace("up 0 -1 0", "up 0 0 0"));
    bad(replace("fov 45", "fov 0.5"));
    bad(replace("fov 45", "fov 171"));
    bad(replace("radius 2", "radius 0"));
    bad(replace("radius 2", "radius -1"));
    bad(kGood + "quality\n");                          // quality without a number
    bad(kGood + "quality x\n");
    bad(kGood + "quality -1\n");
    bad(kGood + "quality 1.5\n");
    bad(kGood + "quality 1 2\n");
    bad(kGood + "quality 99999999999999999999\n");
    bad(kGood + "quality 1\nquality 2\n");           // repeated
    bad(kGood + std::string(splatthumb::limits::kMaxViewFileBytes, ' '));
    std::string many = kGood;
    for (int i = 0; i < 40; ++i) {
        many += "x\n";
    }
    bad(many);
}

TEST(View, CameraRoundTrip) {
    const auto cam = splatthumb::camera_from_view(sample_view());
    ASSERT_EQ(cam.status, Status::Ok);
    const View back = splatthumb::view_from_camera(cam.camera);
    EXPECT_EQ(back.eye, sample_view().eye);
    EXPECT_EQ(back.target, sample_view().target);
    EXPECT_NEAR(back.fov_y_deg, 40.0f, 1e-4f);
    const auto again = splatthumb::camera_from_view(back);
    ASSERT_EQ(again.status, Status::Ok);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(again.camera.up[i], cam.camera.up[i], 1e-5f);
        EXPECT_NEAR(again.camera.right[i], cam.camera.right[i], 1e-5f);
    }
}

// ---- Store ----

class ViewStore : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("splatthumb_views_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                 "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root_);
    }
    void TearDown() override { std::filesystem::remove_all(root_); }

    static Fingerprint fp(std::uint8_t first) {
        Fingerprint f;
        f.bytes.fill(0x5a);
        f.bytes[0] = first;
        return f;
    }

    std::filesystem::path root_;
};

TEST_F(ViewStore, PathUsesTwoDigitSubfolder) {
    const auto p = splatthumb::view_file_path(root_, fp(0xab));
    EXPECT_EQ(p.parent_path().filename(), "ab");
    EXPECT_EQ(p.filename(), fp(0xab).hex() + ".view");
}

TEST_F(ViewStore, MissingMeansNone) {
    EXPECT_FALSE(splatthumb::load_view(root_, fp(1)).has_value());
}

TEST_F(ViewStore, SaveLoadRemove) {
    ASSERT_TRUE(splatthumb::save_view(root_, fp(1), sample_view()));
    const auto loaded = splatthumb::load_view(root_, fp(1));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, sample_view());
    EXPECT_FALSE(splatthumb::load_view(root_, fp(2)).has_value());

    // Saving again replaces it.
    View v2 = sample_view();
    v2.radius = 7.0f;
    ASSERT_TRUE(splatthumb::save_view(root_, fp(1), v2));
    EXPECT_EQ(splatthumb::load_view(root_, fp(1))->radius, 7.0f);

    EXPECT_TRUE(splatthumb::remove_view(root_, fp(1)));
    EXPECT_FALSE(splatthumb::load_view(root_, fp(1)).has_value());
    EXPECT_FALSE(std::filesystem::exists(splatthumb::view_file_path(root_, fp(1)).parent_path()));
    EXPECT_TRUE(splatthumb::remove_view(root_, fp(1)));
}

TEST_F(ViewStore, BrokenOrHugeFilesAreIgnored) {
    const auto path = splatthumb::view_file_path(root_, fp(3));
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream(path, std::ios::binary) << "splatthumb-view 1\neye 1 2\n";
    }
    EXPECT_FALSE(splatthumb::load_view(root_, fp(3)).has_value());
    {
        std::ofstream out(path, std::ios::binary);
        out << kGood << std::string(5000, '\n');
    }
    EXPECT_FALSE(splatthumb::load_view(root_, fp(3)).has_value());
    // A folder where the file should be.
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    EXPECT_FALSE(splatthumb::load_view(root_, fp(3)).has_value());
}

// ---- Settings ----

TEST(Settings, MissingValuesAreDefaults) {
    const Settings s = splatthumb::settings_from_stored({});
    EXPECT_TRUE(s.enabled);
    EXPECT_EQ(s.quality, Quality::Standard);
}

TEST(Settings, StoredValuesRoundTrip) {
    for (const bool enabled : {false, true}) {
        for (const auto q : {Quality::Fast, Quality::Standard, Quality::High}) {
            const Settings s{enabled, q};
            const Settings back =
                splatthumb::settings_from_stored(splatthumb::stored_from_settings(s));
            EXPECT_EQ(back.enabled, enabled);
            EXPECT_EQ(back.quality, q);
        }
    }
}

TEST(Settings, OutOfRangeValuesFallBackOneByOne) {
    StoredSettings st;
    st.enabled = 0;
    st.quality = 99;
    const Settings s = splatthumb::settings_from_stored(st);
    EXPECT_FALSE(s.enabled);
    EXPECT_EQ(s.quality, Quality::Standard);
    st.enabled = 0xffffffffu;
    EXPECT_TRUE(splatthumb::settings_from_stored(st).enabled);
}

TEST(Settings, QualityPicksSampleCount) {
    EXPECT_LT(splatthumb::max_splats_for(Quality::Fast),
              splatthumb::max_splats_for(Quality::Standard));
    EXPECT_LT(splatthumb::max_splats_for(Quality::Standard),
              splatthumb::max_splats_for(Quality::High));
    EXPECT_EQ(splatthumb::max_splats_for(Quality::High), splatthumb::limits::kMaxRenderSplats);
}

// ---- Thumbnails with a saved view ----

// A red splat at x = -2 and a blue one at x = +2, seen from the front.
std::vector<std::byte> two_splats() {
    std::vector<RawSplat> raw(2);
    raw[0].x = -2.0f;
    raw[0].f_dc[0] = 2.0f;
    raw[0].scale[0] = raw[0].scale[1] = raw[0].scale[2] = -1.0f;
    raw[1].x = 2.0f;
    raw[1].f_dc[2] = 2.0f;
    raw[1].scale[0] = raw[1].scale[1] = raw[1].scale[2] = -1.0f;
    return make_standard_file(raw);
}

// Mean red minus blue over the left half of the image.
int left_red_minus_blue(const splatthumb::Image& img) {
    long sum = 0;
    for (int y = 0; y < img.size; ++y) {
        for (int x = 0; x < img.size / 2; ++x) {
            const auto* p = img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.size) +
                                               static_cast<std::size_t>(x)) * 4;
            sum += static_cast<long>(p[0]) - static_cast<long>(p[2]);
        }
    }
    return static_cast<int>(sum / (img.size * img.size / 2));
}

splatthumb::RenderResult render_with(const std::vector<std::byte>& file, const View* view) {
    splatthumb::MemoryByteSource src(file);
    splatthumb::RenderOptions o;
    o.size = 64;
    return splatthumb::render_thumbnail(src, o, {}, splatthumb::Deadline::after(std::chrono::hours(1)),
                                        nullptr, view);
}

TEST(ThumbnailView, SavedViewDecidesWhatIsSeen) {
    const auto file = two_splats();
    // From +z looking toward -z with screen up = world -y, world -x is on the right.
    View from_back;
    from_back.eye = {0, 0, 10};
    from_back.target = {0, 0, 0};
    from_back.up = {0, -1, 0};
    from_back.fov_y_deg = 45;
    from_back.radius = 3;
    View from_front = from_back;
    from_front.eye = {0, 0, -10};

    const auto back = render_with(file, &from_back);
    const auto front = render_with(file, &from_front);
    ASSERT_EQ(back.status, Status::Ok);
    ASSERT_EQ(front.status, Status::Ok);
    // Looking from the other side swaps which color is on the left.
    EXPECT_GT(left_red_minus_blue(front.image), 0);
    EXPECT_LT(left_red_minus_blue(back.image), 0);
}

TEST(ThumbnailView, InvalidViewFallsBackToAutomatic) {
    const auto file = two_splats();
    View broken;
    broken.eye = {0, 0, 0};
    broken.target = {0, 0, 0}; // eye == target
    broken.up = {0, -1, 0};
    const auto with_broken = render_with(file, &broken);
    const auto automatic = render_with(file, nullptr);
    ASSERT_EQ(with_broken.status, Status::Ok);
    ASSERT_EQ(automatic.status, Status::Ok);
    EXPECT_EQ(with_broken.image.rgba, automatic.image.rgba);
}

TEST(ThumbnailView, FingerprintOfAFileFindsItsSavedView) {
    const auto file = two_splats();
    splatthumb::MemoryByteSource src(file);
    const auto fp = splatthumb::fingerprint(src);
    ASSERT_TRUE(fp.has_value());
    const auto root = std::filesystem::temp_directory_path() / "splatthumb_views_lookup";
    std::filesystem::remove_all(root);
    ASSERT_TRUE(splatthumb::save_view(root, *fp, sample_view()));

    splatthumb::MemoryByteSource again(file);
    const auto loaded = splatthumb::load_view(root, *splatthumb::fingerprint(again));
    std::filesystem::remove_all(root);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, sample_view());
}

} // namespace
