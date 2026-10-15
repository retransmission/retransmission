// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/macros.h"

#import "StatsWindowController.h"
#import "Controller.h"
#import "NSStringAdditions.h"

static NSTimeInterval const kUpdateSeconds = 1.0;

@interface StatsWindowController ()<NSWindowRestoration, NSWindowDelegate>

@property(nonatomic) NSTextField* fUploadedField;
@property(nonatomic) NSTextField* fUploadedAllField;
@property(nonatomic) NSTextField* fDownloadedField;
@property(nonatomic) NSTextField* fDownloadedAllField;
@property(nonatomic) NSTextField* fRatioField;
@property(nonatomic) NSTextField* fRatioAllField;
@property(nonatomic) NSTextField* fTimeField;
@property(nonatomic) NSTextField* fTimeAllField;
@property(nonatomic) NSTextField* fNumOpenedField;
@property(nonatomic) NSTimer* fTimer;

@end

@implementation StatsWindowController

static StatsWindowController* fStatsWindowInstance = nil;
static tr_session* fLib = NULL;

+ (StatsWindowController*)statsWindow
{
    if (!fStatsWindowInstance) {
        fStatsWindowInstance = [[self alloc] init];
    }
    return fStatsWindowInstance;
}

- (instancetype)init
{
    fLib = ((Controller*)NSApp.delegate).sessionHandle;
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 330, 240)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                                  backing:NSBackingStoreBuffered
                                                    defer:YES];
    panel.identifier = @"StatsWindow";
    [panel center];
    panel.title = NSLocalizedString(@"Statistics", "Stats window -> title");
    panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    panel.hidesOnDeactivate = NO;
    panel.tabbingMode = NSWindowTabbingModeDisallowed;

    self = [super initWithWindow:panel];
    if (self) {
        panel.delegate = self;
        [self setupUI];
    }
    return self;
}

- (NSTextField*)createLabelWithAlignment:(NSTextAlignment)alignment
{
    NSTextField* field = [NSTextField labelWithString:@""];
    field.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    field.textColor = [NSColor controlTextColor];
    field.alignment = alignment;
    field.lineBreakMode = NSLineBreakByTruncatingTail;

    // label in left column
    BOOL isLabel = alignment == NSTextAlignmentRight;
    auto priority = isLabel ? NSLayoutPriorityRequired : NSLayoutPriorityDefaultLow;

    [field setContentCompressionResistancePriority:priority forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

- (void)setupUI
{
    auto uploadedLabelField = [self createLabelWithAlignment:NSTextAlignmentRight];
    uploadedLabelField.stringValue = NSLocalizedString(@"Uploaded:", "Stats window -> label");

    auto downloadedLabelField = [self createLabelWithAlignment:NSTextAlignmentRight];
    downloadedLabelField.stringValue = NSLocalizedString(@"Downloaded:", "Stats window -> label");

    auto ratioLabelField = [self createLabelWithAlignment:NSTextAlignmentRight];
    ratioLabelField.stringValue = NSLocalizedString(@"Ratio:", "Stats window -> label");

    auto timeLabelField = [self createLabelWithAlignment:NSTextAlignmentRight];
    timeLabelField.stringValue = NSLocalizedString(@"Running time:", "Stats window -> label");

    auto numOpenedLabelField = [self createLabelWithAlignment:NSTextAlignmentRight];
    numOpenedLabelField.stringValue = NSLocalizedString(@"Program started:", "Stats window -> label");

    self.fUploadedField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fUploadedAllField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fDownloadedField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fDownloadedAllField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fRatioField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fRatioAllField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fTimeField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fTimeAllField = [self createLabelWithAlignment:NSTextAlignmentLeft];
    self.fNumOpenedField = [self createLabelWithAlignment:NSTextAlignmentLeft];

    auto resetButton = [NSButton buttonWithTitle:NSLocalizedString(@"Reset", "Stats window -> reset button") target:self
                                          action:@selector(resetStats:)];
    resetButton.controlSize = NSControlSizeSmall;
    resetButton.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    resetButton.translatesAutoresizingMaskIntoConstraints = NO;

    NSArray* rows = @[
        @[ uploadedLabelField, self.fUploadedField ],
        @[ NSGridCell.emptyContentView, self.fUploadedAllField ],
        @[ downloadedLabelField, self.fDownloadedField ],
        @[ NSGridCell.emptyContentView, self.fDownloadedAllField ],
        @[ ratioLabelField, self.fRatioField ],
        @[ NSGridCell.emptyContentView, self.fRatioAllField ],
        @[ timeLabelField, self.fTimeField ],
        @[ NSGridCell.emptyContentView, self.fTimeAllField ],
        @[ numOpenedLabelField, self.fNumOpenedField ]
    ];

    NSGridView* gridView = [NSGridView gridViewWithViews:rows];
    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 4.0;
    gridView.columnSpacing = 8.0;

    [gridView columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [gridView columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;
    gridView.rowAlignment = NSGridRowAlignmentLastBaseline;

    NSView* contentView = self.window.contentView;
    [contentView addSubview:gridView];
    [contentView addSubview:resetButton];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [gridView.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],

        [gridView.leadingAnchor constraintGreaterThanOrEqualToAnchor:contentView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintGreaterThanOrEqualToAnchor:gridView.trailingAnchor constant:20],

        [resetButton.topAnchor constraintEqualToAnchor:gridView.bottomAnchor constant:8],
        [contentView.trailingAnchor constraintEqualToAnchor:resetButton.trailingAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:resetButton.bottomAnchor constant:16]
    ]];

    [self updateStats];

    __weak __auto_type weakSelf = self;
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds repeats:YES block:^(NSTimer* _Nonnull) {
        [weakSelf updateStats];
    }];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSModalPanelRunLoopMode];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSEventTrackingRunLoopMode];

    self.window.restorationClass = [self class];
}

