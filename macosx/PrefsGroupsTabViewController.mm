// PrefsGroupsTabViewController.mm
// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>

#import "PrefsGroupsTabViewController.h"
#import "GroupsController.h"
#import "PrefsUI.h"
#import "ColorTextField.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"
#import "GroupRulesWindowController.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesGroupsTab", @"")

static NSString* const kGroupTableViewDataType = @"GroupTableViewDataType";

typedef NS_ENUM(NSInteger, SegmentTag) {
    SegmentTagAdd = 0,
    SegmentTagRemove = 1,
};

@interface PrefsGroupsTabViewController ()<NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>

@property(nonatomic) NSSplitView* splitView;

@property(nonatomic) NSTableView* fTableView;
@property(nonatomic) NSSegmentedControl* fAddRemoveControl;

@property(nonatomic) NSColorWell* fSelectedColorView;
@property(nonatomic) NSTextField* fSelectedColorNameField;
@property(nonatomic) NSButton* fCustomLocationEnableCheck;
@property(nonatomic) NSPopUpButton* fCustomLocationPopUp;
@property(nonatomic) NSButton* fAutoAssignRulesEnableCheck;
@property(nonatomic) NSButton* fAutoAssignRulesEditButton;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic) GroupRulesWindowController* rulesWindowController;

@end

@implementation PrefsGroupsTabViewController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _fHandle = handle;
        _fDefaults = NSUserDefaults.standardUserDefaults;
    }
    return self;
}

- (void)loadView
{
    NSView* containerView = [[NSView alloc] init];
    containerView.translatesAutoresizingMaskIntoConstraints = NO;

    [self setupSplitView];
    [containerView addSubview:self.splitView];

    NSView* leftPane = [self makeLeftPane];
    NSView* rightPane = [self makeRightPane];

    [self.splitView addArrangedSubview:leftPane];
    [self.splitView addArrangedSubview:rightPane];

    self.view = containerView;
    [self setupConstraintsWithLeftPane:leftPane rightPane:rightPane];

    [self updateSelectedGroup];
}

- (void)setupSplitView
{
    self.splitView = [[NSSplitView alloc] init];
    self.splitView.translatesAutoresizingMaskIntoConstraints = NO;
    self.splitView.vertical = YES;
    self.splitView.dividerStyle = NSSplitViewDividerStyleThin;
}

- (NSView*)makeLeftPane
{
    NSView* leftPane = [[NSView alloc] init];
    leftPane.translatesAutoresizingMaskIntoConstraints = NO;

    NSScrollView* tableScrollView = [[NSScrollView alloc] init];
    tableScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    tableScrollView.hasVerticalScroller = YES;
    tableScrollView.borderType = NSBezelBorder;

    self.fTableView = [[NSTableView alloc] init];
    self.fTableView.headerView = nil;
    self.fTableView.allowsEmptySelection = NO;
    self.fTableView.dataSource = self;
    self.fTableView.delegate = self;
    [self.fTableView registerForDraggedTypes:@[ kGroupTableViewDataType ]];

    NSTableColumn* nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"GroupName"];
    [self.fTableView addTableColumn:nameColumn];
    tableScrollView.documentView = self.fTableView;
    [leftPane addSubview:tableScrollView];

    self.fAddRemoveControl = [[NSSegmentedControl alloc] init];
    self.fAddRemoveControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.fAddRemoveControl.segmentCount = 2;
    self.fAddRemoveControl.trackingMode = NSSegmentSwitchTrackingMomentary;
    [self.fAddRemoveControl setImage:[NSImage imageNamed:NSImageNameAddTemplate] forSegment:SegmentTagAdd];
    [self.fAddRemoveControl setImage:[NSImage imageNamed:NSImageNameRemoveTemplate] forSegment:SegmentTagRemove];
    self.fAddRemoveControl.target = self;
    self.fAddRemoveControl.action = @selector(addRemoveGroup:);
    [leftPane addSubview:self.fAddRemoveControl];

    [NSLayoutConstraint activateConstraints:@[
        [tableScrollView.topAnchor constraintEqualToAnchor:leftPane.topAnchor],
        [tableScrollView.leadingAnchor constraintEqualToAnchor:leftPane.leadingAnchor],
        [tableScrollView.trailingAnchor constraintEqualToAnchor:leftPane.trailingAnchor],
        [tableScrollView.bottomAnchor constraintEqualToAnchor:self.fAddRemoveControl.topAnchor constant:-6],
        [self.fAddRemoveControl.leadingAnchor constraintEqualToAnchor:leftPane.leadingAnchor],
        [self.fAddRemoveControl.bottomAnchor constraintEqualToAnchor:leftPane.bottomAnchor]
    ]];

    return leftPane;
}

