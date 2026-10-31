// The folder search behind "Remake all existing thumbnails" in the settings window.

#include "ply_search.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using splatthumb::app::find_ply_files;
using splatthumb::app::PlySearch;
namespace fs = std::filesystem;

class PlySearchTest : public ::testing::Test {
  protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("splatthumb_search_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        for (const char* f :
             {"a.ply", "B.PLY", "notes.txt", "ply", "sub/c.ply", "sub/deep/d.ply", "sub/x.ply.txt",
              "skipme/e.ply", "$Recycle.Bin/f.ply", "System Volume Information/g.ply"}) {
            const fs::path p = root_ / f;
            fs::create_directories(p.parent_path());
            std::ofstream(p) << "x";
        }
    }
    void TearDown() override {
        fs::remove_all(root_);
    }

    std::vector<std::string> names(const std::vector<fs::path>& found) const {
        std::vector<std::string> out;
        for (const auto& p : found) {
            out.push_back(fs::relative(p, root_).generic_string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    fs::path root_;
};

TEST_F(PlySearchTest, FindsPlyFilesInSubfolders) {
    PlySearch s;
    s.roots = {root_};
    s.skip = {root_ / "SKIPME"};
    std::size_t last_progress = 0;
    s.progress = [&](std::size_t n) { last_progress = n; };
    const auto found = find_ply_files(s);
    EXPECT_EQ(names(found),
              (std::vector<std::string>{"B.PLY", "a.ply", "sub/c.ply", "sub/deep/d.ply"}));
    EXPECT_EQ(last_progress, 4u);
}

TEST_F(PlySearchTest, RootWithTrailingSlash) {
    PlySearch s;
    s.roots = {fs::path(root_.wstring() + L"\\")};
    EXPECT_EQ(find_ply_files(s).size(), 5u); // skipme/e.ply included this time
}

TEST_F(PlySearchTest, Limits) {
    PlySearch s;
    s.roots = {root_};
    s.max_files = 2;
    EXPECT_EQ(find_ply_files(s).size(), 2u);

    s.max_files = 100;
    s.max_depth = 1; // root and its direct subfolders only
    EXPECT_EQ(names(find_ply_files(s)),
              (std::vector<std::string>{"B.PLY", "a.ply", "skipme/e.ply", "sub/c.ply"}));

    s.max_depth = 0;
    EXPECT_EQ(find_ply_files(s).size(), 2u);
}

TEST_F(PlySearchTest, StopsWhenAsked) {
    std::atomic<bool> stop{true};
    PlySearch s;
    s.roots = {root_};
    s.stop = &stop;
    EXPECT_TRUE(find_ply_files(s).empty());
}

TEST_F(PlySearchTest, MissingFolderFindsNothing) {
    PlySearch s;
    s.roots = {root_ / "missing", fs::path()};
    EXPECT_TRUE(find_ply_files(s).empty());
}

TEST(PlySearch, SystemFoldersHasWindows) {
    const auto folders = splatthumb::app::system_folders();
    ASSERT_EQ(folders.size(), 1u);
    EXPECT_TRUE(fs::is_directory(folders[0]));
}

} // namespace
