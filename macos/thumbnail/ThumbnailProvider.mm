// Quick Look thumbnail provider for 3DGS PLY files and Spatialograph Maker projects (.spgm).
// Mirrors shellext/thumbnail_provider.cpp: anything we cannot draw is declined so macOS can
// fall back to whatever it would show otherwise.
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

#include <mach/mach.h>
#include <os/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/render.h"
#include "splatthumb/spgm.h"
#include "splatthumb/status.h"

namespace {

os_log_t log_handle() {
    static os_log_t handle = os_log_create("com.voxelkei.splatthumb", "thumbnail");
    return handle;
}

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

// Memory the extension process is charged for, in MB (what jetsam limits look at).
double footprint_mb() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return -1.0;
    }
    return static_cast<double>(info.phys_footprint) / (1024.0 * 1024.0);
}

NSError* decline(NSString* why) {
    return [NSError errorWithDomain:@"com.voxelkei.splatthumb"
                               code:1
                           userInfo:@{NSLocalizedDescriptionKey : why}];
}

// Pixel side of the square thumbnail for this request, within the core's limits.
int pixel_side(QLFileThumbnailRequest* request) {
    const CGFloat points = std::min(request.maximumSize.width, request.maximumSize.height);
    const double pixels = std::ceil(static_cast<double>(points * request.scale));
    return static_cast<int>(std::clamp(pixels, static_cast<double>(splatthumb::limits::kMinThumbnailSize),
                                       static_cast<double>(splatthumb::limits::kMaxThumbnailSize)));
}

// Wraps the renderer's RGBA (premultiplied, rows top to bottom) in a CGImage.
CGImageRef make_cgimage(const splatthumb::Image& image) {
    const size_t side = static_cast<size_t>(image.size);
    CFDataRef data = CFDataCreate(nullptr, image.rgba.data(), static_cast<CFIndex>(image.rgba.size()));
    if (data == nullptr) {
        return nullptr;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (provider == nullptr) {
        return nullptr;
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef made = CGImageCreate(side, side, 8, 32, side * 4, space,
                                    static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
                                    provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    return made;
}

// Decodes an embedded PNG/JPEG at most max_side pixels on its longer side.
CGImageRef decode_embedded(const std::vector<std::byte>& bytes, int max_side) {
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                                  static_cast<CFIndex>(bytes.size()));
    if (data == nullptr) {
        return nullptr;
    }
    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (source == nullptr) {
        return nullptr;
    }
    NSDictionary* options = @{
        (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (__bridge NSString*)kCGImageSourceShouldCacheImmediately : @YES,
        (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize : @(max_side),
    };
    CGImageRef image =
        CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
    CFRelease(source);
    return image;
}

// Replies with the image drawn into a square of side_points, aspect kept, centred.
void reply_with_image(CGImageRef image, CGFloat side_points,
                      void (^handler)(QLThumbnailReply* _Nullable, NSError* _Nullable)) {
    CGImageRetain(image);
    const CGFloat w = static_cast<CGFloat>(CGImageGetWidth(image));
    const CGFloat h = static_cast<CGFloat>(CGImageGetHeight(image));
    const CGFloat scale = side_points / std::max(w, h);
    const CGSize size = CGSizeMake(std::round(w * scale), std::round(h * scale));
    QLThumbnailReply* reply =
        [QLThumbnailReply replyWithContextSize:size
                                  drawingBlock:^BOOL(CGContextRef context) {
                                      CGContextDrawImage(
                                          context, CGRectMake(0, 0, size.width, size.height), image);
                                      CGImageRelease(image);
                                      return YES;
                                  }];
    handler(reply, nil);
}

} // namespace

@interface ThumbnailProvider : QLThumbnailProvider
@end

@implementation ThumbnailProvider

- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest*)request
                     completionHandler:(void (^)(QLThumbnailReply* _Nullable,
                                                 NSError* _Nullable))handler {
    using namespace splatthumb;
    const auto start = std::chrono::steady_clock::now();
    const Deadline deadline =
        Deadline::after(std::chrono::milliseconds(limits::kRenderTimeLimitMs));
    const int side = pixel_side(request);
    const CGFloat side_points = static_cast<CGFloat>(side) / std::max<CGFloat>(request.scale, 1);
    const std::string name = request.fileURL.lastPathComponent.UTF8String ?: "";

    FileByteSource source;
    if (!source.open(request.fileURL.fileSystemRepresentation)) {
        os_log_error(log_handle(), "%{public}s: cannot open", name.c_str());
        handler(nil, decline(@"cannot open"));
        return;
    }

    if (const SpgmResult spgm = probe_spgm(source); spgm.probe != SpgmProbe::NotSpgm) {
        if (spgm.probe != SpgmProbe::Ok) {
            os_log(log_handle(), "%{public}s: spgm declined (%{public}s)", name.c_str(),
                   std::string(to_string(spgm.probe)).c_str());
            handler(nil, decline(@"spgm without a usable image"));
            return;
        }
        const auto bytes = read_spgm_image(source, spgm.image);
        CGImageRef image = bytes ? decode_embedded(*bytes, side) : nullptr;
        if (image == nullptr) {
            os_log(log_handle(), "%{public}s: spgm image could not be decoded", name.c_str());
            handler(nil, decline(@"spgm image could not be decoded"));
            return;
        }
        os_log(log_handle(), "%{public}s: spgm ok %zux%zu in %.1f ms, footprint %.1f MB",
               name.c_str(), CGImageGetWidth(image), CGImageGetHeight(image), ms_since(start),
               footprint_mb());
        reply_with_image(image, side_points, handler);
        CGImageRelease(image);
        return;
    }

    RenderOptions options;
    options.size = side;
    options.background = Background::Gray;
    LoadOptions load; // standard quality; settings and saved views come later
    ThumbnailStats stats;
    const RenderResult result = render_thumbnail(source, options, load, deadline, &stats);
    if (result.status != Status::Ok) {
        // Not a 3DGS file (e.g. a normal mesh), broken, too slow or empty: let macOS decide.
        os_log(log_handle(), "%{public}s: declined (%{public}s) after %.1f ms", name.c_str(),
               std::string(to_string(result.status)).c_str(), ms_since(start));
        handler(nil, decline(@"not a drawable 3DGS file"));
        return;
    }

    CGImageRef image = make_cgimage(result.image);
    if (image == nullptr) {
        handler(nil, decline(@"out of memory"));
        return;
    }
    os_log(log_handle(),
           "%{public}s: ok %dpx, %llu vertices, %zu drawn, inspect %.1f load %.1f camera %.1f "
           "render %.1f total %.1f ms, footprint %.1f MB",
           name.c_str(), result.image.size, static_cast<unsigned long long>(stats.vertex_count),
           stats.splats_drawn, stats.inspect_ms, stats.load_ms, stats.camera_ms, stats.render_ms,
           ms_since(start), footprint_mb());
    reply_with_image(image, side_points, handler);
    CGImageRelease(image);
}

@end
