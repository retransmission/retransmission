// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cmath>
#include <future>
#include <optional>
#include <utility>

#include <libtransmission/error.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/web-utils.h> // tr_urlIsValidTracker()

#import "CreatorWindowController.h"
#import "Controller.h"
#import "NSStringAdditions.h"
#import "UTTypeAdditions.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"Creator", @"")

typedef NS_ENUM(NSUInteger, TrackerSegment) {
    TrackerSegmentAdd = 0,
    TrackerSegmentRemove = 1,
};

@interface CreatorWindowController ()<NSWindowRestoration, NSWindowDelegate, NSMenuItemValidation, NSTableViewDelegate, NSTableViewDataSource>

@property(nonatomic) NSImageView* fIconView;
@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fStatusField;
@property(nonatomic) NSTextField* fPiecesField;
@property(nonatomic) NSTextField* fLocationField;
@property(nonatomic) NSTableView* fTrackerTable;
@property(nonatomic) NSSegmentedControl* fTrackerAddRemoveControl;
@property(nonatomic) NSTextView* fCommentView;
@property(nonatomic) NSButton* fPrivateCheck;
@property(nonatomic) NSButton* fOpenCheck;
@property(nonatomic) NSTextField* fSource;
@property(nonatomic) NSStepper* fPieceSizeStepper;

@property(nonatomic) NSAlert* fProgressAlert;
@property(nonatomic) NSProgressIndicator* fProgressIndicator;

@property(nonatomic, readonly) std::shared_ptr<tr_metainfo_builder> fBuilder;
@property(nonatomic, readonly) NSURL* fPath;
@property(nonatomic) std::shared_future<tr_metainfo_builder::Checksums> fFuture;
@property(nonatomic) NSURL* fLocation; // path to new torrent file
@property(nonatomic) NSMutableArray<NSString*>* fTrackers;

@property(nonatomic) NSTimer* fTimer;
@property(nonatomic) BOOL fStarted;
@property(nonatomic) BOOL fOpenWhenCreated;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@end

static NSMutableSet* creatorWindowControllerSet;

@implementation CreatorWindowController

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle
{
    //get file/folder for torrent
    NSURL* path;
    if (!(path = [CreatorWindowController chooseFile])) {
        return nil;
    }

    CreatorWindowController* creator = [[self alloc] initWithHandle:handle path:path];
    [creator showWindow:nil];
    return creator;
}

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle forFile:(NSURL*)file
{
    CreatorWindowController* creator = [[self alloc] initWithHandle:handle path:file];
    [creator showWindow:nil];
    return creator;
}

- (instancetype)initWithHandle:(tr_session*)handle path:(NSURL*)path
{
    if ((self = [super initWithWindow:nil])) {
        if (!creatorWindowControllerSet) {
            creatorWindowControllerSet = [NSMutableSet set];
        }

        _fStarted = NO;

        _fPath = path;
        _fBuilder = std::make_shared<tr_metainfo_builder>(_fPath.path.UTF8String);

        if (_fBuilder->file_count() == 0U) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> no files -> button")];
            alert.messageText = NSLocalizedString(@"This folder contains no files.", "Create torrent -> no files -> title");
            alert.informativeText = NSLocalizedString(
                @"There must be at least one file in a folder to create a torrent file.",
                "Create torrent -> no files -> warning");
            alert.alertStyle = NSAlertStyleWarning;

            [alert runModal];

            return nil;
        }
        if (_fBuilder->total_size() == 0U) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> zero size -> button")];
            alert.messageText = NSLocalizedString(@"The total file size is zero bytes.", "Create torrent -> zero size -> title");
            alert.informativeText = NSLocalizedString(@"A torrent file cannot be created for files with no size.", "Create torrent -> zero size -> warning");
            alert.alertStyle = NSAlertStyleWarning;

            [alert runModal];

            return nil;
        }

        _fDefaults = NSUserDefaults.standardUserDefaults;

        //get list of trackers
        _fTrackers = [([_fDefaults arrayForKey:@"CreatorTrackers"] ?: @[]) mutableCopy];

        //remove potentially invalid addresses
        for (NSInteger i = _fTrackers.count - 1; i >= 0; i--) {
            if (!tr_urlIsValidTracker(_fTrackers[i].UTF8String)) {
                [_fTrackers removeObjectAtIndex:i];
            }
        }

        [creatorWindowControllerSet addObject:self];

        [self setupWindow];
    }
    return self;
}

