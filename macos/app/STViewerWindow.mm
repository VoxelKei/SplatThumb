#import "STViewerWindow.h"

#import "STIconSetter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "splatthumb/camera.h"
#include "splatthumb/classify.h"
#include "splatthumb/deadline.h"
#include "splatthumb/file_byte_source.h"
#include "splatthumb/fingerprint.h"
#include "splatthumb/limits.h"
#include "splatthumb/loader.h"
#include "splatthumb/orbit.h"
#include "splatthumb/render.h"
#include "splatthumb/settings.h"
#include "splatthumb/view.h"
#include "splatthumb/view_store.h"

using namespace splatthumb;

namespace {

// Same numbers as the Windows viewer.
constexpr float kPi = std::numbers::pi_v<float>;
constexpr std::size_t kPreviewSplats = 30'000;
constexpr int kPreviewSize = 320;
constexpr auto kPreviewTime = std::chrono::milliseconds(500);
constexpr auto kFullTime = std::chrono::seconds(10);
constexpr auto kLoadTime = std::chrono::seconds(60);
constexpr NSTimeInterval kSettleSeconds = 0.25;
constexpr std::uint8_t kBackgroundLevel = 46; // the dark gray of the thumbnails

CGImageRef make_cgimage(const Image& image) {
    const size_t side = static_cast<size_t>(image.size);
    CFDataRef data =
        CFDataCreate(nullptr, image.rgba.data(), static_cast<CFIndex>(image.rgba.size()));
    if (data == nullptr) {
        return nullptr;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef made = CGImageCreate(side, side, 8, 32, side * 4, space,
                                    static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
                                    provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    return made;
}

struct Loaded {
    std::vector<Splat> splats;
    std::vector<Splat> preview;
    View auto_view;
};

// Loads the splats and the automatic view, as the Windows viewer's load_splats_at does.
std::optional<Loaded> load_file(const char* path, Quality quality) {
    FileByteSource source;
    if (!source.open(path)) {
        return std::nullopt;
    }
    const Classification c = inspect(source);
    if (c.kind == SplatKind::NotSplat) {
        return std::nullopt;
    }
    LoadOptions options;
    options.max_splats = max_splats_for(quality);
    LoadResult loaded = load_splats(source, c, options, Deadline::after(kLoadTime));
    if (loaded.status != Status::Ok) {
        return std::nullopt;
    }
    const CameraResult automatic = auto_camera(loaded.splats);
    if (automatic.status != Status::Ok) {
        return std::nullopt;
    }
    // Then, as the thumbnail does, keep the most important splats of a sampled file. No
    // camera: the view moves here, so splats out of the first view are kept too.
    prefer_important_splats(source, c, options.max_splats, nullptr, loaded,
                            Deadline::after(kLoadTime));
    Loaded out;
    try {
        const std::size_t step = loaded.splats.size() / kPreviewSplats + 1;
        out.preview.reserve(loaded.splats.size() / step + 1);
        for (std::size_t i = 0; i < loaded.splats.size(); i += step) {
            out.preview.push_back(loaded.splats[i]);
        }
        out.splats = std::move(loaded.splats);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    out.auto_view = view_from_camera(automatic.camera);
    return out;
}

} // namespace

// --- picture -------------------------------------------------------------------

@interface STPictureView : NSView
@end

@implementation STPictureView {
@public
    std::vector<Splat> _splats;
    std::vector<Splat> _preview;
    View _view;
    View _autoView;
    bool _loaded;
@private
    bool _dragging;
    bool _panning;
    bool _settling;
    bool _dirty;
    bool _showsPreview;
    int _imageSize;
    CGImageRef _image;
    NSPoint _last;
    NSTimer* _settleTimer;
}

- (void)dealloc {
    if (_image != nullptr) {
        CGImageRelease(_image);
    }
    [_settleTimer invalidate];
}

- (BOOL)isFlipped {
    return YES; // y grows downward, as on Windows, so the mouse math is the same
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

- (NSRect)pictureRect {
    const NSRect b = self.bounds;
    const CGFloat side = std::floor(std::min(b.size.width, b.size.height));
    return NSMakeRect(std::floor((b.size.width - side) / 2), std::floor((b.size.height - side) / 2),
                      side, side);
}

- (void)setView:(const View&)view settle:(bool)settle {
    _view = view;
    _dirty = true;
    if (settle) {
        // Draw the quick picture now and the full one once input stops.
        _settling = true;
        [_settleTimer invalidate];
        _settleTimer = [NSTimer scheduledTimerWithTimeInterval:kSettleSeconds
                                                        target:self
                                                      selector:@selector(settled:)
                                                      userInfo:nil
                                                       repeats:NO];
    }
    [self setNeedsDisplay:YES];
}

- (void)settled:(NSTimer*)timer {
    (void)timer;
    _settling = false;
    _dirty = true;
    [self setNeedsDisplay:YES];
}

- (void)renderIfNeeded:(int)side {
    const bool quick = _dragging || _settling;
    const int want = quick ? std::min(side, kPreviewSize) : std::min(side, limits::kMaxThumbnailSize);
    if (!_dirty && _imageSize == want && _showsPreview == quick) {
        return;
    }
    if (!_loaded || _splats.empty() || want < limits::kMinThumbnailSize) {
        return;
    }
    const CameraResult cam = camera_from_view(_view);
    if (cam.status != Status::Ok) {
        return;
    }
    RenderOptions options;
    options.size = want;
    options.background = Background::Gray;
    RenderResult r = render_splats(quick ? _preview : _splats, cam.camera, options,
                                   Deadline::after(quick ? std::chrono::milliseconds(kPreviewTime)
                                                         : std::chrono::milliseconds(kFullTime)));
    if (r.status != Status::Ok && r.status != Status::NothingToDraw && !quick) {
        // Too slow at full quality: keep the window usable with the quick picture.
        options.size = std::min(side, kPreviewSize);
        r = render_splats(_preview, cam.camera, options, Deadline::after(kPreviewTime));
    }
    if (r.status == Status::NothingToDraw) {
        // Looking away from everything: show the empty background.
        const auto n = static_cast<std::size_t>(options.size);
        r.image.size = options.size;
        r.image.rgba.assign(n * n * 4, kBackgroundLevel);
        for (std::size_t i = 3; i < r.image.rgba.size(); i += 4) {
            r.image.rgba[i] = 255;
        }
    } else if (r.status != Status::Ok) {
        return;
    }
    CGImageRef made = make_cgimage(r.image);
    if (made == nullptr) {
        return;
    }
    if (_image != nullptr) {
        CGImageRelease(_image);
    }
    _image = made;
    _imageSize = r.image.size;
    _showsPreview = quick;
    _dirty = false;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const CGFloat level = kBackgroundLevel / 255.0;
    [[NSColor colorWithSRGBRed:level green:level blue:level alpha:1] setFill];
    NSRectFill(self.bounds);
    const NSRect pic = [self pictureRect];
    const CGFloat scale = self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 1;
    [self renderIfNeeded:static_cast<int>(pic.size.width * scale)];
    if (_image == nullptr) {
        return;
    }
    NSImage* image = [[NSImage alloc] initWithCGImage:_image size:pic.size];
    [image drawInRect:pic
             fromRect:NSZeroRect
            operation:NSCompositingOperationSourceOver
             fraction:1
       respectFlipped:YES
                hints:@{NSImageHintInterpolation : @(NSImageInterpolationHigh)}];
    if (_showsPreview) {
        [self drawAxesIn:pic];
    }
}

// The file's X, Y and Z axes through the origin, while turning: X red, Y green, Z blue,
// negative halves dimmer, labels at the positive ends. Thumbnails never show them.
- (void)drawAxesIn:(NSRect)pic {
    const CameraResult cam = camera_from_view(_view);
    if (cam.status != Status::Ok) {
        return;
    }
    const Camera& c = cam.camera;
    const CGFloat side = pic.size.width;
    const float focal = static_cast<float>(side) * 0.5f / std::tan(c.fov_y * 0.5f);
    const float near_z = std::max(c.radius * 0.01f, 1e-6f);
    const float length = _autoView.radius;
    if (!std::isfinite(focal) || !(length > 0.0f)) {
        return;
    }
    const auto dot = [](const Vec3& a, const Vec3& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto to_camera = [&](const Vec3& p) {
        const Vec3 d{p[0] - c.eye[0], p[1] - c.eye[1], p[2] - c.eye[2]};
        return Vec3{dot(d, c.right), dot(d, c.up), dot(d, c.forward)};
    };
    const auto to_point = [&](const Vec3& q) {
        return NSMakePoint(pic.origin.x + side * 0.5 + static_cast<CGFloat>(focal * q[0] / q[2]),
                           pic.origin.y + side * 0.5 - static_cast<CGFloat>(focal * q[1] / q[2]));
    };
    struct Axis {
        Vec3 dir;
        NSColor* color;
        NSString* label;
    };
    const Axis axes[] = {{{1, 0, 0}, NSColor.systemRedColor, @"X"},
                         {{0, 1, 0}, NSColor.systemGreenColor, @"Y"},
                         {{0, 0, 1}, NSColor.systemBlueColor, @"Z"}};
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(pic);
    for (const Axis& axis : axes) {
        for (const float sign : {-1.0f, 1.0f}) {
            Vec3 a = to_camera({0, 0, 0});
            Vec3 b = to_camera({axis.dir[0] * length * sign, axis.dir[1] * length * sign,
                                axis.dir[2] * length * sign});
            if (a[2] < near_z && b[2] < near_z) {
                continue;
            }
            const auto cut = [&](Vec3& behind, const Vec3& front) {
                const float t = (near_z - behind[2]) / (front[2] - behind[2]);
                for (std::size_t i = 0; i < 3; ++i) {
                    behind[i] += (front[i] - behind[i]) * t;
                }
            };
            if (a[2] < near_z) {
                cut(a, b);
            } else if (b[2] < near_z) {
                cut(b, a);
            }
            NSBezierPath* path = [NSBezierPath bezierPath];
            [path moveToPoint:to_point(a)];
            [path lineToPoint:to_point(b)];
            path.lineWidth = 1.5;
            [[axis.color colorWithAlphaComponent:sign > 0 ? 1.0 : 0.45] setStroke];
            [path stroke];
            if (sign > 0 && b[2] >= near_z) {
                const NSPoint tip = to_point(b);
                [axis.label drawAtPoint:NSMakePoint(tip.x + 4, tip.y - 8)
                         withAttributes:@{
                             NSForegroundColorAttributeName : axis.color,
                             NSFontAttributeName : [NSFont boldSystemFontOfSize:12]
                         }];
            }
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

// --- input -------------------------------------------------------------------

- (void)startDrag:(NSEvent*)event panning:(bool)panning {
    if (!_loaded) {
        return;
    }
    _dragging = true;
    _panning = panning;
    _last = [self convertPoint:event.locationInWindow fromView:nil];
}

- (void)dragTo:(NSEvent*)event {
    if (!_dragging) {
        return;
    }
    const NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat side = std::max<CGFloat>(1, [self pictureRect].size.width);
    const float dx = static_cast<float>((pt.x - _last.x) / side);
    const float dy = static_cast<float>((pt.y - _last.y) / side);
    _last = pt;
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    if (_panning || (event.modifierFlags & NSEventModifierFlagShift) != 0) {
        // The picture follows the mouse: half the picture height is about one radius.
        [self setView:pan(_view, -dx * 2.2f, -dy * 2.2f) settle:false];
    } else {
        // The subject turns with the mouse, as if grabbed.
        [self setView:orbit(_view, -dx * kPi, dy * kPi) settle:false];
    }
}

- (void)endDrag {
    if (_dragging) {
        _dragging = false;
        _dirty = true;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDown:(NSEvent*)event {
    [self startDrag:event panning:false];
}
- (void)rightMouseDown:(NSEvent*)event {
    [self startDrag:event panning:true];
}
- (void)otherMouseDown:(NSEvent*)event {
    [self startDrag:event panning:true];
}
- (void)mouseDragged:(NSEvent*)event {
    [self dragTo:event];
}
- (void)rightMouseDragged:(NSEvent*)event {
    [self dragTo:event];
}
- (void)otherMouseDragged:(NSEvent*)event {
    [self dragTo:event];
}
- (void)mouseUp:(NSEvent*)event {
    (void)event;
    [self endDrag];
}
- (void)rightMouseUp:(NSEvent*)event {
    (void)event;
    [self endDrag];
}
- (void)otherMouseUp:(NSEvent*)event {
    (void)event;
    [self endDrag];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_loaded || event.scrollingDeltaY == 0) {
        return;
    }
    // A wheel notch zooms like on Windows; trackpad scrolling comes in small steps.
    const CGFloat notches =
        event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10.0 : event.scrollingDeltaY;
    [self setView:zoom(_view, std::pow(0.85f, static_cast<float>(notches))) settle:true];
}

- (void)magnifyWithEvent:(NSEvent*)event {
    if (!_loaded) {
        return;
    }
    const float factor = 1.0f / std::max(0.2f, 1.0f + static_cast<float>(event.magnification));
    [self setView:zoom(_view, factor) settle:true];
}

@end

// --- window --------------------------------------------------------------------

@interface STViewerWindow () <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) STPictureView* picture;
@property(strong) NSTextField* status;
@property(strong) NSArray<NSButton*>* buttons;
@property(strong) NSPopUpButton* qualityPopup;
@property(copy) NSURL* url;
@property(copy) void (^log)(NSString*);
@end

static NSMutableArray<STViewerWindow*>* gOpenViewers;

@implementation STViewerWindow {
    std::optional<Fingerprint> _fingerprint;
    std::optional<Quality> _quality; // this file's own count (none: the app setting), saved with the view
    Quality _loadedQuality;          // the count the splats in the picture were loaded with
    bool _accessing;
    bool _reloading;
}

// The popup's choices, in order: the app setting, then each count.
static const std::optional<Quality> kQualityChoices[] = {std::nullopt, Quality::Fast,
                                                         Quality::Standard, Quality::High};

static Quality SettingQuality(void) {
    switch (STIconSetter.qualitySetting) {
    case 0: return Quality::Fast;
    case 2: return Quality::High;
    default: return Quality::Standard;
    }
}

static NSString* QualityTitle(Quality q) {
    switch (q) {
    case Quality::Fast: return NSLocalizedString(@"Up to 50,000", nil);
    case Quality::High: return NSLocalizedString(@"Up to 400,000", nil);
    case Quality::Standard: break;
    }
    return NSLocalizedString(@"Up to 200,000", nil);
}

+ (void)openFile:(NSURL*)url log:(void (^)(NSString*))log {
    for (STViewerWindow* v in gOpenViewers) {
        if ([v.url.path isEqualToString:url.path]) {
            [v.window makeKeyAndOrderFront:nil];
            return;
        }
    }
    if (gOpenViewers == nil) {
        gOpenViewers = [NSMutableArray array];
    }
    STViewerWindow* viewer = [[STViewerWindow alloc] init];
    viewer.url = url;
    viewer.log = log;
    [gOpenViewers addObject:viewer];
    [viewer show];
}

- (NSButton*)button:(NSString*)title action:(SEL)action {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    b.enabled = NO;
    return b;
}

- (void)show {
    _accessing = [self.url startAccessingSecurityScopedResource];
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 640, 720)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title =
        [NSString stringWithFormat:NSLocalizedString(@"SplatThumb - Thumbnail view - %@", nil), self.url.lastPathComponent];
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    self.window.contentMinSize = NSMakeSize(420, 420);
    NSView* content = self.window.contentView;

    self.picture = [[STPictureView alloc] init];
    self.picture.translatesAutoresizingMaskIntoConstraints = NO;
    self.status = [NSTextField labelWithString:NSLocalizedString(@"Loading...", nil)];
    self.status.translatesAutoresizingMaskIntoConstraints = NO;
    self.status.lineBreakMode = NSLineBreakByTruncatingTail;
    NSButton* reset = [self button:NSLocalizedString(@"Reset", nil) action:@selector(reset:)];
    NSButton* save = [self button:NSLocalizedString(@"Use this view", nil) action:@selector(save:)];
    save.keyEquivalent = @"\r";
    NSButton* close = [NSButton buttonWithTitle:NSLocalizedString(@"Close", nil) target:self action:@selector(close:)];
    close.translatesAutoresizingMaskIntoConstraints = NO;
    close.keyEquivalent = @"\e";
    self.buttons = @[ reset, save ];
    // Splats for this file's icon, as on Windows: the app setting or a count of its own.
    self.qualityPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.qualityPopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (const auto& choice : kQualityChoices) {
        [self.qualityPopup addItemWithTitle:
            choice ? QualityTitle(*choice)
                   : [NSString stringWithFormat:NSLocalizedString(@"Same as the app setting (%@)", nil),
                                                QualityTitle(SettingQuality())]];
    }
    self.qualityPopup.target = self;
    self.qualityPopup.action = @selector(qualityChanged:);
    self.qualityPopup.enabled = NO;

    for (NSView* v in @[ self.picture, self.status, reset, self.qualityPopup, save, close ]) {
        [content addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [self.picture.topAnchor constraintEqualToAnchor:content.topAnchor],
        [self.picture.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [self.picture.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [self.status.topAnchor constraintEqualToAnchor:self.picture.bottomAnchor constant:8],
        [self.status.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
        [self.status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
        [reset.topAnchor constraintEqualToAnchor:self.status.bottomAnchor constant:8],
        [reset.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
        [reset.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12],
        [self.qualityPopup.centerYAnchor constraintEqualToAnchor:reset.centerYAnchor],
        [self.qualityPopup.leadingAnchor constraintEqualToAnchor:reset.trailingAnchor constant:8],
        [self.qualityPopup.trailingAnchor constraintLessThanOrEqualToAnchor:save.leadingAnchor constant:-8],
        [close.centerYAnchor constraintEqualToAnchor:reset.centerYAnchor],
        [close.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
        [save.centerYAnchor constraintEqualToAnchor:reset.centerYAnchor],
        [save.trailingAnchor constraintEqualToAnchor:close.leadingAnchor constant:-8],
    ]];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self load];
}

- (void)load {
    NSString* path = self.url.path;
    NSString* root = STIconSetter.viewsRoot;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Classify and fingerprint first: the saved view decides the quality.
        std::optional<Fingerprint> fp;
        bool splat = false;
        {
            FileByteSource source;
            if (source.open(path.fileSystemRepresentation)) {
                splat = inspect(source).kind != SplatKind::NotSplat;
                fp = fingerprint(source);
            }
        }
        std::optional<View> saved;
        if (splat && fp) {
            saved = load_view(root.fileSystemRepresentation, *fp);
        }
        const std::optional<Quality> quality = saved ? saved->quality : std::nullopt;
        const Quality effective = quality.value_or(SettingQuality());
        std::optional<Loaded> loaded;
        if (splat) {
            loaded = load_file(path.fileSystemRepresentation, effective);
        }
        // Shared so the block can carry the (large) splat vectors without copying them.
        auto shared = std::make_shared<std::optional<Loaded>>(std::move(loaded));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!splat) {
                self.status.stringValue = NSLocalizedString(@"This file is not a 3D Gaussian Splatting PLY, so SplatThumb does not draw it.", nil);
                return;
            }
            if (!*shared) {
                self.status.stringValue = NSLocalizedString(@"The file could not be read, or it has nothing to draw.", nil);
                return;
            }
            self->_fingerprint = fp;
            self->_quality = quality;
            self->_loadedQuality = effective;
            [self selectQuality];
            self.qualityPopup.enabled = fp.has_value();
            Loaded& l = **shared;
            STPictureView* p = self.picture;
            p->_splats = std::move(l.splats);
            p->_preview = std::move(l.preview);
            p->_autoView = l.auto_view;
            p->_loaded = true;
            View start = saved ? *saved : l.auto_view;
            start.quality.reset();
            [p setView:start settle:false];
            for (NSButton* b in self.buttons) {
                b.enabled = fp.has_value();
            }
            self.status.stringValue =
                saved ? NSLocalizedString(@"This is the saved view. Drag to turn, right-drag or Shift-drag to move, scroll or pinch to zoom.", nil)
                      : NSLocalizedString(@"Drag to turn, right-drag or Shift-drag to move, scroll or pinch to zoom.", nil);
        });
    });
}

- (void)selectQuality {
    for (NSInteger i = 0; i < static_cast<NSInteger>(std::size(kQualityChoices)); ++i) {
        if (kQualityChoices[i] == _quality) {
            [self.qualityPopup selectItemAtIndex:i];
        }
    }
}

// Loads the splats again when the count to draw changed, keeping the current view.
- (void)reloadIfNeeded:(NSString*)message {
    const Quality want = _quality.value_or(SettingQuality());
    if (want == _loadedQuality || _reloading) {
        self.status.stringValue = message;
        return;
    }
    _reloading = true;
    self.qualityPopup.enabled = NO;
    for (NSButton* b in self.buttons) {
        b.enabled = NO;
    }
    self.status.stringValue = NSLocalizedString(@"Loading...", nil);
    NSString* path = self.url.path;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto shared = std::make_shared<std::optional<Loaded>>(
            load_file(path.fileSystemRepresentation, want));
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_reloading = false;
            self.qualityPopup.enabled = YES;
            for (NSButton* b in self.buttons) {
                b.enabled = YES;
            }
            if (!*shared) {
                self.status.stringValue = NSLocalizedString(@"The file could not be read, or it has nothing to draw.", nil);
                return;
            }
            Loaded& l = **shared;
            STPictureView* p = self.picture;
            p->_splats = std::move(l.splats);
            p->_preview = std::move(l.preview);
            p->_autoView = l.auto_view;
            self->_loadedQuality = want;
            [p setView:p->_view settle:false];
            // The choice may have changed again while loading.
            [self reloadIfNeeded:message];
        });
    });
}

- (void)qualityChanged:(id)sender {
    (void)sender;
    const NSInteger i = self.qualityPopup.indexOfSelectedItem;
    if (i < 0 || i >= static_cast<NSInteger>(std::size(kQualityChoices))) {
        return;
    }
    _quality = kQualityChoices[i];
    [self reloadIfNeeded:NSLocalizedString(@"Splat count changed. Click “Use this view” to use it for the icon.", nil)];
}

// Remakes the Finder icon on a background queue, then reports and runs done on main.
- (void)remakeIcon:(NSString*)message done:(void (^_Nullable)(void))done {
    self.qualityPopup.enabled = NO;
    for (NSButton* b in self.buttons) {
        b.enabled = NO;
    }
    self.status.stringValue = NSLocalizedString(@"Remaking the icon...", nil);
    NSString* path = self.url.path;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString* result = [STIconSetter setIconForFile:path];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.log(result);
            const BOOL ok = [result hasPrefix:@"icon"] || [result hasPrefix:@"same"];
            self.status.stringValue =
                ok ? message
                   : [NSString stringWithFormat:NSLocalizedString(@"%@ But the icon could not be set: %@", nil), message,
                                                result];
            self.qualityPopup.enabled = YES;
            for (NSButton* b in self.buttons) {
                b.enabled = YES;
            }
            if (ok && done != nil) {
                done();
            }
        });
    });
}

