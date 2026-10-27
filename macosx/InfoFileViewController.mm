// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoFileViewController.h"
#import "FileListNode.h"
#import "FileOutlineController.h"
#import "FileOutlineView.h"
#import "Torrent.h"

@interface InfoFileViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;
@property(nonatomic) BOOL fSet;

@property(nonatomic) FileOutlineController* fFileController;

@property(nonatomic) NSSearchField* fFileFilterField;
@property(nonatomic) NSButton* fCheckAllButton;
@property(nonatomic) NSButton* fUncheckAllButton;

@property(nonatomic) NSScrollView* fScrollView;
@property(nonatomic) FileOutlineView* fOutlineView;

@end

@implementation InfoFileViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        self.title = NSLocalizedString(@"Files", "Inspector view -> title");

        _fFileController = [[FileOutlineController alloc] init];
    }

    return self;
}

- (void)loadView
{
    NSView* rootView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 340, 365)];
    rootView.translatesAutoresizingMaskIntoConstraints = NO;
    rootView.clipsToBounds = YES;
    self.view = rootView;

    self.fFileFilterField = [[NSSearchField alloc] init];
    self.fFileFilterField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fFileFilterField.wantsLayer = YES;
    self.fFileFilterField.focusRingType = NSFocusRingTypeNone;

    self.fFileFilterField.controlSize = NSControlSizeSmall;
    self.fFileFilterField.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];

    self.fFileFilterField.lineBreakMode = NSLineBreakByClipping;
    self.fFileFilterField.cell.scrollable = YES;

    self.fFileFilterField.target = self;
    self.fFileFilterField.action = @selector(setFileFilterText:);

    [rootView addSubview:self.fFileFilterField];

    self.fScrollView = [[NSScrollView alloc] init];
    self.fScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    self.fScrollView.borderType = NSBezelBorder;
    self.fScrollView.hasVerticalScroller = YES;
    self.fScrollView.autohidesScrollers = YES;
    [rootView addSubview:self.fScrollView];

    self.fOutlineView = [[FileOutlineView alloc] initWithFrame:NSZeroRect];
    self.fOutlineView.rowHeight = 34;
    self.fOutlineView.allowsExpansionToolTips = YES;
    self.fOutlineView.usesAlternatingRowBackgroundColors = YES;
    self.fOutlineView.allowsMultipleSelection = YES;
    self.fOutlineView.headerView = nil;
    self.fOutlineView.rowSizeStyle = NSTableViewRowSizeStyleCustom;
    self.fOutlineView.indentationPerLevel = 14;
    self.fOutlineView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    self.fOutlineView.autoresizesOutlineColumn = NO;

    self.fScrollView.documentView = self.fOutlineView;

    [self.fFileController setupWithOutlineView:self.fOutlineView];

    NSTableColumn* nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"Name"];
    nameColumn.editable = NO;
    nameColumn.width = 218.0;
    nameColumn.minWidth = 38.599;
    nameColumn.maxWidth = 1000.0;
    nameColumn.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
    [self.fOutlineView addTableColumn:nameColumn];

    NSTableColumn* priorityColumn = [[NSTableColumn alloc] initWithIdentifier:@"Priority"];
    priorityColumn.editable = NO;
    priorityColumn.width = 34.0;
    priorityColumn.minWidth = 10.0;
    priorityColumn.maxWidth = 1000.0;
    priorityColumn.resizingMask = NSTableColumnNoResizing;
    [self.fOutlineView addTableColumn:priorityColumn];

    NSTableColumn* checkColumn = [[NSTableColumn alloc] initWithIdentifier:@"Check"];
    checkColumn.editable = NO;
    checkColumn.width = 31.0;
    checkColumn.minWidth = 10.0;
    checkColumn.maxWidth = 1000.0;
    checkColumn.resizingMask = NSTableColumnNoResizing;
    [self.fOutlineView addTableColumn:checkColumn];

    self.fOutlineView.outlineTableColumn = nameColumn;

    self.fCheckAllButton = [[NSButton alloc] init];
    self.fCheckAllButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fCheckAllButton.bezelStyle = NSBezelStyleRoundRect;
    self.fCheckAllButton.controlSize = NSControlSizeSmall;
    self.fCheckAllButton.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    self.fCheckAllButton.target = self;
    self.fCheckAllButton.action = @selector(checkAll:);
    [rootView addSubview:self.fCheckAllButton];

    self.fUncheckAllButton = [[NSButton alloc] init];
    self.fUncheckAllButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fUncheckAllButton.bezelStyle = NSBezelStyleRoundRect;
    self.fUncheckAllButton.controlSize = NSControlSizeSmall;
    self.fUncheckAllButton.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    self.fUncheckAllButton.target = self;
    self.fUncheckAllButton.action = @selector(uncheckAll:);
    [rootView addSubview:self.fUncheckAllButton];

    /// Constraints
    NSLayoutConstraint* bottomConstraint = [self.fFileFilterField.bottomAnchor constraintEqualToAnchor:rootView.bottomAnchor
                                                                                              constant:-12.0];
    bottomConstraint.priority = 249.0;

    NSLayoutConstraint* btnBottomConstraint = [self.fUncheckAllButton.bottomAnchor constraintEqualToAnchor:rootView.bottomAnchor
                                                                                                  constant:-12.0];
    btnBottomConstraint.priority = 249.0;
    NSLayoutConstraint* topConstraint = [self.fFileFilterField.topAnchor constraintEqualToAnchor:self.fScrollView.bottomAnchor
                                                                                        constant:8.0];
    topConstraint.priority = 249.0;
    NSLayoutConstraint* btnTopConstraint = [self.fUncheckAllButton.topAnchor constraintEqualToAnchor:self.fScrollView.bottomAnchor
                                                                                            constant:8.0];
    btnTopConstraint.priority = 249.0;

    [NSLayoutConstraint activateConstraints:@[
        [self.fScrollView.topAnchor constraintEqualToAnchor:rootView.topAnchor constant:12.0],
        [self.fScrollView.leadingAnchor constraintEqualToAnchor:rootView.leadingAnchor constant:12.0],
        [self.fScrollView.trailingAnchor constraintEqualToAnchor:rootView.trailingAnchor constant:-12.0],

        [self.fScrollView.heightAnchor constraintGreaterThanOrEqualToConstant:150.0],
        [self.fScrollView.widthAnchor constraintGreaterThanOrEqualToConstant:300.0],

        [self.fFileFilterField.leadingAnchor constraintEqualToAnchor:self.fScrollView.leadingAnchor],
        [self.fFileFilterField.topAnchor constraintGreaterThanOrEqualToAnchor:self.fScrollView.bottomAnchor constant:8.0],
        topConstraint,
        [self.fFileFilterField.widthAnchor constraintEqualToConstant:110.0],

        bottomConstraint,
        [self.fFileFilterField.bottomAnchor constraintLessThanOrEqualToAnchor:rootView.bottomAnchor constant:-12.0],
        [self.fUncheckAllButton.trailingAnchor constraintEqualToAnchor:self.fScrollView.trailingAnchor],
        [self.fUncheckAllButton.topAnchor constraintGreaterThanOrEqualToAnchor:self.fScrollView.bottomAnchor constant:8.0],

        btnTopConstraint,

        [self.fUncheckAllButton.bottomAnchor constraintLessThanOrEqualToAnchor:rootView.bottomAnchor constant:-12.0],
        btnBottomConstraint,

        [self.fUncheckAllButton.leadingAnchor constraintEqualToAnchor:self.fCheckAllButton.trailingAnchor constant:8.0],
        [self.fCheckAllButton.widthAnchor constraintEqualToAnchor:self.fUncheckAllButton.widthAnchor],

        [self.fFileFilterField.lastBaselineAnchor constraintEqualToAnchor:self.fCheckAllButton.lastBaselineAnchor],
        [self.fCheckAllButton.lastBaselineAnchor constraintEqualToAnchor:self.fUncheckAllButton.lastBaselineAnchor]
    ]];
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    CGFloat const height = [NSUserDefaults.standardUserDefaults floatForKey:@"InspectorContentHeightFiles"];
    if (height != 0.0) {
        NSRect viewRect = self.view.frame;
        viewRect.size.height = height;
        self.view.frame = viewRect;
    }

    self.fFileFilterField.placeholderString = NSLocalizedString(@"Filter", "inspector -> file filter");
    self.fCheckAllButton.title = NSLocalizedString(@"All", "inspector -> check all");
    self.fUncheckAllButton.title = NSLocalizedString(@"None", "inspector -> check all");
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet) {
        [self setupInfo];
    }

    if (self.fTorrents.count == 1) {
        [self.fFileController reloadVisibleRows];

#warning use TorrentFileCheckChange notification as well
        Torrent* torrent = self.fTorrents[0];
        if (torrent.folder) {
            NSControlStateValue const filesCheckState = [torrent
                checkForFiles:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, torrent.fileCount)]];
            self.fCheckAllButton.enabled = filesCheckState != NSControlStateValueOn; //if anything is unchecked
            self.fUncheckAllButton.enabled = !torrent.allDownloaded; //if there are any checked files that aren't finished
        }
    }
}

