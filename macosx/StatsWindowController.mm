// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/macros.h"

#import "StatsWindowController.h"
#import "Controller.h"
#import "NSStringAdditions.h"
#import "L10n.h"

static NSTimeInterval const kUpdateSeconds = 1.0;

@interface StatsWindowController ()<NSWindowRestoration>

@property(nonatomic) IBOutlet NSTextField* fUploadedField;
@property(nonatomic) IBOutlet NSTextField* fUploadedAllField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedAllField;
@property(nonatomic) IBOutlet NSTextField* fRatioField;
@property(nonatomic) IBOutlet NSTextField* fRatioAllField;
@property(nonatomic) IBOutlet NSTextField* fTimeField;
@property(nonatomic) IBOutlet NSTextField* fTimeAllField;
@property(nonatomic) IBOutlet NSTextField* fNumOpenedField;
@property(nonatomic) IBOutlet NSTextField* fUploadedLabelField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedLabelField;
@property(nonatomic) IBOutlet NSTextField* fRatioLabelField;
@property(nonatomic) IBOutlet NSTextField* fTimeLabelField;
@property(nonatomic) IBOutlet NSTextField* fNumOpenedLabelField;
@property(nonatomic) IBOutlet NSButton* fResetButton;
@property(nonatomic) NSTimer* fTimer;

@end

static NSString* totalString(NSString* const amount)
{
    // Translators: stats total
    return TR_FORMAT("{amount} total", TRArg("amount", amount));
}

@implementation StatsWindowController

static StatsWindowController* fStatsWindowInstance = nil;
static tr_session* fLib = NULL;

+ (StatsWindowController*)statsWindow
{
    if (!fStatsWindowInstance) {
        if ((fStatsWindowInstance = [[self alloc] init])) {
            fLib = ((Controller*)NSApp.delegate).sessionHandle;
        }
    }
    return fStatsWindowInstance;
}

- (instancetype)init
{
    return [super initWithWindowNibName:@"StatsWindow"];
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [self updateStats];

    __weak __auto_type weakSelf = self;
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf updateStats];
    }];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSModalPanelRunLoopMode];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSEventTrackingRunLoopMode];

    self.window.restorationClass = [self class];

    // Translators: Stats window -> title
    self.window.title = TR_TEXT("Statistics");

    //disable fullscreen support
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

    //set label text
    // Translators: Stats window -> label
    self.fUploadedLabelField.stringValue = TR_TEXT("Uploaded:");
    // Translators: Stats window -> label
    self.fDownloadedLabelField.stringValue = TR_TEXT("Downloaded:");
    // Translators: Stats window -> label
    self.fRatioLabelField.stringValue = TR_TEXT("Ratio:");
    // Translators: Stats window -> label
    self.fTimeLabelField.stringValue = TR_TEXT("Running time:");
    // Translators: Stats window -> label
    self.fNumOpenedLabelField.stringValue = TR_TEXT("Program started:");

    // Translators: Stats window -> reset button
    self.fResetButton.title = TR_TEXT("Reset");
}

- (void)windowWillClose:(id)sender
{
    [self.fTimer invalidate];
    self.fTimer = nil;
    fStatsWindowInstance = nil;
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSAssert1([identifier isEqualToString:@"StatsWindow"], @"Trying to restore unexpected identifier %@", identifier);

    completionHandler(StatsWindowController.statsWindow.window, nil);
}

- (IBAction)resetStats:(id)sender
{
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"WarningResetStats"]) {
        [self performResetStats];
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = TR_TEXT("Reset your statistics?");
    alert.informativeText = TR_FORMAT(
        "This will clear the global statistics displayed by {appname}. Individual torrent statistics will not be affected.",
        TRAppNameArg());
    alert.alertStyle = NSAlertStyleWarning;
    // Translators: Stats reset -> button
    [alert addButtonWithTitle:TR_TEXT("Reset")];
    [alert addButtonWithTitle:TR_TEXT("Cancel")];
    alert.showsSuppressionButton = YES;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (alert.suppressionButton.state == NSControlStateValueOn) {
            [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningResetStats"];
        }

        if (returnCode == NSAlertFirstButtonReturn) {
            [self performResetStats];
        }
    }];
}

- (NSString*)windowFrameAutosaveName
{
    return @"StatsWindow";
}

#pragma mark - Private

- (void)updateStats
{
    auto const statsAll = tr_sessionGetCumulativeStats(fLib);
    auto const statsSession = tr_sessionGetStats(fLib);

    NSByteCountFormatter* byteFormatter = [[NSByteCountFormatter alloc] init];
    byteFormatter.allowedUnits = NSByteCountFormatterUseBytes;

    self.fUploadedField.stringValue = [NSString stringForFileSize:statsSession.uploadedBytes];
    self.fUploadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.uploadedBytes];
    self.fUploadedAllField.stringValue = totalString([NSString stringForFileSize:statsAll.uploadedBytes]);
    self.fUploadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.uploadedBytes];

    self.fDownloadedField.stringValue = [NSString stringForFileSize:statsSession.downloadedBytes];
    self.fDownloadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.downloadedBytes];
    self.fDownloadedAllField.stringValue = totalString([NSString stringForFileSize:statsAll.downloadedBytes]);
    self.fDownloadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.downloadedBytes];

    self.fRatioField.stringValue = [NSString stringForRatio:statsSession.ratio];

    NSString* totalRatioString = static_cast<int>(statsAll.ratio) != TR_RATIO_NA ?
        totalString([NSString stringForRatio:statsAll.ratio]) :
        // Translators: stats total
        TR_TEXT("Total N/A");
    self.fRatioAllField.stringValue = totalRatioString;

    static NSDateComponentsFormatter* timeFormatter;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        timeFormatter = [NSDateComponentsFormatter new];
        timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
        timeFormatter.maximumUnitCount = 3;
        timeFormatter.allowedUnits = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitWeekOfMonth | NSCalendarUnitDay |
            NSCalendarUnitHour | NSCalendarUnitMinute;
    });

    self.fTimeField.stringValue = [timeFormatter stringFromTimeInterval:statsSession.secondsActive];
    self.fTimeAllField.stringValue = totalString([timeFormatter stringFromTimeInterval:statsAll.secondsActive]);

    self.fNumOpenedField.stringValue = TR_FORMAT_N(
        // Translators: stats window -> times opened
        "{count:L} time",
        "{count:L} times",
        statsAll.sessionCount,
        TRArg("count", statsAll.sessionCount));
}

- (void)performResetStats
{
    tr_sessionClearStats(fLib);
    [self updateStats];
}

@end