- (NSView*)spacer
{
    NSView* spacer = [[NSView alloc] init];
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    return spacer;
}

- (void)setupWindow
{
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 520, 460)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = self.fPath.lastPathComponent;
    window.minSize = NSMakeSize(440, 380);
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    [window center];
    window.frameAutosaveName = @"CreatorWindow";
    window.delegate = self;
    self.window = window;

    NSStackView* mainStack = [NSStackView stackViewWithViews:@[]];
    mainStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    mainStack.alignment = NSLayoutAttributeWidth;
    mainStack.spacing = 14.0;
    mainStack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    window.contentView = mainStack;

    // ==========================================
    // Section: File's info
    // ==========================================
    NSImageView* iconView = [[NSImageView alloc] init];
    iconView.imageScaling = NSImageScaleProportionallyUpOrDown;
    self.fIconView = iconView;

    NSTextField* nameField = [NSTextField labelWithString:@""];
    nameField.font = [NSFont systemFontOfSize:16 weight:NSFontWeightMedium];
    nameField.lineBreakMode = NSLineBreakByTruncatingTail;
    self.fNameField = nameField;

    NSTextField* statusField = [NSTextField labelWithString:@""];
    statusField.font = [NSFont systemFontOfSize:12];
    statusField.textColor = [NSColor secondaryLabelColor];
    self.fStatusField = statusField;

    NSStackView* infoTextStack = [NSStackView stackViewWithViews:@[ nameField, statusField ]];
    infoTextStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    infoTextStack.alignment = NSLayoutAttributeLeading;
    infoTextStack.spacing = 4.0;

    NSStackView* topFileInfoStack = [NSStackView stackViewWithViews:@[ iconView, infoTextStack, self.spacer ]];
    topFileInfoStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    topFileInfoStack.alignment = NSLayoutAttributeCenterY;
    topFileInfoStack.spacing = 12.0;
    [mainStack addView:topFileInfoStack inGravity:NSStackViewGravityTop];

    // ==========================================
    // Section: Trackers table
    // ==========================================
    NSTableView* tableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
    tableView.delegate = self;
    tableView.dataSource = self;
    tableView.usesAlternatingRowBackgroundColors = YES;
    tableView.allowsMultipleSelection = YES;
    tableView.headerView = [[NSTableHeaderView alloc] initWithFrame:NSMakeRect(0, 0, 0, 24)];

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"TrackerColumn"];
    column.title = NSLocalizedString(@"Trackers", nil);
    [tableView addTableColumn:column];
    self.fTrackerTable = tableView;

    NSScrollView* tableScrollView = [[NSScrollView alloc] init];
    tableScrollView.hasVerticalScroller = YES;
    tableScrollView.borderType = NSBezelBorder;
    tableScrollView.documentView = tableView;
    [mainStack addView:tableScrollView inGravity:NSStackViewGravityCenter];

    NSSegmentedControl* trackerControl = [NSSegmentedControl segmentedControlWithLabels:@[ @"", @"" ]
                                                                           trackingMode:NSSegmentSwitchTrackingMomentary
                                                                                 target:self
                                                                                 action:@selector(addRemoveTracker:)];
    trackerControl.segmentStyle = NSSegmentStyleRoundRect;
    NSString* addTitle = NSLocalizedString(@"Add a tracker", "Create torrent -> tracker buttons");
    NSString* removeTitle = NSLocalizedString(@"Remove selected trackers", "Create torrent -> tracker buttons");
    [trackerControl setImage:[NSImage imageWithSystemSymbolName:@"plus" accessibilityDescription:addTitle] forSegment:0];
    [trackerControl setImage:[NSImage imageWithSystemSymbolName:@"minus" accessibilityDescription:removeTitle] forSegment:1];
    [trackerControl setWidth:32 forSegment:0];
    [trackerControl setWidth:32 forSegment:1];
    [trackerControl setEnabled:NO forSegment:TrackerSegmentRemove];
    self.fTrackerAddRemoveControl = trackerControl;

    NSTextField* trackerHintLabel = [NSTextField labelWithString:LStr(@"Each tracker will be placed into its own tier")];
    trackerHintLabel.font = [NSFont systemFontOfSize:11];
    trackerHintLabel.textColor = [NSColor disabledControlTextColor];

    NSStackView* trackerBarStack = [NSStackView stackViewWithViews:@[ trackerControl, trackerHintLabel, self.spacer ]];
    trackerBarStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    trackerBarStack.alignment = NSLayoutAttributeCenterY;
    trackerBarStack.spacing = 8.0;
    [mainStack addView:trackerBarStack inGravity:NSStackViewGravityCenter];

    // ==========================================
    // Section: Inputs
    // ==========================================

    NSTextField* commentTextField = [NSTextField labelWithString:LStr(@"Comment:")];
    NSStackView* commentTextFieldStack = [NSStackView stackViewWithViews:@[ commentTextField ]];
    commentTextFieldStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    commentTextFieldStack.alignment = NSLayoutAttributeCenterY;
    commentTextFieldStack.distribution = NSStackViewDistributionGravityAreas;
    commentTextFieldStack.spacing = 8.0;
    [commentTextFieldStack addView:self.spacer inGravity:NSStackViewGravityTrailing];
    [mainStack addView:commentTextFieldStack inGravity:NSStackViewGravityCenter];

    NSScrollView* commentScrollView = [NSTextView scrollableTextView];
    commentScrollView.borderType = NSBezelBorder;

    NSTextView* commentTextView = commentScrollView.documentView;
    commentTextView.richText = NO;
    commentTextView.importsGraphics = NO;
    commentTextView.allowsUndo = YES;
    commentTextView.continuousSpellCheckingEnabled = YES;
    commentTextView.font = [NSFont systemFontOfSize:13];
    self.fCommentView = commentTextView;

    [mainStack addView:commentScrollView inGravity:NSStackViewGravityCenter];

    NSTextField* sourceField = [[NSTextField alloc] init];
    sourceField.placeholderString = LStr(@"Source:");
    self.fSource = sourceField;
    [mainStack addView:sourceField inGravity:NSStackViewGravityCenter];

    // ==========================================
    // Section: Settings
    // ==========================================
    NSButton* privateCheck = [NSButton checkboxWithTitle:LStr(@"Private torrent") target:nil action:nil];
    self.fPrivateCheck = privateCheck;

    NSTextField* piecesField = [NSTextField labelWithString:@""];
    piecesField.font = [NSFont systemFontOfSize:12];
    self.fPiecesField = piecesField;

    NSStepper* pieceStepper = [[NSStepper alloc] init];
    pieceStepper.minValue = 14;
    pieceStepper.maxValue = 31;
    pieceStepper.autorepeat = NO;
    pieceStepper.target = self;
    pieceStepper.action = @selector(incrementOrDecrementPieceSize:);
    self.fPieceSizeStepper = pieceStepper;

    NSStackView* stepperStack = [NSStackView stackViewWithViews:@[ piecesField, pieceStepper ]];
    stepperStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stepperStack.alignment = NSLayoutAttributeCenterY;
    stepperStack.spacing = 4.0;

    NSStackView* optionsBarStack = [NSStackView stackViewWithViews:@[ privateCheck ]];
    optionsBarStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    optionsBarStack.alignment = NSLayoutAttributeCenterY;
    optionsBarStack.distribution = NSStackViewDistributionGravityAreas;
    [optionsBarStack setViews:@[ privateCheck, self.spacer ] inGravity:NSStackViewGravityLeading];
    [optionsBarStack setViews:@[ stepperStack ] inGravity:NSStackViewGravityTrailing];
    [mainStack addView:optionsBarStack inGravity:NSStackViewGravityCenter];

    NSBox* separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [mainStack addView:separator inGravity:NSStackViewGravityCenter];

    // ==========================================
    // Section: Location and bottom panel
    // ==========================================
    NSImageView* docIconView = [NSImageView imageViewWithImage:[NSImage imageNamed:@"TransmissionDocument"]];

    NSTextField* locationField = [NSTextField labelWithString:@""];
    locationField.font = [NSFont systemFontOfSize:11];
    locationField.lineBreakMode = NSLineBreakByCharWrapping;
    [locationField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                            forOrientation:NSLayoutConstraintOrientationHorizontal];
    self.fLocationField = locationField;

    NSButton* changeLocationButton = [NSButton buttonWithTitle:LStr(@"Change…") target:self action:@selector(setLocation:)];
    changeLocationButton.controlSize = NSControlSizeSmall;

    NSStackView* locationStack = [NSStackView stackViewWithViews:@[]];
    locationStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    locationStack.alignment = NSLayoutAttributeCenterY;
    locationStack.distribution = NSStackViewDistributionGravityAreas;
    locationStack.spacing = 6.0;
    [locationStack setViews:@[ docIconView, locationField ] inGravity:NSStackViewGravityLeading];
    [locationStack setViews:@[ self.spacer, changeLocationButton ] inGravity:NSStackViewGravityTrailing];
    [mainStack addView:locationStack inGravity:NSStackViewGravityBottom];

    NSButton* openCheck = [NSButton checkboxWithTitle:LStr(@"Open when created") target:nil action:nil];
    openCheck.state = NSControlStateValueOn;
    self.fOpenCheck = openCheck;

    NSButton* cancelButton = [NSButton buttonWithTitle:LStr(@"Cancel") target:self action:@selector(cancelCreateWindow:)];
    cancelButton.keyEquivalent = @"\e";
    NSButton* createButton = [NSButton buttonWithTitle:LStr(@"Create") target:self action:@selector(create:)];
    createButton.keyEquivalent = @"\r";

    NSStackView* actionButtonsStack = [NSStackView stackViewWithViews:@[ cancelButton, createButton ]];
    actionButtonsStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    actionButtonsStack.spacing = 12.0;

    NSStackView* bottomActionStack = [NSStackView stackViewWithViews:@[]];
    bottomActionStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    bottomActionStack.alignment = NSLayoutAttributeCenterY;
    bottomActionStack.distribution = NSStackViewDistributionGravityAreas;
    [bottomActionStack setViews:@[ openCheck, self.spacer ] inGravity:NSStackViewGravityLeading];
    [bottomActionStack setViews:@[ actionButtonsStack ] inGravity:NSStackViewGravityTrailing];
    [mainStack addView:bottomActionStack inGravity:NSStackViewGravityBottom];

    // ==========================================
    // Constraints
    // ==========================================
    [NSLayoutConstraint activateConstraints:@[
        [iconView.widthAnchor constraintEqualToConstant:64],
        [iconView.heightAnchor constraintEqualToConstant:64],

        [tableScrollView.heightAnchor constraintGreaterThanOrEqualToConstant:100],
        [commentScrollView.heightAnchor constraintEqualToConstant:60],

        [docIconView.widthAnchor constraintEqualToConstant:16],
        [docIconView.heightAnchor constraintEqualToConstant:16],
        [cancelButton.widthAnchor constraintEqualToAnchor:createButton.widthAnchor]
    ]];

    [tableScrollView setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
    [tableScrollView setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                              forOrientation:NSLayoutConstraintOrientationVertical];

    for (NSView* view in mainStack.arrangedSubviews) {
        if (view != tableScrollView) {
            [view setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
            [view setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
        }
    }

    [self configureContent];
}

- (void)configureContent
{
    self.window.identifier = @"CreatorWindow";
    self.window.restorationClass = [self class];

    NSString* name = self.fPath.lastPathComponent;

    self.window.title = name;

    //disable fullscreen support
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

    self.fNameField.stringValue = name;
    self.fNameField.toolTip = self.fPath.path;

    auto const is_folder = self.fBuilder->file_count() > 1 || self.fBuilder->path(0).contains('/');

    auto fileType = [UTType contentTypeForFilenameExtension:self.fPath.pathExtension isFolder:is_folder];

    self.fIconView.image = [NSWorkspace.sharedWorkspace iconForContentType:fileType];

    NSString* status_string = [NSString stringForFileSize:self.fBuilder->total_size()];
    if (is_folder) {
        NSUInteger const count = self.fBuilder->file_count();
        NSString* const fileString = count != 1 ?
            [NSString localizedStringWithFormat:NSLocalizedString(@"%lu files", "Create torrent -> info"), count] :
            NSLocalizedString(@"1 file", "Create torrent -> info");
        status_string = [NSString stringWithFormat:@"%@, %@", fileString, status_string];
    }
    self.fStatusField.stringValue = status_string;

    [self updatePiecesField];
    self.fPieceSizeStepper.intValue = static_cast<int>(log2(self.fBuilder->piece_size()));

    self.fLocation = [[self.fDefaults URLForKey:@"CreatorLocationURL"]
        URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"torrent"]];

    [self updateLocationField];

    //set previously saved values
    if ([self.fDefaults objectForKey:@"CreatorPrivate"]) {
        self.fPrivateCheck.state = [self.fDefaults boolForKey:@"CreatorPrivate"] ? NSControlStateValueOn : NSControlStateValueOff;
    }

    if ([self.fDefaults objectForKey:@"CreatorSource"]) {
        self.fSource.stringValue = [self.fDefaults stringForKey:@"CreatorSource"];
    }

    self.fOpenCheck.state = [self.fDefaults boolForKey:@"CreatorOpen"] ? NSControlStateValueOn : NSControlStateValueOff;

    //set tracker table column width to table width
    [self.fTrackerTable sizeToFit];
}

- (void)dealloc
{
    _fBuilder.reset();

    [_fTimer invalidate];
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSURL* path = [state decodeObjectForKey:@"TRCreatorPath"];
    if (!path || ![path checkResourceIsReachableAndReturnError:nil]) {
        completionHandler(nil, [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotOpenFile userInfo:nil]);
        return;
    }

    NSWindow* window = [self createTorrentFile:((Controller*)NSApp.delegate).sessionHandle forFile:path].window;
    completionHandler(window, nil);
}

- (void)window:(NSWindow*)window willEncodeRestorableState:(NSCoder*)state
{
    [state encodeObject:self.fPath forKey:@"TRCreatorPath"];
    [state encodeObject:self.fLocation forKey:@"TRCreatorLocation"];
    [state encodeObject:self.fTrackers forKey:@"TRCreatorTrackers"];
    [state encodeInteger:self.fOpenCheck.state forKey:@"TRCreatorOpenCheck"];
    [state encodeInteger:self.fPrivateCheck.state forKey:@"TRCreatorPrivateCheck"];
    [state encodeObject:self.fSource.stringValue forKey:@"TRCreatorSource"];
    [state encodeObject:self.fCommentView.string forKey:@"TRCreatorPrivateComment"];
}

- (void)window:(NSWindow*)window didDecodeRestorableState:(NSCoder*)coder
{
    self.fLocation = [coder decodeObjectForKey:@"TRCreatorLocation"];
    [self updateLocationField];

    self.fTrackers = [coder decodeObjectForKey:@"TRCreatorTrackers"];
    [self.fTrackerTable reloadData];

    self.fOpenCheck.state = [coder decodeIntegerForKey:@"TRCreatorOpenCheck"];
    self.fPrivateCheck.state = [coder decodeIntegerForKey:@"TRCreatorPrivateCheck"];
    self.fSource.stringValue = [coder decodeObjectForKey:@"TRCreatorSource"];
    self.fCommentView.string = [coder decodeObjectForKey:@"TRCreatorPrivateComment"];
}

- (IBAction)setLocation:(id)sender
{
    NSSavePanel* panel = [NSSavePanel savePanel];

    panel.prompt = NSLocalizedString(@"Select", "Create torrent -> location sheet -> button");
    panel.message = NSLocalizedString(@"Select the name and location for the torrent file.", "Create torrent -> location sheet -> message");

    panel.allowedContentTypes = @[ UTType.torrent ];
    panel.canSelectHiddenExtension = YES;

    panel.directoryURL = self.fLocation.URLByDeletingLastPathComponent;
    panel.nameFieldStringValue = self.fLocation.lastPathComponent;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            self.fLocation = panel.URL;
            [self updateLocationField];
        }
    }];
}

