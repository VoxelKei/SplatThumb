// Host app. Carries the Quick Look extension (and declares the .spgm type), and for the
// option B spike puts SplatThumb pictures on 3DGS PLY files as custom Finder icons.
//
// The user registers folders. Icons are set when a folder is added, and while watching is
// on, new or changed .ply files in the registered folders get icons too. Watching runs
// either only while the app is open, or "resident": the app is a login item, keeps
// running in the menu bar after its window is closed, and resumes after a restart. Folders
// are remembered as security-scoped bookmarks.
#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>

#import "STIconSetter.h"

static NSString* const kBookmarksKey = @"FolderBookmarks";
static NSString* const kOldBookmarkKey = @"FolderBookmark"; // single folder, earlier spike
static NSString* const kWatchingKey = @"Watching";
static NSString* const kResidentKey = @"Resident";

@interface AppDelegate
    : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTableView* table;
@property(strong) NSTextView* logView;
@property(strong) NSButton* removeFolderButton;
@property(strong) NSButton* removeAllButton;
@property(strong) NSButton* watchCheckbox;
@property(strong) NSButton* whileOpenRadio;
@property(strong) NSButton* residentRadio;
@property(strong) NSStatusItem* statusItem;
@property(strong) NSMutableArray<NSURL*>* folders;            // security-scoped while listed
@property(strong) NSMutableArray<STFolderWatcher*>* watchers; // empty when not watching
@property(strong) dispatch_queue_t work; // icons are made one at a time, off the main thread
@end

@implementation AppDelegate

// --- small helpers -------------------------------------------------------------

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

- (NSUserDefaults*)defaults {
    return NSUserDefaults.standardUserDefaults;
}

- (BOOL)resident {
    return [self.defaults boolForKey:kResidentKey];
}

- (BOOL)watching {
    return self.watchers.count > 0;
}

