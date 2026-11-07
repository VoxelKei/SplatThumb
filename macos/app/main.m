// Host app. Carries the Quick Look extension (and declares the .spgm type), and for the
// option B spike lets the user put SplatThumb pictures on 3DGS PLY files as Finder icons.
#import <Cocoa/Cocoa.h>

#import "STIconSetter.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* folderLabel;
@property(strong) NSTextView* logView;
@property(strong) NSButton* removeButton;
@property(strong) NSButton* watchCheckbox;
@property(copy) NSURL* folder;
@property(strong) STFolderWatcher* watcher;
@property(strong) dispatch_queue_t work; // icons are made one at a time, off the main thread
@end

@implementation AppDelegate

- (NSButton*)button:(NSString*)title action:(SEL)action {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    self.work = dispatch_queue_create("com.voxelkei.splatthumb.icons", DISPATCH_QUEUE_SERIAL);

    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 640, 420)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"SplatThumb";
    NSView* content = self.window.contentView;

    NSButton* choose = [self button:@"フォルダを選んでアイコンを付ける…" action:@selector(chooseFolder:)];
    choose.keyEquivalent = @"\r";
    self.removeButton = [self button:@"このフォルダのアイコンを外す" action:@selector(removeIcons:)];
    self.watchCheckbox = [NSButton checkboxWithTitle:@"このフォルダを見張る（新しい・変わった .ply に自動で付ける）"
                                              target:self
                                              action:@selector(toggleWatch:)];
    self.watchCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.removeButton.enabled = NO;
    self.watchCheckbox.enabled = NO;

    self.folderLabel = [NSTextField labelWithString:@"フォルダが選ばれていません"];
    self.folderLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.folderLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;

    NSScrollView* scroll = [NSTextView scrollableTextView];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.logView = scroll.documentView;
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];

    for (NSView* v in @[ choose, self.removeButton, self.watchCheckbox, self.folderLabel, scroll ]) {
        [content addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [choose.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
        [choose.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [self.removeButton.centerYAnchor constraintEqualToAnchor:choose.centerYAnchor],
        [self.removeButton.leadingAnchor constraintEqualToAnchor:choose.trailingAnchor constant:8],
        [self.folderLabel.topAnchor constraintEqualToAnchor:choose.bottomAnchor constant:10],
        [self.folderLabel.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [self.folderLabel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [self.watchCheckbox.topAnchor constraintEqualToAnchor:self.folderLabel.bottomAnchor constant:8],
        [self.watchCheckbox.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [scroll.topAnchor constraintEqualToAnchor:self.watchCheckbox.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    ]];

    [self log:@"3DGS の .ply に、SplatThumb の絵を Finder のアイコンとして付けます。"];
    [self log:@"点群データは変わりません（アイコンはファイルのおまけ情報に入ります）。"];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)log:(NSString*)text {
    void (^append)(void) = ^{
        NSDictionary* attrs = @{
            NSFontAttributeName : self.logView.font,
            NSForegroundColorAttributeName : NSColor.textColor
        };
        [self.logView.textStorage
            appendAttributedString:[[NSAttributedString alloc]
                                       initWithString:[text stringByAppendingString:@"\n"]
                                           attributes:attrs]];
        [self.logView scrollToEndOfDocument:nil];
    };
    if (NSThread.isMainThread) {
        append();
    } else {
        dispatch_async(dispatch_get_main_queue(), append);
    }
}

// Runs one icon operation per .ply in the folder on the work queue, then a summary line.
- (void)runOnFolder:(NSString*)verb operation:(NSString* (^)(NSString*))operation {
    NSString* folder = self.folder.path;
    [self log:[NSString stringWithFormat:@"— %@: %@", verb, folder]];
    dispatch_async(self.work, ^{
        NSDate* start = [NSDate date];
        NSArray<NSString*>* files = [STIconSetter plyFilesAt:folder];
        for (NSString* f in files) {
            [self log:operation(f)];
        }
        [self log:[NSString stringWithFormat:@"— %lu 個の .ply、%.1f 秒", (unsigned long)files.count,
                                             -start.timeIntervalSinceNow]];
    });
}

- (void)chooseFolder:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"アイコンを付ける";
    panel.message = @"このフォルダ（とその中のフォルダ）の 3DGS .ply にアイコンを付けます";
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    [self.watcher stop];
    self.watcher = nil;
    self.watchCheckbox.state = NSControlStateValueOff;
    self.folder = panel.URL;
    self.folderLabel.stringValue = self.folder.path;
    self.removeButton.enabled = YES;
    self.watchCheckbox.enabled = YES;
    [self runOnFolder:@"アイコンを付ける" operation:^NSString*(NSString* f) {
        return [STIconSetter setIconForFile:f];
    }];
}

- (void)removeIcons:(id)sender {
    (void)sender;
    if (self.folder == nil) {
        return;
    }
    [self runOnFolder:@"アイコンを外す" operation:^NSString*(NSString* f) {
        return [STIconSetter removeIconForFile:f];
    }];
}

- (void)toggleWatch:(id)sender {
    (void)sender;
    [self.watcher stop];
    self.watcher = nil;
    if (self.watchCheckbox.state != NSControlStateValueOn || self.folder == nil) {
        [self log:@"— 見張りを止めました"];
        return;
    }
    __weak AppDelegate* weakSelf = self;
    self.watcher = [[STFolderWatcher alloc]
        initWithFolder:self.folder.path
                settle:3.0
               handler:^(NSString* path) {
                   AppDelegate* me = weakSelf;
                   if (me == nil) {
                       return;
                   }
                   dispatch_async(me.work, ^{
                       [me log:[STIconSetter setIconForFile:path]];
                   });
               }];
    [self.watcher start];
    [self log:@"— 見張りを始めました（アプリを開いている間だけ）"];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