- (IBAction)create:(id)sender
{
    //make sure the trackers are no longer being verified
    if (self.fTrackerTable.editedRow != -1) {
        [self.window endEditingFor:self.fTrackerTable];
    }

    BOOL const isPrivate = self.fPrivateCheck.state == NSControlStateValueOn;
    if (self.fTrackers.count == 0 &&
        [self.fDefaults boolForKey:isPrivate ? @"WarningCreatorPrivateBlankAddress" : @"WarningCreatorBlankAddress"]) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"There are no tracker addresses.", "Create torrent -> blank address -> title");

        NSString* infoString = isPrivate ?
            NSLocalizedString(
                @"A torrent marked as private with no tracker addresses will be unable to connect to peers."
                 " The torrent file will only be useful if you plan to upload the file to a tracker website"
                 " that will add the addresses for you.",
                "Create torrent -> blank address -> message") :
            NSLocalizedString(
                @"The torrent will not contact trackers for peers, and will have to rely solely on"
                 " non-tracker peer discovery methods such as PEX and DHT to download and seed.",
                "Create torrent -> blank address -> message");

        alert.informativeText = infoString;
        [alert addButtonWithTitle:NSLocalizedString(@"Create", "Create torrent -> blank address -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Create torrent -> blank address -> button")];
        alert.showsSuppressionButton = YES;

        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
            if (alert.suppressionButton.state == NSControlStateValueOn) {
                [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningCreatorBlankAddress"]; //set regardless of private/public
                if (self.fPrivateCheck.state == NSControlStateValueOn) {
                    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningCreatorPrivateBlankAddress"];
                }
            }

            if (returnCode == NSAlertFirstButtonReturn) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self createReal];
                });
            }
        }];
    } else {
        [self createReal];
    }
}

