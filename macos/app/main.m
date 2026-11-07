// Host app. Carries the Quick Look extension (and declares the .spgm type), and for the
// option B spike lets the user put SplatThumb pictures on 3DGS PLY files as Finder icons.
//
// Watching runs either only while the app is open, or "resident": the app is a login item,
// keeps running in the menu bar after its window is closed, and resumes watching the
// remembered folder (a security-scoped bookmark) after a restart.
#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>

#import "STIconSetter.h"

static NSString* const kBookmarkKey = @"FolderBookmark";
static NSString* const kWatchingKey = @"Watching";
static NSString* const kResidentKey = @"Resident";

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* folderLabel;
@property(strong) NSTextView* logView;
@property(strong) NSButton* removeButton;
@property(strong) NSButton* watchCheckbox;
@property(strong) NSButton* whileOpenRadio;
@property(strong) NSButton* residentRadio;
@property(strong) NSStatusItem* statusItem;
@property(copy) NSURL* folder; // security-scoped while set
@property(strong) STFolderWatcher* watcher;
@property(strong) dispatch_queue_t work; // icons are made one at a time, off the main thread
@end

@implementation AppDelegate

- (NSButton*)button:(NSString*)title action:(SEL)action {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

- (NSButton*)radio:(NSString*)title {
    NSButton* b = [NSButton radioButtonWithTitle:title target:self action:@selector(modeChanged:)];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

- (BOOL)resident {
    return [NSUserDefaults.standardUserDefaults boolForKey:kResidentKey];
}

- (BOOL)launchedAsLoginItem {
    NSAppleEventDescriptor* event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
    return event.eventID == kAEOpenApplication &&
           [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue ==
               keyAELaunchedAsLogInItem;
}

- (void)buildWindow {
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 660, 460)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"SplatThumb";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    NSView* content = self.window.contentView;

    NSButton* choose = [self button:@"フォルダを選んでアイコンを付ける…" action:@selector(chooseFolder:)];
    choose.keyEquivalent = @"\r";
    self.removeButton = [self button:@"このフォルダのアイコンを外す" action:@selector(removeIcons:)];
    self.watchCheckbox = [NSButton checkboxWithTitle:@"このフォルダを見張る（新しい・変わった .ply に自動で付ける）"
                                              target:self
                                              action:@selector(toggleWatch:)];
    self.watchCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.whileOpenRadio = [self radio:@"アプリを開いている間だけ見張る"];
    self.residentRadio =
        [self radio:@"常駐して見張る（ログイン時に自動で起動、ウィンドウを閉じてもメニューバーで続ける）"];
    (self.resident ? self.residentRadio : self.whileOpenRadio).state = NSControlStateValueOn;

    self.folderLabel = [NSTextField labelWithString:@"フォルダが選ばれていません"];
    self.folderLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.folderLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;

    NSScrollView* scroll = [NSTextView scrollableTextView];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.logView = scroll.documentView;
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];

    for (NSView* v in @[
             choose, self.removeButton, self.folderLabel, self.watchCheckbox, self.whileOpenRadio,
             self.residentRadio, scroll
         ]) {
        [content addSubview:v];
    }
    NSLayoutXAxisAnchor* left = content.leadingAnchor;
    [NSLayoutConstraint activateConstraints:@[
        [choose.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
        [choose.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.removeButton.centerYAnchor constraintEqualToAnchor:choose.centerYAnchor],
        [self.removeButton.leadingAnchor constraintEqualToAnchor:choose.trailingAnchor constant:8],
        [self.folderLabel.topAnchor constraintEqualToAnchor:choose.bottomAnchor constant:10],
        [self.folderLabel.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.folderLabel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [self.watchCheckbox.topAnchor constraintEqualToAnchor:self.folderLabel.bottomAnchor constant:8],
        [self.watchCheckbox.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.whileOpenRadio.topAnchor constraintEqualToAnchor:self.watchCheckbox.bottomAnchor constant:6],
        [self.whileOpenRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [self.residentRadio.topAnchor constraintEqualToAnchor:self.whileOpenRadio.bottomAnchor constant:4],
        [self.residentRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [scroll.topAnchor constraintEqualToAnchor:self.residentRadio.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:left constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    ]];
    [self.window center];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    self.work = dispatch_queue_create("com.voxelkei.splatthumb.icons", DISPATCH_QUEUE_SERIAL);
    [self buildWindow];
    [self log:@"3DGS の .ply に、SplatThumb の絵を Finder のアイコンとして付けます。"];
    [self log:@"点群データは変わりません（アイコンはファイルのおまけ情報に入ります）。"];

    [self restoreFolder];
    [self updateControls];
    [self updateStatusItem];

    const BOOL quiet = self.resident && [self launchedAsLoginItem];
    if (self.folder != nil && [NSUserDefaults.standardUserDefaults boolForKey:kWatchingKey]) {
        // Catch up on what changed while the app was not running, then keep watching.
        [self runOnFolder:@"前回の続き：アイコンを確認" operation:^NSString*(NSString* f) {
            return [STIconSetter setIconForFile:f];
        }];
        [self startWatching];
    }
    if (quiet) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    } else {
        [self showWindow:nil];
    }
}

- (void)showWindow:(id)sender {
    (void)sender;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)visible {
    (void)sender;
    if (!visible) {
        [self showWindow:nil];
    }
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (self.resident) {
        // Keep watching from the menu bar, without a Dock icon.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return !self.resident;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self.watcher stop];
    [self.folder stopAccessingSecurityScopedResource];
}

// --- log -----------------------------------------------------------------------

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

// --- folder ------------------------------------------------------------------

- (void)setFolderURL:(NSURL*)url {
    [self.folder stopAccessingSecurityScopedResource];
    self.folder = url;
    [url startAccessingSecurityScopedResource];
    self.folderLabel.stringValue = url != nil ? url.path : @"フォルダが選ばれていません";
}

- (void)restoreFolder {
    NSData* bookmark = [NSUserDefaults.standardUserDefaults dataForKey:kBookmarkKey];
    if (bookmark == nil) {
        return;
    }
    BOOL stale = NO;
    NSError* error = nil;
    NSURL* url = [NSURL URLByResolvingBookmarkData:bookmark
                                           options:NSURLBookmarkResolutionWithSecurityScope
                                     relativeToURL:nil
                               bookmarkDataIsStale:&stale
                                             error:&error];
    if (url == nil) {
        [self log:[NSString stringWithFormat:@"前回のフォルダが見つかりません：%@",
                                             error.localizedDescription]];
        [NSUserDefaults.standardUserDefaults removeObjectForKey:kBookmarkKey];
        return;
    }
    [self setFolderURL:url];
    if (stale) {
        [self rememberFolder];
    }
}

- (void)rememberFolder {
    NSError* error = nil;
    NSData* bookmark = [self.folder bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                             includingResourceValuesForKeys:nil
                                              relativeToURL:nil
                                                      error:&error];
    if (bookmark != nil) {
        [NSUserDefaults.standardUserDefaults setObject:bookmark forKey:kBookmarkKey];
    } else {
        [self log:[NSString stringWithFormat:@"フォルダを覚えられませんでした：%@",
                                             error.localizedDescription]];
    }
}

- (void)updateControls {
    const BOOL has = self.folder != nil;
    self.removeButton.enabled = has;
    self.watchCheckbox.enabled = has;
    self.watchCheckbox.state =
        self.watcher != nil ? NSControlStateValueOn : NSControlStateValueOff;
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
    [self stopWatching];
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:kWatchingKey];
    [self setFolderURL:panel.URL];
    [self rememberFolder];
    [self updateControls];
    [self runOnFolder:@"アイコンを付ける" operation:^NSString*(NSString* f) {
        return [STIconSetter setIconForFile:f];
    }];
}

- (void)removeIcons:(id)sender {
    (void)sender;
    if (self.folder == nil) {
        return;
    }
    // Removing an icon is itself a change in the folder; a running watcher would put the
    // icons straight back, so removing always stops watching.
    if (self.watcher != nil) {
        [self stopWatching];
        [NSUserDefaults.standardUserDefaults setBool:NO forKey:kWatchingKey];
        [self updateControls];
        [self log:@"— 見張りを止めました（アイコンを外すため）"];
    }
    [self runOnFolder:@"アイコンを外す" operation:^NSString*(NSString* f) {
        return [STIconSetter removeIconForFile:f];
    }];
}

// --- watching ----------------------------------------------------------------

- (void)startWatching {
    [self stopWatching];
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
    [self log:self.resident ? @"— 見張りを始めました（常駐）"
                            : @"— 見張りを始めました（アプリを開いている間だけ）"];
    [self updateControls];
    [self updateStatusItem];
}

- (void)stopWatching {
    if (self.watcher == nil) {
        return;
    }
    [self.watcher stop];
    self.watcher = nil;
    [self updateStatusItem];
}

- (void)toggleWatch:(id)sender {
    (void)sender;
    const BOOL on = self.watchCheckbox.state == NSControlStateValueOn && self.folder != nil;
    [NSUserDefaults.standardUserDefaults setBool:on forKey:kWatchingKey];
    if (on) {
        [self startWatching];
    } else {
        [self stopWatching];
        [self updateControls];
        [self log:@"— 見張りを止めました"];
    }
}

// --- resident mode -----------------------------------------------------------

- (void)modeChanged:(id)sender {
    const BOOL resident = sender == self.residentRadio;
    self.residentRadio.state = resident ? NSControlStateValueOn : NSControlStateValueOff;
    self.whileOpenRadio.state = resident ? NSControlStateValueOff : NSControlStateValueOn;
    if (resident == self.resident) {
        return;
    }
    NSError* error = nil;
    SMAppService* service = SMAppService.mainAppService;
    const BOOL ok = resident ? [service registerAndReturnError:&error]
                             : [service unregisterAndReturnError:&error];
    if (!ok && resident) {
        [self log:[NSString stringWithFormat:@"ログイン項目に登録できませんでした：%@",
                                             error.localizedDescription]];
        self.residentRadio.state = NSControlStateValueOff;
        self.whileOpenRadio.state = NSControlStateValueOn;
        return;
    }
    [NSUserDefaults.standardUserDefaults setBool:resident forKey:kResidentKey];
    [self log:resident ? @"— 常駐にしました：ログイン時に起動し、ウィンドウを閉じてもメニューバーで見張りを続けます"
                       : @"— アプリを開いている間だけにしました（ログイン項目から外しました）"];
    if (resident && service.status == SMAppServiceStatusRequiresApproval) {
        [self log:@"  システム設定 → 一般 → ログイン項目 で SplatThumb を許可してください"];
    }
    [self updateStatusItem];
}

- (void)updateStatusItem {
    if (!self.resident) {
        if (self.statusItem != nil) {
            [NSStatusBar.systemStatusBar removeStatusItem:self.statusItem];
            self.statusItem = nil;
        }
        return;
    }
    if (self.statusItem == nil) {
        self.statusItem =
            [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
        self.statusItem.button.image = [NSImage imageWithSystemSymbolName:@"cube.transparent"
                                                 accessibilityDescription:@"SplatThumb"];
    }
    NSMenu* menu = [[NSMenu alloc] init];
    NSString* state = self.watcher != nil
                          ? [NSString stringWithFormat:@"見張り中：%@", self.folder.lastPathComponent]
                          : @"見張っていません";
    [menu addItemWithTitle:state action:nil keyEquivalent:@""].enabled = NO;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"SplatThumb を開く" action:@selector(showWindow:) keyEquivalent:@""]
        .target = self;
    [menu addItemWithTitle:@"終了" action:@selector(terminate:) keyEquivalent:@"q"];
    self.statusItem.menu = menu;
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
