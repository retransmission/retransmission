// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>
#import "PrefsTransfersAddingViewController.h"
#import "PrefsUI.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesTransfersTab", @"")

typedef NS_ENUM(NSUInteger, DownloadPopupIndex) {
    DownloadPopupIndexFolder = 0,
    DownloadPopupIndexTorrent = 2,
};

@interface PrefsTransfersAddingViewController ()

@property(nonatomic) NSPopUpButton* fFolderPopUp;
@property(nonatomic) NSButton* fDeleteOriginalTorrentButton;
@property(nonatomic) NSButton* fRenamePartialFilesButton;
@property(nonatomic) NSButton* fUseIncompleteFolderButton;
@property(nonatomic) NSPopUpButton* fIncompleteFolderPopUp;
@property(nonatomic) NSButton* fAutoStartDownloadButton;
@property(nonatomic) NSButton* fDownloadAskButton;
@property(nonatomic) NSButton* fDownloadAskMultiButton;
@property(nonatomic) NSButton* fDownloadAskManualButton;
@property(nonatomic) NSButton* fShowMagnetAddWindowCheck;
@property(nonatomic) NSButton* fAutoImportButton;
@property(nonatomic) NSPopUpButton* fImportFolderPopUp;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@end

@implementation PrefsTransfersAddingViewController

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
    NSView* contentView = [[NSView alloc] init];
    contentView.translatesAutoresizingMaskIntoConstraints = NO;
    [containerView addSubview:contentView];

    [self createUIElements];
    [self setupLayoutInView:contentView];

    [NSLayoutConstraint activateConstraints:@[
        [contentView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-40],
        [contentView.leadingAnchor constraintEqualToAnchor:containerView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintEqualToAnchor:containerView.trailingAnchor constant:-20],
    ]];

    self.view = containerView;
}

- (void)createUIElements
{
    self.fFolderPopUp = [PrefsUI popUpButton];

    self.fDeleteOriginalTorrentButton = [PrefsUI checkbox:LStr(@"Move torrent file to the Trash") target:nil action:nil];
    self.fRenamePartialFilesButton = [PrefsUI checkbox:LStr(@"Append \".part\" to incomplete files' names") target:self
                                                action:@selector(setRenamePartialFiles:)];

    self.fUseIncompleteFolderButton = [PrefsUI checkbox:LStr(@"Keep incomplete files in:") target:self
                                                 action:@selector(setUseIncompleteFolder:)];
    self.fIncompleteFolderPopUp = [PrefsUI popUpButton];

    self.fAutoStartDownloadButton = [PrefsUI checkbox:LStr(@"Start added torrents") target:self
                                               action:@selector(setAutoStartDownloads:)];
    self.fDownloadAskButton = [PrefsUI checkbox:LStr(@"Display a window when opening a torrent file") target:nil action:nil];
    self.fDownloadAskMultiButton = [PrefsUI checkbox:LStr(@"Only when there are multiple files") target:nil action:nil];
    self.fDownloadAskManualButton = [PrefsUI checkbox:LStr(@"Only when adding manually") target:nil action:nil];
    self.fShowMagnetAddWindowCheck = [PrefsUI checkbox:LStr(@"Display a window when opening a magnet link") target:self
                                                action:@selector(setShowAddMagnetWindow:)];

    self.fAutoImportButton = [PrefsUI checkbox:LStr(@"Automatically add torrent files from:") target:self
                                        action:@selector(setAutoImport:)];
    self.fImportFolderPopUp = [PrefsUI popUpButton];

    [self configurePopupButtons];
}

