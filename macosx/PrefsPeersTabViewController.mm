// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>

#import "PrefsPeersTabViewController.h"
#import "PrefsUI.h"
#import "BlocklistDownloaderViewController.h"
#import "ColorTextField.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesPeersTab", @"")

@interface PrefsPeersTabViewController ()<NSTextFieldDelegate>

@property(nonatomic) NSTextField* fPeersGlobalField;
@property(nonatomic) NSTextField* fPeersTorrentField;

@property(nonatomic) NSButton* fPexButton;
@property(nonatomic) NSButton* fDhtButton;
@property(nonatomic) NSButton* fLpdButton;

@property(nonatomic) NSButton* fEncryptionPreferButton;
@property(nonatomic) NSButton* fEncryptionRequireButton;

@property(nonatomic) NSButton* fBlocklistNewButton;
@property(nonatomic) ColorTextField* fBlocklistURLTitle;
@property(nonatomic) NSTextField* fBlocklistURLField;
@property(nonatomic) NSButton* fBlocklistButton;
@property(nonatomic) NSTextField* fBlocklistMessageField;
@property(nonatomic) NSTextField* fBlocklistDateField;
@property(nonatomic) NSButton* fBlocklistAutoUpdateButton;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

// validate textField input default value string.
@property(nonatomic, copy) NSString* fInitialTextFieldString;

@end

@implementation PrefsPeersTabViewController

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
    NSView* contentView = [[NSView alloc] init];
    contentView.translatesAutoresizingMaskIntoConstraints = NO;
    [containerView addSubview:contentView];

    [self createUIElements];
    [self setupLayoutInView:contentView];

    NSLayoutConstraint* preferredWidth = [contentView.widthAnchor constraintEqualToConstant:502];
    preferredWidth.priority = NSLayoutPriorityDefaultHigh;

    [NSLayoutConstraint activateConstraints:@[
        [contentView.centerXAnchor constraintEqualToAnchor:containerView.centerXAnchor],
        [contentView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-40],
        [contentView.leadingAnchor constraintGreaterThanOrEqualToAnchor:containerView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintLessThanOrEqualToAnchor:containerView.trailingAnchor constant:-20],
        preferredWidth
    ]];

    [containerView setFrameSize:containerView.fittingSize];
    self.view = containerView;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self setupBindings];

    [self updateBlocklistButton];
    [self updateBlocklistFields];
}

#pragma mark - UI Generation

- (void)createUIElements
{
    // Peer limits
    self.fPeersGlobalField = [PrefsUI numberInputFieldWithWidth:60 minimum:1 maximum:3000];
    self.fPeersGlobalField.delegate = self;
    self.fPeersGlobalField.target = self;
    self.fPeersGlobalField.action = @selector(setPeersGlobal:);

    self.fPeersTorrentField = [PrefsUI numberInputFieldWithWidth:60 minimum:1 maximum:3000];
    self.fPeersTorrentField.delegate = self;
    self.fPeersTorrentField.target = self;
    self.fPeersTorrentField.action = @selector(setPeersTorrent:);

    // Discovery protocols
    self.fPexButton = [PrefsUI checkbox:LStr(@"Use PEX to find more peers for public torrents") target:self
                                 action:@selector(setPEX:)];
    self.fDhtButton = [PrefsUI checkbox:LStr(@"Use DHT to find more peers for public torrents") target:self
                                 action:@selector(setDHT:)];
    self.fLpdButton = [PrefsUI checkbox:LStr(@"Use Local Peer Discovery to find more peers for public torrents") target:self
                                 action:@selector(setLPD:)];

    // Encryption
    self.fEncryptionPreferButton = [PrefsUI checkbox:LStr(@"Prefer encryption") target:self action:@selector(setEncryptionMode:)];
    self.fEncryptionRequireButton = [PrefsUI checkbox:LStr(@"Require encryption") target:self action:@selector(setEncryptionMode:)];

    // Blocklist
    self.fBlocklistNewButton = [PrefsUI checkbox:LStr(@"Enable blocklist") target:self action:@selector(setBlocklistEnabled:)];

    self.fBlocklistURLTitle = [ColorTextField labelWithString:LStr(@"URL:")];

    self.fBlocklistURLField = [[NSTextField alloc] init];
    self.fBlocklistURLField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fBlocklistURLField.delegate = self;
    self.fBlocklistURLField.focusRingType = NSFocusRingTypeNone;
    self.fBlocklistURLField.drawsBackground = YES;

    self.fBlocklistButton = [PrefsUI pushButton:LStr(@"Update") target:self action:@selector(updateBlocklist:)];
    self.fBlocklistMessageField = [NSTextField labelWithString:@""];
    self.fBlocklistMessageField.translatesAutoresizingMaskIntoConstraints = NO;

    self.fBlocklistDateField = [PrefsUI smallLabel:@""];
    self.fBlocklistAutoUpdateButton = [PrefsUI checkbox:LStr(@"Automatically update weekly") target:self
                                                 action:@selector(setBlocklistAutoUpdate:)];
}