- (BOOL)launchedAsLoginItem {
    NSAppleEventDescriptor* event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
    return event.eventID == kAEOpenApplication &&
           [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue ==
               keyAELaunchedAsLogInItem;
}

// --- window ------------------------------------------------------------------

- (void)buildWindow {
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 700, 560)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"SplatThumb";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    NSView* content = self.window.contentView;

    NSTextField* heading = [NSTextField labelWithString:@"アイコンを付けるフォルダ"];
    heading.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    heading.translatesAutoresizingMaskIntoConstraints = NO;

    self.table = [[NSTableView alloc] init];
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"path"];
    column.title = @"フォルダ";
    column.resizingMask = NSTableColumnAutoresizingMask;
    [self.table addTableColumn:column];
    self.table.headerView = nil;
    self.table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    self.table.dataSource = self;
    self.table.delegate = self;
    self.table.allowsMultipleSelection = YES;
    NSScrollView* tableScroll = [[NSScrollView alloc] init];
    tableScroll.documentView = self.table;
    tableScroll.hasVerticalScroller = YES;
    tableScroll.borderType = NSBezelBorder;
    tableScroll.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton* add = [self button:@"フォルダを追加…" action:@selector(addFolders:)];
    self.removeFolderButton = [self button:@"選んだフォルダを一覧から外す"
                                    action:@selector(removeSelectedFolders:)];
    self.removeAllButton = [self button:@"登録したフォルダのアイコンをすべて外す"
                                 action:@selector(removeAllIcons:)];

    self.watchCheckbox =
        [NSButton checkboxWithTitle:@"登録したフォルダを見張る（新しい・変わった .ply に自動で付ける）"
                             target:self
                             action:@selector(toggleWatch:)];
    self.watchCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.whileOpenRadio = [self radio:@"アプリを開いている間だけ"];
    self.residentRadio = [self radio:@"常駐（ログイン時に起動し、ウィンドウを閉じてもメニューバーで続ける）"];
    (self.resident ? self.residentRadio : self.whileOpenRadio).state = NSControlStateValueOn;

    NSScrollView* logScroll = [NSTextView scrollableTextView];
    logScroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.logView = logScroll.documentView;
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];

    for (NSView* v in @[
             heading, tableScroll, add, self.removeFolderButton, self.removeAllButton,
             self.watchCheckbox, self.whileOpenRadio, self.residentRadio, logScroll
         ]) {
        [content addSubview:v];
    }
    NSLayoutXAxisAnchor* left = content.leadingAnchor;
    NSLayoutXAxisAnchor* right = content.trailingAnchor;
    [NSLayoutConstraint activateConstraints:@[
        [heading.topAnchor constraintEqualToAnchor:content.topAnchor constant:14],
        [heading.leadingAnchor constraintEqualToAnchor:left constant:16],
        [tableScroll.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:6],
        [tableScroll.leadingAnchor constraintEqualToAnchor:left constant:16],
        [tableScroll.trailingAnchor constraintEqualToAnchor:right constant:-16],
        [tableScroll.heightAnchor constraintEqualToConstant:110],
        [add.topAnchor constraintEqualToAnchor:tableScroll.bottomAnchor constant:8],
        [add.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.removeFolderButton.centerYAnchor constraintEqualToAnchor:add.centerYAnchor],
        [self.removeFolderButton.leadingAnchor constraintEqualToAnchor:add.trailingAnchor constant:8],
        [self.removeAllButton.centerYAnchor constraintEqualToAnchor:add.centerYAnchor],
        [self.removeAllButton.trailingAnchor constraintEqualToAnchor:right constant:-16],
        [self.watchCheckbox.topAnchor constraintEqualToAnchor:add.bottomAnchor constant:14],
        [self.watchCheckbox.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.whileOpenRadio.topAnchor constraintEqualToAnchor:self.watchCheckbox.bottomAnchor constant:6],
        [self.whileOpenRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [self.residentRadio.topAnchor constraintEqualToAnchor:self.whileOpenRadio.bottomAnchor constant:4],
        [self.residentRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [logScroll.topAnchor constraintEqualToAnchor:self.residentRadio.bottomAnchor constant:12],
        [logScroll.leadingAnchor constraintEqualToAnchor:left constant:16],
        [logScroll.trailingAnchor constraintEqualToAnchor:right constant:-16],
        [logScroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    ]];
    [self.window center];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return (NSInteger)self.folders.count;
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)tableColumn
                   row:(NSInteger)row {
    (void)tableColumn;
    NSTextField* cell = [tableView makeViewWithIdentifier:@"cell" owner:self];
    if (cell == nil) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = @"cell";
        cell.lineBreakMode = NSLineBreakByTruncatingMiddle;
    }
    cell.stringValue = self.folders[(NSUInteger)row].path;
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    (void)notification;
    self.removeFolderButton.enabled = self.table.selectedRowIndexes.count > 0;
}

- (void)updateControls {
    const BOOL any = self.folders.count > 0;
    [self.table reloadData];
    self.removeFolderButton.enabled = self.table.selectedRowIndexes.count > 0;
    self.removeAllButton.enabled = any;
    self.watchCheckbox.enabled = any;
    self.watchCheckbox.state = self.watching ? NSControlStateValueOn : NSControlStateValueOff;
    [self updateStatusItem];
}

// --- life cycle --------------------------------------------------------------

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    self.work = dispatch_queue_create("com.voxelkei.splatthumb.icons", DISPATCH_QUEUE_SERIAL);
    self.folders = [NSMutableArray array];
    self.watchers = [NSMutableArray array];
    [self buildWindow];
    [self log:@"3DGS の .ply に、SplatThumb の絵を Finder のアイコンとして付けます。"];
    [self log:@"点群データは変わりません（アイコンはファイルのおまけ情報に入ります）。"];

    [self restoreFolders];
    // The login item may have been removed in System Settings; follow what macOS says.
    if (self.resident && SMAppService.mainAppService.status == SMAppServiceStatusNotRegistered) {
        [self.defaults setBool:NO forKey:kResidentKey];
        self.whileOpenRadio.state = NSControlStateValueOn;
        self.residentRadio.state = NSControlStateValueOff;
        [self log:@"ログイン項目から外されていたので「アプリを開いている間だけ」に戻しました"];
    }
    const BOOL quiet = self.resident && [self launchedAsLoginItem];
    if (self.folders.count > 0 && [self.defaults boolForKey:kWatchingKey]) {
        // Catch up on what changed while the app was not running, then keep watching.
        [self runOnFolders:self.folders
                      verb:@"前回の続き：アイコンを確認"
                 operation:^NSString*(NSString* f) {
                     return [STIconSetter setIconForFile:f];
                 }];
        [self startWatching];
    }
    [self updateControls];
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
    [self stopWatching];
    for (NSURL* url in self.folders) {
        [url stopAccessingSecurityScopedResource];
    }
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

// --- folders -------------------------------------------------------------------

- (void)saveFolders {
    NSMutableArray<NSData*>* bookmarks = [NSMutableArray array];
    for (NSURL* url in self.folders) {
        NSError* error = nil;
        NSData* data = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                     includingResourceValuesForKeys:nil
                                      relativeToURL:nil
                                              error:&error];
        if (data != nil) {
            [bookmarks addObject:data];
        } else {
            [self log:[NSString stringWithFormat:@"フォルダを覚えられませんでした：%@ (%@)", url.path,
                                                 error.localizedDescription]];
        }
    }
    [self.defaults setObject:bookmarks forKey:kBookmarksKey];
    [self.defaults removeObjectForKey:kOldBookmarkKey];
}