- (void)windowWillClose:(NSNotification*)notification
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

- (NSString*)windowFrameAutosaveName
{
    return @"StatsWindow";
}

- (void)resetStats:(id)sender
{
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"WarningResetStats"]) {
        [self performResetStats];
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = NSLocalizedString(@"Reset your statistics?", "Stats reset -> title");
    alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                           @"This will clear the global statistics displayed by %@."
                                                            " Individual torrent statistics will not be affected.",
                                                           "Stats reset -> message"),
                                                       @TR_PROJ_APPNAME_CAPITALIZED];
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"Reset", "Stats reset -> button")];
    [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Stats reset -> button")];
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

#pragma mark - Private

- (void)updateStats
{
    auto const statsAll = tr_sessionGetCumulativeStats(fLib);
    auto const statsSession = tr_sessionGetStats(fLib);

    NSByteCountFormatter* byteFormatter = [[NSByteCountFormatter alloc] init];
    byteFormatter.allowedUnits = NSByteCountFormatterUseBytes;

    self.fUploadedField.stringValue = [NSString stringForFileSize:statsSession.uploadedBytes];
    self.fUploadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.uploadedBytes];
    self.fUploadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.uploadedBytes]];
    self.fUploadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.uploadedBytes];

    self.fDownloadedField.stringValue = [NSString stringForFileSize:statsSession.downloadedBytes];
    self.fDownloadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.downloadedBytes];
    self.fDownloadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.downloadedBytes]];
    self.fDownloadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.downloadedBytes];

    self.fRatioField.stringValue = [NSString stringForRatio:statsSession.ratio];

    NSString* totalRatioString = static_cast<int>(statsAll.ratio) != TR_RATIO_NA ?
        [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForRatio:statsAll.ratio]] :
        NSLocalizedString(@"Total N/A", "stats total");
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
    self.fTimeAllField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"),
                                                                [timeFormatter stringFromTimeInterval:statsAll.secondsActive]];

    if (statsAll.sessionCount == 1) {
        self.fNumOpenedField.stringValue = NSLocalizedString(@"1 time", "stats window -> times opened");
    } else {
        self.fNumOpenedField.stringValue = [NSString
            localizedStringWithFormat:NSLocalizedString(@"%llu times", "stats window -> times opened"), statsAll.sessionCount];
    }
}

- (void)performResetStats
{
    tr_sessionClearStats(fLib);
    [self updateStats];
}

@end
