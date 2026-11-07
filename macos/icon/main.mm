// splatthumb-icon: option B spike. Renders 3DGS PLY files with the shared core and sets the
// result as each file's custom Finder icon (stored in the file's extended attributes; the
// PLY data itself is not touched).
//
//   splatthumb-icon set <file-or-folder>...     set or refresh icons
//   splatthumb-icon remove <file-or-folder>...  remove icons this tool set
//   splatthumb-icon watch <folder>              set icons now, then keep them up to date
//
// A fingerprint of the file is stored next to the icon so unchanged files are skipped and
// edited files get a fresh icon.
#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "splatthumb/status.h"

namespace {

constexpr const char* kFingerprintAttr = "com.voxelkei.splatthumb.icon";
constexpr int kIconSize = 512;
constexpr int kRenderTimeLimitMs = 10000; // no Quick Look deadline here, but stay bounded

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

std::optional<std::string> read_attr(const char* path, const char* name) {
    char buf[128];
    const ssize_t n = getxattr(path, name, buf, sizeof(buf), 0, XATTR_NOFOLLOW);
    if (n < 0) {
        return std::nullopt;
    }
    return std::string(buf, static_cast<size_t>(n));
}

ssize_t attr_size(const char* path, const char* name) {
    return getxattr(path, name, nullptr, 0, 0, XATTR_NOFOLLOW);
}

bool is_ply(NSString* path) {
    return [path.pathExtension caseInsensitiveCompare:@"ply"] == NSOrderedSame;
}

std::vector<std::string> collect(NSString* arg) {
    std::vector<std::string> out;
    BOOL dir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:arg isDirectory:&dir]) {
        std::fprintf(stderr, "not found: %s\n", arg.UTF8String);
        return out;
    }
    if (!dir) {
        out.emplace_back(arg.fileSystemRepresentation);
        return out;
    }
    NSDirectoryEnumerator* it = [[NSFileManager defaultManager] enumeratorAtPath:arg];
    for (NSString* rel in it) {
        if (is_ply(rel)) {
            out.emplace_back([arg stringByAppendingPathComponent:rel].fileSystemRepresentation);
        }
    }
    return out;
}

