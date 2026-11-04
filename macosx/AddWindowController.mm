// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddWindowController.h"
#import "AddTorrentViewModel.h"
#import "Controller.h"
#import "FileOutlineController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "GroupPopUpButtonCell.h"
#import "PriorityPopUpButtonCell.h"
#import "FileOutlineView.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"AddWindow", @"")

static NSTimeInterval const kUpdateSeconds = 1.0;

@interface AddWindowController ()<NSWindowDelegate>

@property(nonatomic) NSImageView* fIconView;
@property(nonatomic) NSImageView* fLocationImageView;
@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fStatusField;
@property(nonatomic) NSTextField* fLocationField;
@property(nonatomic) NSButton* fStartCheck;
@property(nonatomic) NSButton* fDeleteCheck;
@property(nonatomic) NSPopUpButton* fGroupPopUp;
@property(nonatomic) NSPopUpButton* fPriorityPopUp;
@property(nonatomic) NSProgressIndicator* fVerifyIndicator;

@property(nonatomic) NSStackView* fFileFilterStack;
@property(nonatomic) NSTextField* fFileFilterField;
@property(nonatomic) NSButton* fCheckAllButton;
@property(nonatomic) NSButton* fUncheckAllButton;

@property(nonatomic) FileOutlineController* fFileController;
@property(nonatomic) NSScrollView* fFileScrollView;

@property(nonatomic, readonly) Controller* fController;

@property(nonatomic) AddTorrentViewModel* viewModel;
@property(nonatomic, readonly) NSString* fTorrentFile;
@property(nonatomic, readonly) BOOL fDeleteTorrentEnableInitially;
@property(nonatomic, readonly) BOOL fCanToggleDelete;

@property(nonatomic, weak) NSTimer* fTimer;

@end

@implementation AddWindowController

- (Torrent*)torrent
{
    return self.viewModel.torrent;
}

- (NSStackView*)stacked:(NSView*)view
{
    return [NSStackView stackViewWithViews:@[ view, self.spacer ]];
}

