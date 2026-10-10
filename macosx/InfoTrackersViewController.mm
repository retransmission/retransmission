// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/macros.h"
#import "InfoTrackersViewController.h"
#import "Torrent.h"
#import "TrackerNode.h"
#import "TrackersViews.h"

static CGFloat const kTrackerGroupSeparatorHeight = 14.0;
typedef NS_ENUM(NSInteger, TrackerSegmentTag) {
    TrackerSegmentTagAdd = 0,
    TrackerSegmentTagRemove = 1,
};

@interface InfoTrackersViewController ()<NSTableViewDataSource, NSTableViewDelegate, NSMenuItemValidation, TrackerInputViewDelegate>
@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;
@property(nonatomic) BOOL fSet;
@property(nonatomic) NSMutableArray* fTrackers;
@property(nonatomic) IBOutlet NSTableView* fTrackerTable;
@property(nonatomic) IBOutlet NSSegmentedControl* fTrackerAddRemoveControl;
@end

@implementation InfoTrackersViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoTrackersView" bundle:nil])) {
        self.title = NSLocalizedString(@"Trackers", "Inspector view -> title");
    }
    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [self.fTrackerAddRemoveControl.cell setToolTip:NSLocalizedString(@"Add a tracker", "Inspector view -> tracker buttons")
                                        forSegment:TrackerSegmentTagAdd];
    [self.fTrackerAddRemoveControl.cell setToolTip:NSLocalizedString(@"Remove selected trackers", "Inspector view -> tracker buttons")
                                        forSegment:TrackerSegmentTagRemove];

    [self.fTrackerTable tableColumnWithIdentifier:@"Tracker"].maxWidth = 5000;

    CGFloat const height = [NSUserDefaults.standardUserDefaults floatForKey:@"InspectorContentHeightTracker"];
    if (height != 0.0) {
        NSRect viewRect = self.view.frame;
        viewRect.size.height = height;
        self.view.frame = viewRect;
    }
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self.fTrackerTable sizeToFit];
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    if ([self isAddingTrackerRightNow]) {
        [self.fTrackers removeObjectsInRange:NSMakeRange(self.fTrackers.count - 2, 2)];
        [self.fTrackerTable reloadData];
    }
    self.fTorrents = torrents;
    self.fSet = NO;
}

- (BOOL)isAddingTrackerRightNow
{
    id lastItem = self.fTrackers.lastObject;
    return [lastItem isKindOfClass:[NSString class]] && [(NSString*)lastItem isEqualToString:@""];
}

- (void)updateInfo
{
    if (!self.fSet) {
        [self setupInfo];
    }

    if (self.fTorrents.count == 0) {
        return;
    }

    if (![self isAddingTrackerRightNow]) {
        NSArray* oldTrackers = self.fTrackers;

        if (self.fTorrents.count == 1) {
            self.fTrackers = self.fTorrents[0].allTrackerStats;
        } else {
            self.fTrackers = [[NSMutableArray alloc] init];
            for (Torrent* torrent in self.fTorrents) {
                [self.fTrackers addObjectsFromArray:torrent.allTrackerStats];
            }
        }

        if (oldTrackers && [self.fTrackers isEqualToArray:oldTrackers]) {
            [self.fTrackerTable reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTrackers.count)]
                                          columnIndexes:[NSIndexSet indexSetWithIndex:0]];
        } else {
            [self.fTrackerTable reloadData];
        }
    } else {
        NSAssert1(self.fTorrents.count == 1, @"Attempting to add tracker with %ld transfers selected", self.fTorrents.count);

        NSIndexSet* addedIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(self.fTrackers.count - 2, 2)];
        NSArray* tierAndTrackerBeingAdded = [self.fTrackers objectsAtIndexes:addedIndexes];

        self.fTrackers = self.fTorrents[0].allTrackerStats;
        [self.fTrackers addObjectsFromArray:tierAndTrackerBeingAdded];

        NSIndexSet *updateIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTrackers.count - 2)],
                   *columnIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTrackerTable.tableColumns.count)];
        [self.fTrackerTable reloadDataForRowIndexes:updateIndexes columnIndexes:columnIndexes];
    }
}

