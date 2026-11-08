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
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "STIconSetter.h"
#import "STViewerWindow.h"

// Window content that accepts .ply files dropped on it (they open in the viewer).
@interface STDropView : NSView
@property(copy) void (^onDrop)(NSArray<NSURL*>* urls);
@end

@implementation STDropView
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}
- (NSArray<NSURL*>*)plyURLs:(id<NSDraggingInfo>)info {
    NSArray<NSURL*>* urls = [info.draggingPasteboard
        readObjectsForClasses:@[ NSURL.class ]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    NSMutableArray<NSURL*>* out = [NSMutableArray array];
    for (NSURL* u in urls) {
        if ([u.pathExtension caseInsensitiveCompare:@"ply"] == NSOrderedSame) {
            [out addObject:u];
        }
    }
    return out;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    return [self plyURLs:info].count > 0 ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    NSArray<NSURL*>* urls = [self plyURLs:info];
    if (urls.count == 0 || self.onDrop == nil) {
        return NO;
    }
    self.onDrop(urls);
    return YES;
}
@end

static NSString* const kBookmarksKey = @"FolderBookmarks";
static NSString* const kOldBookmarkKey = @"FolderBookmark"; // single folder, earlier spike
static NSString* const kWatchingKey = @"Watching";
static NSString* const kResidentKey = @"Resident";

// The only pages the app opens, and only when clicked; it never connects to the network.
static NSString* const kRepoURL = @"https://github.com/VoxelKei/SplatThumb";
static NSString* const kAppURL = @"https://www.spatialograph.app/";
static NSString* const kAuthorURL = @"https://x.com/VoxelKei";

static NSString* AppVersion(void) {
    NSString* v = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"];
    return v.length > 0 ? v : @"?";
}

// "text" with "link" appended as a clickable link (in a selectable text field).
static NSMutableAttributedString* LinkedText(NSString* before, NSString* link, NSString* url,
                                              NSString* after, CGFloat size) {
    NSFont* font = [NSFont systemFontOfSize:size];
    NSDictionary* plain = @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : NSColor.secondaryLabelColor
    };
    NSMutableAttributedString* s = [[NSMutableAttributedString alloc] initWithString:before
                                                                          attributes:plain];
    [s appendAttributedString:[[NSAttributedString alloc]
                                  initWithString:link
                                      attributes:@{
                                          NSFontAttributeName : font,
                                          NSLinkAttributeName : [NSURL URLWithString:url]
                                      }]];
    [s appendAttributedString:[[NSAttributedString alloc] initWithString:after attributes:plain]];
    return s;
}

