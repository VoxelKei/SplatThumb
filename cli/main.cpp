#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "png_writer.h"
#include "splatthumb/classify.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"
#include "splatthumb/render.h"
#include "splatthumb/spgm.h"
#include "splatthumb/version.h"
#include "splatthumb/view_store.h"

namespace {

// Exit codes
constexpr int kExitSplat = 0;
constexpr int kExitNotSplat = 1;
constexpr int kExitUsage = 2;
constexpr int kExitCannotOpen = 3;
constexpr int kExitRenderFailed = 4;

void print_usage() {
    std::puts("usage: splatthumb-cli --version\n"
              "       splatthumb-cli info <file.ply|file.spgm>\n"
              "       splatthumb-cli fingerprint <file.ply>\n"
              "       splatthumb-cli render <file.ply> <out.png> [--size N]\n"
              "                     [--background gray|transparent]\n"
              "                     [--sampling blocks|uniform] [--importance on|off]");
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
    splatthumb::FileByteSource source;
    if (!source.open(path)) {
        std::fputs("error: cannot open file\n", stderr);
        return kExitCannotOpen;
    }
    // A Spatialograph Maker project: only its embedded preview image is looked at.
    if (const splatthumb::SpgmResult spgm = splatthumb::probe_spgm(source);
        spgm.probe != splatthumb::SpgmProbe::NotSpgm) {
        print("kind:", "Spatialograph Maker project");
        if (spgm.probe != splatthumb::SpgmProbe::ReadError) {
            print("version:", spgm.image.version);
        }
        print("file size:", source.size());
        if (spgm.probe != splatthumb::SpgmProbe::Ok) {
            print("reason:", splatthumb::to_string(spgm.probe));
            return kExitNotSplat;
        }
        print("image:", spgm.image.format == splatthumb::EmbeddedImageFormat::Png ? "PNG" : "JPEG");
        print("image size:", spgm.image.size);
        print("offset:", spgm.image.offset);
        return kExitSplat;
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

// Prints the fingerprint that names the file's saved view, and where that view is kept.
int run_fingerprint(const std::filesystem::path& path) {
    splatthumb::FileByteSource source;
    if (!source.open(path)) {
        std::fputs("error: cannot open file\n", stderr);
        return kExitCannotOpen;
    }
    const auto fp = splatthumb::fingerprint(source);
    if (!fp) {
        std::fputs("error: cannot read file\n", stderr);
        return kExitCannotOpen;
    }
    print("fingerprint:", fp->hex());
    const std::string view =
        splatthumb::view_file_path("views", *fp).generic_string();
    print("view file:", view);
    return 0;
}

struct RenderArgs {
    splatthumb::RenderOptions render;
    splatthumb::LoadOptions load;
};

// Parses the optional render flags in args[0..n). Returns false on bad input.
bool parse_render_flags(const std::string* args, int n, RenderArgs& out) {
    for (int i = 0; i < n; i += 2) {
        if (i + 1 >= n) {
            return false;
        }
        const std::string& key = args[i];
        const std::string& value = args[i + 1];
        if (key == "--size") {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != '\0' || v < splatthumb::limits::kMinThumbnailSize ||
                v > splatthumb::limits::kMaxThumbnailSize) {
                return false;
            }
            out.render.size = static_cast<int>(v);
        } else if (key == "--background" && value == "transparent") {
            out.render.background = splatthumb::Background::Transparent;
        } else if (key == "--background" && value == "gray") {
            out.render.background = splatthumb::Background::Gray;
        } else if (key == "--sampling" && value == "blocks") {
            out.load.mode = splatthumb::SampleMode::Blocks;
        } else if (key == "--sampling" && value == "uniform") {
            out.load.mode = splatthumb::SampleMode::Uniform;
        } else if (key == "--importance" && value == "on") {
            out.load.importance = true;
        } else if (key == "--importance" && value == "off") {
            out.load.importance = false;
        } else {
            return false;
        }
    }
    return true;
}

int run_render(const std::filesystem::path& in, const std::filesystem::path& out,
               const RenderArgs& args) {
    splatthumb::FileByteSource source;
    if (!source.open(in)) {
        std::fputs("error: cannot open file\n", stderr);
        return kExitCannotOpen;
    }
    splatthumb::ThumbnailStats stats;
    const auto deadline = splatthumb::Deadline::after(
        std::chrono::milliseconds(splatthumb::limits::kRenderTimeLimitMs));
    const auto start = std::chrono::steady_clock::now();
    const splatthumb::RenderResult r =
        splatthumb::render_thumbnail(source, args.render, args.load, deadline, &stats);
    const double total_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    print("vertices:", stats.vertex_count);
    print("loaded:", stats.splats_loaded);
    print("drawn:", stats.splats_drawn);
    print("importance:", stats.importance_records_read);
    std::printf("%-12s inspect %.1f / load %.1f / camera %.1f / importance %.1f / render %.1f / "
                "total %.1f ms\n",
                "time:", stats.inspect_ms, stats.load_ms, stats.camera_ms, stats.importance_ms,
                stats.render_ms, total_ms);
    if (r.status != splatthumb::Status::Ok) {
        print("error:", splatthumb::to_string(r.status));
        return kExitRenderFailed;
    }
    if (!splatthumb::cli::write_png(out, r.image)) {
        std::fputs("error: cannot write PNG\n", stderr);
        return kExitCannotOpen;
    }
    return 0;
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
    if (argc == 3 && std::filesystem::path(argv[1]) == "fingerprint") {
        return run_fingerprint(std::filesystem::path(argv[2]));
    }
    if (argc >= 4 && argc <= 12 && std::filesystem::path(argv[1]) == "render") {
        // Flags are ASCII; convert them through path for both char and wchar_t argv.
        std::string flags[8];
        const int n = argc - 4;
        for (int i = 0; i < n; ++i) {
            flags[i] = std::filesystem::path(argv[4 + i]).string();
        }
        RenderArgs args;
        if (parse_render_flags(flags, n, args)) {
            return run_render(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), args);
        }
    }
    print_usage();
    return kExitUsage;
}

} // namespace

#ifdef _WIN32
// wmain so that paths with non-ASCII characters (e.g. Japanese folder names) work.
int wmain(int argc, wchar_t** argv) {
    try {
        return run(argc, argv);
    } catch (...) {
        print_usage();
        return kExitUsage;
    }
}
#else
int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (...) {
        print_usage();
        return kExitUsage;
    }
}
#endif