- (void)saveViewSize
{
    [NSUserDefaults.standardUserDefaults setFloat:NSHeight(self.view.frame) forKey:@"InspectorContentHeightTracker"];
}

- (void)clearView
{
    self.fTrackers = nil;
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fTrackers ? self.fTrackers.count : 0;
}

#pragma mark - NSTableViewDelegate

- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row
{
    if ([self.fTrackers[row] isKindOfClass:[NSDictionary class]]) {
        return kTrackerGroupSeparatorHeight;
    } else {
        return tableView.rowHeight;
    }
}

- (BOOL)isTierRow:(NSInteger)row
{
    id node = self.fTrackers[row];
    return [node isKindOfClass:[NSDictionary class]];
}

- (nullable NSString*)tierLabelForRow:(NSInteger)row
{
    id item = self.fTrackers[row];
    if (![item isKindOfClass:[NSDictionary class]]) {
        return nil;
    }

    NSInteger const tier = [item[@"Tier"] integerValue];
    NSString* tierString = tier == -1 ? NSLocalizedString(@"New Tier", "Inspector -> tracker table") :
                                        [NSString stringWithFormat:NSLocalizedString(@"Tier %ld", "Inspector -> tracker table"), tier];

    if (self.fTorrents.count > 1) {
        tierString = [tierString stringByAppendingFormat:@" - %@", item[@"Name"]];
    }
    return tierString;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    id item = self.fTrackers[row];

    if ([self isTierRow:row]) {
        TrackerTierView* tierView = [tableView makeViewWithIdentifier:@"TrackerTierRowView" owner:self];
        if (tierView == nil) {
            tierView = [[TrackerTierView alloc] initWithFrame:NSZeroRect];
            tierView.identifier = @"TrackerTierRowView";
        }
        [tierView setTier:[self tierLabelForRow:row]];
        return tierView;
    }

    if ([item isKindOfClass:[NSString class]] && [item isEqualToString:@""]) {
        TrackerInputView* inputView = [tableView makeViewWithIdentifier:@"TrackerInputRowView" owner:self];
        if (inputView == nil) {
            inputView = [[TrackerInputView alloc] initWithFrame:NSZeroRect];
            inputView.identifier = @"TrackerInputRowView";
        }
        inputView.textFieldView.stringValue = @"";
        inputView.delegate = self;
        return inputView;
    }

    TrackerView* cellView = [tableView makeViewWithIdentifier:@"TrackerRowView" owner:self];
    if (cellView == nil) {
        cellView = [[TrackerView alloc] initWithFrame:NSZeroRect];
        cellView.identifier = @"TrackerRowView";
    }

    if ([item isKindOfClass:[TrackerNode class]]) {
        cellView.toolTip = [(TrackerNode*)item fullAnnounceAddress];
    }

    [cellView configureWithNode:item];
    return cellView;
}

- (BOOL)tableView:(NSTableView*)tableView shouldEditTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    return NO;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fTrackerAddRemoveControl setEnabled:self.fTrackerTable.numberOfSelectedRows > 0 forSegment:TrackerSegmentTagRemove];
}

- (BOOL)tableView:(NSTableView*)tableView isGroupRow:(NSInteger)row
{
    return NO;
}

- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row
{
    return [self.fTrackers[row] isKindOfClass:[TrackerNode class]];
}

#pragma mark - TrackerInputViewDelegate

- (void)trackerInputView:(TrackerInputView*)inputView didCommitAddress:(NSString*)address
{
    Torrent* torrent = self.fTorrents[0];
    BOOL added = NO;

    for (NSString* tracker in [address componentsSeparatedByString:@"\n"]) {
        if ([torrent addTrackerToNewTier:tracker]) {
            added = YES;
        }
    }

    if (!added) {
        NSBeep();
    }

    self.fTrackers = torrent.allTrackerStats;
    [self.fTrackerTable reloadData];
    [self.fTrackerTable deselectAll:self];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil]; //in case sort by tracker
}

