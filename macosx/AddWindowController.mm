// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddWindowController.h"
#import "AddTorrentViewModel.h"
#import "Controller.h"
#import "FileOutlineController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

static NSTimeInterval const kUpdateSeconds = 1.0;

@interface AddWindowController ()

@property(nonatomic) IBOutlet NSImageView* fIconView;
@property(nonatomic) IBOutlet NSImageView* fLocationImageView;
@property(nonatomic) IBOutlet NSTextField* fNameField;
@property(nonatomic) IBOutlet NSTextField* fStatusField;
@property(nonatomic) IBOutlet NSTextField* fLocationField;
@property(nonatomic) IBOutlet NSButton* fStartCheck;
@property(nonatomic) IBOutlet NSButton* fDeleteCheck;
@property(nonatomic) IBOutlet NSPopUpButton* fGroupPopUp;
@property(nonatomic) IBOutlet NSPopUpButton* fPriorityPopUp;
@property(nonatomic) IBOutlet NSProgressIndicator* fVerifyIndicator;

@property(nonatomic) IBOutlet NSTextField* fFileFilterField;
@property(nonatomic) IBOutlet NSButton* fCheckAllButton;
@property(nonatomic) IBOutlet NSButton* fUncheckAllButton;

@property(nonatomic) IBOutlet FileOutlineController* fFileController;
@property(nonatomic) IBOutlet NSScrollView* fFileScrollView;

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

- (instancetype)initWithTorrent:(Torrent*)torrent
                          destination:(NSString*)path
                      lockDestination:(BOOL)lockDestination
                           controller:(Controller*)controller
                          torrentFile:(NSString*)torrentFile
    deleteTorrentCheckEnableInitially:(BOOL)deleteTorrent
                      canToggleDelete:(BOOL)canToggleDelete
{
    if ((self = [super initWithWindowNibName:@"AddWindow"])) {
        _viewModel = [[AddTorrentViewModel alloc] initWithTorrent:torrent destination:path lockDestination:lockDestination];

        _fController = controller;

        _fTorrentFile = torrentFile.stringByExpandingTildeInPath;
        _fDeleteTorrentEnableInitially = deleteTorrent;
        _fCanToggleDelete = canToggleDelete;
    }
    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateCheckButtons:) name:@"TorrentFileCheckChange"
                                             object:self.torrent];

    self.fFileController.torrent = self.torrent;

    NSString* name = self.torrent.name;
    self.window.title = name;
    self.fNameField.stringValue = name;
    self.fNameField.toolTip = name;

    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    self.fIconView.image = self.torrent.icon;

    if (!self.torrent.folder) {
        self.fFileFilterField.hidden = YES;
        self.fCheckAllButton.hidden = YES;
        self.fUncheckAllButton.hidden = YES;

        NSRect scrollFrame = self.fFileScrollView.frame;
        CGFloat const diff = NSMinY(self.fFileScrollView.frame) - NSMinY(self.fFileFilterField.frame);
        scrollFrame.origin.y -= diff;
        scrollFrame.size.height += diff;
        self.fFileScrollView.frame = scrollFrame;
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
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    // if there is no destination, prompt for one right away
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
    [self.fTimer invalidate];
    self.fTimer = nil;
    self.fFileController.torrent = nil; // avoid a crash when window tries to update

    [self.fController askOpenConfirmed:self add:NO];
    return YES;
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

- (IBAction)verifyLocalData:(id)sender
{
    [self.torrent resetCache];
    [self updateFiles];
}

- (void)updateFiles
{
    [self.torrent update];
    [self.fFileController reloadVisibleRows];
    [self updateCheckButtons:nil]; // call in case button state changed by checking

    if (self.torrent.checking) {
        BOOL const waiting = self.torrent.checkingWaiting;
        self.fVerifyIndicator.indeterminate = waiting;
        if (waiting) {
            [self.fVerifyIndicator startAnimation:self];
        } else {
            self.fVerifyIndicator.doubleValue = self.torrent.checkingProgress;
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