- (void)saveViewSize
{
    [NSUserDefaults.standardUserDefaults setFloat:NSHeight(self.view.frame) forKey:@"InspectorContentHeightFiles"];
}

- (IBAction)setFileFilterText:(id)sender
{
    self.fFileController.filterText = [sender stringValue];
}

- (IBAction)checkAll:(id)sender
{
    [self.fFileController checkAll];
}

- (IBAction)uncheckAll:(id)sender
{
    [self.fFileController uncheckAll];
}

- (void)keyDown:(NSEvent*)event
{
    unichar const firstChar = [event.charactersIgnoringModifiers characterAtIndex:0];

    if (firstChar == ' ') {
        [self toggleQuickLook:nil];
    } else {
        [super keyDown:event];
    }
}

- (void)toggleQuickLook:(id)sender
{
    if ([QLPreviewPanel sharedPreviewPanel].visible) {
        [[QLPreviewPanel sharedPreviewPanel] orderOut:nil];
    } else {
        [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront:nil];
    }
}

- (NSArray<NSURL*>*)quickLookURLs
{
    FileOutlineView* fileOutlineView = self.fFileController.outlineView;
    Torrent* torrent = self.fTorrents[0];
    NSIndexSet* indexes = fileOutlineView.selectedRowIndexes;
    NSMutableArray* urlArray = [NSMutableArray arrayWithCapacity:indexes.count];

    for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i]) {
        FileListNode* item = [fileOutlineView itemAtRow:i];
        if ([self canQuickLookFile:item]) {
            [urlArray addObject:[NSURL fileURLWithPath:[torrent fileLocation:item]]];
        }
    }

    return urlArray;
}

