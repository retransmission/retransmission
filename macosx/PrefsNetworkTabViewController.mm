// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/string-utils.h>
#include <libtransmission/transmission.h>

#import "PrefsNetworkTabViewController.h"
#import "Controller.h"
#import "PrefsUI.h"
#import "PortChecker.h"
#import "Utils.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesNetworkTab", @"")

static NSString* TRDisplayableBindInterface(NSString* bindInterface)
{
    return [bindInterface isEqualToString:TRBlockedBindInterfaceName] ? @"" : (bindInterface ?: @"");
}

static NSString* TRDisplayableSessionBindInterface(NSString* bindInterface)
{
    if ([bindInterface isEqualToString:TRBlockedBindInterfaceName]) {
        return NSLocalizedString(@"No active VPN; traffic blocked", "Preferences -> Network -> bind interface info");
    }

    return bindInterface.length > 0 ? bindInterface : NSLocalizedString(@"Automatic", "Preferences -> Network -> bind interface info");
}

@interface PrefsNetworkTabViewController ()<NSTextFieldDelegate, PortCheckerDelegate>

@property(nonatomic) NSPopUpButton* fBindInterfacePopUp;
@property(nonatomic) NSTextField* fBindInterfaceStatusField;
@property(nonatomic) NSButton* fBindInterfaceInfoButton;
@property(nonatomic) NSPopover* fBindInterfaceInfoPopover;
@property(nonatomic, copy) NSString* fActiveVPNPortProbeBindInterface;

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

        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(activeVPNBindInterfaceDidChange:)
                                                   name:TRActiveVPNBindInterfaceDidChangeNotification
                                                 object:nil];
    }
    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
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

    [self updateBindInterfaceMenu];
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
    self.fBindInterfacePopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.fBindInterfacePopUp.translatesAutoresizingMaskIntoConstraints = NO;
    self.fBindInterfacePopUp.target = self;
    self.fBindInterfacePopUp.action = @selector(setBindInterface:);
    self.fBindInterfacePopUp.toolTip = LStr(
        @"Choose which network connection Retransmission may use. Automatic uses the normal system connection. Active VPN Tunnel only allows traffic through the detected VPN and blocks traffic if the VPN is unavailable. Name lookups for trackers and web seeds use the system resolver and are not routed through the selected connection.");

    self.fBindInterfaceStatusField = [PrefsUI smallLabel:@""];
    self.fBindInterfaceStatusField.lineBreakMode = NSLineBreakByTruncatingTail;
    [self.fBindInterfaceStatusField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                                             forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.fBindInterfaceInfoButton = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"info.circle"
                                                                        accessibilityDescription:nil]
                                                       target:self
                                                       action:@selector(showBindInterfaceInfo:)];
    self.fBindInterfaceInfoButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fBindInterfaceInfoButton.bordered = NO;
    self.fBindInterfaceInfoButton.toolTip = LStr(@"Show details about the current bind connection.");

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

    NSStackView* const bindInterfaceStatusStack = [NSStackView
        stackViewWithViews:@[ self.fBindInterfaceStatusField, self.fBindInterfaceInfoButton ]];
    bindInterfaceStatusStack.spacing = 4;
    bindInterfaceStatusStack.alignment = NSLayoutAttributeCenterY;

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"Bind connection:")], self.fBindInterfacePopUp ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, bindInterfaceStatusStack ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"Peer communication:")], self.fUtpButton ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"Port for incoming connections:")], portStatusStack ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fRandomPortOnStartButton ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, [PrefsUI indentedView:self.fRandomizeButton] ]];

    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, self.fNatTraversalButton ]];

    NSTextField* const natHintLabel = [PrefsUI smallLabel:LStr(@"NAT traversal uses either NAT-PMP or UPnP")];
    [gridView addRowWithViews:@[ NSGridCell.emptyContentView, [PrefsUI indentedView:natHintLabel] ]];

    [gridView addRowWithViews:@[ [NSTextField labelWithString:LStr(@"System sleep:")], self.fSleepPreventButton ]];

    [gridView rowAtIndex:3].rowAlignment = NSGridRowAlignmentNone;

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

- (BOOL)usesActiveVPNBindInterface
{
    return [[self.fDefaults stringForKey:@"BindInterfaceMode"] isEqualToString:TRBindInterfaceModeActiveVPN];
}

