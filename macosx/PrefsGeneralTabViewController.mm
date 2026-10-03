// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PrefsGeneralTabViewController.h"
#import "DefaultAppHelper.h"

@interface PrefsGeneralTabViewController ()

@property(nonatomic) NSButton* fSystemPreferencesButton;
@property(nonatomic) NSButton* fSetDefaultForMagnetButton;
@property(nonatomic) NSButton* fSetDefaultForTorrentButton;
@property(nonatomic) NSButton* fCheckForUpdatesButton;
@property(nonatomic) NSButton* fCheckForUpdatesBetaButton;

@property(nonatomic) NSButton* fAutoSizeButton;
@property(nonatomic) NSButton* fBadgeDownloadButton;
@property(nonatomic) NSButton* fBadgeUploadButton;
@property(nonatomic) NSButton* fPromptRemoveButton;
@property(nonatomic) NSButton* fPromptRemoveDownloadingButton;
@property(nonatomic) NSButton* fPromptQuitButton;
@property(nonatomic) NSButton* fPromptQuitDownloadingButton;
@property(nonatomic) NSButton* fResetWarningsButton;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@property(nonatomic, readonly) DefaultAppHelper* fDefaultAppHelper;

@end

@implementation PrefsGeneralTabViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _fDefaults = NSUserDefaults.standardUserDefaults;
        _fDefaultAppHelper = [[DefaultAppHelper alloc] init];
    }
    return self;
}

- (void)loadView
{
    NSView* containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 542, 414)];
    containerView.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* contentView = [[NSView alloc] init];
    contentView.translatesAutoresizingMaskIntoConstraints = NO;
    [containerView addSubview:contentView];

    [self createUIElements];

    [self setupLayoutInView:contentView];

    [NSLayoutConstraint activateConstraints:@[
        [contentView.centerXAnchor constraintEqualToAnchor:containerView.centerXAnchor],
        [contentView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-40],
        [contentView.leadingAnchor constraintGreaterThanOrEqualToAnchor:containerView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintLessThanOrEqualToAnchor:containerView.trailingAnchor constant:-20],
        [contentView.widthAnchor constraintEqualToConstant:502]
    ]];

    self.view = containerView;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self setupBindings];
    [self updateDefaultsStates];
}

#pragma mark - UI Generation

- (void)createUIElements
{
    self.fAutoSizeButton = [NSButton checkboxWithTitle:@"Automatically size window to fit all transfers" target:self
                                                action:@selector(setAutoSize:)];

    self.fBadgeDownloadButton = [NSButton checkboxWithTitle:@"Total download rate" target:self action:@selector(setBadge:)];
    self.fBadgeUploadButton = [NSButton checkboxWithTitle:@"Total upload rate" target:self action:@selector(setBadge:)];

    self.fSystemPreferencesButton = [NSButton buttonWithTitle:@"Configure In System Preferences" target:self
                                                       action:@selector(openNotificationSystemPrefs:)];

    self.fPromptRemoveButton = [NSButton checkboxWithTitle:@"Removal of active transfers" target:nil action:nil];
    self.fPromptRemoveDownloadingButton = [NSButton checkboxWithTitle:@"Only when transfers are downloading" target:nil action:nil];
    self.fPromptQuitButton = [NSButton checkboxWithTitle:@"Quit with active transfers" target:nil action:nil];
    self.fPromptQuitDownloadingButton = [NSButton checkboxWithTitle:@"Only when transfers are downloading" target:nil action:nil];

    self.fResetWarningsButton = [NSButton buttonWithTitle:@"Reset" target:self action:@selector(resetWarnings:)];

    self.fSetDefaultForMagnetButton = [NSButton buttonWithTitle:@"Set Default Application" target:self
                                                         action:@selector(setDefaultForMagnets:)];
    self.fSetDefaultForTorrentButton = [NSButton buttonWithTitle:@"Set Default Application" target:self
                                                          action:@selector(setDefaultForTorrentFiles:)];

    self.fCheckForUpdatesButton = [NSButton checkboxWithTitle:@"Automatically check daily" target:nil action:nil];
    self.fCheckForUpdatesBetaButton = [NSButton checkboxWithTitle:@"Include beta releases" target:nil action:nil];
}

#pragma mark - Layout Setup

