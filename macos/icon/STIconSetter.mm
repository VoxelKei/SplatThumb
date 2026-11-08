#import "STIconSetter.h"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "splatthumb/settings.h"
#include "splatthumb/status.h"
#include "splatthumb/view.h"
#include "splatthumb/view_store.h"

namespace {

// Marks icons this tool set; the value is the file's fingerprint when the icon was made.
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

void restore_times(const char* path, const struct stat& before) {
    const struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
    utimensat(AT_FDCWD, path, times, 0);
}

NSImage* make_nsimage(const splatthumb::Image& image) {
    const size_t side = static_cast<size_t>(image.size);
    CFDataRef data =
        CFDataCreate(nullptr, image.rgba.data(), static_cast<CFIndex>(image.rgba.size()));
    if (data == nullptr) {
        return nil;
    }
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

// Stable 64-bit FNV-1a, so the marker does not depend on the standard library's hash.
std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h = (h ^ c) * 1099511628211ull;
    }
    return h;
}

// Marker stored with the icon: the file's fingerprint, plus a hash of the saved view when
// there is one, so a new angle (or going back to automatic) makes a fresh icon.
std::string icon_marker(const std::string& fp_hex, const std::optional<splatthumb::View>& view) {
    if (!view) {
        return fp_hex;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), ":%016llx",
                  static_cast<unsigned long long>(fnv1a(splatthumb::format_view(*view))));
    return fp_hex + buf;
}

NSString* const kQualityKey = @"Quality";

splatthumb::Quality quality_from_setting(NSInteger q) {
    switch (q) {
    case 0: return splatthumb::Quality::Fast;
    case 2: return splatthumb::Quality::High;
    default: return splatthumb::Quality::Standard;
    }
}

NSString* line(NSString* tag, NSString* path, NSString* detail) {
    return detail.length > 0
               ? [NSString stringWithFormat:@"%@ %@: %@", tag, path.lastPathComponent, detail]
               : [NSString stringWithFormat:@"%@ %@", tag, path.lastPathComponent];
}

} // namespace

@implementation STIconSetter

+ (NSString*)viewsRoot {
    // The app is sandboxed, so its home is its container; the command-line tool is not, so
    // it looks inside the app's container to share the same views.
    NSString* home = NSHomeDirectory();
    if (NSProcessInfo.processInfo.environment[@"APP_SANDBOX_CONTAINER_ID"] == nil) {
        home = [home stringByAppendingPathComponent:
                         @"Library/Containers/com.voxelkei.splatthumb/Data"];
    }
    return [home stringByAppendingPathComponent:@"Library/Application Support/SplatThumb/views"];
}

+ (NSArray<NSString*>*)plyFilesAt:(NSString*)path {
    NSMutableArray<NSString*>* out = [NSMutableArray array];
    BOOL dir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&dir]) {
        return out;
    }
    if (!dir) {
        [out addObject:path];
        return out;
    }
    for (NSString* rel in [[NSFileManager defaultManager] enumeratorAtPath:path]) {
        if ([rel.pathExtension caseInsensitiveCompare:@"ply"] == NSOrderedSame) {
            [out addObject:[path stringByAppendingPathComponent:rel]];
        }
    }
    return out;
}

+ (NSString*)removeIconForFile:(NSString*)path {
    const char* p = path.fileSystemRepresentation;
    if (!read_attr(p, kFingerprintAttr)) {
        return line(@"skip  ", path, NSLocalizedString(@"no icon set by SplatThumb", nil));
    }
    struct stat before {};
    stat(p, &before);
    const BOOL ok = [[NSWorkspace sharedWorkspace] setIcon:nil forFile:path options:0];
    removexattr(p, kFingerprintAttr, XATTR_NOFOLLOW);
    // setIcon:nil leaves a resource fork with no resources in it (an empty resource map);
    // drop it so the file is as before. The header's data length (bytes 8-11) is 0 then.
    unsigned char head[16] = {};
    if (getxattr(p, XATTR_RESOURCEFORK_NAME, head, sizeof(head), 0, XATTR_NOFOLLOW) ==
            static_cast<ssize_t>(sizeof(head)) &&
        head[8] == 0 && head[9] == 0 && head[10] == 0 && head[11] == 0) {
        removexattr(p, XATTR_RESOURCEFORK_NAME, XATTR_NOFOLLOW);
    }
    restore_times(p, before);
    return line(ok ? @"remove" : @"FAILED", path, nil);
}

+ (NSInteger)qualitySetting {
    const id value = [NSUserDefaults.standardUserDefaults objectForKey:kQualityKey];
    const NSInteger q = [value isKindOfClass:NSNumber.class] ? [value integerValue] : 1;
    return q >= 0 && q <= 2 ? q : 1;
}

+ (void)setQualitySetting:(NSInteger)quality {
    [NSUserDefaults.standardUserDefaults setInteger:(quality >= 0 && quality <= 2 ? quality : 1)
                                             forKey:kQualityKey];
}

+ (NSString*)setIconForFile:(NSString*)path {
    return [self setIconForFile:path force:NO];
}