static NSTextField* LinkLabel(NSAttributedString* text) {
    NSTextField* label = [NSTextField labelWithAttributedString:text];
    // Links are only clickable in a selectable field that keeps its attributes.
    label.selectable = YES;
    label.allowsEditingTextAttributes = YES;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

@interface AppDelegate
    : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTableView* table;
@property(strong) NSTextView* logView;
@property(strong) NSButton* removeFolderButton;
@property(strong) NSButton* removeAllButton;
@property(strong) NSButton* remakeAllButton;
@property(strong) NSButton* watchCheckbox;
@property(strong) NSButton* whileOpenRadio;
@property(strong) NSButton* residentRadio;
@property(strong) NSPopUpButton* qualityPopup;
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
        initWithContentRect:NSMakeRect(0, 0, 720, 820)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"SplatThumb";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    STDropView* drop = [[STDropView alloc] initWithFrame:NSZeroRect];
    __weak AppDelegate* weakSelf = self;
    drop.onDrop = ^(NSArray<NSURL*>* urls) {
        [weakSelf openViewers:urls];
    };
    self.window.contentView = drop;
    NSView* content = self.window.contentView;

    NSTextField* heading = [NSTextField labelWithString:NSLocalizedString(@"Folders that get icons", nil)];
    heading.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    heading.translatesAutoresizingMaskIntoConstraints = NO;

    self.table = [[NSTableView alloc] init];
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"path"];
    column.title = @"Folder"; // header is hidden
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

    NSButton* add = [self button:NSLocalizedString(@"Add Folder…", nil) action:@selector(addFolders:)];
    self.removeFolderButton = [self button:NSLocalizedString(@"Remove Selected from List", nil)
                                    action:@selector(removeSelectedFolders:)];
    self.remakeAllButton = [self button:NSLocalizedString(@"Remake All Icons in These Folders", nil)
                                 action:@selector(remakeAllIcons:)];
    self.removeAllButton = [self button:NSLocalizedString(@"Remove All Icons in These Folders", nil)
                                 action:@selector(removeAllIcons:)];
    // How to set a file's thumbnail angle, in a box so it stands out. Finder's context menu
    // is the usual way; the button is a fallback.
    NSBox* angleBox = [[NSBox alloc] init];
    angleBox.title = NSLocalizedString(@"How to set the angle", nil);
    angleBox.titleFont = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    angleBox.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* angleHelp = [NSTextField wrappingLabelWithString:
        NSLocalizedString(@"In Finder, right-click a .ply and choose Open With → SplatThumb to open the angle window. Adjust the angle, then click “Use this view” to remake the icon from that angle.\nYou can also drop a .ply on this window or on SplatThumb in the Dock.\nThis also works for a .ply outside the registered folders, but such a file is not watched and is left out of Remake All and Remove All. To take its icon off, select it in Finder, press ⌘I, click the small icon at the top left and press Delete.", nil)];
    angleHelp.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* angleButtonNote = [NSTextField labelWithString:NSLocalizedString(@"Or use this button:", nil)];
    angleButtonNote.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton* angle = [self button:NSLocalizedString(@"Choose a PLY…", nil) action:@selector(openPly:)];
    NSView* box = angleBox.contentView;
    for (NSView* v in @[ angleHelp, angleButtonNote, angle ]) {
        [box addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [angleHelp.topAnchor constraintEqualToAnchor:box.topAnchor constant:8],
        [angleHelp.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:10],
        [angleHelp.trailingAnchor constraintEqualToAnchor:box.trailingAnchor constant:-10],
        [angleButtonNote.topAnchor constraintEqualToAnchor:angleHelp.bottomAnchor constant:10],
        [angleButtonNote.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:10],
        [angle.centerYAnchor constraintEqualToAnchor:angleButtonNote.centerYAnchor],
        [angle.leadingAnchor constraintEqualToAnchor:angleButtonNote.trailingAnchor constant:8],
        [angle.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-10],
    ]];

    self.watchCheckbox =
        [NSButton checkboxWithTitle:NSLocalizedString(@"Watch these folders (new or changed .ply files get icons automatically)", nil)
                             target:self
                             action:@selector(toggleWatch:)];
    self.watchCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.whileOpenRadio = [self radio:NSLocalizedString(@"Only while the app is open", nil)];
    self.residentRadio = [self radio:NSLocalizedString(@"Always (start at login; keep running in the menu bar after the window is closed)", nil)];
    (self.resident ? self.residentRadio : self.whileOpenRadio).state = NSControlStateValueOn;

    // Same choices as the Windows settings window.
    NSTextField* qualityLabel = [NSTextField labelWithString:NSLocalizedString(@"Splats used for icons:", nil)];
    qualityLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.qualityPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.qualityPopup.translatesAutoresizingMaskIntoConstraints = NO;
    [self.qualityPopup addItemsWithTitles:@[
        NSLocalizedString(@"Up to 50,000", nil), NSLocalizedString(@"Up to 200,000", nil),
        NSLocalizedString(@"Up to 400,000", nil)
    ]];
    [self.qualityPopup selectItemAtIndex:STIconSetter.qualitySetting];
    self.qualityPopup.target = self;
    self.qualityPopup.action = @selector(qualityChanged:);

    NSScrollView* logScroll = [NSTextView scrollableTextView];
    logScroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.logView = logScroll.documentView;
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];

    // Same line as the Windows settings window.
    const CGFloat small = NSFont.smallSystemFontSize;
    NSTextField* about = [NSTextField
        labelWithString:[NSString stringWithFormat:@"SplatThumb %@  ·  © 2026 VoxelKei  ·  MIT License",
                                                   AppVersion()]];
    about.font = [NSFont systemFontOfSize:small];
    about.textColor = NSColor.secondaryLabelColor;
    about.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* repo = LinkLabel(LinkedText(@"GitHub: ", @"github.com/VoxelKei/SplatThumb", kRepoURL, @"", small));
    NSTextField* app = LinkLabel(LinkedText(@"", @"Spatialograph Maker", kAppURL,
                                            NSLocalizedString(@" - a 3DGS editing app by the same author", nil), small));

    for (NSView* v in @[
             heading, tableScroll, add, self.removeFolderButton, self.remakeAllButton, self.removeAllButton,
             self.watchCheckbox, self.whileOpenRadio, self.residentRadio, qualityLabel,
             self.qualityPopup, angleBox,
             logScroll, about, repo, app
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
        [self.remakeAllButton.topAnchor constraintEqualToAnchor:add.bottomAnchor constant:6],
        [self.remakeAllButton.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.removeAllButton.centerYAnchor constraintEqualToAnchor:self.remakeAllButton.centerYAnchor],
        [self.removeAllButton.leadingAnchor constraintEqualToAnchor:self.remakeAllButton.trailingAnchor constant:8],
        [self.watchCheckbox.topAnchor constraintEqualToAnchor:self.remakeAllButton.bottomAnchor constant:14],
        [self.watchCheckbox.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.whileOpenRadio.topAnchor constraintEqualToAnchor:self.watchCheckbox.bottomAnchor constant:6],
        [self.whileOpenRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [self.residentRadio.topAnchor constraintEqualToAnchor:self.whileOpenRadio.bottomAnchor constant:4],
        [self.residentRadio.leadingAnchor constraintEqualToAnchor:left constant:36],
        [self.qualityPopup.topAnchor constraintEqualToAnchor:self.residentRadio.bottomAnchor constant:14],
        [qualityLabel.firstBaselineAnchor constraintEqualToAnchor:self.qualityPopup.firstBaselineAnchor],
        [qualityLabel.leadingAnchor constraintEqualToAnchor:left constant:16],
        [self.qualityPopup.leadingAnchor constraintEqualToAnchor:qualityLabel.trailingAnchor constant:8],
        [angleBox.topAnchor constraintEqualToAnchor:self.qualityPopup.bottomAnchor constant:16],
        [angleBox.leadingAnchor constraintEqualToAnchor:left constant:13],
        [angleBox.trailingAnchor constraintEqualToAnchor:right constant:-13],
        [logScroll.topAnchor constraintEqualToAnchor:angleBox.bottomAnchor constant:12],
        [logScroll.leadingAnchor constraintEqualToAnchor:left constant:16],
        [logScroll.trailingAnchor constraintEqualToAnchor:right constant:-16],
        [about.topAnchor constraintEqualToAnchor:logScroll.bottomAnchor constant:10],
        [about.leadingAnchor constraintEqualToAnchor:left constant:16],
        [repo.topAnchor constraintEqualToAnchor:about.bottomAnchor constant:2],
        [repo.leadingAnchor constraintEqualToAnchor:left constant:16],
        [app.centerYAnchor constraintEqualToAnchor:repo.centerYAnchor],
        [app.leadingAnchor constraintEqualToAnchor:repo.trailingAnchor constant:16],
        [app.trailingAnchor constraintLessThanOrEqualToAnchor:right constant:-16],
        [repo.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12],
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
    self.remakeAllButton.enabled = any;
    self.watchCheckbox.enabled = any;
    self.watchCheckbox.state = self.watching ? NSControlStateValueOn : NSControlStateValueOff;
    [self updateStatusItem];
}

// --- life cycle --------------------------------------------------------------

// --- menus and About ----------------------------------------------------------

- (void)buildMainMenu {
    // One app menu with the minimum.
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"SplatThumb"];
    [appMenu addItemWithTitle:NSLocalizedString(@"About SplatThumb", nil) action:@selector(showAbout:) keyEquivalent:@""]
        .target = self;
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:NSLocalizedString(@"Open a PLY to Set Its Angle…", nil) action:@selector(openPly:) keyEquivalent:@"o"]
        .target = self;
    [appMenu addItemWithTitle:NSLocalizedString(@"Close Window", nil) action:@selector(performClose:) keyEquivalent:@"w"];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:NSLocalizedString(@"Quit SplatThumb", nil) action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:appMenu.title action:nil keyEquivalent:@""];
    item.submenu = appMenu;
    NSMenu* bar = [[NSMenu alloc] init];
    [bar addItem:item];
    NSApp.mainMenu = bar;
}