- (NSView*)spacer
{
    NSView* spacer = [[NSView alloc] init];
    spacer.translatesAutoresizingMaskIntoConstraints = NO;
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [spacer setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    return spacer;
}

- (instancetype)initWithTorrent:(Torrent*)torrent
                          destination:(NSString*)path
                      lockDestination:(BOOL)lockDestination
                           controller:(Controller*)controller
                          torrentFile:(NSString*)torrentFile
    deleteTorrentCheckEnableInitially:(BOOL)deleteTorrent
                      canToggleDelete:(BOOL)canToggleDelete
{
    if ((self = [super initWithWindow:nil])) {
        _viewModel = [[AddTorrentViewModel alloc] initWithTorrent:torrent destination:path lockDestination:lockDestination];

        _fController = controller;

        _fTorrentFile = torrentFile.stringByExpandingTildeInPath;
        _fDeleteTorrentEnableInitially = deleteTorrent;
        _fCanToggleDelete = canToggleDelete;

        _fFileController = [[FileOutlineController alloc] init];

        [self setupWindow];
        [self configureContent];
    }
    return self;
}

- (void)setupWindow
{
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 545, 640)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.restorable = NO;
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(440, 480);
    [window center];
    window.frameAutosaveName = @"AddWindow";
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    window.delegate = self;
    self.window = window;

    // --- Header ---
    self.fIconView = [[NSImageView alloc] init];
    self.fIconView.imageScaling = NSImageScaleAxesIndependently;

    self.fNameField = [NSTextField labelWithString:@""];
    self.fNameField.font = [NSFont systemFontOfSize:18];
    self.fNameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.fNameField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                              forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.fStatusField = [NSTextField labelWithString:@""];
    self.fStatusField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.fStatusField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                                forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* nameStatusStack = [NSStackView stackViewWithViews:@[ self.fNameField, self.fStatusField ]];
    nameStatusStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    nameStatusStack.alignment = NSLayoutAttributeLeading;
    nameStatusStack.spacing = 4;

    NSStackView* headerStack = [NSStackView stackViewWithViews:@[ self.fIconView, nameStatusStack ]];
    headerStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    headerStack.spacing = 12;
    headerStack.alignment = NSLayoutAttributeCenterY;

    // --- Files View ---
    self.fFileScrollView = [[NSScrollView alloc] init];
    self.fFileScrollView.borderType = NSBezelBorder;
    self.fFileScrollView.hasVerticalScroller = YES;
    self.fFileScrollView.autohidesScrollers = YES;

    FileOutlineView* outlineView = [[FileOutlineView alloc] initWithFrame:NSZeroRect];
    outlineView.rowHeight = 34;
    outlineView.allowsExpansionToolTips = YES;
    outlineView.allowsColumnReordering = NO;
    outlineView.allowsColumnResizing = NO;
    outlineView.allowsMultipleSelection = YES;
    outlineView.headerView = nil;
    outlineView.indentationPerLevel = 14;
    outlineView.autoresizesOutlineColumn = NO;
    outlineView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;

    NSTableColumn* nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"Name"];
    nameColumn.editable = NO;
    nameColumn.width = 403;
    nameColumn.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
    [outlineView addTableColumn:nameColumn];
    outlineView.outlineTableColumn = nameColumn;

    NSTableColumn* priorityColumn = [[NSTableColumn alloc] initWithIdentifier:@"Priority"];
    priorityColumn.editable = NO;
    priorityColumn.width = 34;
    priorityColumn.resizingMask = NSTableColumnNoResizing;
    [outlineView addTableColumn:priorityColumn];

    NSTableColumn* checkColumn = [[NSTableColumn alloc] initWithIdentifier:@"Check"];
    checkColumn.editable = NO;
    checkColumn.width = 31;
    checkColumn.resizingMask = NSTableColumnNoResizing;
    [outlineView addTableColumn:checkColumn];

    self.fFileScrollView.documentView = outlineView;
    [self.fFileController setupWithOutlineView:outlineView];

    // --- Filter Panel ---
    self.fFileFilterField = [[NSSearchField alloc] init];
    self.fFileFilterField.controlSize = NSControlSizeSmall;
    self.fFileFilterField.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    self.fFileFilterField.placeholderString = LStr(@"Filter");
    self.fFileFilterField.target = self;
    self.fFileFilterField.action = @selector(setFileFilterText:);

    self.fCheckAllButton = [NSButton buttonWithTitle:LStr(@"All") target:self action:@selector(checkAll:)];
    self.fCheckAllButton.controlSize = NSControlSizeSmall;
    self.fCheckAllButton.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    self.fCheckAllButton.bezelStyle = NSBezelStyleRoundRect;

    self.fUncheckAllButton = [NSButton buttonWithTitle:LStr(@"None") target:self action:@selector(uncheckAll:)];
    self.fUncheckAllButton.controlSize = NSControlSizeSmall;
    self.fUncheckAllButton.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    self.fUncheckAllButton.bezelStyle = NSBezelStyleRoundRect;

    NSStackView* filterStack = [NSStackView
        stackViewWithViews:@[ self.fFileFilterField, self.fCheckAllButton, self.fUncheckAllButton ]];
    filterStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    filterStack.distribution = NSStackViewDistributionGravityAreas;
    [filterStack setViews:@[ self.fFileFilterField ] inGravity:NSStackViewGravityLeading];
    [filterStack setViews:@[ self.fCheckAllButton, self.fUncheckAllButton ] inGravity:NSStackViewGravityTrailing];
    self.fFileFilterStack = filterStack;

    // --- Settings Box ---
    NSTextField* destLabel = [NSTextField labelWithString:LStr(@"Destination folder:")];
    NSTextField* groupLabel = [NSTextField labelWithString:LStr(@"Group:")];
    NSTextField* priorityLabel = [NSTextField labelWithString:LStr(@"Priority:")];

    self.fLocationImageView = [NSImageView imageViewWithImage:[NSImage imageNamed:@"NSApplicationIcon"]];

    self.fLocationField = [NSTextField labelWithString:@""];
    self.fLocationField.font = [NSFont systemFontOfSize:11];
    self.fLocationField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.fLocationField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                                  forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* pathContainer = [NSStackView stackViewWithViews:@[ self.fLocationImageView, self.fLocationField ]];
    pathContainer.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    pathContainer.spacing = 6;
    pathContainer.edgeInsets = NSEdgeInsetsMake(4, 8, 4, 8);

    NSBox* pathBorderBox = [[NSBox alloc] init];
    pathBorderBox.titlePosition = NSNoTitle;
    pathContainer.translatesAutoresizingMaskIntoConstraints = NO;
    [pathBorderBox addSubview:pathContainer];

    NSButton* changeButton = [NSButton buttonWithTitle:LStr(@"Change…") target:self action:@selector(setDestination:)];
    changeButton.controlSize = NSControlSizeSmall;
    changeButton.font = [NSFont systemFontOfSize:11];

    self.fGroupPopUp = [[GroupPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.fPriorityPopUp = [[PriorityPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.fPriorityPopUp setAction:@selector(changePriority:)];
    [self.fPriorityPopUp setTarget:self];

    [self.fPriorityPopUp addItemWithTitle:LStr(@"High")];
    [self.fPriorityPopUp lastItem].image = [NSImage imageNamed:@"PriorityHighTemplate"];

    [self.fPriorityPopUp addItemWithTitle:LStr(@"Normal")];
    [self.fPriorityPopUp lastItem].image = [NSImage imageNamed:@"PriorityNormalTemplate"];

    [self.fPriorityPopUp addItemWithTitle:LStr(@"Low")];
    [self.fPriorityPopUp lastItem].image = [NSImage imageNamed:@"PriorityLowTemplate"];

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ destLabel, pathBorderBox, changeButton ],
        @[ groupLabel, [self stacked:self.fGroupPopUp], [NSGridCell emptyContentView] ],
        @[ priorityLabel, [self stacked:self.fPriorityPopUp], [NSGridCell emptyContentView] ]
    ]];
    gridView.rowSpacing = 8;
    gridView.columnSpacing = 10;
    [gridView columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [gridView columnAtIndex:1].xPlacement = NSGridCellPlacementFill;
    [gridView columnAtIndex:2].xPlacement = NSGridCellPlacementTrailing;
    gridView.yPlacement = NSGridCellPlacementCenter;

    NSBox* settingsBox = [[NSBox alloc] init];
    settingsBox.titlePosition = NSNoTitle;
    settingsBox.contentView = gridView;

    // --- Bottom Panel (Footer) ---
    self.fDeleteCheck = [NSButton checkboxWithTitle:LStr(@"Move torrent file to the Trash") target:nil action:nil];
    self.fStartCheck = [NSButton checkboxWithTitle:LStr(@"Start when added") target:nil action:nil];

    NSButton* verifyButton = [NSButton buttonWithTitle:LStr(@"Verify Local Data") target:self action:@selector(verifyLocalData:)];

    self.fVerifyIndicator = [[NSProgressIndicator alloc] init];
    self.fVerifyIndicator.controlSize = NSControlSizeSmall;
    self.fVerifyIndicator.style = NSProgressIndicatorStyleBar;
    self.fVerifyIndicator.displayedWhenStopped = NO;
    self.fVerifyIndicator.indeterminate = YES;
    self.fVerifyIndicator.maxValue = 1.0;

    NSButton* cancelButton = [NSButton buttonWithTitle:LStr(@"Cancel") target:self action:@selector(cancelAdd:)];
    cancelButton.keyEquivalent = @"\e";

    NSButton* addButton = [NSButton buttonWithTitle:LStr(@"Add") target:self action:@selector(add:)];
    addButton.keyEquivalent = @"\r";

    NSStackView* checksStack = [NSStackView stackViewWithViews:@[ self.fDeleteCheck, self.fStartCheck ]];
    checksStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    checksStack.alignment = NSLayoutAttributeLeading;
    checksStack.spacing = 6;

    NSStackView* verifyActionStack = [NSStackView stackViewWithViews:@[ verifyButton, self.fVerifyIndicator ]];
    verifyActionStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    verifyActionStack.alignment = NSLayoutAttributeCenterY;
    verifyActionStack.spacing = 8;

    NSStackView* leftFooterSide = [NSStackView stackViewWithViews:@[ checksStack, verifyActionStack ]];
    leftFooterSide.orientation = NSUserInterfaceLayoutOrientationVertical;
    leftFooterSide.alignment = NSLayoutAttributeLeading;
    leftFooterSide.spacing = 12;
    NSStackView* rightFooterSide = [NSStackView stackViewWithViews:@[ cancelButton, addButton ]];
    rightFooterSide.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    rightFooterSide.spacing = 12;
    NSStackView* footerStack = [NSStackView stackViewWithViews:@[ leftFooterSide, rightFooterSide ]];
    footerStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    footerStack.alignment = NSLayoutAttributeBottom;
    footerStack.distribution = NSStackViewDistributionGravityAreas;
    [footerStack setViews:@[ leftFooterSide, self.spacer ] inGravity:NSStackViewGravityLeading];
    [footerStack setViews:@[ rightFooterSide ] inGravity:NSStackViewGravityTrailing];

    // --- Main Stack ---
    NSStackView* mainStack = [NSStackView
        stackViewWithViews:@[ headerStack, self.fFileScrollView, filterStack, settingsBox, footerStack ]];
    mainStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    mainStack.spacing = 14;
    mainStack.alignment = NSLayoutAttributeLeading;
    mainStack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    window.contentView = mainStack;

    [NSLayoutConstraint activateConstraints:@[
        [self.fFileFilterField.widthAnchor constraintEqualToConstant:110],
        [self.fCheckAllButton.widthAnchor constraintEqualToAnchor:self.fUncheckAllButton.widthAnchor],
        [self.fLocationImageView.widthAnchor constraintEqualToConstant:16],
        [self.fLocationImageView.heightAnchor constraintEqualToConstant:16],
        [pathContainer.centerYAnchor constraintEqualToAnchor:pathBorderBox.centerYAnchor],
        [pathContainer.leadingAnchor constraintEqualToAnchor:pathBorderBox.leadingAnchor],
        [pathContainer.trailingAnchor constraintLessThanOrEqualToAnchor:pathBorderBox.trailingAnchor],
        [pathBorderBox.heightAnchor constraintEqualToConstant:24],
        [self.fVerifyIndicator.widthAnchor constraintEqualToConstant:120],

        [self.fIconView.widthAnchor constraintEqualToConstant:64],
        [self.fIconView.heightAnchor constraintEqualToConstant:64],

        [self.fGroupPopUp.widthAnchor constraintGreaterThanOrEqualToConstant:120],
        [self.fPriorityPopUp.widthAnchor constraintEqualToAnchor:self.fGroupPopUp.widthAnchor],
        [cancelButton.widthAnchor constraintEqualToAnchor:addButton.widthAnchor],
        [addButton.widthAnchor constraintGreaterThanOrEqualToConstant:76],

        [self.fFileScrollView.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20],
        [filterStack.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20],
        [settingsBox.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20],
        [footerStack.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20],
    ]];
}