+ (NSString*)setIconForFile:(NSString*)path force:(BOOL)force {
    using namespace splatthumb;
    const auto start = std::chrono::steady_clock::now();
    const char* p = path.fileSystemRepresentation;
    FileByteSource source;
    if (!source.open(p)) {
        return line(@"FAILED", path, NSLocalizedString(@"cannot open", nil));
    }
    const auto fp = fingerprint(source);
    std::optional<View> view;
    if (fp) {
        view = load_view(self.viewsRoot.fileSystemRepresentation, *fp);
    }
    // A count saved with the view wins over the app setting.
    const Quality quality =
        view && view->quality ? *view->quality : quality_from_setting(self.qualitySetting);
    std::string marker = fp ? icon_marker(fp->hex(), view) : std::string();
    if (fp && quality != Quality::Standard) {
        // So a changed count is not taken for an unchanged icon. Standard adds nothing,
        // which keeps the marks of icons made before the setting existed.
        marker += quality == Quality::Fast ? ":q0" : ":q2";
    }
    if (!force && fp && read_attr(p, kFingerprintAttr) == marker) {
        return line(@"same  ", path, nil);
    }

    RenderOptions options;
    options.size = kIconSize;
    options.background = Background::Gray;
    LoadOptions load;
    load.max_splats = max_splats_for(quality);
    const RenderResult result = render_thumbnail(
        source, options, load, Deadline::after(std::chrono::milliseconds(kRenderTimeLimitMs)),
        nullptr, view ? &*view : nullptr);
    if (result.status != Status::Ok) {
        // Not 3DGS (or broken): drop an icon we set earlier, in case the file was replaced.
        if (read_attr(p, kFingerprintAttr)) {
            [self removeIconForFile:path];
        }
        return line(@"not3DGS", path, @(std::string(to_string(result.status)).c_str()));
    }

    struct stat before {};
    stat(p, &before);
    NSImage* image = make_nsimage(result.image);
    if (image == nil || ![[NSWorkspace sharedWorkspace] setIcon:image forFile:path options:0]) {
        return line(@"FAILED", path, NSLocalizedString(@"could not set the icon (read-only or locked?)", nil));
    }
    if (fp) {
        setxattr(p, kFingerprintAttr, marker.data(), marker.size(), 0, XATTR_NOFOLLOW);
    }
    // setIcon bumps the modification date although the data is unchanged; put it back.
    restore_times(p, before);
    return line(@"icon  ", path,
                [NSString stringWithFormat:@"%.0f ms%@", ms_since(start), view ? NSLocalizedString(@" (using the saved angle)", nil) : @""]);
}

@end

// --- STFolderWatcher ---------------------------------------------------------

@interface STFolderWatcher ()
@property(nonatomic, copy) NSString* folder;
@property(nonatomic) NSTimeInterval settle;
@property(nonatomic, copy) void (^handler)(NSString*);
// path -> time of the latest event for it
@property(nonatomic, strong) NSMutableDictionary<NSString*, NSDate*>* pending;
@end

@implementation STFolderWatcher {
    FSEventStreamRef _stream;
}

- (instancetype)initWithFolder:(NSString*)folder
                        settle:(NSTimeInterval)settle
                       handler:(void (^)(NSString*))handler {
    if ((self = [super init])) {
        _folder = [folder copy];
        _settle = settle;
        _handler = [handler copy];
        _pending = [NSMutableDictionary dictionary];
    }
    return self;
}

- (void)dealloc {
    [self stop];
}

static void st_fs_callback(ConstFSEventStreamRef, void* info, size_t count, void* paths,
                           const FSEventStreamEventFlags flags[], const FSEventStreamEventId[]) {
    STFolderWatcher* watcher = (__bridge STFolderWatcher*)info;
    NSArray* list = (__bridge NSArray*)paths;
    for (size_t i = 0; i < count; ++i) {
        NSString* path = list[i];
        if ((flags[i] & kFSEventStreamEventFlagItemIsFile) == 0 ||
            [path.pathExtension caseInsensitiveCompare:@"ply"] != NSOrderedSame) {
            continue;
        }
        [watcher noteChange:path];
    }
}

- (void)noteChange:(NSString*)path {
    self.pending[path] = [NSDate date];
    __weak STFolderWatcher* weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(self.settle * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                       [weakSelf settleCheck:path];
                   });
}

// Runs settle seconds after an event; acts only if no newer event came in meanwhile.
- (void)settleCheck:(NSString*)path {
    NSDate* last = self.pending[path];
    if (last == nil || -last.timeIntervalSinceNow < self.settle - 0.05) {
        return; // a newer event will check again
    }
    [self.pending removeObjectForKey:path];
    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
        self.handler(path);
    }
}

- (void)start {
    if (_stream != nullptr) {
        return;
    }
    FSEventStreamContext context{};
    context.info = (__bridge void*)self;
    _stream = FSEventStreamCreate(nullptr, &st_fs_callback, &context,
                                  (__bridge CFArrayRef) @[ self.folder ],
                                  kFSEventStreamEventIdSinceNow, 0.5,
                                  kFSEventStreamCreateFlagFileEvents |
                                      kFSEventStreamCreateFlagUseCFTypes);
    FSEventStreamSetDispatchQueue(_stream, dispatch_get_main_queue());
    FSEventStreamStart(_stream);
}

- (void)stop {
    if (_stream == nullptr) {
        return;
    }
    FSEventStreamStop(_stream);
    FSEventStreamInvalidate(_stream);
    FSEventStreamRelease(_stream);
    _stream = nullptr;
    [self.pending removeAllObjects];
}

@end
