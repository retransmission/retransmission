// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#import "StatusBarController.h"
#import "NSStringAdditions.h"
#import "Utils.h"

typedef NSString* StatusRatioType NS_TYPED_EXTENSIBLE_ENUM;

static StatusRatioType const StatusRatioTypeTotal = @"RatioTotal";
static StatusRatioType const StatusRatioTypeSession = @"RatioSession";

typedef NSString* StatusTransferType NS_TYPED_EXTENSIBLE_ENUM;

static StatusTransferType const StatusTransferTypeTotal = @"TransferTotal";
static StatusTransferType const StatusTransferTypeSession = @"TransferSession";

typedef NS_ENUM(NSUInteger, StatusTag) {
    StatusTagTotalRatio = 0,
    StatusTagSessionRatio = 1,
    StatusTagTotalTransfer = 2,
    StatusTagSessionTransfer = 3
};

@interface StatusBarController ()

@property(nonatomic) NSPopUpButton* fStatusButton;
@property(nonatomic) NSTextField* fTotalDLField;
@property(nonatomic) NSTextField* fTotalULField;
@property(nonatomic) NSImageView* fTotalDLImageView;
@property(nonatomic) NSImageView* fTotalULImageView;

@property(nonatomic, readonly) tr_session* fLib;

@property(nonatomic) CGFloat fPreviousDownloadRate;
@property(nonatomic) CGFloat fPreviousUploadRate;

@end

@implementation StatusBarController

- (instancetype)initWithLib:(tr_session*)lib
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _fLib = lib;

        _fPreviousDownloadRate = -1.0;
        _fPreviousUploadRate = -1.0;
    }

    return self;
}

- (void)loadView
{
    NSView* statusBarView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, 24)];
    statusBarView.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    self.view = statusBarView;

    [self createSubviews];

    [self.fStatusButton setContentCompressionResistancePriority:250 forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* stackView = [[NSStackView alloc] init];
    [stackView addView:self.fStatusButton inGravity:NSStackViewGravityLeading];
    [stackView addView:self.fTotalDLImageView inGravity:NSStackViewGravityTrailing];
    [stackView addView:self.fTotalDLField inGravity:NSStackViewGravityTrailing];
    [stackView addView:self.fTotalULImageView inGravity:NSStackViewGravityTrailing];
    [stackView addView:self.fTotalULField inGravity:NSStackViewGravityTrailing];

    stackView.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stackView.alignment = NSLayoutAttributeCenterY;
    stackView.spacing = 5.0;
    [stackView setHuggingPriority:NSLayoutPriorityDefaultLow - 1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    [stackView setCustomSpacing:20.0 afterView:self.fTotalDLField];

    stackView.translatesAutoresizingMaskIntoConstraints = NO;
    [statusBarView addSubview:stackView];

    NSBox* separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    separator.translatesAutoresizingMaskIntoConstraints = NO;
    [statusBarView addSubview:separator];

    [NSLayoutConstraint activateConstraints:@[
        [stackView.leadingAnchor constraintEqualToAnchor:statusBarView.leadingAnchor constant:5],
        [stackView.trailingAnchor constraintEqualToAnchor:statusBarView.trailingAnchor constant:-20],
        [stackView.topAnchor constraintEqualToAnchor:statusBarView.topAnchor],
        [stackView.bottomAnchor constraintEqualToAnchor:statusBarView.bottomAnchor],

        [separator.topAnchor constraintEqualToAnchor:statusBarView.topAnchor],
        [separator.leadingAnchor constraintEqualToAnchor:statusBarView.leadingAnchor],
        [separator.trailingAnchor constraintEqualToAnchor:statusBarView.trailingAnchor],
        [separator.heightAnchor constraintEqualToConstant:1.0]
    ]];
}

- (void)createSubviews
{
    NSPopUpButton* button = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
    button.showsBorderOnlyWhileMouseInside = YES;
    button.controlSize = NSControlSizeSmall;
    button.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [button.cell setArrowPosition:NSPopUpNoArrow];
    button.lineBreakMode = NSLineBreakByTruncatingTail;

    NSMenu* menu = [[NSMenu alloc] init];

    NSMenuItem* hiddenItem = [[NSMenuItem alloc] initWithTitle:@"status menu" action:nil keyEquivalent:@""];
    hiddenItem.image = [NSImage imageNamed:@"YingYangTemplate"];
    hiddenItem.hidden = YES;
    hiddenItem.tag = -1;
    [menu addItem:hiddenItem];

    void (^addStatusItem)(NSString*, StatusTag) = ^void(NSString* const title, StatusTag const tag) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(setStatusLabel:) keyEquivalent:@""];
        item.target = self;
        item.tag = tag;
        [menu addItem:item];
    };

    addStatusItem(NSLocalizedString(@"Total Ratio", "Status Bar -> status menu"), StatusTagTotalRatio);
    addStatusItem(NSLocalizedString(@"Session Ratio", "Status Bar -> status menu"), StatusTagSessionRatio);
    addStatusItem(NSLocalizedString(@"Total Transfer", "Status Bar -> status menu"), StatusTagTotalTransfer);
    addStatusItem(NSLocalizedString(@"Session Transfer", "Status Bar -> status menu"), StatusTagSessionTransfer);

    button.menu = menu;
    self.fStatusButton = button;

    NSTextField* (^createTextField)(void) = ^NSTextField* {
        auto result = [NSTextField labelWithString:@"999.9 KB/s"];
        result.controlSize = NSControlSizeSmall;
        result.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
        return result;
    };

    self.fTotalDLImageView = [NSImageView imageViewWithImage:[NSImage imageNamed:@"DownArrowTemplate"]];

    self.fTotalDLField = createTextField();

    self.fTotalULImageView = [NSImageView imageViewWithImage:[NSImage imageNamed:@"UpArrowTemplate"]];

    self.fTotalULField = createTextField();
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self setupUIConfiguration];
}

