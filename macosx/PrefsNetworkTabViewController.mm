// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>

#import "PrefsNetworkTabViewController.h"
#import "PrefsUI.h"
#import "PortChecker.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesNetworkTab", @"")

@interface PrefsNetworkTabViewController ()<NSTextFieldDelegate, PortCheckerDelegate>

@property(nonatomic) NSButton* fUtpButton;
@property(nonatomic) NSTextField* fPortField;
@property(nonatomic) NSButton* fRandomizeButton;
@property(nonatomic) NSButton* fRandomPortOnStartButton;
@property(nonatomic) NSButton* fNatTraversalButton;
@property(nonatomic) NSButton* fSleepPreventButton;

@property(nonatomic) NSTextField* fPortStatusField;
@property(nonatomic) NSImageView* fPortStatusImage;
@property(nonatomic) NSProgressIndicator* fPortStatusProgress;

@property(nonatomic) PortChecker* fPortChecker;
@property(nonatomic) NSTimer* fPortStatusTimer;
@property(nonatomic) int fPeerPort;
@property(nonatomic) int fNatStatus;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

// validate textField input default value string.
@property(nonatomic, copy) NSString* fInitialTextFieldString;

@end

@implementation PrefsNetworkTabViewController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _fHandle = handle;
        _fDefaults = NSUserDefaults.standardUserDefaults;
        _fPeerPort = -1;
        _fNatStatus = -1;
    }
    return self;
}

- (void)dealloc
{
    [_fPortStatusTimer invalidate];
    [_fPortChecker cancelProbe];
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
}

- (void)viewWillAppear
{
    [super viewWillAppear];

    [self updatePortStatus];

    [self.fPortStatusTimer invalidate];

    __weak __auto_type weakSelf = self;
    self.fPortStatusTimer = [NSTimer scheduledTimerWithTimeInterval:5.0 repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf updatePortStatus];
    }];
}

- (void)viewDidDisappear
{
    [super viewDidDisappear];

    [self.fPortStatusTimer invalidate];
    self.fPortStatusTimer = nil;

    if (self.fPortChecker) {
        [self.fPortChecker cancelProbe];
        self.fPortChecker = nil;
        // A cancelled probe never updates the UI, so force a new one on the next appearance.
        self.fPeerPort = -1;
    }
}

#pragma mark - UI Generation

