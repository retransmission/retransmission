// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>
#import <libtransmission/types.h>

#import "PrefsController.h"
#import "PrefsBandwidthTabViewController.h"
#import "PrefsUI.h"
#import "ColorTextField.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesBandwidthTab", @"")

@interface PrefsBandwidthTabViewController ()<NSTextFieldDelegate>

@property(nonatomic) NSButton* fCheckDownloadButton;
@property(nonatomic) NSTextField* fDownloadField;
@property(nonatomic) ColorTextField* fDownloadFieldSuffixLabel; // KB/s
@property(nonatomic) NSButton* fCheckUploadButton;
@property(nonatomic) NSTextField* fUploadField;
@property(nonatomic) ColorTextField* fUploadFieldSuffixLabel; // KB/s

@property(nonatomic) NSTextField* fSpeedLimitDownloadField;
@property(nonatomic) NSTextField* fSpeedLimitUploadField;

@property(nonatomic) NSButton* fSpeedLimitAutoButton;
@property(nonatomic) NSPopUpButton* fAutoSpeedDayTypePopUp;
@property(nonatomic) NSDatePicker* fSpeedLimitAutoOnDatePicker;
@property(nonatomic) ColorTextField* fSpeedLimitAutoOffDatePickerPreposition;
@property(nonatomic) NSDatePicker* fSpeedLimitAutoOffDatePicker;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

// validate textField input default value string.
@property(nonatomic, copy) NSString* fInitialTextFieldString;

@end

@implementation PrefsBandwidthTabViewController

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
}

#pragma mark - UI Generation

- (void)createUIElements
{
    self.fCheckDownloadButton = [PrefsUI checkbox:LStr(@"Download:") target:self action:@selector(applySpeedSettings:)];
    self.fDownloadField = [PrefsUI numberInputFieldWithWidth:60 minimum:0 maximum:99999];
    self.fDownloadField.delegate = self;
    self.fDownloadField.target = self;
    self.fDownloadField.action = @selector(setGlobalLimit:);
    self.fDownloadFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"KB/s")];

    self.fCheckUploadButton = [PrefsUI checkbox:LStr(@"Upload:") target:self action:@selector(applySpeedSettings:)];
    self.fUploadField = [PrefsUI numberInputFieldWithWidth:60 minimum:0 maximum:99999];
    self.fUploadField.delegate = self;
    self.fUploadField.target = self;
    self.fUploadField.action = @selector(setGlobalLimit:);
    self.fUploadFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"KB/s")];

    self.fSpeedLimitDownloadField = [PrefsUI numberInputFieldWithWidth:60 minimum:0 maximum:99999];
    self.fSpeedLimitDownloadField.delegate = self;
    self.fSpeedLimitDownloadField.target = self;
    self.fSpeedLimitDownloadField.action = @selector(setSpeedLimit:);

    self.fSpeedLimitUploadField = [PrefsUI numberInputFieldWithWidth:60 minimum:0 maximum:99999];
    self.fSpeedLimitUploadField.delegate = self;
    self.fSpeedLimitUploadField.target = self;
    self.fSpeedLimitUploadField.action = @selector(setSpeedLimit:);

    self.fSpeedLimitAutoButton = [PrefsUI checkbox:LStr(@"Scheduled times:") target:self action:@selector(setAutoSpeedLimit:)];

    self.fAutoSpeedDayTypePopUp = [PrefsUI popUpButton];

    [self configureAutoSpeedDayTypePopup];

    self.fAutoSpeedDayTypePopUp.target = self;
    self.fAutoSpeedDayTypePopUp.action = @selector(setAutoSpeedLimitDay:);

    self.fSpeedLimitAutoOnDatePicker = [[NSDatePicker alloc] init];
    self.fSpeedLimitAutoOnDatePicker.translatesAutoresizingMaskIntoConstraints = NO;
    self.fSpeedLimitAutoOnDatePicker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    self.fSpeedLimitAutoOnDatePicker.datePickerElements = NSDatePickerElementFlagHourMinute;
    self.fSpeedLimitAutoOnDatePicker.target = self;
    self.fSpeedLimitAutoOnDatePicker.action = @selector(setAutoSpeedLimitTime:);

    self.fSpeedLimitAutoOffDatePickerPreposition = [ColorTextField labelWithString:LStr(@"to")];

    self.fSpeedLimitAutoOffDatePicker = [[NSDatePicker alloc] init];
    self.fSpeedLimitAutoOffDatePicker.translatesAutoresizingMaskIntoConstraints = NO;
    self.fSpeedLimitAutoOffDatePicker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    self.fSpeedLimitAutoOffDatePicker.datePickerElements = NSDatePickerElementFlagHourMinute;
    self.fSpeedLimitAutoOffDatePicker.target = self;
    self.fSpeedLimitAutoOffDatePicker.action = @selector(setAutoSpeedLimitTime:);
}

