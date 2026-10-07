// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "StatusBarController.h"
#import "NSStringAdditions.h"
#import "Utils.h"
#import "L10n.h"

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

@property(nonatomic) IBOutlet NSButton* fStatusButton;
@property(nonatomic) IBOutlet NSTextField* fTotalDLField;
@property(nonatomic) IBOutlet NSTextField* fTotalULField;
@property(nonatomic) IBOutlet NSImageView* fTotalDLImageView;
@property(nonatomic) IBOutlet NSImageView* fTotalULImageView;

@property(nonatomic, readonly) tr_session* fLib;

@property(nonatomic) CGFloat fPreviousDownloadRate;
@property(nonatomic) CGFloat fPreviousUploadRate;

@end

@implementation StatusBarController

- (instancetype)initWithLib:(tr_session*)lib
{
    if ((self = [super initWithNibName:@"StatusBar" bundle:nil])) {
        _fLib = lib;

        _fPreviousDownloadRate = -1.0;
        _fPreviousUploadRate = -1.0;
    }

    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    //localize menu items
    // Translators: Status Bar -> status menu
    [self.fStatusButton.menu itemWithTag:StatusTagTotalRatio].title = NSLocalizedString(@"Total Ratio", nil);
    // Translators: Status Bar -> status menu
    [self.fStatusButton.menu itemWithTag:StatusTagSessionRatio].title = NSLocalizedString(@"Session Ratio", nil);
    // Translators: Status Bar -> status menu
    [self.fStatusButton.menu itemWithTag:StatusTagTotalTransfer].title = NSLocalizedString(@"Total Transfer", nil);
    // Translators: Status Bar -> status menu
    [self.fStatusButton.menu itemWithTag:StatusTagSessionTransfer].title = NSLocalizedString(@"Session Transfer", nil);

    self.fStatusButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLImageView.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULImageView.cell.backgroundStyle = NSBackgroundStyleRaised;

    [self updateSpeedFieldsToolTips];

    //update when speed limits are changed
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

        // Translators: status bar -> status label
        statusString = [NSLocalizedString(@"Ratio", nil) stringByAppendingFormat:@": %@", [NSString stringForRatio:stats.ratio]];
    } else //StatusTransferTypeTotal or StatusTransferTypeSession
    {
        total = [statusLabel isEqualToString:StatusTransferTypeTotal];

        auto const stats = total ? tr_sessionGetCumulativeStats(self.fLib) : tr_sessionGetStats(self.fLib);

        statusString = [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"Down: %@, Up: %@", @"Formats", nil),
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
    NSUserDefaults* const defaults = NSUserDefaults.standardUserDefaults;
    NSString* (^limitText)(NSString*) = ^(NSString* limitKey) {
        return [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"%lu KB/s", @"Formats", nil),
                                                   static_cast<NSUInteger>([defaults integerForKey:limitKey])];
    };
    // Translators: Status Bar -> speed tooltip
    NSString* const unlimited = NSLocalizedString(@"unlimited", nil);

    NSString *uploadText, *downloadText;
    if ([defaults boolForKey:@"SpeedLimit"]) {
        // Translators: Status Bar -> speed tooltip
        NSString* const altSpeedLimits = NSLocalizedString(@"Alternative Speed Limits", nil);
        uploadText = [NSString stringWithFormat:@"%@ (%@)", limitText(@"SpeedLimitUploadLimit"), altSpeedLimits];
        downloadText = [NSString stringWithFormat:@"%@ (%@)", limitText(@"SpeedLimitDownloadLimit"), altSpeedLimits];
    } else {
        uploadText = [defaults boolForKey:@"CheckUpload"] ? limitText(@"UploadLimit") : unlimited;
        downloadText = [defaults boolForKey:@"CheckDownload"] ? limitText(@"DownloadLimit") : unlimited;
    }

    // Translators: Status Bar -> speed tooltip
    uploadText = [NSLocalizedString(@"Global upload limit", nil) stringByAppendingFormat:@": %@", uploadText];
    // Translators: Status Bar -> speed tooltip
    downloadText = [NSLocalizedString(@"Global download limit", nil) stringByAppendingFormat:@": %@", downloadText];

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