- (NSString*)bindInterfaceStatusString
{
    if (![self usesActiveVPNBindInterface]) {
        // an RPC client can set the blocked value in literal mode too, and
        // libtransmission ignores the whitespace around it
        NSString* trimmedInterface = [[self.fDefaults stringForKey:@"BindInterface"]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if ([trimmedInterface isEqualToString:TRBlockedBindInterfaceName]) {
            return NSLocalizedString(@"Traffic blocked", "Preferences -> Network -> bind interface status");
        }

        NSString* bindInterface = TRDisplayableBindInterface([self.fDefaults stringForKey:@"BindInterface"]);
        return bindInterface.length > 0 ?
            [NSString stringWithFormat:NSLocalizedString(@"Literal interface: %@", "Preferences -> Network -> bind interface status"),
                                       bindInterface] :
            NSLocalizedString(@"Automatic default connection", "Preferences -> Network -> bind interface status");
    }

    NSDictionary<NSString*, id>* status = [((Controller*)NSApp.delegate) activeVPNBindInterfaceStatus];
    NSString* resolvedInterface = status[TRActiveVPNResolutionInterfaceKey];
    NSString* displayName = status[TRActiveVPNResolutionDisplayNameKey];
    NSArray<NSString*>* candidates = status[TRActiveVPNResolutionCandidatesKey];
    if ([status[TRActiveVPNResolutionActiveKey] boolValue]) {
        return [NSString stringWithFormat:NSLocalizedString(@"Active VPN Tunnel: %@", "Preferences -> Network -> bind interface status"),
                                          displayName.length > 0 ? displayName : resolvedInterface];
    }
    if (candidates.count > 1) {
        return NSLocalizedString(@"VPN tunnel ambiguous; traffic blocked", "Preferences -> Network -> bind interface status");
    }

    NSString* lastInterface = TRDisplayableBindInterface([self.fDefaults stringForKey:@"BindInterface"]);
    return lastInterface.length > 0 ?
        [NSString stringWithFormat:NSLocalizedString(@"No active VPN; blocked on %@", "Preferences -> Network -> bind interface status"),
                                   lastInterface] :
        NSLocalizedString(@"No active VPN; traffic blocked", "Preferences -> Network -> bind interface status");
}

- (NSString*)bindInterfaceInfoString
{
    NSDictionary<NSString*, id>* status = [((Controller*)NSApp.delegate) activeVPNBindInterfaceStatus];
    NSString* routeInterface = status[TRActiveVPNResolutionRouteInterfaceKey];
    NSArray<NSString*>* candidates = status[TRActiveVPNResolutionCandidatesKey];
    NSString* resolvedInterface = status[TRActiveVPNResolutionInterfaceKey];
    NSString* serviceName = status[TRActiveVPNResolutionServiceNameKey];
    NSString* providerIdentifier = status[TRActiveVPNResolutionProviderIdentifierKey];
    NSString* sessionInterface = tr_strv_to_utf8_nsstring(tr_sessionGetBindInterface(self.fHandle));

    return [NSString
        stringWithFormat:@"%@\n%@: %@\n%@: %@\n%@: %@\n%@: %@\n%@: %@\n%@: %@",
                         [self usesActiveVPNBindInterface] ?
                             NSLocalizedString(@"Mode: Active VPN Tunnel", "Preferences -> Network -> bind interface info") :
                             NSLocalizedString(@"Mode: Literal Interface", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"Resolved interface", "Preferences -> Network -> bind interface info"),
                         resolvedInterface.length > 0 ? resolvedInterface :
                                                        NSLocalizedString(@"None", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"VPN service", "Preferences -> Network -> bind interface info"),
                         serviceName.length > 0 ? serviceName : NSLocalizedString(@"Unknown", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"Provider", "Preferences -> Network -> bind interface info"),
                         providerIdentifier.length > 0 ? providerIdentifier :
                                                         NSLocalizedString(@"Unknown", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"Current route", "Preferences -> Network -> bind interface info"),
                         routeInterface.length > 0 ? routeInterface : NSLocalizedString(@"Unknown", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"VPN candidates", "Preferences -> Network -> bind interface info"),
                         candidates.count > 0 ? [candidates componentsJoinedByString:@", "] :
                                                NSLocalizedString(@"None", "Preferences -> Network -> bind interface info"),
                         NSLocalizedString(@"Session bind", "Preferences -> Network -> bind interface info"),
                         TRDisplayableSessionBindInterface(sessionInterface)];
}

- (void)updateBindInterfaceStatus
{
    self.fBindInterfaceStatusField.stringValue = [self bindInterfaceStatusString];
    self.fBindInterfaceStatusField.toolTip = [self bindInterfaceInfoString];
}

- (void)activeVPNBindInterfaceDidChange:(NSNotification*)notification
{
    NSDictionary<NSString*, id>* status = [notification.userInfo isKindOfClass:NSDictionary.class] ? notification.userInfo : @{};
    NSString* resolvedInterface = status[TRActiveVPNResolutionInterfaceKey];
    NSString* activeVPNBindInterface = resolvedInterface.length > 0 ? resolvedInterface : TRBlockedBindInterfaceName;
    BOOL const probeBindChanged = ![self.fActiveVPNPortProbeBindInterface isEqualToString:activeVPNBindInterface];

    [self updateBindInterfaceMenu];

    if ([self usesActiveVPNBindInterface] && probeBindChanged) {
        self.fActiveVPNPortProbeBindInterface = activeVPNBindInterface;
        [self resetPortStatus];
    }
}

- (void)showBindInterfaceInfo:(id)sender
{
    NSViewController* viewController = [[NSViewController alloc] init];
    NSTextField* textField = [NSTextField wrappingLabelWithString:[self bindInterfaceInfoString]];
    textField.frame = NSMakeRect(0.0, 0.0, 320.0, 150.0);

    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 340.0, 170.0)];
    textField.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:textField];
    [NSLayoutConstraint activateConstraints:@[
        [textField.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:10.0],
        [textField.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-10.0],
        [textField.topAnchor constraintEqualToAnchor:view.topAnchor constant:10.0],
        [textField.bottomAnchor constraintLessThanOrEqualToAnchor:view.bottomAnchor constant:-10.0],
    ]];

    viewController.view = view;
    self.fBindInterfaceInfoPopover = [[NSPopover alloc] init];
    self.fBindInterfaceInfoPopover.contentViewController = viewController;
    self.fBindInterfaceInfoPopover.behavior = NSPopoverBehaviorTransient;
    [self.fBindInterfaceInfoPopover showRelativeToRect:self.fBindInterfaceInfoButton.bounds ofView:self.fBindInterfaceInfoButton
                                         preferredEdge:NSRectEdgeMaxY];
}

