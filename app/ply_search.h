#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

namespace splatthumb::app {

// Windows keeps no list of the files it has thumbnails for, and the thumbnail handler only
// sees file contents, so "remake every thumbnail" means: find the .ply files, then remake
// those that already have a thumbnail in the cache.

struct PlySearch {
    std::vector<std::filesystem::path> roots; // folders to search, with their subfolders
    std::vector<std::filesystem::path> skip;  // folders not to enter (compared ignoring case)
    std::size_t max_files = 100'000;          // stop after finding this many
    int max_depth = 64;                       // folders deeper than this are not entered
    const std::atomic<bool>* stop = nullptr;  // checked between folders
    // Called now and then with the number of .ply files found so far.
    std::function<void(std::size_t)> progress;
};

// Finds .ply files (any case) under the roots. Never follows links or junctions, and skips
// "$Recycle.Bin", "System Volume Information" and the `skip` folders. Folders that cannot be
// read are skipped. Never throws.
std::vector<std::filesystem::path> find_ply_files(const PlySearch& search) noexcept;

// The Windows folder, which never holds the user's files.
std::vector<std::filesystem::path> system_folders() noexcept;

} // namespace splatthumb::app