- (void)trackerInputViewDidCancel:(TrackerInputView*)inputView
{
    if (self.fTrackers.count >= 2) {
        [self.fTrackers removeLastObject];
        [self.fTrackers removeLastObject];
    }

    [self.fTrackerTable reloadData];
    [self.fTrackerTable deselectAll:self];

    [self setupInfo];
}

#pragma mark - Actions

- (IBAction)addRemoveTracker:(id)sender
{
    if ([self isAddingTrackerRightNow]) {
        return;
    }

    [self updateInfo];

    if ([[sender cell] tagForSegment:[sender selectedSegment]] == TrackerSegmentTagRemove) {
        [self removeTrackers];
    } else {
        [self addTrackers];
    }
}

#pragma mark - Private

- (void)setupInfo
{
    NSUInteger const numberSelected = self.fTorrents.count;
    if (numberSelected != 1) {
        if (numberSelected == 0) {
            self.fTrackers = nil;
            [self.fTrackerTable reloadData];
        }

        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagAdd];
        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagRemove];
    } else {
        [self.fTrackerAddRemoveControl setEnabled:YES forSegment:TrackerSegmentTagAdd];
        [self.fTrackerAddRemoveControl setEnabled:NO forSegment:TrackerSegmentTagRemove];
    }
    [self.fTrackerTable deselectAll:self];
    self.fSet = YES;
}

- (void)addTrackers
{
    NSAssert1(self.fTorrents.count == 1, @"Attempting to add tracker with %ld transfers selected", self.fTorrents.count);
    [self.fTrackers addObject:@{ @"Tier" : @-1 }];
    [self.fTrackers addObject:@""];
    [self.fTrackerTable reloadData];
    NSInteger const newRow = self.fTrackers.count - 1;
    [self.fTrackerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:newRow] byExtendingSelection:NO];
    dispatch_async(dispatch_get_main_queue(), ^{
        TrackerInputView* inputCell = [self.fTrackerTable viewAtColumn:0 row:newRow makeIfNecessary:YES];
        if ([inputCell isKindOfClass:[TrackerInputView class]]) {
            [self.view.window makeFirstResponder:inputCell.textFieldView];
        }
    });
}