- (void)showAbout:(id)sender {
    (void)sender;
    const CGFloat size = NSFont.smallSystemFontSize;
    NSMutableAttributedString* credits = [[NSMutableAttributedString alloc] init];
    NSDictionary* plain = @{
        NSFontAttributeName : [NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName : NSColor.labelColor
    };
    [credits appendAttributedString:[[NSAttributedString alloc]
                                        initWithString:NSLocalizedString(@"Shows 3D Gaussian Splatting PLY files as Finder icons.\n\n", nil)
                                            attributes:plain]];
    [credits appendAttributedString:LinkedText(NSLocalizedString(@"Author: VoxelKei (", nil), @"X: @VoxelKei", kAuthorURL, @")\n", size)];
    [credits appendAttributedString:LinkedText(@"GitHub: ", @"github.com/VoxelKei/SplatThumb", kRepoURL, @"\n", size)];
    [credits appendAttributedString:LinkedText(@"", @"Spatialograph Maker", kAppURL,
                                               NSLocalizedString(@" - a 3DGS editing app by the same author", nil), size)];
    NSMutableParagraphStyle* centered = [[NSMutableParagraphStyle alloc] init];
    centered.alignment = NSTextAlignmentCenter;
    [credits addAttribute:NSParagraphStyleAttributeName
                    value:centered
                    range:NSMakeRange(0, credits.length)];
    [NSApp activateIgnoringOtherApps:YES];
    // Pass the icon explicitly: the panel does not always find it on its own.
    NSImage* icon = [NSBundle.mainBundle imageForResource:@"AppIcon"];
    if (icon == nil) {
        icon = [NSWorkspace.sharedWorkspace iconForFile:NSBundle.mainBundle.bundlePath];
    }
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationIcon : icon,
        NSAboutPanelOptionApplicationName : @"SplatThumb",
        NSAboutPanelOptionApplicationVersion : AppVersion(),
        NSAboutPanelOptionVersion : @"",
        NSAboutPanelOptionCredits : credits,
    }];
}