- (void)setupUIConfiguration
{
    self.fStatusButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLImageView.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULImageView.cell.backgroundStyle = NSBackgroundStyleRaised;

    [self updateSpeedFieldsToolTips];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateSpeedFieldsToolTips) name:@"SpeedLimitUpdate"
                                             object:nil];
}

- (void)updateWithDownload:(CGFloat)dlRate upload:(CGFloat)ulRate
{
    //set rates
    if (!isSpeedEqual(self.fPreviousDownloadRate, dlRate)) {
        self.fTotalDLField.stringValue = [NSString stringForSpeed:dlRate];
        self.fPreviousDownloadRate = dlRate;
    }

    if (!isSpeedEqual(self.fPreviousUploadRate, ulRate)) {
        self.fTotalULField.stringValue = [NSString stringForSpeed:ulRate];
        self.fPreviousUploadRate = ulRate;
    }

    //set status button text
    NSString *statusLabel = [NSUserDefaults.standardUserDefaults stringForKey:@"StatusLabel"], *statusString;
    BOOL total;
    if ((total = [statusLabel isEqualToString:StatusRatioTypeTotal]) || [statusLabel isEqualToString:StatusRatioTypeSession]) {
        auto const stats = total ? tr_sessionGetCumulativeStats(self.fLib) : tr_sessionGetStats(self.fLib);

        statusString = [NSLocalizedString(@"Ratio", "status bar -> status label")
            stringByAppendingFormat:@": %@", [NSString stringForRatio:stats.ratio]];
    } else //StatusTransferTypeTotal or StatusTransferTypeSession
    {
        total = [statusLabel isEqualToString:StatusTransferTypeTotal];

        auto const stats = total ? tr_sessionGetCumulativeStats(self.fLib) : tr_sessionGetStats(self.fLib);

        statusString = [NSString stringWithFormat:NSLocalizedString(@"Down: %@, Up: %@", "status bar -> status label"),
                                                  [NSString stringForFileSize:stats.downloadedBytes],
                                                  [NSString stringForFileSize:stats.uploadedBytes]];
    }

    if (![self.fStatusButton.title isEqualToString:statusString]) {
        self.fStatusButton.title = statusString;
    }
}

- (void)setStatusLabel:(id)sender
{
    NSString* statusLabel;
    switch ([sender tag]) {
    case StatusTagTotalRatio:
        statusLabel = StatusRatioTypeTotal;
        break;
    case StatusTagSessionRatio:
        statusLabel = StatusRatioTypeSession;
        break;
    case StatusTagTotalTransfer:
        statusLabel = StatusTransferTypeTotal;
        break;
    case StatusTagSessionTransfer:
        statusLabel = StatusTransferTypeSession;
        break;
    default:
        NSAssert1(NO, @"Unknown status label tag received: %ld", [sender tag]);
        return;
    }

    [NSUserDefaults.standardUserDefaults setObject:statusLabel forKey:@"StatusLabel"];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

- (void)updateSpeedFieldsToolTips
{
    NSString *uploadText, *downloadText;

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"SpeedLimit"]) {
        NSString* speedString = [NSString stringWithFormat:@"%@ (%@)",
                                                           NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                           NSLocalizedString(@"Alternative Speed Limits", "Status Bar -> speed tooltip")];

        uploadText = [NSString stringWithFormat:speedString, [NSUserDefaults.standardUserDefaults integerForKey:@"SpeedLimitUploadLimit"]];
        downloadText = [NSString stringWithFormat:speedString, [NSUserDefaults.standardUserDefaults integerForKey:@"SpeedLimitDownloadLimit"]];
    } else {
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"CheckUpload"]) {
            uploadText = [NSString localizedStringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                             [NSUserDefaults.standardUserDefaults integerForKey:@"UploadLimit"]];
        } else {
            uploadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        }

        if ([NSUserDefaults.standardUserDefaults boolForKey:@"CheckDownload"]) {
            downloadText = [NSString localizedStringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                               [NSUserDefaults.standardUserDefaults integerForKey:@"DownloadLimit"]];
        } else {
            downloadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        }
    }

    uploadText = [NSLocalizedString(@"Global upload limit", "Status Bar -> speed tooltip") stringByAppendingFormat:@": %@", uploadText];
    downloadText = [NSLocalizedString(@"Global download limit", "Status Bar -> speed tooltip") stringByAppendingFormat:@": %@", downloadText];

    self.fTotalULField.toolTip = uploadText;
    self.fTotalDLField.toolTip = downloadText;
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    //enable sort options
    if (action == @selector(setStatusLabel:)) {
        NSString* statusLabel;
        switch (menuItem.tag) {
        case StatusTagTotalRatio:
            statusLabel = StatusRatioTypeTotal;
            break;
        case StatusTagSessionRatio:
            statusLabel = StatusRatioTypeSession;
            break;
        case StatusTagTotalTransfer:
            statusLabel = StatusTransferTypeTotal;
            break;
        case StatusTagSessionTransfer:
            statusLabel = StatusTransferTypeSession;
            break;
        default:
            NSAssert1(NO, @"Unknown status label tag received: %ld", menuItem.tag);
            statusLabel = StatusRatioTypeTotal;
        }

        menuItem.state = [statusLabel isEqualToString:[NSUserDefaults.standardUserDefaults stringForKey:@"StatusLabel"]] ?
            NSControlStateValueOn :
            NSControlStateValueOff;
        return YES;
    }

    return YES;
}

@end