- (IBAction)cancelCreateWindow:(id)sender
{
    [self.window close];
}

- (void)windowWillClose:(NSNotification*)notification
{
    [creatorWindowControllerSet removeObject:self];
}

- (void)handleProgressCancellation
{
    self.fBuilder->cancel_checksums();
    [self.fTimer fire];
}

- (IBAction)incrementOrDecrementPieceSize:(id)sender
{
    uint32_t const piece_size = 1U << [(NSStepper*)sender intValue];

    if (self.fBuilder->set_piece_size(piece_size)) {
        [self updatePiecesField];
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fTrackers.count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    return self.fTrackers[row];
}

- (IBAction)addRemoveTracker:(id)sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if (self.fTrackerTable.editedRow != -1) {
        return;
    }

    if ([sender selectedSegment] == TrackerSegmentRemove) {
        [self.fTrackers removeObjectsAtIndexes:self.fTrackerTable.selectedRowIndexes];

        [self.fTrackerTable deselectAll:self];
        [self.fTrackerTable reloadData];
    } else {
        [self.fTrackers addObject:@""];
        [self.fTrackerTable reloadData];

        NSInteger const row = self.fTrackers.count - 1;
        [self.fTrackerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [self.fTrackerTable editColumn:0 row:row withEvent:nil select:YES];
    }
}

- (void)tableView:(NSTableView*)tableView
    setObjectValue:(id)object
    forTableColumn:(NSTableColumn*)tableColumn
               row:(NSInteger)row
{
    NSString* tracker = (NSString*)object;

    tracker = [tracker stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];

    if ([tracker rangeOfString:@"://"].location == NSNotFound) {
        tracker = [@"http://" stringByAppendingString:tracker];
    }

    if (!tr_urlIsValidTracker(tracker.UTF8String)) {
        NSBeep();
        [self.fTrackers removeObjectAtIndex:row];
    } else {
        self.fTrackers[row] = tracker;
    }

    [self.fTrackerTable deselectAll:self];
    [self.fTrackerTable reloadData];
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fTrackerAddRemoveControl setEnabled:self.fTrackerTable.numberOfSelectedRows > 0 forSegment:TrackerSegmentRemove];
}