// ⌘C and ⌘A in text views without an Edit menu. (A menu holding Copy would be treated as
// the Edit menu, and macOS would add Writing Tools, AutoFill, Dictation and Emoji to it.)
- (void)handleEditKeys {
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                          handler:^NSEvent*(NSEvent* event) {
        const NSEventModifierFlags mods =
            event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
        if (mods != NSEventModifierFlagCommand) {
            return event;
        }
        NSResponder* responder = event.window.firstResponder;
        if (![responder isKindOfClass:NSTextView.class]) {
            return event;
        }
        NSString* key = event.charactersIgnoringModifiers.lowercaseString;
        if ([key isEqualToString:@"c"]) {
            [(NSTextView*)responder copy:nil];
            return nil;
        }
        if ([key isEqualToString:@"a"]) {
            [(NSTextView*)responder selectAll:nil];
            return nil;
        }
        return event;
    }];
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    (void)notification;
    // Keep macOS from adding Dictation and Emoji & Symbols to the menu.
    [self.defaults registerDefaults:@{
        @"NSDisabledDictationMenuItem" : @YES,
        @"NSDisabledCharacterPaletteMenuItem" : @YES,
    }];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self buildMainMenu];
    [self handleEditKeys];
    self.work = dispatch_queue_create("com.voxelkei.splatthumb.icons", DISPATCH_QUEUE_SERIAL);
    self.folders = [NSMutableArray array];
    self.watchers = [NSMutableArray array];
    [self buildWindow];
    [self log:NSLocalizedString(@"Puts SplatThumb pictures on 3DGS .ply files as their Finder icons.", nil)];
    [self log:NSLocalizedString(@"The splat data is not changed (the icon is kept in the file's extended attributes).", nil)];

    [self restoreFolders];
    // The login item may have been removed in System Settings; follow what macOS says.
    if (self.resident && SMAppService.mainAppService.status == SMAppServiceStatusNotRegistered) {
        [self.defaults setBool:NO forKey:kResidentKey];
        self.whileOpenRadio.state = NSControlStateValueOn;
        self.residentRadio.state = NSControlStateValueOff;
        [self log:NSLocalizedString(@"SplatThumb was removed from Login Items, so it is back to “Only while the app is open”.", nil)];
    }
    const BOOL quiet = self.resident && [self launchedAsLoginItem];
    if (self.folders.count > 0 && [self.defaults boolForKey:kWatchingKey]) {
        // Catch up on what changed while the app was not running, then keep watching.
        [self runOnFolders:self.folders
                      verb:NSLocalizedString(@"Checking icons", nil)
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
            [self log:[NSString stringWithFormat:NSLocalizedString(@"Could not remember the folder: %@ (%@)", nil), url.path,
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
            [self log:[NSString stringWithFormat:NSLocalizedString(@"A registered folder could not be found: %@", nil),
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
    [self runOnFolders:folders verb:verb operation:operation done:nil];
}

// The same, then runs done on the main queue.
- (void)runOnFolders:(NSArray<NSURL*>*)folders
                verb:(NSString*)verb
           operation:(NSString* (^)(NSString*))operation
                done:(void (^_Nullable)(void))done {
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    for (NSURL* url in folders) {
        [paths addObject:url.path];
    }
    [self log:[NSString stringWithFormat:NSLocalizedString(@"— %@ (%lu folder(s))", nil), verb, (unsigned long)paths.count]];
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
        [self log:[NSString stringWithFormat:NSLocalizedString(@"— %lu .ply file(s), %.1f s", nil), (unsigned long)count,
                                             -start.timeIntervalSinceNow]];
        if (done != nil) {
            dispatch_async(dispatch_get_main_queue(), done);
        }
    });
}

- (void)addFolders:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = YES;
    panel.prompt = NSLocalizedString(@"Add and Set Icons", nil);
    panel.message = NSLocalizedString(@"Icons are set on the 3DGS .ply files in this folder and the folders inside it.", nil);
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    NSMutableArray<NSURL*>* added = [NSMutableArray array];
    for (NSURL* url in panel.URLs) {
        NSURL* covering = [self folderCovering:url];
        if (covering != nil) {
            [self log:[covering.path isEqualToString:url.path]
                          ? [NSString stringWithFormat:NSLocalizedString(@"Already registered: %@", nil), url.path]
                          : [NSString stringWithFormat:NSLocalizedString(@"Already included, as it is inside %@: %@", nil),
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
                  verb:NSLocalizedString(@"Adding folders and setting icons", nil)
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
        [self log:[NSString stringWithFormat:NSLocalizedString(@"Removed from the list (its icons stay): %@", nil),
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

- (void)remakeAllIcons:(id)sender {
    (void)sender;
    if (self.folders.count == 0) {
        return;
    }
    // Draws every icon again (after a change of settings or of SplatThumb itself), whether
    // or not the file changed, from the saved angle where there is one. Files that are not
    // 3DGS are still left alone. The watch is paused meanwhile: the new icons are changes in
    // the folders, and it would otherwise look at every file again.
    const BOOL wasWatching = self.watching;
    [self stopWatching];
    self.remakeAllButton.enabled = NO;
    self.removeAllButton.enabled = NO;
    [self runOnFolders:self.folders
                  verb:NSLocalizedString(@"Remaking all icons", nil)
             operation:^NSString*(NSString* f) {
                 return [STIconSetter setIconForFile:f force:YES];
             }
                  done:^{
                      if (wasWatching && self.folders.count > 0) {
                          [self startWatching];
                      }
                      [self updateControls];
                  }];
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
        [self log:NSLocalizedString(@"— Stopped watching (to remove icons)", nil)];
    }
    [self runOnFolders:self.folders
                  verb:NSLocalizedString(@"Removing all icons", nil)
             operation:^NSString*(NSString* f) {
                 return [STIconSetter removeIconForFile:f];
             }];
    [self updateControls];
}

// --- angle ---------------------------------------------------------------------

- (void)openViewers:(NSArray<NSURL*>*)urls {
    __weak AppDelegate* weakSelf = self;
    for (NSURL* url in urls) {
        [STViewerWindow openFile:url
                             log:^(NSString* line) {
                                 [weakSelf log:line];
                             }];
    }
}

- (void)openPly:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    UTType* ply = [UTType typeWithFilenameExtension:@"ply"];
    panel.allowedContentTypes = ply != nil ? @[ ply ] : @[];
    panel.prompt = NSLocalizedString(@"Open", nil);
    panel.message = NSLocalizedString(@"Choose a .ply to set its angle.", nil);
    if ([panel runModal] == NSModalResponseOK) {
        [self openViewers:panel.URLs];
    }
}

// Files opened from Finder ("Open With"), or dropped on the Dock icon.
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    (void)application;
    if (self.window == nil) {
        // Launched by opening a file: finish launching first.
        dispatch_async(dispatch_get_main_queue(), ^{
            [self openViewers:urls];
        });
        return;
    }
    [self openViewers:urls];
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
    [self log:[NSString stringWithFormat:NSLocalizedString(@"— Watching %lu folder(s) (%@)", nil),
                                         (unsigned long)self.watchers.count,
                                         self.resident ? NSLocalizedString(@"always", nil)
                                                       : NSLocalizedString(@"only while the app is open", nil)]];
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
        [self log:NSLocalizedString(@"— Stopped watching", nil)];
    }
    [self updateControls];
}

// --- splat count ---------------------------------------------------------------

- (void)qualityChanged:(id)sender {
    (void)sender;
    const NSInteger q = self.qualityPopup.indexOfSelectedItem;
    if (q == STIconSetter.qualitySetting) {
        return;
    }
    STIconSetter.qualitySetting = q;
    [self log:[NSString stringWithFormat:NSLocalizedString(@"— Splats used for icons: %@. Files with a count saved in their view keep it. To update the icons already set, click “Remake All Icons in These Folders”.", nil),
                                         self.qualityPopup.titleOfSelectedItem]];
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
        [self log:[NSString stringWithFormat:resident
                                                 ? NSLocalizedString(@"Could not add SplatThumb to Login Items: %@", nil)
                                                 : NSLocalizedString(@"Could not remove SplatThumb from Login Items: %@", nil),
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
    [self log:resident ? NSLocalizedString(@"— Always: starts at login and keeps watching from the menu bar after the window is closed", nil)
                       : NSLocalizedString(@"— Only while the app is open (removed from Login Items)", nil)];
    if (resident && service.status == SMAppServiceStatusRequiresApproval) {
        [self log:NSLocalizedString(@"  Allow SplatThumb in System Settings → General → Login Items.", nil)];
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
        // The app icon in one colour; as a template image macOS tints it for light and
        // dark menu bars.
        NSImage* icon = [NSImage imageNamed:@"StatusIcon"];
        icon.template = YES;
        icon.size = NSMakeSize(18, 18);
        icon.accessibilityDescription = @"SplatThumb";
        self.statusItem.button.image = icon;
    }
    NSMenu* menu = [[NSMenu alloc] init];
    NSString* state = self.watching
                          ? [NSString stringWithFormat:NSLocalizedString(@"Watching %lu folder(s)", nil),
                                                       (unsigned long)self.watchers.count]
                          : NSLocalizedString(@"Not watching", nil);
    [menu addItemWithTitle:state action:nil keyEquivalent:@""].enabled = NO;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:NSLocalizedString(@"Open SplatThumb", nil) action:@selector(showWindow:) keyEquivalent:@""]
        .target = self;
    [menu addItemWithTitle:NSLocalizedString(@"About SplatThumb", nil) action:@selector(showAbout:) keyEquivalent:@""]
        .target = self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:NSLocalizedString(@"Quit", nil) action:@selector(terminate:) keyEquivalent:@"q"];
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