- (NSView*)makeRightPane
{
    NSView* rightPane = [[NSView alloc] init];
    rightPane.translatesAutoresizingMaskIntoConstraints = NO;

    self.fSelectedColorView = [[NSColorWell alloc] init];
    [self.fSelectedColorView.widthAnchor constraintEqualToConstant:44].active = YES;
    [self.fSelectedColorView.heightAnchor constraintEqualToConstant:24].active = YES;
    if (@available(macOS 13.0, *)) {
        self.fSelectedColorView.colorWellStyle = NSColorWellStyleMinimal;
    }
    [self.fSelectedColorView addObserver:self forKeyPath:@"color" options:0 context:NULL];

    self.fSelectedColorNameField = [[NSTextField alloc] init];
    [self.fSelectedColorNameField.widthAnchor constraintEqualToConstant:271].active = YES;
    self.fSelectedColorNameField.placeholderString = LStr(@"Group Name:");
    self.fSelectedColorNameField.delegate = self;

    NSStackView* headerStack = [NSStackView stackViewWithViews:@[ self.fSelectedColorView, self.fSelectedColorNameField ]];
    headerStack.spacing = 8;
    headerStack.alignment = NSLayoutAttributeCenterY;

    self.fCustomLocationEnableCheck = [PrefsUI checkbox:LStr(@"Custom location:") target:self
                                                 action:@selector(toggleUseCustomDownloadLocation:)];
    self.fCustomLocationPopUp = [PrefsUI popUpButton];
    [self.fCustomLocationPopUp addItemWithTitle:@""];
    [self.fCustomLocationPopUp.menu addItem:[NSMenuItem separatorItem]];
    [self.fCustomLocationPopUp addItemWithTitle:LStr(@"Other…")];
    self.fCustomLocationPopUp.lastItem.target = self;
    self.fCustomLocationPopUp.lastItem.action = @selector(customDownloadLocationSheetShow:);

    NSStackView* locationStack = [NSStackView
        stackViewWithViews:@[ self.fCustomLocationEnableCheck, self.fCustomLocationPopUp ]];
    locationStack.spacing = 8;
    locationStack.alignment = NSLayoutAttributeCenterY;

    self.fAutoAssignRulesEnableCheck = [PrefsUI checkbox:LStr(@"Assign group to new torrents based on criteria") target:self
                                                  action:@selector(toggleUseAutoAssignRules:)];
    self.fAutoAssignRulesEditButton = [[NSButton alloc] init];
    self.fAutoAssignRulesEditButton.title = LStr(@"Edit…");
    self.fAutoAssignRulesEditButton.bezelStyle = NSBezelStyleRounded;
    self.fAutoAssignRulesEditButton.target = self;
    self.fAutoAssignRulesEditButton.action = @selector(orderFrontRulesSheet:);

    NSTextField* footerLabel = [PrefsUI smallLabel:LStr(@"Torrents will be assigned to the first group where all criteria are met")];
    footerLabel.translatesAutoresizingMaskIntoConstraints = NO;
    footerLabel.lineBreakMode = NSLineBreakByWordWrapping;
    footerLabel.maximumNumberOfLines = 0;
    footerLabel.preferredMaxLayoutWidth = 400;

    NSTextField* locationHintLabel = [PrefsUI smallLabel:LStr(@"The location will only be set when adding the torrent")];
    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ headerStack ],
        @[ locationStack ],
        @[ [PrefsUI indentedView:locationHintLabel] ],
        @[ self.fAutoAssignRulesEnableCheck ],
        @[ [PrefsUI indentedView:self.fAutoAssignRulesEditButton] ],
        @[ [PrefsUI indentedView:footerLabel] ]
    ]];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 12;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;
    [rightPane addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:rightPane.topAnchor constant:4],
        [gridView.leadingAnchor constraintEqualToAnchor:rightPane.leadingAnchor constant:16],
        [gridView.trailingAnchor constraintEqualToAnchor:rightPane.trailingAnchor constant:-4],
        [gridView.bottomAnchor constraintLessThanOrEqualToAnchor:rightPane.bottomAnchor]
    ]];

    return rightPane;
}