NSImage* make_nsimage(const splatthumb::Image& image) {
    const size_t side = static_cast<size_t>(image.size);
    CFDataRef data =
        CFDataCreate(nullptr, image.rgba.data(), static_cast<CFIndex>(image.rgba.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef cg = CGImageCreate(side, side, 8, 32, side * 4, space,
                                  static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
                                  provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    if (cg == nullptr) {
        return nil;
    }
    NSImage* made = [[NSImage alloc] initWithCGImage:cg
                                                size:NSMakeSize(static_cast<CGFloat>(side),
                                                                static_cast<CGFloat>(side))];
    CGImageRelease(cg);
    return made;
}

bool remove_icon(const std::string& path, bool quiet) {
    if (!read_attr(path.c_str(), kFingerprintAttr)) {
        if (!quiet) {
            std::printf("skip   %s (no icon set by SplatThumb)\n", path.c_str());
        }
        return true;
    }
    struct stat before {};
    stat(path.c_str(), &before);
    NSString* p = [NSString stringWithUTF8String:path.c_str()];
    const bool ok = [[NSWorkspace sharedWorkspace] setIcon:nil forFile:p options:0];
    removexattr(path.c_str(), kFingerprintAttr, XATTR_NOFOLLOW);
    // setIcon:nil leaves a resource fork with no resources in it (an empty resource map);
    // drop it so the file is as before. The header's data length (bytes 8-11) is 0 then.
    unsigned char head[16] = {};
    if (getxattr(path.c_str(), XATTR_RESOURCEFORK_NAME, head, sizeof(head), 0, XATTR_NOFOLLOW) ==
            static_cast<ssize_t>(sizeof(head)) &&
        head[8] == 0 && head[9] == 0 && head[10] == 0 && head[11] == 0) {
        removexattr(path.c_str(), XATTR_RESOURCEFORK_NAME, XATTR_NOFOLLOW);
    }
    const struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
    utimensat(AT_FDCWD, path.c_str(), times, 0);
    std::printf("%s %s\n", ok ? "remove" : "FAILED", path.c_str());
    return ok;
}

// Sets or refreshes the icon of one file. Not-3DGS files are left alone (and lose an icon
// this tool set earlier, in case the file was replaced by something else).
void set_icon(const std::string& path) {
    using namespace splatthumb;
    const auto start = std::chrono::steady_clock::now();
    FileByteSource source;
    if (!source.open(path)) {
        std::printf("FAILED %s (cannot open)\n", path.c_str());
        return;
    }
    const auto fp = fingerprint(source);
    const std::string fp_hex = fp ? fp->hex() : std::string();
    if (fp && read_attr(path.c_str(), kFingerprintAttr) == fp_hex) {
        std::printf("same   %s\n", path.c_str());
        return;
    }

    RenderOptions options;
    options.size = kIconSize;
    options.background = Background::Gray;
    LoadOptions load;
    const RenderResult result = render_thumbnail(
        source, options, load,
        Deadline::after(std::chrono::milliseconds(kRenderTimeLimitMs)));
    if (result.status != Status::Ok) {
        std::printf("not3DGS %s (%s)\n", path.c_str(),
                    std::string(to_string(result.status)).c_str());
        remove_icon(path, true);
        return;
    }
    const double render_ms = ms_since(start);

    struct stat before {};
    stat(path.c_str(), &before);
    NSImage* image = make_nsimage(result.image);
    NSString* p = [NSString stringWithUTF8String:path.c_str()];
    const bool ok = image != nil && [[NSWorkspace sharedWorkspace] setIcon:image
                                                                   forFile:p
                                                                   options:0];
    if (!ok) {
        std::printf("FAILED %s (setIcon, read-only or locked?)\n", path.c_str());
        return;
    }
    if (fp) {
        setxattr(path.c_str(), kFingerprintAttr, fp_hex.data(), fp_hex.size(), 0, XATTR_NOFOLLOW);
    }
    struct stat after {};
    stat(path.c_str(), &after);
    {
        // setIcon bumps the modification date although the data is unchanged; put it back so
        // sorting by date and "changed since" tools are not fooled.
        const struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
        utimensat(AT_FDCWD, path.c_str(), times, 0);
    }
    std::printf("icon   %s  render %.0f ms, total %.0f ms, icon data %zd bytes, mtime %s\n",
                path.c_str(), render_ms, ms_since(start),
                attr_size(path.c_str(), XATTR_RESOURCEFORK_NAME),
                before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
                        before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec
                    ? "unchanged"
                    : "bumped by setIcon, restored");
}

void fs_callback(ConstFSEventStreamRef, void*, size_t count, void* paths,
                 const FSEventStreamEventFlags flags[], const FSEventStreamEventId[]) {
    NSArray* list = (__bridge NSArray*)paths;
    for (size_t i = 0; i < count; ++i) {
        NSString* path = list[i];
        if (!is_ply(path) || (flags[i] & kFSEventStreamEventFlagItemIsFile) == 0) {
            continue;
        }
        if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            continue; // removed or renamed away
        }
        set_icon(path.fileSystemRepresentation);
    }
}

int watch(NSString* folder) {
    for (const auto& f : collect(folder)) {
        set_icon(f);
    }
    std::printf("watching %s (Ctrl+C to stop)\n", folder.UTF8String);
    std::fflush(stdout);
    FSEventStreamContext context{};
    FSEventStreamRef stream = FSEventStreamCreate(
        nullptr, &fs_callback, &context, (__bridge CFArrayRef) @[ folder ],
        kFSEventStreamEventIdSinceNow, 1.0,
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagUseCFTypes |
            kFSEventStreamCreateFlagNoDefer);
    FSEventStreamSetDispatchQueue(stream, dispatch_get_main_queue());
    FSEventStreamStart(stream);
    dispatch_main();
}

int usage() {
    std::fprintf(stderr, "usage: splatthumb-icon set|remove <file-or-folder>...\n"
                         "       splatthumb-icon watch <folder>\n");
    return 2;
}

} // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc < 3) {
            return usage();
        }
        const std::string command = argv[1];
        setvbuf(stdout, nullptr, _IOLBF, 0);
        if (command == "watch" && argc == 3) {
            return watch([NSString stringWithUTF8String:argv[2]]);
        }
        if (command != "set" && command != "remove") {
            return usage();
        }
        for (int i = 2; i < argc; ++i) {
            for (const auto& f : collect([NSString stringWithUTF8String:argv[i]])) {
                if (command == "set") {
                    set_icon(f);
                } else {
                    remove_icon(f, false);
                }
            }
        }
    }
    return 0;
}