- (void)createUIElements
{
    self.fUtpButton = [PrefsUI checkbox:LStr(@"Enable µTP for peer connections") target:self action:@selector(setUTP:)];

    self.fPortField = [PrefsUI numberInputFieldWithWidth:60 minimum:1 maximum:65535];
    self.fPortField.delegate = self;
    self.fPortField.target = self;
    self.fPortField.action = @selector(setPort:);

    [self.fPortField setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                              forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.fRandomizeButton = [PrefsUI pushButton:LStr(@"Randomize") target:self action:@selector(randomPort:)];
    self.fRandomPortOnStartButton = [PrefsUI checkbox:LStr(@"Pick a random port at startup") target:self
                                               action:@selector(setRandomPortOnStart:)];
    self.fNatTraversalButton = [PrefsUI checkbox:LStr(@"Automatically map port") target:self action:@selector(setNat:)];

    self.fSleepPreventButton = [PrefsUI checkbox:LStr(@"Prevent computer from sleeping with active torrents") target:nil action:nil];

    self.fPortStatusProgress = [[NSProgressIndicator alloc] init];
    self.fPortStatusProgress.translatesAutoresizingMaskIntoConstraints = NO;
    self.fPortStatusProgress.style = NSProgressIndicatorStyleSpinning;
    self.fPortStatusProgress.controlSize = NSControlSizeSmall;
    self.fPortStatusProgress.displayedWhenStopped = YES;

    self.fPortStatusImage = [[NSImageView alloc] init];
    self.fPortStatusImage.translatesAutoresizingMaskIntoConstraints = NO;
    self.fPortStatusImage.imageScaling = NSImageScaleProportionallyDown;

    self.fPortStatusField = [PrefsUI smallLabel:@""];
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

    gridView.rowAlignment = NSGridRowAlignmentFirstBaseline;

    NSStackView* const portStatusStack = [NSStackView
        stackViewWithViews:@[ self.fPortField, self.fPortStatusProgress, self.fPortStatusImage, self.fPortStatusField ]];
    portStatusStack.spacing = 6;
    portStatusStack.alignment = NSLayoutAttributeCenterY;

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"Peer communication:")], self.fUtpButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"Port for incoming connections:")], portStatusStack ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fRandomPortOnStartButton ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, [PrefsUI indentedView:self.fRandomizeButton] ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fNatTraversalButton ]];

    NSTextField* const natHintLabel = [PrefsUI smallLabel:LStr(@"NAT traversal uses either NAT-PMP or UPnP")];
    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, [PrefsUI indentedView:natHintLabel] ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"System sleep:")], self.fSleepPreventButton ]];

    [gridView rowAtIndex:1].rowAlignment = NSGridRowAlignmentNone;

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

    [self.fPortField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.BindPort" options:nil];
    [self.fUtpButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.UTPGlobal" options:nil];
    [self.fRandomPortOnStartButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RandomPort" options:nil];
    [self.fNatTraversalButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.NatTraversal" options:nil];
    [self.fSleepPreventButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SleepPrevent" options:nil];
}

#pragma mark - Actions & Network Logic

- (void)setPort:(id)sender
{
    uint16_t const port = [sender intValue];
    [self.fDefaults setInteger:port forKey:@"BindPort"];
    tr_sessionSetPeerPort(self.fHandle, port);

    self.fPeerPort = -1;
    [self updatePortStatus];
}

- (void)randomPort:(id)sender
{
    auto const port = tr_sessionSetPeerPortRandom(self.fHandle);
    [self.fDefaults setInteger:port forKey:@"BindPort"];

    self.fPeerPort = -1;
    [self updatePortStatus];
}

- (void)setRandomPortOnStart:(id)sender
{
    tr_sessionSetPeerPortRandomOnStart(self.fHandle, ((NSButton*)sender).state == NSControlStateValueOn);
}

- (void)setNat:(id)sender
{
    tr_sessionSetPortForwardingEnabled(self.fHandle, [self.fDefaults boolForKey:@"NatTraversal"]);

    self.fNatStatus = -1;
    [self updatePortStatus];
}

- (void)setUTP:(id)sender
{
    tr_sessionSetUTPEnabled(self.fHandle, [self.fDefaults boolForKey:@"UTPGlobal"]);
}

- (void)updatePortStatus
{
    auto const fwd = tr_sessionGetPortForwarding(self.fHandle);
    int const port = tr_sessionGetPeerPort(self.fHandle);
    BOOL const natStatusChanged = (self.fNatStatus != fwd);
    BOOL const peerPortChanged = (self.fPeerPort != port);

    if (natStatusChanged || peerPortChanged) {
        self.fNatStatus = fwd;
        self.fPeerPort = port;

        self.fPortStatusField.stringValue = @"";
        self.fPortStatusImage.image = nil;

        self.fPortStatusProgress.hidden = NO;

        [self.fPortStatusProgress startAnimation:self];

        if (self.fPortChecker) {
            [self.fPortChecker cancelProbe];
        }
        BOOL const delay = natStatusChanged || tr_sessionIsPortForwardingEnabled(self.fHandle);
        self.fPortChecker = [[PortChecker alloc] initForPort:self.fPeerPort delay:delay withDelegate:self];
    }
}

- (void)portCheckerDidFinishProbing:(PortChecker*)portChecker
{
    // A cancelled probe still reports back (as an error); only the current checker may update the UI.
    if (portChecker != self.fPortChecker) {
        return;
    }

    [self.fPortStatusProgress stopAnimation:self];

    self.fPortStatusProgress.hidden = YES;

    switch (self.fPortChecker.status) {
    case PortStatusOpen:
        self.fPortStatusField.stringValue = NSLocalizedString(@"Port is open", "Preferences -> Network -> port status");
        self.fPortStatusImage.image = [NSImage imageNamed:NSImageNameStatusAvailable];
        break;
    case PortStatusClosed:
        self.fPortStatusField.stringValue = NSLocalizedString(@"Port is closed", "Preferences -> Network -> port status");
        self.fPortStatusImage.image = [NSImage imageNamed:NSImageNameStatusUnavailable];
        break;
    case PortStatusError:
        self.fPortStatusField.stringValue = NSLocalizedString(@"Port check site is down", "Preferences -> Network -> port status");
        self.fPortStatusImage.image = [NSImage imageNamed:NSImageNameStatusPartiallyAvailable];
        break;
    case PortStatusChecking:
        break;
    default:
        NSAssert(NO, @"Port checker returned invalid status: %lu", self.fPortChecker.status);
        break;
    }
    self.fPortChecker = nil;
}

#pragma mark - NSTextFieldDelegate

// Input validation
- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    if (control == self.fPortField) {
        self.fInitialTextFieldString = self.fPortField.stringValue;
    }
    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    if (control == self.fPortField) {
        NSBeep();
        if (self.fInitialTextFieldString) {
            self.fPortField.stringValue = self.fInitialTextFieldString;
            self.fInitialTextFieldString = nil;
        }
    }
    return NO;
}

@end