- (void)save:(id)sender {
    (void)sender;
    View saved = self.picture->_view;
    saved.quality = _quality;
    if (!_fingerprint ||
        !save_view(STIconSetter.viewsRoot.fileSystemRepresentation, *_fingerprint, saved)) {
        self.status.stringValue = NSLocalizedString(@"The view could not be saved.", nil);
        return;
    }
    self.log([NSString stringWithFormat:NSLocalizedString(@"View saved: %@", nil), self.url.lastPathComponent]);
    [self remakeIcon:NSLocalizedString(@"View saved. The icon has been remade.", nil)
                done:^{
                    [self.window close];
                }];
}

- (void)reset:(id)sender {
    (void)sender;
    if (_fingerprint) {
        remove_view(STIconSetter.viewsRoot.fileSystemRepresentation, *_fingerprint);
    }
    _quality.reset();
    [self selectQuality];
    [self.picture setView:self.picture->_autoView settle:false];
    self.log([NSString stringWithFormat:NSLocalizedString(@"Back to the automatic view: %@", nil), self.url.lastPathComponent]);
    [self remakeIcon:NSLocalizedString(@"Back to the automatic view. The icon has been remade.", nil) done:nil];
}

- (void)close:(id)sender {
    (void)sender;
    [self.window close];
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (_accessing) {
        [self.url stopAccessingSecurityScopedResource];
        _accessing = false;
    }
    // Keep self alive until this method returns.
    STViewerWindow* me = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        [gOpenViewers removeObject:me];
    });
}

@end
