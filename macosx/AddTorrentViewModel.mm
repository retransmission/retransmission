// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddTorrentViewModel.h"
#import "Torrent.h"
#import "ExpandedPathToIconTransformer.h"
#import "GroupsController.h"

typedef NS_ENUM(NSUInteger, PopupPriority) {
    PopupPriorityHigh = 0,
    PopupPriorityNormal = 1,
    PopupPriorityLow = 2,
};

@interface AddTorrentViewModel ()

@property(nonatomic, readwrite) Torrent* torrent;
@property(nonatomic, readwrite) NSString* destinationPath;
@property(nonatomic, readwrite) NSImage* locationIcon;
@property(nonatomic, readwrite) NSInteger groupValue;
@property(nonatomic, readwrite) BOOL lockDestination;

@property(nonatomic) TorrentDeterminationType groupDeterminationType;

@end

@implementation AddTorrentViewModel

- (NSString*)guardedDestinationPath
{
    return self.destinationPath ?: @"";
}

- (void)triggerDataUpdate
{
    if (self.onDataUpdate) {
        self.onDataUpdate(self);
    }
}

- (void)triggerGroupsUpdate
{
    if (self.onGroupsUpdate) {
        self.onGroupsUpdate(self);
    }
}

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path lockDestination:(BOOL)lockDestination
{
    self = [super init];
    if (self) {
        _torrent = torrent;
        _lockDestination = lockDestination;
        _groupValue = torrent.groupValue;
        _groupDeterminationType = TorrentDeterminationAutomatic;

        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroupMenu:) name:@"UpdateGroups" object:nil];

        if (path) {
            [self updateDestinationPath:path
                      determinationType:(lockDestination ? TorrentDeterminationUserSpecified : TorrentDeterminationAutomatic)];
        }
    }
    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

#pragma mark - UI API

- (void)selectDestinationModalForWindow:(NSWindow*)window
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.prompt = NSLocalizedString(@"Select", "Open torrent -> prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.message = [NSString stringWithFormat:NSLocalizedString(@"Select the download folder for \"%@\"", "Add -> select destination folder"),
                                               self.torrent.name];

    __weak __auto_type weakSelf = self;
    [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSModalResponseOK) {
            weakSelf.lockDestination = YES;
            [weakSelf updateDestinationPath:panel.URLs[0].path determinationType:TorrentDeterminationUserSpecified];
        } else if (!weakSelf.destinationPath && weakSelf.onCancelTrigger) {
            dispatch_async(dispatch_get_main_queue(), weakSelf.onCancelTrigger);
        }
    }];
}

- (NSInteger)priorityIndex
{
    switch (self.torrent.priority) {
    case TR_PRI_HIGH:
        return PopupPriorityHigh;
    case TR_PRI_NORMAL:
        return PopupPriorityNormal;
    case TR_PRI_LOW:
        return PopupPriorityLow;
    default:
        NSAssert1(NO, @"Unknown priority for adding torrent: %d", self.torrent.priority);
        return PopupPriorityNormal;
    }
}
- (void)changePriorityToIndex:(NSInteger)index
{
    tr_priority_t priority;
    switch (index) {
    case PopupPriorityHigh:
        priority = TR_PRI_HIGH;
        break;
    case PopupPriorityNormal:
        priority = TR_PRI_NORMAL;
        break;
    case PopupPriorityLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown priority index for adding torrent: %ld", index);
        priority = TR_PRI_NORMAL;
    }
    self.torrent.priority = priority;
}

- (void)changeGroupValueToTag:(NSInteger)tag
{
    NSInteger previousGroup = self.groupValue;
    self.groupValue = tag;
    self.groupDeterminationType = TorrentDeterminationUserSpecified;

    if (!self.lockDestination) {
        if ([GroupsController.groups usesCustomDownloadLocationForIndex:self.groupValue]) {
            [self updateDestinationPath:[GroupsController.groups customDownloadLocationForIndex:self.groupValue]
                      determinationType:TorrentDeterminationAutomatic];
        } else if ([self.destinationPath isEqualToString:[GroupsController.groups customDownloadLocationForIndex:previousGroup]]) {
            [self updateDestinationPath:[NSUserDefaults.standardUserDefaults stringForKey:@"DownloadFolder"]
                      determinationType:TorrentDeterminationAutomatic];
        }
    } else {
        [self triggerDataUpdate];
    }
}

- (NSMenu*)createGroupsMenuWithTarget:(id)target action:(SEL)action
{
    return [GroupsController.groups groupMenuWithTarget:target action:action isSmall:NO];
}

- (void)confirmAddWithWindow:(NSWindow*)window
{
    if ([self.destinationPath.lastPathComponent isEqualToString:self.torrent.name] &&
        [NSUserDefaults.standardUserDefaults boolForKey:@"WarningFolderDataSameName"]) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"The destination directory and root data directory have the same name.", "Add torrent -> same name -> title");
        alert.informativeText = NSLocalizedString(
            @"If you are attempting to use already existing data,"
             " the root data directory should be inside the destination directory.",
            "Add torrent -> same name -> message");
        alert.alertStyle = NSAlertStyleWarning;
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Add torrent -> same name -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Add", "Add torrent -> same name -> button")];
        alert.showsSuppressionButton = YES;

        __weak __auto_type weakSelf = self;
        [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse returnCode) {
            if (alert.suppressionButton.state == NSControlStateValueOn) {
                [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningFolderDataSameName"];
            }
            if (returnCode == NSAlertSecondButtonReturn) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [weakSelf executeFinalAdd];
                });
            }
        }];
    } else {
        [self executeFinalAdd];
    }
}

#pragma mark - Private

- (void)updateDestinationPath:(NSString*)path determinationType:(TorrentDeterminationType)determinationType
{
    NSString* expandedPath = path.stringByExpandingTildeInPath;
    if (!self.destinationPath || ![self.destinationPath isEqualToString:expandedPath] || determinationType == TorrentDeterminationUserSpecified) {
        self.destinationPath = expandedPath;
        [self.torrent changeDownloadFolderBeforeUsing:self.destinationPath determinationType:determinationType];
    }

    ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    self.locationIcon = [iconTransformer transformedValue:self.destinationPath];

    [self triggerDataUpdate];
}

- (void)updateGroupMenu:(NSNotification*)notification
{
    if (self.groupValue != -1 && [GroupsController.groups rowValueForIndex:self.groupValue] == -1) {
        self.groupValue = -1;
        self.groupDeterminationType = TorrentDeterminationAutomatic;
    }

    [self triggerGroupsUpdate];

    [self triggerDataUpdate];
}

- (void)executeFinalAdd
{
    [self.torrent setGroupValue:self.groupValue determinationType:self.groupDeterminationType];
    if (self.onSuccessTrigger) {
        self.onSuccessTrigger();
    }
}

@end