- (void)removeTrackers
{
    NSMutableDictionary* removeIdentifiers = [NSMutableDictionary dictionaryWithCapacity:self.fTorrents.count];
    NSUInteger removeTrackerCount = 0;
    NSIndexSet* selectedIndexes = self.fTrackerTable.selectedRowIndexes;
    BOOL groupSelected = NO;
    NSUInteger groupRowIndex = NSNotFound;
    NSMutableIndexSet* removeIndexes = [NSMutableIndexSet indexSet];
    for (NSUInteger i = 0; i < self.fTrackers.count; ++i) {
        id object = self.fTrackers[i];
        if ([object isKindOfClass:[TrackerNode class]]) {
            TrackerNode* node = (TrackerNode*)object;
            if (groupSelected || [selectedIndexes containsIndex:i]) {
                Torrent* torrent = node.torrent;
                NSMutableSet* removeSet;
                if (!(removeSet = removeIdentifiers[torrent])) {
                    removeSet = [NSMutableSet set];
                    removeIdentifiers[torrent] = removeSet;
                }
                [removeSet addObject:node.fullAnnounceAddress];
                ++removeTrackerCount;
                [removeIndexes addIndex:i];
            } else {
                groupRowIndex = NSNotFound;
            }
        } else {
            if (groupRowIndex != NSNotFound) {
                [removeIndexes addIndex:groupRowIndex];
            }
            groupSelected = [selectedIndexes containsIndex:i];
            if (!groupSelected && i > selectedIndexes.lastIndex) {
                groupRowIndex = NSNotFound;
                break;
            }
            groupRowIndex = i;
        }
    }
    if (groupRowIndex != NSNotFound) {
        [removeIndexes addIndex:groupRowIndex];
    }
    NSAssert2(
        removeTrackerCount <= removeIndexes.count,
        @"Marked %ld trackers to remove, but only removing %ld rows",
        removeTrackerCount,
        removeIndexes.count);
    if (removeTrackerCount == 0) {
        return;
    }
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"WarningRemoveTrackers"]) {
        NSAlert* alert = [[NSAlert alloc] init];
        if (removeTrackerCount > 1) {
            alert.messageText = [NSString
                localizedStringWithFormat:NSLocalizedString(@"Are you sure you want to remove %lu trackers?", "Remove trackers alert -> title"),
                                          removeTrackerCount];
            alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                                   @"Once removed, %@ will no longer attempt to contact them."
                                                                    " This cannot be undone.",
                                                                   "Remove trackers alert -> message"),
                                                               @TR_PROJ_APPNAME_CAPITALIZED];
        } else {
            alert.messageText = NSLocalizedString(@"Are you sure you want to remove this tracker?", "Remove trackers alert -> title");
            alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                                   @"Once removed, %@ will no longer attempt to contact it."
                                                                    " This cannot be undone.",
                                                                   "Remove trackers alert -> message"),
                                                               @TR_PROJ_APPNAME_CAPITALIZED];
        }
        [alert addButtonWithTitle:NSLocalizedString(@"Remove", nil)];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
        alert.showsSuppressionButton = YES;
        NSInteger result = [alert runModal];
        if (alert.suppressionButton.state == NSControlStateValueOn) {
            [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningRemoveTrackers"];
        }
        if (result != NSAlertFirstButtonReturn) {
            return;
        }
    }
    [self.fTrackerTable beginUpdates];
    for (Torrent* torrent in removeIdentifiers) {
        [torrent removeTrackers:removeIdentifiers[torrent]];
    }
    self.fTrackers = [[NSMutableArray alloc] init];
    for (Torrent* torrent in self.fTorrents) {
        [self.fTrackers addObjectsFromArray:torrent.allTrackerStats];
    }
    [self.fTrackerTable removeRowsAtIndexes:removeIndexes withAnimation:NSTableViewAnimationSlideLeft];
    [self.fTrackerTable endUpdates];
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

- (void)copy:(id)sender
{
    NSMutableArray* addresses = [NSMutableArray arrayWithCapacity:self.fTrackers.count];
    NSIndexSet* indexes = self.fTrackerTable.selectedRowIndexes;
    [indexes enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* _Nonnull stop) {
        id item = self.fTrackers[idx];
        if ([item isKindOfClass:[TrackerNode class]]) {
            [addresses addObject:((TrackerNode*)item).fullAnnounceAddress];
        }
    }];
    NSString* text = [addresses componentsJoinedByString:@"\n"];
    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ text ]];
}

- (void)paste:(id)sender
{
    NSAssert(self.fTorrents.count == 1, @"no torrent but trying to paste");
    if (self.fTorrents.count != 1)
        return;
    Torrent* torrent = self.fTorrents[0];
    BOOL added = NO;
    NSArray* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
    for (NSString* pbItem in items) {
        for (NSString* item in [pbItem componentsSeparatedByString:@"\n"]) {
            if ([torrent addTrackerToNewTier:item]) {
                added = YES;
            }
        }
    }
    if (!added) {
        NSBeep();
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;
    if (action == @selector(copy:)) {
        return self.fTrackerTable.numberOfSelectedRows > 0;
    }
    if (action == @selector(paste:)) {
        return self.fTorrents.count == 1 && [NSPasteboard.generalPasteboard canReadObjectForClasses:@[ [NSString class] ] options:nil];
    }
    return YES;
}

@end