- (void)configureAutoSpeedDayTypePopup
{
    NSDateFormatter* dateFormatter = [[NSDateFormatter alloc] init];
    NSArray<NSString*>* weekdays = [dateFormatter standaloneWeekdaySymbols];
    NSMutableArray* weekdaysResult = [NSMutableArray arrayWithArray:weekdays];

    // first is sunday.
    NSString* sunday = [weekdaysResult firstObject];
    [weekdaysResult removeObjectAtIndex:0];
    [weekdaysResult addObject:sunday];
    NSArray* orderedWeekdays = [NSArray arrayWithArray:weekdaysResult];

    NSString* strEveryDay = LStr(@"Every Day");
    NSString* strWeekdays = LStr(@"Weekdays");
    NSString* strWeekends = LStr(@"Weekends");

    NSArray* menuItems = @[
        @{ @"title" : strEveryDay, @"tag" : @(TR_SCHED_ALL) },
        @{ @"title" : strWeekdays, @"tag" : @(TR_SCHED_WEEKDAY) },
        @{ @"title" : strWeekends, @"tag" : @(TR_SCHED_WEEKEND) },

        @{ @"title" : @"---SEPARATOR---", @"tag" : @(-1) },

        @{ @"title" : orderedWeekdays[0], @"tag" : @(TR_SCHED_MON) },
        @{ @"title" : orderedWeekdays[1], @"tag" : @(TR_SCHED_TUES) },
        @{ @"title" : orderedWeekdays[2], @"tag" : @(TR_SCHED_WED) },
        @{ @"title" : orderedWeekdays[3], @"tag" : @(TR_SCHED_THURS) },
        @{ @"title" : orderedWeekdays[4], @"tag" : @(TR_SCHED_FRI) },
        @{ @"title" : orderedWeekdays[5], @"tag" : @(TR_SCHED_SAT) },
        @{ @"title" : orderedWeekdays[6], @"tag" : @(TR_SCHED_SUN) }
    ];

    [self.fAutoSpeedDayTypePopUp removeAllItems];

    for (NSDictionary* item in menuItems) {
        if ([item[@"title"] isEqualToString:@"---SEPARATOR---"]) {
            [self.fAutoSpeedDayTypePopUp.menu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        [self.fAutoSpeedDayTypePopUp addItemWithTitle:item[@"title"]];
        self.fAutoSpeedDayTypePopUp.lastItem.tag = [item[@"tag"] integerValue];
    }
}

#pragma mark - Layout Setup
- (NSView*)spacer
{
    NSView* spacer = [[NSView alloc] init];
    spacer.translatesAutoresizingMaskIntoConstraints = NO;

    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [spacer setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    return spacer;
}

- (void)setupLayoutInView:(NSView*)parent
{
    NSGridView* gridView = [[NSGridView alloc] init];
    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 10;
    gridView.columnSpacing = 8;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    NSTextField* globalTitle = [NSTextField labelWithString:LStr(@"Speed limits:")];
    globalTitle.font = [NSFont systemFontOfSize:NSFont.systemFontSize weight:NSFontWeightBold];

    NSGridRow* r1 = [gridView addRowWithViews:@[
        globalTitle,
        NSGridCell.emptyContentView,
        NSGridCell.emptyContentView,
        NSGridCell.emptyContentView
    ]];
    NSInteger r1Index = [gridView indexOfRow:r1];
    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 4) verticalRange:NSMakeRange(r1Index, 1)];

    [gridView addRowWithViews:@[
        [PrefsUI indentedView:self.fCheckDownloadButton],
        self.fDownloadField,
        self.fDownloadFieldSuffixLabel,
        self.spacer
    ]];
    [gridView addRowWithViews:@[
        [PrefsUI indentedView:self.fCheckUploadButton],
        self.fUploadField,
        self.fUploadFieldSuffixLabel,
        self.spacer
    ]];

    NSTextField* altTitle = [NSTextField labelWithString:LStr(@"Alternative speed limits:")];
    altTitle.font = [NSFont systemFontOfSize:NSFont.systemFontSize weight:NSFontWeightBold];

    NSImageView* tortoise = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"tortoise.fill"
                                                                      accessibilityDescription:nil]];
    tortoise.imageScaling = NSImageScaleProportionallyDown;
    NSStackView* altTitleStackView = [[NSStackView alloc] init];
    altTitleStackView.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    altTitleStackView.alignment = NSLayoutAttributeCenterY;
    altTitleStackView.spacing = 6;

    [altTitleStackView addArrangedSubview:tortoise];
    [altTitleStackView addArrangedSubview:altTitle];

    NSGridRow* r4 = [gridView addRowWithViews:@[ altTitleStackView ]];
    r4.topPadding = 14;
    NSInteger r4Index = [gridView indexOfRow:r4];
    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 4) verticalRange:NSMakeRange(r4Index, 1)];

    NSTextField* dlAltLabel = [NSTextField labelWithString:LStr(@"Download:")];
    [gridView addRowWithViews:@[
        [PrefsUI indentedView:dlAltLabel],
        self.fSpeedLimitDownloadField,
        [NSTextField labelWithString:LStr(@"KB/s")],
        self.spacer
    ]];

    NSTextField* ulAltLabel = [NSTextField labelWithString:LStr(@"Upload:")];
    [gridView addRowWithViews:@[
        [PrefsUI indentedView:ulAltLabel],
        self.fSpeedLimitUploadField,
        [NSTextField labelWithString:LStr(@"KB/s")],
        self.spacer
    ]];

    NSTextField* hintLabel = [PrefsUI smallLabel:LStr(@"Override normal speed limits manually or at scheduled times")];
    hintLabel.lineBreakMode = NSLineBreakByWordWrapping;
    hintLabel.maximumNumberOfLines = 0;
    hintLabel.preferredMaxLayoutWidth = 460;

    NSGridRow* r7 = [gridView addRowWithViews:@[ [PrefsUI indentedView:hintLabel] ]];
    NSInteger r7Index = [gridView indexOfRow:r7];
    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 4) verticalRange:NSMakeRange(r7Index, 1)];

    NSGridRow* r8 = [gridView addRowWithViews:@[ self.fSpeedLimitAutoButton ]];
    NSInteger r8Index = [gridView indexOfRow:r8];
    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 4) verticalRange:NSMakeRange(r8Index, 1)];

    NSStackView* const scheduleControlsStack = [NSStackView stackViewWithViews:@[
        self.fAutoSpeedDayTypePopUp,
        self.fSpeedLimitAutoOnDatePicker,
        self.fSpeedLimitAutoOffDatePickerPreposition,
        self.fSpeedLimitAutoOffDatePicker
    ]];
    scheduleControlsStack.spacing = 6;
    scheduleControlsStack.alignment = NSLayoutAttributeCenterY;

    [self.fSpeedLimitAutoOnDatePicker.widthAnchor constraintEqualToConstant:83].active = YES;
    [self.fSpeedLimitAutoOffDatePicker.widthAnchor constraintEqualToConstant:83].active = YES;

    NSGridRow* r9 = [gridView addRowWithViews:@[ [PrefsUI indentedView:scheduleControlsStack] ]];
    NSInteger r9Index = [gridView indexOfRow:r9];
    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 4) verticalRange:NSMakeRange(r9Index, 1)];

    [gridView columnAtIndex:1].xPlacement = NSGridCellPlacementTrailing;

    [parent addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:parent.topAnchor],
        [gridView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        [gridView.leadingAnchor constraintGreaterThanOrEqualToAnchor:parent.leadingAnchor],
        [gridView.centerXAnchor constraintEqualToAnchor:parent.centerXAnchor],
        [gridView.trailingAnchor constraintLessThanOrEqualToAnchor:parent.trailingAnchor]
    ]];
}