#pragma mark - Constraints Setup

- (void)setupConstraintsWithLeftPane:(NSView*)leftPane rightPane:(NSView*)rightPane
{
    [NSLayoutConstraint activateConstraints:@[
        [self.splitView.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:10],
        [self.splitView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [self.splitView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
        [self.splitView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-40],

        [leftPane.widthAnchor constraintGreaterThanOrEqualToConstant:140],
    ]];

    [self.view setFrameSize:NSMakeSize(562, 280)];
}

- (void)dealloc
{
    [self.fSelectedColorView removeObserver:self forKeyPath:@"color"];
}

#pragma mark - NSTableViewDataSource & Drag and Drop

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableview
{
    return GroupsController.groups.numberOfGroups;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    NSTableCellView* cellView = [tableView makeViewWithIdentifier:@"GroupCell" owner:self];
    if (!cellView) {
        cellView = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cellView.identifier = @"GroupCell";
        NSImageView* imageView = [[NSImageView alloc] init];
        imageView.translatesAutoresizingMaskIntoConstraints = NO;
        [cellView addSubview:imageView];
        cellView.imageView = imageView;
        NSTextField* textField = [NSTextField labelWithString:@""];
        textField.translatesAutoresizingMaskIntoConstraints = NO;
        [cellView addSubview:textField];
        cellView.textField = textField;
        [NSLayoutConstraint activateConstraints:@[
            [imageView.leadingAnchor constraintEqualToAnchor:cellView.leadingAnchor constant:4],
            [imageView.centerYAnchor constraintEqualToAnchor:cellView.centerYAnchor],
            [imageView.widthAnchor constraintEqualToConstant:12],
            [imageView.heightAnchor constraintEqualToConstant:12],
            [textField.leadingAnchor constraintEqualToAnchor:imageView.trailingAnchor constant:8],
            [textField.trailingAnchor constraintEqualToAnchor:cellView.trailingAnchor constant:-4],
            [textField.centerYAnchor constraintEqualToAnchor:cellView.centerYAnchor]
        ]];
    }
    NSInteger const groupsIndex = [GroupsController.groups indexForRow:row];
    cellView.imageView.image = [GroupsController.groups imageForIndex:groupsIndex];
    cellView.textField.stringValue = [GroupsController.groups nameForIndex:groupsIndex];
    return cellView;
}

- (BOOL)tableView:(NSTableView*)tableView writeRowsWithIndexes:(NSIndexSet*)rowIndexes toPasteboard:(NSPasteboard*)pboard
{
    [pboard declareTypes:@[ kGroupTableViewDataType ] owner:self];
    [pboard setData:[NSKeyedArchiver archivedDataWithRootObject:rowIndexes requiringSecureCoding:YES error:nil]
            forType:kGroupTableViewDataType];
    return YES;
}

- (NSDragOperation)tableView:(NSTableView*)tableView
                validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)operation
{
    if ([info.draggingPasteboard.types containsObject:kGroupTableViewDataType]) {
        [self.fTableView setDropRow:row dropOperation:NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    return NSDragOperationNone;
}

- (BOOL)tableView:(NSTableView*)tableView
       acceptDrop:(id<NSDraggingInfo>)info
              row:(NSInteger)newRow
    dropOperation:(NSTableViewDropOperation)operation
{
    if ([info.draggingPasteboard.types containsObject:kGroupTableViewDataType]) {
        NSIndexSet* indexes = [NSKeyedUnarchiver unarchivedObjectOfClass:NSIndexSet.class fromData:[info.draggingPasteboard dataForType:kGroupTableViewDataType]
                                                                   error:nil];
        NSInteger oldRow = indexes.firstIndex;
        if (oldRow < newRow) {
            newRow--;
        }
        [self.fTableView beginUpdates];
        [GroupsController.groups moveGroupAtRow:oldRow toRow:newRow];
        [self.fTableView moveRowAtIndex:oldRow toIndex:newRow];
        [self.fTableView endUpdates];
    }
    return YES;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self updateSelectedGroup];
}

#pragma mark - Actions & Logic

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context
{
    if (object == self.fSelectedColorView && self.fTableView.numberOfSelectedRows == 1) {
        NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        [GroupsController.groups setColor:self.fSelectedColorView.color forIndex:index];
        [self.fTableView reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:self.fTableView.selectedRow]
                                   columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (notification.object == self.fSelectedColorNameField && self.fTableView.numberOfSelectedRows == 1) {
        NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        [GroupsController.groups setName:self.fSelectedColorNameField.stringValue forIndex:index];
        [self.fTableView reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:self.fTableView.selectedRow]
                                   columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
}

- (void)addRemoveGroup:(id)sender
{
    if (NSColorPanel.sharedColorPanelExists) {
        [NSColorPanel.sharedColorPanel close];
    }
    NSInteger row;
    switch ([sender selectedSegment]) {
    case SegmentTagAdd:
        [self.fTableView beginUpdates];
        [GroupsController.groups addNewGroup];
        row = self.fTableView.numberOfRows;
        [self.fTableView insertRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [self.fTableView endUpdates];
        [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [self.fTableView scrollRowToVisible:row];
        [self.fSelectedColorNameField.window makeFirstResponder:self.fSelectedColorNameField];
        break;
    case SegmentTagRemove:
        row = self.fTableView.selectedRow;
        [self.fTableView beginUpdates];
        [GroupsController.groups removeGroupWithRowIndex:row];
        [self.fTableView removeRowsAtIndexes:[NSIndexSet indexSetWithIndex:row] withAnimation:NSTableViewAnimationSlideUp];
        [self.fTableView endUpdates];
        if (self.fTableView.numberOfRows > 0) {
            if (row == self.fTableView.numberOfRows) {
                --row;
            }
            [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [self.fTableView scrollRowToVisible:row];
        }
        break;
    }
    [self updateSelectedGroup];
}

- (void)customDownloadLocationSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSInteger result) {
        NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        if (result == NSModalResponseOK) {
            NSString* path = panel.URLs[0].path;
            [GroupsController.groups setCustomDownloadLocation:path forIndex:index];
            [GroupsController.groups setUsesCustomDownloadLocation:YES forIndex:index];
        } else {
            if (![GroupsController.groups customDownloadLocationForIndex:index]) {
                [GroupsController.groups setUsesCustomDownloadLocation:NO forIndex:index];
            }
        }
        [self refreshCustomLocationWithSingleGroup];
        [self.fCustomLocationPopUp selectItemAtIndex:0];
    }];
}

- (void)toggleUseCustomDownloadLocation:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    if (self.fCustomLocationEnableCheck.state == NSControlStateValueOn) {
        if ([GroupsController.groups customDownloadLocationForIndex:index]) {
            [GroupsController.groups setUsesCustomDownloadLocation:YES forIndex:index];
        } else {
            [self customDownloadLocationSheetShow:nil];
        }
    } else {
        [GroupsController.groups setUsesCustomDownloadLocation:NO forIndex:index];
    }
    self.fCustomLocationPopUp.enabled = (self.fCustomLocationEnableCheck.state == NSControlStateValueOn);
}

- (void)toggleUseAutoAssignRules:(id)sender
{
    NSInteger index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    if (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn) {
        if ([GroupsController.groups autoAssignRulesForIndex:index]) {
            [GroupsController.groups setUsesAutoAssignRules:YES forIndex:index];
        } else {
            [self orderFrontRulesSheet:nil];
        }
    } else {
        [GroupsController.groups setUsesAutoAssignRules:NO forIndex:index];
    }
    self.fAutoAssignRulesEditButton.enabled = (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn);
}

- (void)orderFrontRulesSheet:(id)sender
{
    NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    NSPredicate* predicate = [GroupsController.groups autoAssignRulesForIndex:index];

    self.rulesWindowController = [[GroupRulesWindowController alloc]
        initWithPredicate:predicate completionHandler:^(BOOL saved, NSPredicate* finalPredicate) {
            [self.view.window endSheet:self.rulesWindowController.window];

            NSInteger const currentIndex = [GroupsController.groups indexForRow:self.fTableView.selectedRow];

            if (saved) {
                BOOL const hasRules = (finalPredicate != nil);
                [GroupsController.groups setUsesAutoAssignRules:hasRules forIndex:currentIndex];
                [GroupsController.groups setAutoAssignRules:finalPredicate forIndex:currentIndex];
            } else {
                if (![GroupsController.groups autoAssignRulesForIndex:currentIndex]) {
                    [GroupsController.groups setUsesAutoAssignRules:NO forIndex:currentIndex];
                }
            }

            self.fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:currentIndex];
            self.fAutoAssignRulesEditButton.enabled = (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn);

            self.rulesWindowController = nil;
        }];

    [self.view.window beginSheet:self.rulesWindowController.window completionHandler:nil];
}

#pragma mark - Private State Management

- (void)updateSelectedGroup
{
    [self.fAddRemoveControl setEnabled:self.fTableView.numberOfSelectedRows > 0 forSegment:SegmentTagRemove];
    if (self.fTableView.numberOfSelectedRows == 1) {
        NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
        self.fSelectedColorView.color = [GroupsController.groups colorForIndex:index];
        self.fSelectedColorView.enabled = YES;
        self.fSelectedColorNameField.stringValue = [GroupsController.groups nameForIndex:index];
        self.fSelectedColorNameField.enabled = YES;
        [self refreshCustomLocationWithSingleGroup];
        self.fAutoAssignRulesEnableCheck.state = [GroupsController.groups usesAutoAssignRulesForIndex:index];
        self.fAutoAssignRulesEnableCheck.enabled = YES;
        self.fAutoAssignRulesEditButton.enabled = (self.fAutoAssignRulesEnableCheck.state == NSControlStateValueOn);
    } else {
        self.fSelectedColorView.color = NSColor.whiteColor;
        self.fSelectedColorView.enabled = NO;
        self.fSelectedColorNameField.stringValue = @"";
        self.fSelectedColorNameField.enabled = NO;
        self.fCustomLocationEnableCheck.enabled = NO;
        self.fCustomLocationPopUp.enabled = NO;
        self.fAutoAssignRulesEnableCheck.enabled = NO;
        self.fAutoAssignRulesEditButton.enabled = NO;
    }
}

- (void)refreshCustomLocationWithSingleGroup
{
    NSInteger const index = [GroupsController.groups indexForRow:self.fTableView.selectedRow];
    BOOL const hasCustomLocation = [GroupsController.groups usesCustomDownloadLocationForIndex:index];
    self.fCustomLocationEnableCheck.state = hasCustomLocation;
    self.fCustomLocationEnableCheck.enabled = YES;
    self.fCustomLocationPopUp.enabled = hasCustomLocation;
    NSString* location = [GroupsController.groups customDownloadLocationForIndex:index];
    if (location) {
        ExpandedPathToPathTransformer* pathTransformer = [[ExpandedPathToPathTransformer alloc] init];
        [self.fCustomLocationPopUp itemAtIndex:0].title = [pathTransformer transformedValue:location];
        ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
        [self.fCustomLocationPopUp itemAtIndex:0].image = [iconTransformer transformedValue:location];
    } else {
        [self.fCustomLocationPopUp itemAtIndex:0].title = @"";
        [self.fCustomLocationPopUp itemAtIndex:0].image = nil;
    }
}
@end