- (void)configureContent
{
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateCheckButtons:) name:@"TorrentFileCheckChange"
                                             object:self.torrent];

    self.fFileController.torrent = self.torrent;

    NSString* name = self.torrent.name;
    self.window.title = name;
    self.fNameField.stringValue = name;
    self.fNameField.toolTip = name;

    self.fIconView.image = self.torrent.icon;

    if (!self.torrent.folder) {
        // Single-file torrents have nothing to filter or check; hiding the row lets the file list take its space.
        self.fFileFilterStack.hidden = YES;
    } else {
        [self updateCheckButtons:nil];
    }

    __weak __auto_type weakSelf = self;
    self.viewModel.onDataUpdate = ^(AddTorrentViewModel* viewModel) {
        weakSelf.fLocationField.stringValue = viewModel.guardedDestinationPath.stringByAbbreviatingWithTildeInPath;
        weakSelf.fLocationField.toolTip = viewModel.guardedDestinationPath;
        weakSelf.fLocationImageView.image = viewModel.locationIcon;
        [weakSelf.fGroupPopUp selectItemWithTag:viewModel.groupValue];
    };

    self.viewModel.onCancelTrigger = ^{
        [weakSelf cancelAdd:nil];
    };

    self.viewModel.onSuccessTrigger = ^{
        [weakSelf confirmAdd];
    };

    self.viewModel.onGroupsUpdate = ^(AddTorrentViewModel* viewModel) {
        weakSelf.fGroupPopUp.menu = [viewModel createGroupsMenuWithTarget:weakSelf action:@selector(changeGroupValue:)];
    };
    [self.viewModel triggerGroupsUpdate];

    [self.fPriorityPopUp selectItemAtIndex:self.viewModel.priorityIndex];

    self.fStartCheck.state = [NSUserDefaults.standardUserDefaults boolForKey:@"AutoStartDownload"] ? NSControlStateValueOn :
                                                                                                     NSControlStateValueOff;

    self.fDeleteCheck.state = self.fDeleteTorrentEnableInitially ? NSControlStateValueOn : NSControlStateValueOff;
    self.fDeleteCheck.enabled = self.fCanToggleDelete;

    [self.viewModel triggerDataUpdate];

    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf updateFiles];
    }];

    [self updateFiles];

    if (!self.viewModel.destinationPath) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.viewModel selectDestinationModalForWindow:self.window];
        });
    }
}