- (void)copy:(id)sender
{
    NSArray* addresses = [self.fTrackers objectsAtIndexes:self.fTrackerTable.selectedRowIndexes];
    NSString* text = [addresses componentsJoinedByString:@"\n"];

    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ text ]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    if (action == @selector(copy:)) {
        return self.window.firstResponder == self.fTrackerTable && self.fTrackerTable.numberOfSelectedRows > 0;
    }

    if (action == @selector(paste:)) {
        return self.window.firstResponder == self.fTrackerTable &&
            [NSPasteboard.generalPasteboard canReadObjectForClasses:@[ [NSString class] ] options:nil];
    }

    return YES;
}

- (void)paste:(id)sender
{
    NSMutableArray* tempTrackers = [NSMutableArray array];

    NSArray* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
    NSAssert(items != nil, @"no string items to paste; should not be able to call this method");

    for (NSString* pbItem in items) {
        for (NSString* tracker in [pbItem componentsSeparatedByString:@"\n"]) {
            [tempTrackers addObject:tracker];
        }
    }

    BOOL added = NO;

    for (__strong NSString* tracker in tempTrackers) {
        tracker = [tracker stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];

        if ([tracker rangeOfString:@"://"].location == NSNotFound) {
            tracker = [@"http://" stringByAppendingString:tracker];
        }

        if (tr_urlIsValidTracker(tracker.UTF8String)) {
            [self.fTrackers addObject:tracker];
            added = YES;
        }
    }

    if (added) {
        [self.fTrackerTable deselectAll:self];
        [self.fTrackerTable reloadData];
    } else {
        NSBeep();
    }
}