- (void)setupLayoutInView:(NSView*)parent
{
    NSGridView* gridView = [[NSGridView alloc] init];
    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 12;
    gridView.columnSpacing = 12;

    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Auto resize:"], self.fAutoSizeButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Badge Dock icon with:"], self.fBadgeDownloadButton ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fBadgeUploadButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Notifications:"], self.fSystemPreferencesButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Prompt user for:"], self.fPromptRemoveButton ]];

    NSStackView* fPromptRemoveDownloadingButtonStack = [[NSStackView alloc] init];
    [fPromptRemoveDownloadingButtonStack addArrangedSubview:self.fPromptRemoveDownloadingButton];
    fPromptRemoveDownloadingButtonStack.edgeInsets = { 0, 19, 0, 0 };
    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, fPromptRemoveDownloadingButtonStack ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fPromptQuitButton ]];

    NSStackView* fPromptQuitDownloadingButtonStack = [[NSStackView alloc] init];
    [fPromptQuitDownloadingButtonStack addArrangedSubview:self.fPromptQuitDownloadingButton];
    fPromptQuitDownloadingButtonStack.edgeInsets = { 0, 19, 0, 0 };

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, fPromptQuitDownloadingButtonStack ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Reset all alerts:"], self.fResetWarningsButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Accept magnet links:"], self.fSetDefaultForMagnetButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Open torrent files:"], self.fSetDefaultForTorrentButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:@"Check for update:"], self.fCheckForUpdatesButton ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fCheckForUpdatesBetaButton ]];

    [gridView columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;

    [parent addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:parent.topAnchor],
        [gridView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        [gridView.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor],
        [gridView.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor]
    ]];
}

#pragma mark - Cocoa Bindings

- (void)setupBindings
{
    NSUserDefaultsController* defaultsController = [NSUserDefaultsController sharedUserDefaultsController];

    [self.fAutoSizeButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.AutoSize" options:nil];

    [self.fBadgeDownloadButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.BadgeDownloadRate" options:nil];

    [self.fBadgeUploadButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.BadgeUploadRate" options:nil];

    [self.fPromptRemoveButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckRemove" options:nil];

    [self.fPromptRemoveDownloadingButton bind:NSValueBinding toObject:defaultsController
                                  withKeyPath:@"values.CheckRemoveDownloading"
                                      options:nil];

    [self.fPromptRemoveDownloadingButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckRemove"

                                      options:nil];
    [self.fPromptQuitButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckQuit" options:nil];
    [self.fPromptQuitDownloadingButton bind:NSValueBinding toObject:defaultsController
                                withKeyPath:@"values.CheckQuitDownloading"
                                    options:nil];
    [self.fPromptQuitDownloadingButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckQuit"
                                    options:nil];
    [self.fCheckForUpdatesButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SUEnableAutomaticChecks"
                              options:nil];
    [self.fCheckForUpdatesBetaButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.AutoUpdateBeta"
                                  options:nil];
}

#pragma mark - Actions & Logic

- (void)setAutoSize:(id)sender
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"AutoSizeSettingChange" object:self];
}

- (void)setBadge:(id)sender
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:self];
}

- (void)openNotificationSystemPrefs:(NSButton*)sender
{
    NSURL* prefPaneUrl = nil;
    if (@available(macOS 13.0, *)) {
        NSString* prefPaneName = @"x-apple.systempreferences:com.apple.Notifications-Settings.extension?id=";
        prefPaneName = [prefPaneName stringByAppendingString:NSBundle.mainBundle.bundleIdentifier];
        prefPaneUrl = [NSURL URLWithString:prefPaneName];
    } else {
        NSString* prefPaneName = @"x-apple.systempreferences:com.apple.preference.notifications?id=";
        prefPaneName = [prefPaneName stringByAppendingString:NSBundle.mainBundle.bundleIdentifier];
        prefPaneUrl = [NSURL URLWithString:prefPaneName];
    }
    [NSWorkspace.sharedWorkspace openURL:prefPaneUrl];
}

- (void)resetWarnings:(id)sender
{
    [self.fDefaults removeObjectForKey:@"WarningDuplicate"];
    [self.fDefaults removeObjectForKey:@"WarningRemainingSpace"];
    [self.fDefaults removeObjectForKey:@"WarningFolderDataSameName"];
    [self.fDefaults removeObjectForKey:@"WarningResetStats"];
    [self.fDefaults removeObjectForKey:@"WarningCreatorBlankAddress"];
    [self.fDefaults removeObjectForKey:@"WarningCreatorPrivateBlankAddress"];
    [self.fDefaults removeObjectForKey:@"WarningRemoveTrackers"];
    [self.fDefaults removeObjectForKey:@"WarningInvalidOpen"];
    [self.fDefaults removeObjectForKey:@"WarningRemoveCompleted"];
    [self.fDefaults removeObjectForKey:@"WarningDonate"];
}

- (void)setDefaultForMagnets:(id)sender
{
    __weak __auto_type weakSelf = self;
    [self.fDefaultAppHelper setDefaultForMagnetURLs:^{
        [weakSelf updateDefaultsStates];
    }];
}

- (void)setDefaultForTorrentFiles:(id)sender
{
    __weak __auto_type weakSelf = self;
    [self.fDefaultAppHelper setDefaultForTorrentFiles:^{
        [weakSelf updateDefaultsStates];
    }];
}

- (void)updateDefaultsStates
{
    BOOL const isDefaultForMagnetURLs = [self.fDefaultAppHelper isDefaultForMagnetURLs];
    self.fSetDefaultForMagnetButton.enabled = !isDefaultForMagnetURLs;
    BOOL const isDefaultForTorrentFiles = [self.fDefaultAppHelper isDefaultForTorrentFiles];
    self.fSetDefaultForTorrentButton.enabled = !isDefaultForTorrentFiles;
}

@end
