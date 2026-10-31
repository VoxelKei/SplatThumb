#include "ply_search.h"

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>
#include <utility>

namespace splatthumb::app {

namespace {

bool equal_ignoring_case(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_ply_name(std::wstring_view name) noexcept {
    return name.size() > 4 && equal_ignoring_case(name.substr(name.size() - 4), L".ply");
}

std::wstring without_trailing_slash(std::wstring s) {
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) {
        s.pop_back();
    }
    return s;
}

} // namespace

std::vector<std::filesystem::path> find_ply_files(const PlySearch& search) noexcept {
    std::vector<std::filesystem::path> found;
    try {
        std::vector<std::wstring> skip;
        for (const auto& s : search.skip) {
            skip.push_back(without_trailing_slash(s.wstring()));
        }
        // Depth-first with an explicit stack, so deep trees cannot overflow the call stack.
        std::vector<std::pair<std::wstring, int>> pending;
        for (auto it = search.roots.rbegin(); it != search.roots.rend(); ++it) {
            if (!it->empty()) {
                pending.emplace_back(without_trailing_slash(it->wstring()), 0);
            }
        }
        std::size_t folders = 0;
        while (!pending.empty() && found.size() < search.max_files) {
            if (search.stop != nullptr && search.stop->load()) {
                break;
            }
            auto [folder, depth] = std::move(pending.back());
            pending.pop_back();
            if (++folders % 256 == 0 && search.progress) {
                search.progress(found.size());
            }
            const std::wstring prefix =
                folder.back() == L'\\' || folder.back() == L'/' ? folder : folder + L'\\';
            WIN32_FIND_DATAW data;
            const HANDLE h =
                FindFirstFileExW((prefix + L'*').c_str(), FindExInfoBasic, &data,
                                 FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (h == INVALID_HANDLE_VALUE) {
                continue;
            }
            std::vector<std::wstring> subfolders;
            do {
                const std::wstring_view name = data.cFileName;
                if (name == L"." || name == L"..") {
                    continue;
                }
                const DWORD attrs = data.dwFileAttributes;
                if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    // Links and junctions can loop or lead elsewhere; never follow them.
                    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || depth >= search.max_depth ||
                        equal_ignoring_case(name, L"$Recycle.Bin") ||
                        equal_ignoring_case(name, L"System Volume Information")) {
                        continue;
                    }
                    std::wstring path = prefix + std::wstring(name);
                    bool skipped = false;
                    for (const auto& s : skip) {
                        skipped = skipped || equal_ignoring_case(path, s);
                    }
                    if (!skipped) {
                        subfolders.push_back(std::move(path));
                    }
                } else if (is_ply_name(name) && found.size() < search.max_files) {
                    found.emplace_back(prefix + std::wstring(name));
                }
            } while (FindNextFileW(h, &data));
            FindClose(h);
            // Visit subfolders in name order.
            for (auto it = subfolders.rbegin(); it != subfolders.rend(); ++it) {
                pending.emplace_back(std::move(*it), depth + 1);
            }
        }
        if (search.progress) {
            search.progress(found.size());
        }
    } catch (...) {
        // Out of memory: return what was found so far.
    }
    return found;
}

std::vector<std::filesystem::path> system_folders() noexcept {
    std::vector<std::filesystem::path> folders;
    try {
        wchar_t buf[MAX_PATH];
        const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            folders.emplace_back(buf);
        }
    } catch (...) {
    }
    return folders;
}

} // namespace splatthumb::app
