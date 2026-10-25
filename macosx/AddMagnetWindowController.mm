// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddMagnetWindowController.h"
#import "AddTorrentViewModel.h"
#import "Controller.h"
#import "Torrent.h"

@interface AddMagnetWindowController ()

@property(nonatomic) IBOutlet NSImageView* fLocationImageView;
@property(nonatomic) IBOutlet NSTextField* fNameField;
@property(nonatomic) IBOutlet NSTextField* fLocationField;
@property(nonatomic) IBOutlet NSButton* fStartCheck;
@property(nonatomic) IBOutlet NSPopUpButton* fGroupPopUp;
@property(nonatomic) IBOutlet NSPopUpButton* fPriorityPopUp;

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
    if ((self = [super initWithWindowNibName:@"AddMagnetWindow"])) {
        _fController = controller;
        _viewModel = [[AddTorrentViewModel alloc] initWithTorrent:torrent destination:path lockDestination:NO];
    }
    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];

    NSString* name = self.viewModel.torrent.name;
    self.window.title = name;
    self.fNameField.stringValue = name;
    self.fNameField.toolTip = name;

    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

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