- (void)setupLayoutInView:(NSView*)parent
{
    NSStackView* saveStack = [NSStackView
        stackViewWithViews:@[ [NSTextField labelWithString:LStr(@"Save to location:")], self.fFolderPopUp ]];
    saveStack.spacing = 8;
    saveStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* incompleteStack = [NSStackView
        stackViewWithViews:@[ self.fUseIncompleteFolderButton, self.fIncompleteFolderPopUp ]];
    incompleteStack.spacing = 8;
    incompleteStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* importStack = [NSStackView stackViewWithViews:@[ self.fAutoImportButton, self.fImportFolderPopUp ]];
    importStack.spacing = 8;
    importStack.alignment = NSLayoutAttributeCenterY;

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ [PrefsUI sectionTitle:LStr(@"Settings:")] ],
        @[ [PrefsUI indentedView:saveStack] ],
        @[ [PrefsUI indentedView:incompleteStack] ],
        @[ [PrefsUI indentedView:self.fAutoStartDownloadButton] ],
        @[ [PrefsUI indentedView:self.fDeleteOriginalTorrentButton] ],
        @[ [PrefsUI indentedView:self.fRenamePartialFilesButton] ],

        @[ [PrefsUI sectionTitle:LStr(@"Add window:")] ],
        @[ [PrefsUI indentedView:self.fDownloadAskButton] ],
        @[ [PrefsUI indentedView:self.fDownloadAskMultiButton level:2] ],
        @[ [PrefsUI indentedView:self.fDownloadAskManualButton level:2] ],
        @[ [PrefsUI indentedView:self.fShowMagnetAddWindowCheck] ],

        @[ [PrefsUI sectionTitle:LStr(@"Auto add:")] ],
        @[ [PrefsUI indentedView:importStack] ]
    ]];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 10;
    gridView.columnSpacing = 8;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    [gridView rowAtIndex:6].topPadding = 14;
    [gridView rowAtIndex:11].topPadding = 14;

    [parent addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:parent.topAnchor],
        [gridView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        [gridView.leadingAnchor constraintGreaterThanOrEqualToAnchor:parent.leadingAnchor],
        [gridView.trailingAnchor constraintLessThanOrEqualToAnchor:parent.trailingAnchor],
        [gridView.centerXAnchor constraintEqualToAnchor:parent.centerXAnchor]
    ]];

    [self updateShowAddMagnetWindowField];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    NSUserDefaultsController* dc = [NSUserDefaultsController sharedUserDefaultsController];

    [self.fDeleteOriginalTorrentButton bind:NSValueBinding toObject:dc withKeyPath:@"values.DeleteOriginalTorrent" options:nil];
    [self.fRenamePartialFilesButton bind:NSValueBinding toObject:dc withKeyPath:@"values.RenamePartialFiles" options:nil];
    [self.fUseIncompleteFolderButton bind:NSValueBinding toObject:dc withKeyPath:@"values.UseIncompleteDownloadFolder" options:nil];
    [self.fIncompleteFolderPopUp bind:NSEnabledBinding toObject:dc withKeyPath:@"values.UseIncompleteDownloadFolder" options:nil];
    [self.fAutoStartDownloadButton bind:NSValueBinding toObject:dc withKeyPath:@"values.AutoStartDownload" options:nil];

    [self.fDownloadAskButton bind:NSValueBinding toObject:dc withKeyPath:@"values.DownloadAsk" options:nil];
    [self.fDownloadAskMultiButton bind:NSValueBinding toObject:dc withKeyPath:@"values.DownloadAskMulti" options:nil];
    [self.fDownloadAskMultiButton bind:NSEnabledBinding toObject:dc withKeyPath:@"values.DownloadAsk" options:nil];
    [self.fDownloadAskManualButton bind:NSValueBinding toObject:dc withKeyPath:@"values.DownloadAskManual" options:nil];
    [self.fDownloadAskManualButton bind:NSEnabledBinding toObject:dc withKeyPath:@"values.DownloadAsk" options:nil];

    [self.fAutoImportButton bind:NSValueBinding toObject:dc withKeyPath:@"values.AutoImport" options:nil];
    [self.fImportFolderPopUp bind:NSEnabledBinding toObject:dc withKeyPath:@"values.AutoImport" options:nil];

    [self.fFolderPopUp selectItemAtIndex:[self.fDefaults boolForKey:@"DownloadLocationConstant"] ? DownloadPopupIndexFolder :
                                                                                                   DownloadPopupIndexTorrent];
}

#pragma mark - Configure