- (void)restoreFolders {
    NSMutableArray* bookmarks = [[self.defaults arrayForKey:kBookmarksKey] mutableCopy];
    if (bookmarks == nil) {
        bookmarks = [NSMutableArray array];
    }
    NSData* old = [self.defaults dataForKey:kOldBookmarkKey];
    if (old != nil) {
        [bookmarks addObject:old];
    }
    BOOL changed = old != nil;
    for (id item in bookmarks) {
        if (![item isKindOfClass:NSData.class]) {
            changed = YES;
            continue;
        }
        BOOL stale = NO;
        NSError* error = nil;
        NSURL* url = [NSURL URLByResolvingBookmarkData:item
                                               options:NSURLBookmarkResolutionWithSecurityScope
                                         relativeToURL:nil
                                   bookmarkDataIsStale:&stale
                                                 error:&error];
        if (url == nil) {
            [self log:[NSString stringWithFormat:@"登録していたフォルダが見つかりません：%@",
                                                 error.localizedDescription]];
            changed = YES;
            continue;
        }
        if (![self containsFolder:url]) {
            [url startAccessingSecurityScopedResource];
            [self.folders addObject:url];
        }
        changed = changed || stale;
    }
    if (changed) {
        [self saveFolders];
    }
}

- (BOOL)containsFolder:(NSURL*)url {
    return [self folderCovering:url] != nil;
}

// The registered folder that is url or contains it (files are searched recursively, so a
// subfolder of a registered folder is already covered).
- (NSURL*)folderCovering:(NSURL*)url {
    NSString* path = url.URLByStandardizingPath.path;
    for (NSURL* f in self.folders) {
        NSString* root = f.URLByStandardizingPath.path;
        if ([path isEqualToString:root] ||
            [path hasPrefix:[root stringByAppendingString:@"/"]]) {
            return f;
        }
    }
    return nil;
}

// Runs one icon operation per .ply in the folders on the work queue, then a summary line.
- (void)runOnFolders:(NSArray<NSURL*>*)folders
                verb:(NSString*)verb
           operation:(NSString* (^)(NSString*))operation {
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    for (NSURL* url in folders) {
        [paths addObject:url.path];
    }
    [self log:[NSString stringWithFormat:@"— %@：%lu 個のフォルダ", verb, (unsigned long)paths.count]];
    dispatch_async(self.work, ^{
        NSDate* start = [NSDate date];
        // Folders may overlap (one registered before its parent), so handle each file once.
        NSMutableOrderedSet<NSString*>* files = [NSMutableOrderedSet orderedSet];
        for (NSString* folder in paths) {
            [files addObjectsFromArray:[STIconSetter plyFilesAt:folder]];
        }
        const NSUInteger count = files.count;
        for (NSString* f in files) {
            [self log:operation(f)];
        }
        [self log:[NSString stringWithFormat:@"— %lu 個の .ply、%.1f 秒", (unsigned long)count,
                                             -start.timeIntervalSinceNow]];
    });
}

- (void)addFolders:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = YES;
    panel.prompt = @"追加してアイコンを付ける";
    panel.message = @"このフォルダ（とその中のフォルダ）の 3DGS .ply にアイコンを付けます";
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    NSMutableArray<NSURL*>* added = [NSMutableArray array];
    for (NSURL* url in panel.URLs) {
        NSURL* covering = [self folderCovering:url];
        if (covering != nil) {
            [self log:[covering.path isEqualToString:url.path]
                          ? [NSString stringWithFormat:@"すでに登録されています：%@", url.path]
                          : [NSString stringWithFormat:@"%@ の中なので、もう含まれています：%@",
                                                       covering.path, url.path]];
            continue;
        }
        [url startAccessingSecurityScopedResource];
        [self.folders addObject:url];
        [added addObject:url];
    }
    if (added.count == 0) {
        return;
    }
    [self saveFolders];
    [self runOnFolders:added
                  verb:@"フォルダを追加してアイコンを付ける"
             operation:^NSString*(NSString* f) {
                 return [STIconSetter setIconForFile:f];
             }];
    if (self.watching) {
        [self startWatching]; // include the new folders
    }
    [self updateControls];
}