- (BOOL)canQuickLook
{
    if (self.fTorrents.count != 1) {
        return NO;
    }

    Torrent* torrent = self.fTorrents[0];
    if (!torrent.folder) {
        return NO;
    }

    FileOutlineView* fileOutlineView = self.fFileController.outlineView;
    NSIndexSet* indexes = fileOutlineView.selectedRowIndexes;

    for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i]) {
        if ([self canQuickLookFile:[fileOutlineView itemAtRow:i]]) {
            return YES;
        }
    }

    return NO;
}

- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item
{
    FileOutlineView* fileOutlineView = self.fFileController.outlineView;

    NSString* fullPath = ((NSURL*)item).path;
    Torrent* torrent = self.fTorrents[0];
    NSRange visibleRows = [fileOutlineView rowsInRect:fileOutlineView.bounds];

    for (NSUInteger row = visibleRows.location; row < NSMaxRange(visibleRows); row++) {
        FileListNode* rowItem = [fileOutlineView itemAtRow:row];
        if ([[torrent fileLocation:rowItem] isEqualToString:fullPath]) {
            NSRect frame = [fileOutlineView iconRectForRow:row];

            if (!NSIntersectsRect(fileOutlineView.visibleRect, frame)) {
                return NSZeroRect;
            }

            frame.origin = [fileOutlineView convertPoint:frame.origin toView:nil];
            frame = [self.view.window convertRectToScreen:frame];
            frame.origin.y -= frame.size.height;
            return frame;
        }
    }

    return NSZeroRect;
}

#pragma mark - Private

- (void)setupInfo
{
    self.fFileFilterField.stringValue = @"";
    [self setFileFilterText:self.fFileFilterField];

    if (self.fTorrents.count == 1) {
        Torrent* torrent = self.fTorrents[0];

        self.fFileController.torrent = torrent;

        BOOL const isFolder = torrent.folder;
        self.fFileFilterField.enabled = isFolder;

        if (!isFolder) {
            self.fCheckAllButton.enabled = NO;
            self.fUncheckAllButton.enabled = NO;
        }
    } else {
        self.fFileController.torrent = nil;

        self.fFileFilterField.enabled = NO;

        self.fCheckAllButton.enabled = NO;
        self.fUncheckAllButton.enabled = NO;
    }

    self.fSet = YES;
}

- (BOOL)canQuickLookFile:(FileListNode*)item
{
    Torrent* torrent = self.fTorrents[0];
    return (item.isFolder || [torrent fileProgress:item] >= 1.0) && [torrent fileLocation:item];
}

@end