- (void)configurePopupButtons
{
    // fFolderPopUp
    NSMenuItem* defaultFolderItem = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(setDownloadLocation:) keyEquivalent:@""];
    defaultFolderItem.target = self;
    [defaultFolderItem bind:NSTitleBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
                withKeyPath:@"values.DownloadFolder"
                    options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToPathTransformer" }];
    [defaultFolderItem bind:NSImageBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
                withKeyPath:@"values.DownloadFolder"
                    options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToIconTransformer" }];
    [self.fFolderPopUp.menu addItem:defaultFolderItem];

    [self.fFolderPopUp.menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* sameAsTorrentItem = [[NSMenuItem alloc] initWithTitle:LStr(@"Same as torrent file")
                                                               action:@selector(setDownloadLocation:)
                                                        keyEquivalent:@""];
    sameAsTorrentItem.target = self;
    [self.fFolderPopUp.menu addItem:sameAsTorrentItem];

    [self.fFolderPopUp.menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* otherFolderItem = [[NSMenuItem alloc] initWithTitle:LStr(@"Other…") action:@selector(folderSheetShow:)
                                                      keyEquivalent:@""];
    otherFolderItem.target = self;
    [self.fFolderPopUp.menu addItem:otherFolderItem];

    // fIncompleteFolderPopUp
    NSMenuItem* incompletePathItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [incompletePathItem bind:NSTitleBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
                 withKeyPath:@"values.IncompleteDownloadFolder"
                     options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToPathTransformer" }];
    [incompletePathItem bind:NSImageBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
                 withKeyPath:@"values.IncompleteDownloadFolder"
                     options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToIconTransformer" }];
    [self.fIncompleteFolderPopUp.menu addItem:incompletePathItem];
    [self.fIncompleteFolderPopUp.menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* incompleteOtherItem = [[NSMenuItem alloc] initWithTitle:LStr(@"Other…") action:@selector(incompleteFolderSheetShow:)
                                                          keyEquivalent:@""];
    incompleteOtherItem.target = self;
    [self.fIncompleteFolderPopUp.menu addItem:incompleteOtherItem];

    // fImportFolderPopUp
    NSMenuItem* importPathItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [importPathItem bind:NSTitleBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
             withKeyPath:@"values.AutoImportDirectory"
                 options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToPathTransformer" }];
    [importPathItem bind:NSImageBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
             withKeyPath:@"values.AutoImportDirectory"
                 options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToIconTransformer" }];
    [self.fImportFolderPopUp.menu addItem:importPathItem];
    [self.fImportFolderPopUp.menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* importOtherItem = [[NSMenuItem alloc] initWithTitle:LStr(@"Other…") action:@selector(importFolderSheetShow:)
                                                      keyEquivalent:@""];
    importOtherItem.target = self;
    [self.fImportFolderPopUp.menu addItem:importOtherItem];
}

#pragma mark - Actions & Logic
- (void)folderSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;

    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            [self.fFolderPopUp selectItemAtIndex:DownloadPopupIndexFolder];

            NSString* folder = panel.URLs[0].path;
            [self.fDefaults setObject:folder forKey:@"DownloadFolder"];
            [self.fDefaults setBool:YES forKey:@"DownloadLocationConstant"];
            [self updateShowAddMagnetWindowField];

            tr_sessionSetDownloadDir(self.fHandle, folder.fileSystemRepresentation);
        } else {
            [self.fFolderPopUp selectItemAtIndex:[self.fDefaults boolForKey:@"DownloadLocationConstant"] ? DownloadPopupIndexFolder :
                                                                                                           DownloadPopupIndexTorrent];
        }
    }];
}

- (void)setDownloadLocation:(id)sender
{
    [self.fDefaults setBool:self.fFolderPopUp.indexOfSelectedItem == DownloadPopupIndexFolder forKey:@"DownloadLocationConstant"];
    [self updateShowAddMagnetWindowField];
}

- (void)setRenamePartialFiles:(id)sender
{
    tr_sessionSetIncompleteFileNamingEnabled(self.fHandle, [self.fDefaults boolForKey:@"RenamePartialFiles"]);
}

- (void)setUseIncompleteFolder:(id)sender
{
    tr_sessionSetIncompleteDirEnabled(self.fHandle, [self.fDefaults boolForKey:@"UseIncompleteDownloadFolder"]);
}

- (void)setAutoStartDownloads:(id)sender
{
    tr_sessionSetPaused(self.fHandle, ![self.fDefaults boolForKey:@"AutoStartDownload"]);
}

- (void)setShowAddMagnetWindow:(id)sender
{
    [self.fDefaults setBool:(self.fShowMagnetAddWindowCheck.state == NSControlStateValueOn) forKey:@"MagnetOpenAsk"];
}

- (void)updateShowAddMagnetWindowField
{
    if (![self.fDefaults boolForKey:@"DownloadLocationConstant"]) {
        // always show the add window for magnet links when the download location is the same as the torrent file
        self.fShowMagnetAddWindowCheck.state = NSControlStateValueOn;
        self.fShowMagnetAddWindowCheck.enabled = NO;
        self.fShowMagnetAddWindowCheck.toolTip = NSLocalizedString(
            @"This option is not available if Save to location is set to Same as torrent file.",
            "Preferences -> Transfers -> Adding -> Magnet tooltip");
    } else {
        self.fShowMagnetAddWindowCheck.state = [self.fDefaults boolForKey:@"MagnetOpenAsk"];
        self.fShowMagnetAddWindowCheck.enabled = YES;
        self.fShowMagnetAddWindowCheck.toolTip = nil;
    }
}

- (void)incompleteFolderSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");

    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            NSString* folder = panel.URLs[0].path;
            [self.fDefaults setObject:folder forKey:@"IncompleteDownloadFolder"];
            tr_sessionSetIncompleteDir(self.fHandle, folder.fileSystemRepresentation);
        }
        [self.fIncompleteFolderPopUp selectItemAtIndex:0];
    }];
}

- (void)importFolderSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");

    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            NSString* path = panel.URLs[0].path;
            [self.fDefaults setObject:path forKey:@"AutoImportDirectory"];
            [[NSNotificationCenter defaultCenter] postNotificationName:@"AutoImportSettingChange" object:self];
        } else {
            if (![self.fDefaults stringForKey:@"AutoImportDirectory"])
                [self.fDefaults setBool:NO forKey:@"AutoImport"];
        }
        [self.fImportFolderPopUp selectItemAtIndex:0];
    }];
}

- (void)setAutoImport:(id)sender
{
    if ([self.fDefaults stringForKey:@"AutoImportDirectory"]) {
        [[NSNotificationCenter defaultCenter] postNotificationName:@"AutoImportSettingChange" object:self];
    } else {
        [self importFolderSheetShow:nil];
    }
}
@end