#pragma mark - Private

- (void)updatePiecesField
{
    auto const piece_size = self.fBuilder->piece_size();
    auto const piece_count = self.fBuilder->piece_count();

    if (piece_count == 1U) {
        self.fPiecesField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"1 piece, %@", "Create torrent -> info"),
                                                                   [NSString stringForFileSize:piece_size]];
    } else {
        self.fPiecesField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%u pieces, %@ each", "Create torrent -> info"),
                                                                   piece_count,
                                                                   [NSString stringForFileSize:piece_size]];
    }
}

- (void)updateLocationField
{
    NSString* pathString = self.fLocation.path;
    self.fLocationField.stringValue = pathString.stringByAbbreviatingWithTildeInPath;
    self.fLocationField.toolTip = pathString;
}

+ (NSURL*)chooseFile
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.title = NSLocalizedString(@"Create Torrent File", "Create torrent -> select file");
    panel.prompt = NSLocalizedString(@"Select", "Create torrent -> select file");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = NO;

    panel.message = NSLocalizedString(@"Select a file or folder for the torrent file.", "Create torrent -> select file");

    BOOL success = [panel runModal] == NSModalResponseOK;
    return success ? panel.URLs[0] : nil;
}

- (void)createReal
{
    //check if the location currently exists
    if (![self.fLocation.URLByDeletingLastPathComponent checkResourceIsReachableAndReturnError:NULL]) {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> directory doesn't exist warning -> button")];
        alert.messageText = NSLocalizedString(@"The chosen torrent file location does not exist.", "Create torrent -> directory doesn't exist warning -> title");
        alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                               @"The directory \"%@\" does not currently exist. "
                                                                "Create this directory or choose a different one to create the torrent file.",
                                                               "Create torrent -> directory doesn't exist warning -> warning"),
                                                           self.fLocation.URLByDeletingLastPathComponent.path];
        alert.alertStyle = NSAlertStyleWarning;

        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    //check if a file with the same name and location already exists
    if ([self.fLocation checkResourceIsReachableAndReturnError:NULL]) {
        NSArray* pathComponents = self.fLocation.pathComponents;
        NSInteger count = pathComponents.count;

        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> file already exists warning -> button")];
        alert.messageText = NSLocalizedString(
            @"A torrent file with this name and directory cannot be created.",
            "Create torrent -> file already exists warning -> title");
        alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                               @"A file with the name \"%@\" already exists in the directory \"%@\". "
                                                                "Choose a new name or directory to create the torrent file.",
                                                               "Create torrent -> file already exists warning -> warning"),
                                                           pathComponents[count - 1],
                                                           pathComponents[count - 2]];
        alert.alertStyle = NSAlertStyleWarning;

        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    // trackers
    auto trackers = tr_announce_list{};
    for (NSUInteger i = 0; i < self.fTrackers.count; ++i) {
        trackers.add((char*)(self.fTrackers[i]).UTF8String, trackers.nextTier());
    }
    self.fBuilder->set_announce_list(std::move(trackers));

    //store values
    [self.fDefaults setObject:self.fTrackers forKey:@"CreatorTrackers"];
    [self.fDefaults setBool:self.fPrivateCheck.state == NSControlStateValueOn forKey:@"CreatorPrivate"];
    [self.fDefaults setObject:self.fSource.stringValue forKey:@"CreatorSource"];
    [self.fDefaults setBool:self.fOpenCheck.state == NSControlStateValueOn forKey:@"CreatorOpen"];
    self.fOpenWhenCreated = self.fOpenCheck.state ==
        NSControlStateValueOn; //need this since the check box might not exist, and value in prefs might have changed from another creator window
    [self.fDefaults setURL:self.fLocation.URLByDeletingLastPathComponent forKey:@"CreatorLocationURL"];

    self.window.restorable = NO;

    [NSNotificationCenter.defaultCenter postNotificationName:@"BeginCreateTorrentFile" object:self.fLocation userInfo:nil];

    self.fBuilder->set_comment(self.fCommentView.string.UTF8String);
    self.fBuilder->set_private(self.fPrivateCheck.state == NSControlStateValueOn);
    self.fBuilder->set_source(self.fSource.stringValue.UTF8String);

    self.fFuture = self.fBuilder->make_checksums();

    __weak __auto_type weakSelf = self;
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf checkProgress];
    }];

    [self startProgress];
}

