// Viewer for choosing a file's thumbnail angle, like the Windows viewer (app/viewer_window.cpp).
// Drag turns, right-drag or Shift-drag moves, scroll or pinch zooms. The view is saved with
// the shared core (view_store), so the same .view files work on Windows and macOS, and the
// file's Finder icon is remade from it.
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface STViewerWindow : NSObject

// Opens a viewer for a .ply. log receives one-line reports (icon results and errors).
+ (void)openFile:(NSURL*)url log:(void (^)(NSString* line))log;

@end

NS_ASSUME_NONNULL_END
