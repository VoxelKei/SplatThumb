#include <cstdio>
#include <filesystem>
#include <string_view>

#include "file_byte_source.h"
#include "splatthumb/classify.h"
#include "splatthumb/version.h"

namespace {

// Exit codes
constexpr int kExitSplat = 0;
constexpr int kExitNotSplat = 1;
constexpr int kExitUsage = 2;
constexpr int kExitCannotOpen = 3;

void print_usage() {
    std::puts("usage: splatthumb-cli --version\n"
              "       splatthumb-cli info <file.ply>");
}

void print(std::string_view label, std::string_view value) {
    std::printf("%-12s %.*s\n", label.data(), static_cast<int>(value.size()), value.data());
}

void print(std::string_view label, unsigned long long value) {
    std::printf("%-12s %llu\n", label.data(), value);
}

std::string_view kind_name(splatthumb::SplatKind kind) {
    switch (kind) {
    case splatthumb::SplatKind::Standard: return "standard 3DGS";
    case splatthumb::SplatKind::Compressed: return "compressed 3DGS (PlayCanvas)";
    case splatthumb::SplatKind::NotSplat: return "not a splat";
    }
    return "unknown";
}

std::string_view sh_degree(std::size_t rest_count) {
    switch (rest_count) {
    case 0: return "0";
    case 9: return "1";
    case 24: return "2";
    case 45: return "3";
    default: return "unknown";
    }
}

int run_info(const std::filesystem::path& path) {
    splatthumb::cli::FileByteSource source;
    if (!source.open(path)) {
        std::fputs("error: cannot open file\n", stderr);
        return kExitCannotOpen;
    }
    const splatthumb::Classification c = splatthumb::inspect(source);
    print("kind:", kind_name(c.kind));
    if (c.kind == splatthumb::SplatKind::NotSplat) {
        print("reason:", splatthumb::to_string(c.status));
        return kExitNotSplat;
    }
    print("vertices:", c.vertex_count);
    print("stride:", c.vertex_stride);
    print("data offset:", c.vertex_data_offset);
    print("file size:", source.size());
    if (c.kind == splatthumb::SplatKind::Standard) {
        print("sh degree:", sh_degree(c.sh_rest_count));
    }
    return kExitSplat;
}

template <typename Char>
int run(int argc, Char** argv) {
    if (argc == 2 && std::filesystem::path(argv[1]) == "--version") {
        const std::string_view v = splatthumb::version();
        std::printf("splatthumb-cli %.*s\n", static_cast<int>(v.size()), v.data());
        return 0;
    }
    if (argc == 3 && std::filesystem::path(argv[1]) == "info") {
        return run_info(std::filesystem::path(argv[2]));
    }
    print_usage();
    return kExitUsage;
}

} // namespace

#ifdef _WIN32
// wmain so that paths with non-ASCII characters (e.g. Japanese folder names) work.
int wmain(int argc, wchar_t** argv) {
    return run(argc, argv);
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