- (void)configureProgressAlert
{
    self.fProgressAlert = [[NSAlert alloc] init];
    self.fProgressAlert.messageText = LStr(@"Creating torrent…");
    self.fProgressAlert.alertStyle = NSAlertStyleInformational;

    NSButton* cancelButton = [self.fProgressAlert addButtonWithTitle:LStr(@"Cancel")];
    cancelButton.target = self;
    cancelButton.action = @selector(handleProgressCancellation);
    cancelButton.keyEquivalent = @"\e"; // Esc

    self.fProgressIndicator = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 300, 20)];
    self.fProgressIndicator.indeterminate = NO;
    self.fProgressIndicator.minValue = 0.0;
    self.fProgressIndicator.maxValue = 1.0;
    self.fProgressIndicator.doubleValue = 0.0;
    self.fProgressIndicator.style = NSProgressIndicatorStyleBar;

    self.fProgressAlert.accessoryView = self.fProgressIndicator;
}

- (void)startProgress
{
    [self configureProgressAlert];
    [self.fProgressAlert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)checkProgress
{
    auto const is_done = !self.fFuture.valid() || self.fFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;

    if (!is_done) {
        auto const [current, total] = self.fBuilder->checksum_status();
        self.fProgressIndicator.doubleValue = static_cast<double>(current) / total;

        if (!self.fStarted) {
            self.fStarted = YES;
            [self.window standardWindowButton:NSWindowCloseButton].enabled = NO;
        }
        return;
    }

    // stop the timer
    [self.fTimer invalidate];
    self.fTimer = nil;

    if (self.fProgressAlert) {
        [self.window endSheet:self.fProgressAlert.window returnCode:NSModalResponseStop];
        self.fProgressAlert = nil;
    }

    auto checksums = self.fFuture.get();
    auto error = std::move(checksums.error);
    if (!error) {
        self.fBuilder->set_piece_hashes(std::move(checksums.piece_hashes));
        self.fBuilder->save(self.fLocation.path.UTF8String, &error);
    }

    if (error) {
        auto* const alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> failed -> button")];
        alert.messageText = [NSString stringWithFormat:NSLocalizedString(@"Creation of \"%@\" failed.", "Create torrent -> failed -> title"),
                                                       self.fLocation.lastPathComponent];
        alert.alertStyle = NSAlertStyleWarning;

        alert.informativeText = [NSString
            stringWithFormat:@"%.*s (%d)", static_cast<int>(std::size(error.message())), std::data(error.message()), error.code()];
        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse /*returnCode*/) {
            [self.window close];
        }];
    } else {
        if (self.fOpenWhenCreated) {
            NSDictionary* dict = @{ @"File" : self.fLocation.path, @"Path" : self.fPath.URLByDeletingLastPathComponent.path };
            [NSNotificationCenter.defaultCenter postNotificationName:@"OpenCreatedTorrentFile" object:self userInfo:dict];
        }

        [self.window close];
    }
}

@end