- (void)dealloc
{
    [_fTimer invalidate];
}

- (void)confirmAdd
{
    [self.fTimer invalidate];
    self.fTimer = nil;

    if (self.fTorrentFile && self.fCanToggleDelete && self.fDeleteCheck.state == NSControlStateValueOn) {
        [Torrent trashFile:self.fTorrentFile error:nil];
    }

    if (self.fStartCheck.state == NSControlStateValueOn) {
        [self.torrent startTransfer];
    }

    self.fFileController.torrent = nil; //avoid a crash when window tries to update

    [self close];
    [self.fController askOpenConfirmed:self add:YES];
}

- (void)setDestination:(id)sender
{
    [self.viewModel selectDestinationModalForWindow:self.window];
}

- (void)changeGroupValue:(id)sender
{
    [self.viewModel changeGroupValueToTag:[sender tag]];
}

- (void)changePriority:(id)sender
{
    [self.viewModel changePriorityToIndex:[sender indexOfSelectedItem]];
}

- (void)add:(id)sender
{
    [self.viewModel confirmAddWithWindow:self.window];
}

- (void)cancelAdd:(id)sender
{
    [self.window performClose:sender];
}

// only called on cancel
- (BOOL)windowShouldClose:(id)window
{
    [self.fTimer invalidate];
    self.fTimer = nil;
    self.fFileController.torrent = nil; // avoid a crash when window tries to update

    [self.fController askOpenConfirmed:self add:NO];
    return YES;
}

