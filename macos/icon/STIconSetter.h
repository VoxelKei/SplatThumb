// Option B spike: custom Finder icons for 3DGS PLY files, shared by the app and the CLI.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface STIconSetter : NSObject

// Folder of saved views (same .view format and layout as Windows), inside the app's
// sandbox container: Library/Application Support/SplatThumb/views.
@property(class, readonly) NSString* viewsRoot;

// The .ply files at path: the file itself, or every .ply inside a folder (recursively).
+ (NSArray<NSString*>*)plyFilesAt:(NSString*)path;

// Renders a 3DGS PLY and sets the picture as its Finder icon, from the file's saved view
// when there is one. Unchanged files (same fingerprint and view as when the icon was set)
// are skipped; non-3DGS files are left alone.
// The PLY data and its modification date are kept. Returns a one-line report.
+ (NSString*)setIconForFile:(NSString*)path;

// Removes an icon this tool set (files without our marker are left alone), leaving the
// file's attributes and modification date as they were before. Returns a one-line report.
+ (NSString*)removeIconForFile:(NSString*)path;

@end

// Watches a folder and calls the handler for each .ply that was added or changed, once it
// has stopped changing for `settle` seconds (so files still being copied are not read).
@interface STFolderWatcher : NSObject

- (instancetype)initWithFolder:(NSString*)folder
                        settle:(NSTimeInterval)settle
                       handler:(void (^)(NSString* path))handler;
- (void)start;
- (void)stop;

@end

NS_ASSUME_NONNULL_END