- (void)removeSelectedFolders:(id)sender {
    (void)sender;
    NSIndexSet* rows = self.table.selectedRowIndexes;
    if (rows.count == 0) {
        return;
    }
    NSArray<NSURL*>* removed = [self.folders objectsAtIndexes:rows];
    [self.folders removeObjectsAtIndexes:rows];
    for (NSURL* url in removed) {
        [self log:[NSString stringWithFormat:@"一覧から外しました（付いているアイコンはそのまま）：%@",
                                             url.path]];
        [url stopAccessingSecurityScopedResource];
    }
    [self saveFolders];
    if (self.watching) {
        if (self.folders.count > 0) {
            [self startWatching];
        } else {
            [self stopWatching];
            [self.defaults setBool:NO forKey:kWatchingKey];
        }
    }
    [self updateControls];
}

- (void)removeAllIcons:(id)sender {
    (void)sender;
    if (self.folders.count == 0) {
        return;
    }
    // Removing an icon is itself a change in the folder; running watchers would put the
    // icons straight back, so removing always stops watching first.
    if (self.watching) {
        [self stopWatching];
        [self.defaults setBool:NO forKey:kWatchingKey];
        [self log:@"— 見張りを止めました（アイコンを外すため）"];
    }
    [self runOnFolders:self.folders
                  verb:@"アイコンをすべて外す"
             operation:^NSString*(NSString* f) {
                 return [STIconSetter removeIconForFile:f];
             }];
    [self updateControls];
}

// --- watching ----------------------------------------------------------------

- (void)startWatching {
    [self stopWatching];
    __weak AppDelegate* weakSelf = self;
    for (NSURL* url in self.folders) {
        STFolderWatcher* watcher = [[STFolderWatcher alloc]
            initWithFolder:url.path
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
        [watcher start];
        [self.watchers addObject:watcher];
    }
    [self log:[NSString stringWithFormat:@"— 見張りを始めました：%lu 個のフォルダ（%@）",
                                         (unsigned long)self.watchers.count,
                                         self.resident ? @"常駐" : @"アプリを開いている間だけ"]];
    [self updateStatusItem];
}

- (void)stopWatching {
    for (STFolderWatcher* watcher in self.watchers) {
        [watcher stop];
    }
    [self.watchers removeAllObjects];
    [self updateStatusItem];
}

- (void)toggleWatch:(id)sender {
    (void)sender;
    const BOOL on = self.watchCheckbox.state == NSControlStateValueOn && self.folders.count > 0;
    [self.defaults setBool:on forKey:kWatchingKey];
    if (on) {
        [self startWatching];
    } else {
        [self stopWatching];
        [self log:@"— 見張りを止めました"];
    }
    [self updateControls];
}

// --- resident mode -----------------------------------------------------------

- (void)modeChanged:(id)sender {
    const BOOL resident = sender == self.residentRadio;
    if (resident == self.resident) {
        return;
    }
    NSError* error = nil;
    SMAppService* service = SMAppService.mainAppService;
    const BOOL ok = resident ? [service registerAndReturnError:&error]
                             : [service unregisterAndReturnError:&error];
    if (!ok) {
        [self log:[NSString stringWithFormat:@"ログイン項目を%@できませんでした：%@",
                                             resident ? @"登録" : @"解除",
                                             error.localizedDescription]];
        if (resident) {
            self.residentRadio.state = NSControlStateValueOff;
            self.whileOpenRadio.state = NSControlStateValueOn;
            return;
        }
    }
    [self.defaults setBool:resident forKey:kResidentKey];
    self.residentRadio.state = resident ? NSControlStateValueOn : NSControlStateValueOff;
    self.whileOpenRadio.state = resident ? NSControlStateValueOff : NSControlStateValueOn;
    [self log:resident ? @"— 常駐にしました：ログイン時に起動し、ウィンドウを閉じてもメニューバーで見張りを続けます"
                       : @"— 「アプリを開いている間だけ」にしました（ログイン項目から外しました）"];
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
    NSString* state = self.watching
                          ? [NSString stringWithFormat:@"見張り中：%lu 個のフォルダ",
                                                       (unsigned long)self.watchers.count]
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