- (void)setFileFilterText:(id)sender
{
    self.fFileController.filterText = [sender stringValue];
}

- (void)checkAll:(id)sender
{
    [self.fFileController checkAll];
}

- (void)uncheckAll:(id)sender
{
    [self.fFileController uncheckAll];
}

- (void)verifyLocalData:(id)sender
{
    [self.torrent resetCache];
    [self updateFiles];
}

- (void)updateFiles
{
    [self.fFileController reloadVisibleRows];
    [self updateCheckButtons:nil]; // call in case button state changed by checking

    if (self.torrent.checking) {
        BOOL const waiting = self.torrent.checkingWaiting;
        self.fVerifyIndicator.indeterminate = waiting;
        if (waiting) {
            [self.fVerifyIndicator startAnimation:self];
        } else {
            self.fVerifyIndicator.doubleValue = self.torrent.checkingProgress.to_fraction();
        }
    } else {
        self.fVerifyIndicator.indeterminate = YES; // we want to hide when stopped, which only applies when indeterminate
        [self.fVerifyIndicator stopAnimation:self];
    }
}

- (void)updateCheckButtons:(NSNotification*)notification
{
    NSString* statusString = [NSString stringForFileSize:self.torrent.size];
    if (self.torrent.folder) {
        // keep synced with identical code in InfoFileViewController.mm
        NSControlStateValue const filesCheckState = [self.torrent
            checkForFiles:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.torrent.fileCount)]];
        self.fCheckAllButton.enabled = filesCheckState != NSControlStateValueOn; // if anything is unchecked
        self.fUncheckAllButton.enabled = !self.torrent.allDownloaded; // if there are any checked files that aren't finished

        NSString* fileString;
        NSUInteger count = self.torrent.fileCount;
        if (count != 1) {
            fileString = [NSString localizedStringWithFormat:NSLocalizedString(@"%lu files", "Add torrent -> info"), count];
        } else {
            fileString = NSLocalizedString(@"1 file", "Add torrent -> info");
        }

        NSString* selectedString = [NSString stringWithFormat:NSLocalizedString(@"%@ selected", "Add torrent -> info"),
                                                              [NSString stringForFileSize:self.torrent.totalSizeSelected]];

        statusString = [NSString stringWithFormat:@"%@, %@ (%@)", fileString, statusString, selectedString];
    }

    self.fStatusField.stringValue = statusString;
}

@end