#pragma mark - Cocoa Bindings

- (void)setupBindings
{
    NSUserDefaultsController* defaultsController = [NSUserDefaultsController sharedUserDefaultsController];

    [self.fCheckDownloadButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckDownload" options:nil];
    [self.fCheckUploadButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];
    [self.fSpeedLimitAutoButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAuto" options:nil];

    [self.fDownloadField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.DownloadLimit" options:nil];
    [self.fDownloadField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckDownload" options:nil];

    [self.fDownloadFieldSuffixLabel bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckDownload"
                                 options:nil];

    [self.fUploadField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.UploadLimit" options:nil];
    [self.fUploadField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];

    [self.fUploadFieldSuffixLabel bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];

    [self.fSpeedLimitDownloadField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitDownloadLimit"
                                options:nil];
    [self.fSpeedLimitUploadField bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitUploadLimit"
                              options:nil];

    [self.fAutoSpeedDayTypePopUp bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAuto" options:nil];
    [self.fAutoSpeedDayTypePopUp bind:NSSelectedTagBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAutoDay"
                              options:nil];

    [self.fSpeedLimitAutoOnDatePicker bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAuto"
                                   options:nil];
    [self.fSpeedLimitAutoOnDatePicker bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAutoOnDate"
                                   options:nil];

    [self.fSpeedLimitAutoOffDatePickerPreposition bind:NSEnabledBinding toObject:defaultsController
                                           withKeyPath:@"values.SpeedLimitAuto"
                                               options:nil];

    [self.fSpeedLimitAutoOffDatePicker bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.SpeedLimitAuto"
                                    options:nil];
    [self.fSpeedLimitAutoOffDatePicker bind:NSValueBinding toObject:defaultsController
                                withKeyPath:@"values.SpeedLimitAutoOffDate"
                                    options:nil];
}

#pragma mark - Actions & Transmission Logic

- (void)setGlobalLimit:(id)sender
{
    NSString* key = (sender == self.fUploadField) ? @"UploadLimit" : @"DownloadLimit";
    [self.fDefaults setInteger:[sender integerValue] forKey:key];
    [self applySpeedSettings:self];
}

- (void)applySpeedSettings:(id)sender
{
    tr_sessionLimitSpeed(self.fHandle, tr_direction::Up, [self.fDefaults boolForKey:@"CheckUpload"]);
    tr_sessionSetSpeedLimit_KBps(self.fHandle, tr_direction::Up, [self.fDefaults integerForKey:@"UploadLimit"]);
    tr_sessionLimitSpeed(self.fHandle, tr_direction::Down, [self.fDefaults boolForKey:@"CheckDownload"]);
    tr_sessionSetSpeedLimit_KBps(self.fHandle, tr_direction::Down, [self.fDefaults integerForKey:@"DownloadLimit"]);
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (void)setSpeedLimit:(id)sender
{
    NSString* key = (sender == self.fSpeedLimitUploadField) ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit";
    [self.fDefaults setInteger:[sender integerValue] forKey:key];
    [self applyAltSpeedSettings];
}

- (void)applyAltSpeedSettings
{
    tr_sessionSetAltSpeed_KBps(self.fHandle, tr_direction::Up, [self.fDefaults integerForKey:@"SpeedLimitUploadLimit"]);
    tr_sessionSetAltSpeed_KBps(self.fHandle, tr_direction::Down, [self.fDefaults integerForKey:@"SpeedLimitDownloadLimit"]);
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (void)setAutoSpeedLimit:(id)sender
{
    tr_sessionUseAltSpeedTime(self.fHandle, [self.fDefaults boolForKey:@"SpeedLimitAuto"]);
}

- (void)setAutoSpeedLimitTime:(id)sender
{
    tr_sessionSetAltSpeedBegin(self.fHandle, [PrefsController dateToTimeSum:[self.fDefaults objectForKey:@"SpeedLimitAutoOnDate"]]);
    tr_sessionSetAltSpeedEnd(self.fHandle, [PrefsController dateToTimeSum:[self.fDefaults objectForKey:@"SpeedLimitAutoOffDate"]]);
}

- (void)setAutoSpeedLimitDay:(id)sender
{
    tr_sessionSetAltSpeedDay(self.fHandle, static_cast<tr_sched_day>([sender selectedItem].tag));
}

#pragma mark - NSTextFieldDelegate

// Input validation
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

@end