#pragma mark - Layout Setup

- (void)setupLayoutInView:(NSView*)parent
{
    // Content width and wrapping configurations
    [self.fBlocklistURLField.widthAnchor constraintEqualToConstant:300].active = YES;

    // --- SECTION: Peer Limits Horizontal Stacks ---
    NSStackView* globalPeersStack = [NSStackView
        stackViewWithViews:@[ [NSTextField labelWithString:LStr(@"Maximum peers overall:")], self.fPeersGlobalField ]];
    globalPeersStack.spacing = 8;
    globalPeersStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    globalPeersStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* torrentPeersStack = [NSStackView stackViewWithViews:@[
        [NSTextField labelWithString:LStr(@"Maximum peers for new torrents:")],
        self.fPeersTorrentField
    ]];
    torrentPeersStack.spacing = 8;
    torrentPeersStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    torrentPeersStack.alignment = NSLayoutAttributeCenterY;

    NSTextField* limitsHint = [PrefsUI smallLabel:LStr(@"High connection limits might significantly impact system performance")];
    limitsHint.lineBreakMode = NSLineBreakByWordWrapping;
    limitsHint.maximumNumberOfLines = 0;
    limitsHint.preferredMaxLayoutWidth = 370;

    // --- SECTION: Blocklist Horizontal Stacks ---
    NSStackView* urlStack = [NSStackView stackViewWithViews:@[ self.fBlocklistURLTitle, self.fBlocklistURLField ]];
    urlStack.spacing = 6;
    urlStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    urlStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* updateStack = [NSStackView stackViewWithViews:@[ self.fBlocklistButton, self.fBlocklistMessageField ]];
    updateStack.spacing = 8;
    updateStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    updateStack.alignment = NSLayoutAttributeCenterY;

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        // --- Peer Limits Section ---
        @[ [PrefsUI sectionTitle:LStr(@"Peer limits:")] ],
        @[ [PrefsUI indentedView:globalPeersStack] ],
        @[ [PrefsUI indentedView:torrentPeersStack] ],
        @[ [PrefsUI indentedView:limitsHint] ],

        // --- Discovery Section (Indented under Peer Limits) ---
        @[ [PrefsUI indentedView:self.fPexButton] ],
        @[ [PrefsUI indentedView:self.fDhtButton] ],
        @[ [PrefsUI indentedView:self.fLpdButton] ],

        // --- Encryption Section ---
        @[ [PrefsUI sectionTitle:LStr(@"Encryption:")] ],
        @[ [PrefsUI indentedView:self.fEncryptionPreferButton] ],
        @[ [PrefsUI indentedView:self.fEncryptionRequireButton] ],

        // --- Blocklist Section ---
        @[ [PrefsUI sectionTitle:LStr(@"Blocklist:")] ],
        @[ [PrefsUI indentedView:self.fBlocklistNewButton] ],
        @[ [PrefsUI indentedView:urlStack] ],
        @[ [PrefsUI indentedView:updateStack] ],
        @[ [PrefsUI indentedView:self.fBlocklistDateField level:2] ],
        @[ [PrefsUI indentedView:self.fBlocklistAutoUpdateButton] ]
    ]];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 10;
    gridView.columnSpacing = 8;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    // Set top paddings for logical section separators
    [gridView rowAtIndex:7].topPadding = 14; // Before Encryption section
    [gridView rowAtIndex:10].topPadding = 14; // Before Blocklist section

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
    [self.fPeersGlobalField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.PeersTotal" options:nil];
    [self.fPeersTorrentField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.PeersTorrent" options:nil];

    [self.fPexButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.PEXGlobal" options:nil];
    [self.fDhtButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.DHTGlobal" options:nil];
    [self.fLpdButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.LocalPeerDiscoveryGlobal" options:nil];
    [self.fEncryptionPreferButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.EncryptionPrefer"
                               options:nil];
    [self.fEncryptionRequireButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.EncryptionRequire"
                                options:nil];
    [self.fEncryptionRequireButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.EncryptionPrefer"
                                options:nil];
    [self.fBlocklistNewButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.BlocklistNew" options:nil];
    [self.fBlocklistURLTitle bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.BlocklistNew" options:nil];
    [self.fBlocklistURLField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.BlocklistNew" options:nil];
    [self.fBlocklistAutoUpdateButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.BlocklistAutoUpdate"
                                  options:nil];
    [self.fBlocklistAutoUpdateButton bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.BlocklistNew"
                                  options:nil];
}

#pragma mark - Actions & Transmission Logic
- (void)setPeersGlobal:(id)sender
{
    tr_sessionSetPeerLimit(self.fHandle, [self.fDefaults integerForKey:@"PeersTotal"]);
}

- (void)setPeersTorrent:(id)sender
{
    tr_sessionSetPeerLimitPerTorrent(self.fHandle, [self.fDefaults integerForKey:@"PeersTorrent"]);
}

- (void)setPEX:(id)sender
{
    tr_sessionSetPexEnabled(self.fHandle, [self.fDefaults boolForKey:@"PEXGlobal"]);
}

- (void)setDHT:(id)sender
{
    tr_sessionSetDHTEnabled(self.fHandle, [self.fDefaults boolForKey:@"DHTGlobal"]);
}

- (void)setLPD:(id)sender
{
    tr_sessionSetLPDEnabled(self.fHandle, [self.fDefaults boolForKey:@"LocalPeerDiscoveryGlobal"]);
}

- (void)setEncryptionMode:(id)sender
{
    tr_encryption_mode const mode = [self.fDefaults boolForKey:@"EncryptionPrefer"] ?
        ([self.fDefaults boolForKey:@"EncryptionRequire"] ? TR_ENCRYPTION_REQUIRED : TR_ENCRYPTION_PREFERRED) :
        TR_CLEAR_PREFERRED;
    tr_sessionSetEncryption(self.fHandle, mode);
}

- (void)setBlocklistEnabled:(id)sender
{
    tr_blocklistSetEnabled(self.fHandle, [self.fDefaults boolForKey:@"BlocklistNew"]);
    [self updateBlocklistButton];
}

- (void)setBlocklistAutoUpdate:(id)sender
{
    tr_blocklistSetUpdatesEnabled(self.fHandle, [self.fDefaults boolForKey:@"BlocklistAutoUpdate"]);
}

- (void)updateBlocklist:(id)sender
{
    auto const blocklistDownloaderViewController = [[BlocklistDownloaderViewController alloc] initWithSessionHandle:self.fHandle];
    __weak auto weakSelf = self;
    [blocklistDownloaderViewController showAsSheetModalForWindow:self.view.window completionHandler:^(NSString* _Nullable error) {
        if (error) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", @"Blocklist -> button")];
            alert.messageText = NSLocalizedString(@"Download of the blocklist failed.", @"Blocklist -> message");
            alert.alertStyle = NSAlertStyleWarning;
            alert.informativeText = error;
            [alert beginSheetModalForWindow:weakSelf.view.window completionHandler:nil];
        } else {
            [weakSelf updateBlocklistFields];
        }
    }];
}

- (void)updateBlocklistFields
{
    BOOL const exists = tr_blocklistExists(self.fHandle);
    if (exists) {
        self.fBlocklistMessageField.stringValue = [NSString
            localizedStringWithFormat:NSLocalizedString(@"%lu IP address rules in list", "Prefs -> blocklist -> message"),
                                      tr_blocklistGetRuleCount(self.fHandle)];
    } else {
        self.fBlocklistMessageField.stringValue = NSLocalizedString(@"A blocklist must first be downloaded", "Prefs -> blocklist -> message");
    }
    NSString* updatedDateString;
    if (exists) {
        time_t const mtime = tr_blocklistGetMTime(self.fHandle);
        NSDate* updatedDate = mtime > 0 ? [NSDate dateWithTimeIntervalSince1970:mtime] : nil;
        if (updatedDate) {
            updatedDateString = [NSDateFormatter localizedStringFromDate:updatedDate dateStyle:NSDateFormatterFullStyle
                                                               timeStyle:NSDateFormatterShortStyle];
        } else {
            updatedDateString = NSLocalizedString(@"N/A", "Prefs -> blocklist -> message");
        }
    } else {
        updatedDateString = NSLocalizedString(@"Never", "Prefs -> blocklist -> message");
    }
    self.fBlocklistDateField.stringValue = [NSString
        stringWithFormat:@"%@: %@", NSLocalizedString(@"Last updated", "Prefs -> blocklist -> message"), updatedDateString];

    self.fBlocklistURLField.stringValue = [self.fDefaults stringForKey:@"BlocklistURL"] ?: @"";
}

- (void)updateBlocklistURLField
{
    NSString* blocklistString = self.fBlocklistURLField.stringValue;
    [self.fDefaults setObject:blocklistString forKey:@"BlocklistURL"];
    tr_blocklistSetURL(self.fHandle, blocklistString.UTF8String);
    [self updateBlocklistButton];
}

- (void)updateBlocklistButton
{
    NSString* blocklistString = [self.fDefaults objectForKey:@"BlocklistURL"];
    BOOL const enable = (blocklistString && ![blocklistString isEqualToString:@""]) && [self.fDefaults boolForKey:@"BlocklistNew"];
    self.fBlocklistButton.enabled = enable;
}

#pragma mark - NSTextFieldDelegate
- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialTextFieldString = control.stringValue;
    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialTextFieldString) {
        control.stringValue = self.fInitialTextFieldString;
        self.fInitialTextFieldString = nil;
    }
    return NO;
}

- (void)controlTextDidChange:(NSNotification*)obj
{
    if (obj.object == self.fBlocklistURLField) {
        [self updateBlocklistURLField];
    }
}
@end
