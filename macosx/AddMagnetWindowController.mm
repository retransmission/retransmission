// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddMagnetWindowController.h"
#import "AddTorrentViewModel.h"
#import "Controller.h"
#import "Torrent.h"
#import "GroupPopUpButtonCell.h"
#import "PriorityPopUpButtonCell.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"AddMagnetWindow", @"")

@interface AddMagnetWindowController ()<NSWindowDelegate>

@property(nonatomic) NSImageView* fLocationImageView;
@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fLocationField;
@property(nonatomic) NSButton* fStartCheck;
@property(nonatomic) NSPopUpButton* fGroupPopUp;
@property(nonatomic) NSPopUpButton* fPriorityPopUp;

@property(nonatomic, readonly) Controller* fController;
@property(nonatomic) AddTorrentViewModel* viewModel;

@end

@implementation AddMagnetWindowController

- (Torrent*)torrent
{
    return self.viewModel.torrent;
}

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller
{
    if ((self = [super initWithWindow:nil])) {
        _fController = controller;
        _viewModel = [[AddTorrentViewModel alloc] initWithTorrent:torrent destination:path lockDestination:NO];
        [self setupWindow];
        [self configureContent];
    }
    return self;
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

- (void)setupWindow
{
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 440, 260)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.restorable = NO;
    window.releasedWhenClosed = NO;
    [window center];
    window.frameAutosaveName = @"AddMagnetWindow";
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    window.delegate = self;

    self.window = window;

    NSImageView* magnetIcon = [NSImageView imageViewWithImage:[NSImage imageNamed:@"Magnet"]];
    [magnetIcon.widthAnchor constraintEqualToConstant:16].active = YES;
    [magnetIcon.heightAnchor constraintEqualToConstant:16].active = YES;

    NSTextField* titleLabel = [NSTextField labelWithString:LStr(@"Magnet Link")];
    titleLabel.font = [NSFont systemFontOfSize:[NSFont systemFontSize] weight:NSFontWeightBold];

    self.fNameField = [NSTextField labelWithString:@""];
    self.fNameField.font = [NSFont systemFontOfSize:18];
    self.fNameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.fNameField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                              forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.fStartCheck = [NSButton checkboxWithTitle:LStr(@"Start when added") target:nil action:nil];

    NSButton* cancelButton = [NSButton buttonWithTitle:LStr(@"Cancel") target:self action:@selector(cancelAdd:)];
    cancelButton.keyEquivalent = @"\e"; // Escape

    NSButton* addButton = [NSButton buttonWithTitle:LStr(@"Add") target:self action:@selector(add:)];
    addButton.keyEquivalent = @"\r"; // Enter

    NSTextField* destLabel = [NSTextField labelWithString:LStr(@"Destination folder:")];
    NSTextField* groupLabel = [NSTextField labelWithString:LStr(@"Group:")];
    NSTextField* priorityLabel = [NSTextField labelWithString:LStr(@"Priority:")];

    self.fLocationImageView = [NSImageView imageViewWithImage:[NSImage imageNamed:@"NSApplicationIcon"]];
    [self.fLocationImageView.widthAnchor constraintEqualToConstant:16].active = YES;
    [self.fLocationImageView.heightAnchor constraintEqualToConstant:16].active = YES;

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

    [pathContainer.centerYAnchor constraintEqualToAnchor:pathBorderBox.centerYAnchor].active = YES;
    [pathContainer.leadingAnchor constraintEqualToAnchor:pathBorderBox.leadingAnchor].active = YES;
    [pathContainer.trailingAnchor constraintLessThanOrEqualToAnchor:pathBorderBox.trailingAnchor].active = YES;

    [pathBorderBox.heightAnchor constraintEqualToConstant:24].active = YES;

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

    gridView.rowSpacing = 12;
    gridView.columnSpacing = 10;

    [gridView columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [gridView columnAtIndex:1].xPlacement = NSGridCellPlacementFill;
    [gridView columnAtIndex:2].xPlacement = NSGridCellPlacementTrailing;

    gridView.yPlacement = NSGridCellPlacementCenter;

    NSBox* settingsBox = [[NSBox alloc] init];
    settingsBox.titlePosition = NSNoTitle;
    settingsBox.contentView = gridView;

    NSStackView* headerStack = [NSStackView stackViewWithViews:@[ magnetIcon, titleLabel ]];
    headerStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    headerStack.spacing = 8;
    headerStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* footerStack = [NSStackView stackViewWithViews:@[ self.fStartCheck, cancelButton, addButton ]];
    footerStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    footerStack.spacing = 12;
    footerStack.distribution = NSStackViewDistributionGravityAreas;
    [footerStack setViews:@[ self.fStartCheck ] inGravity:NSStackViewGravityLeading];
    [footerStack setViews:@[ cancelButton, addButton ] inGravity:NSStackViewGravityTrailing];

    NSStackView* mainStack = [NSStackView stackViewWithViews:@[ headerStack, self.fNameField, settingsBox ]];
    [mainStack addView:footerStack inGravity:NSStackViewGravityBottom];
    mainStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    mainStack.spacing = 16;
    mainStack.alignment = NSLayoutAttributeLeading;
    mainStack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);

    window.contentView = mainStack;

    [self.fGroupPopUp.widthAnchor constraintGreaterThanOrEqualToConstant:100].active = YES;
    [self.fPriorityPopUp.widthAnchor constraintEqualToAnchor:self.fGroupPopUp.widthAnchor].active = YES;

    [cancelButton.widthAnchor constraintEqualToAnchor:addButton.widthAnchor].active = YES;
    [addButton.widthAnchor constraintGreaterThanOrEqualToConstant:76].active = YES;

    [self.fNameField.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20].active = YES;
    [settingsBox.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20].active = YES;
    [footerStack.trailingAnchor constraintEqualToAnchor:mainStack.trailingAnchor constant:-20].active = YES;

    window.minSize = NSMakeSize(440, 260);
}

- (void)configureContent
{
    NSString* name = self.viewModel.torrent.name;
    self.window.title = name;
    self.fNameField.stringValue = name;
    self.fNameField.toolTip = name;

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

    [self.viewModel triggerDataUpdate];

    // if there is no destination, prompt for one right away
    if (!self.viewModel.destinationPath) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.viewModel selectDestinationModalForWindow:self.window];
        });
    }
}

- (void)confirmAdd
{
    if (self.fStartCheck.state == NSControlStateValueOn) {
        [self.torrent startMagnetTransferAfterMetaDownload];
    }

    [self close];
    [self.fController askOpenMagnetConfirmed:self add:YES];
}

- (IBAction)setDestination:(id)sender
{
    [self.viewModel selectDestinationModalForWindow:self.window];
}

- (IBAction)changeGroupValue:(id)sender
{
    [self.viewModel changeGroupValueToTag:[sender tag]];
}

- (IBAction)changePriority:(id)sender
{
    [self.viewModel changePriorityToIndex:[sender indexOfSelectedItem]];
}

- (IBAction)add:(id)sender
{
    [self.viewModel confirmAddWithWindow:self.window];
}

- (IBAction)cancelAdd:(id)sender
{
    [self.window performClose:sender];
}

// only called on cancel
- (BOOL)windowShouldClose:(id)window
{
    [self.fController askOpenMagnetConfirmed:self add:NO];
    return YES;
}

@end