- (void)updateBindInterfaceMenu
{
    TRPopulateAppBindInterfacePopUp(
        self.fBindInterfacePopUp,
        [self.fDefaults stringForKey:@"BindInterface"],
        [self.fDefaults stringForKey:@"BindInterfaceMode"] ?: TRBindInterfaceModeLiteral);
    [self updateBindInterfaceStatus];
}

- (void)setBindInterface:(id)sender
{
    NSString* bindInterfaceMode = TRBindInterfacePopUpModeValue(self.fBindInterfacePopUp);
    [self.fDefaults setObject:bindInterfaceMode forKey:@"BindInterfaceMode"];
    // forget the VPN an earlier choice resolved to, so this one binds to the VPN active now
    [self.fDefaults setObject:@"" forKey:TRBindInterfaceServiceNameDefaultsKey];
    [self.fDefaults setObject:@"" forKey:TRBindInterfaceProviderIdentifierDefaultsKey];

    if ([bindInterfaceMode isEqualToString:TRBindInterfaceModeActiveVPN]) {
        [((Controller*)NSApp.delegate) updateActiveVPNBindInterfacePreference];
    } else {
        NSString* bindInterface = TRBindInterfacePopUpValue(self.fBindInterfacePopUp);
        [self.fDefaults setObject:bindInterface forKey:@"BindInterface"];
        tr_sessionSetBindInterface(self.fHandle, bindInterface.UTF8String);
    }

    [self updateBindInterfaceMenu];
    [self resetPortStatus];
}

- (void)updateBindInterfaceFromSession
{
    if ([self usesActiveVPNBindInterface]) {
        [((Controller*)NSApp.delegate) updateActiveVPNBindInterfacePreference];
    } else {
        [self.fDefaults setObject:tr_strv_to_utf8_nsstring(tr_sessionGetBindInterface(self.fHandle)) forKey:@"BindInterface"];
    }

    if (self.viewLoaded) {
        [self updateBindInterfaceMenu];
    }
    [self resetPortStatus];
}

// Forgets the last probe. Only a visible tab probes, so a hidden one waits for viewWillAppear.
- (void)resetPortStatus
{
    self.fPeerPort = -1;
    self.fNatStatus = -1;

    if (self.viewLoaded && self.view.window != nil) {
        [self updatePortStatus];
    }
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
