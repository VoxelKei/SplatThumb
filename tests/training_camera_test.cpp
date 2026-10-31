#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "splatthumb/training_camera.h"

namespace {

// Shaped like the cameras.json written by the original 3D Gaussian Splatting code.
const std::string kJson = R"([
  {"id": 0, "img_name": "00001", "width": 1600, "height": 1200,
   "position": [1.0, -0.5, -4.0],
   "rotation": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
   "fy": 1448.5, "fx": 1448.5},
  {"id": 1, "img_name": "00002", "width": 1600, "height": 1200,
   "position": [9, 9, 9], "rotation": [[1,0,0],[0,1,0],[0,0,1]], "fy": 1, "fx": 1}
])";

splatthumb::Camera framing() {
    splatthumb::View v;
    v.eye = {0, 0, -10};
    v.target = {0, 0, 0};
    v.up = {0, -1, 0};
    v.fov_y_deg = 45;
    v.radius = 2;
    return splatthumb::camera_from_view(v).camera;
}

TEST(TrainingCamera, ReadsFirstCamera) {
    const auto cam = splatthumb::parse_first_training_camera(kJson);
    ASSERT_TRUE(cam.has_value());
    EXPECT_EQ(cam->position[2], -4.0f);
    EXPECT_EQ(cam->height, 1200.0f);
    EXPECT_EQ(cam->fy, 1448.5f);
}

TEST(TrainingCamera, ViewLooksAlongCameraZWithYDown) {
    const auto cam = splatthumb::parse_first_training_camera(kJson);
    ASSERT_TRUE(cam.has_value());
    const auto v = splatthumb::view_from_training_camera(*cam, framing());
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->eye, cam->position);
    // Looking along +z to the depth of the framing target (z = 0).
    EXPECT_NEAR(v->target[2], 0.0f, 1e-5f);
    EXPECT_NEAR(v->target[0], 1.0f, 1e-5f);
    EXPECT_NEAR(v->up[1], -1.0f, 1e-6f);
    // 2 * atan(600 / 1448.5) is about 45 degrees.
    EXPECT_NEAR(v->fov_y_deg, 45.0f, 0.1f);
    EXPECT_EQ(v->radius, 2.0f);
}

TEST(TrainingCamera, TargetBehindUsesFramingDistance) {
    std::string json = kJson;
    json.replace(json.find("[1.0, -0.5, -4.0]"), 17, "[0.0, 0.0, 5.0]");
    const auto cam = splatthumb::parse_first_training_camera(json);
    ASSERT_TRUE(cam.has_value());
    const auto v = splatthumb::view_from_training_camera(*cam, framing());
    ASSERT_TRUE(v.has_value());
    EXPECT_NEAR(v->target[2], 15.0f, 1e-4f);
}

TEST(TrainingCamera, RejectsBrokenInput) {
    const auto bad = [](const std::string& json) {
        EXPECT_FALSE(splatthumb::parse_first_training_camera(json).has_value()) << json;
    };
    const auto replace = [](std::string from, std::string to) {
        std::string t = kJson;
        t.replace(t.find(from), from.size(), to);
        return t;
    };
    bad("");
    bad("{}");
    bad("[]");
    bad("[{");
    bad(R"([{"position": [1, 2, 3]}])");
    bad(replace("\"fy\": 1448.5", "\"fx_only\": 1"));
    bad(replace("[1.0, -0.5, -4.0]", "[1.0, -0.5]"));
    bad(replace("[1.0, -0.5, -4.0]", "[1.0, -0.5, nan]"));
    bad(replace("[1.0, -0.5, -4.0]", "[1.0, -0.5, 1e400]"));
    bad(replace("[1.0, -0.5, -4.0]", "[1.0, -0.5, 1e9]"));
    bad(replace("\"fy\": 1448.5", "\"fy\": -1"));
    bad(replace("\"height\": 1200", "\"height\": 0"));
    // Not a rotation: scaled, or a reflection.
    bad(replace("[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]",
                "[[2.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]"));
    bad(replace("[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]",
                "[[-1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]"));
    // Keys of the second camera do not fill in for the first.
    bad(replace("\"position\": [1.0, -0.5, -4.0],", ""));
}

TEST(TrainingCamera, KeyMustBeWholeAndQuoted) {
    std::string json = kJson;
    json.replace(json.find("\"position\""), 10, "\"xposition\"");
    EXPECT_FALSE(splatthumb::parse_first_training_camera(json).has_value());
}

TEST(TrainingCamera, FoundUpToThreeFoldersAbove) {
    const auto root = std::filesystem::temp_directory_path() / "splatthumb_cameras_test";
    std::filesystem::remove_all(root);
    const auto ply_dir = root / "out" / "point_cloud" / "iteration_30000";
    std::filesystem::create_directories(ply_dir);
    const auto ply = ply_dir / "point_cloud.ply";
    EXPECT_FALSE(splatthumb::find_training_camera(ply).has_value());

    std::ofstream(root / "out" / "cameras.json", std::ios::binary) << kJson;
    const auto found = splatthumb::find_training_camera(ply);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->position[2], -4.0f);

    // Four levels above is too far.
    const auto deeper = ply_dir / "a" / "b" / "c.ply";
    std::filesystem::create_directories(deeper.parent_path());
    EXPECT_FALSE(splatthumb::find_training_camera(deeper).has_value());

    // The nearest cameras.json wins, even when it is broken.
    std::ofstream(ply_dir / "cameras.json", std::ios::binary) << "not json";
    EXPECT_FALSE(splatthumb::find_training_camera(ply).has_value());
    std::filesystem::remove_all(root);
}

} // namespace
