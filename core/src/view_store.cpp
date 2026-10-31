#include "splatthumb/view_store.h"

#include <fstream>
#include <new>
#include <string>
#include <system_error>

#include "splatthumb/limits.h"

namespace splatthumb {

std::filesystem::path view_file_path(const std::filesystem::path& root, const Fingerprint& fp) {
    const std::string hex = fp.hex();
    return root / hex.substr(0, 2) / (hex + ".view");
}

std::optional<View> load_view(const std::filesystem::path& root, const Fingerprint& fp) noexcept {
    try {
        std::ifstream in(view_file_path(root, fp), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        // One byte more than allowed, so an oversized file is noticed and rejected.
        std::string text(limits::kMaxViewFileBytes + 1, '\0');
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (in.bad()) {
            return std::nullopt;
        }
        text.resize(static_cast<std::size_t>(in.gcount()));
        return parse_view(text);
    } catch (...) {
        // bad_alloc, or a path that cannot be converted.
        return std::nullopt;
    }
}

bool save_view(const std::filesystem::path& root, const Fingerprint& fp, const View& view) noexcept {
    try {
        const std::filesystem::path path = view_file_path(root, fp);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
        std::filesystem::path temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            const std::string text = format_view(view);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out) {
                std::filesystem::remove(temp, ec);
                return false;
            }
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool remove_view(const std::filesystem::path& root, const Fingerprint& fp) noexcept {
    try {
        const std::filesystem::path path = view_file_path(root, fp);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            return false;
        }
        // Leave no empty subfolder behind; fails harmlessly when others remain.
        std::filesystem::remove(path.parent_path(), ec);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace splatthumb
