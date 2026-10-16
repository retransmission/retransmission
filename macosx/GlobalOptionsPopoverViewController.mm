// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#import "GlobalOptionsPopoverViewController.h"
#import "ColorTextField.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"GlobalOptionsPopover", @"")

@interface GlobalOptionsPopoverViewController ()<NSTextFieldDelegate>

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic) NSTextField* fUploadLimitField;
@property(nonatomic) NSTextField* fDownloadLimitField;

@property(nonatomic) NSTextField* fRatioStopField;
@property(nonatomic) NSTextField* fIdleStopField;

@property(nonatomic, copy) NSString* fInitialString;

@end

@implementation GlobalOptionsPopoverViewController

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
    [self setupUI];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self configureContent];
}

- (void)setupUI
{
    NSView* mainView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 325, 0)];
    mainView.translatesAutoresizingMaskIntoConstraints = NO;

    [NSLayoutConstraint activateConstraints:@[ [mainView.widthAnchor constraintGreaterThanOrEqualToConstant:325] ]];

    NSButton* statusSelectedButton = [NSButton checkboxWithTitle:LStr(@"Status of selected files") target:self
                                                          action:@selector(updatedDisplayString:)];
    NSButton* remainingTimeButton = [NSButton checkboxWithTitle:LStr(@"Remaining time") target:self
                                                         action:@selector(updatedDisplayString:)];

    NSTextField* speedHeader = [NSTextField labelWithString:LStr(@"Speed")];
    speedHeader.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];

    NSButton* limitDownButton = [NSButton checkboxWithTitle:LStr(@"Limit download speed:") target:self
                                                     action:@selector(setDownSpeedSetting:)];
    self.fDownloadLimitField = [self limitFieldWithAction:@selector(setDownSpeedLimit:)];
    NSTextField* kbpsDownLabel = [ColorTextField labelWithString:LStr(@"KB/s")];

    NSButton* limitUpButton = [NSButton checkboxWithTitle:LStr(@"Limit upload speed:") target:self
                                                   action:@selector(setUpSpeedSetting:)];
    self.fUploadLimitField = [self limitFieldWithAction:@selector(setUpSpeedLimit:)];
    NSTextField* kbpsUpLabel = [ColorTextField labelWithString:LStr(@"KB/s")];

    NSTextField* seedingHeader = [NSTextField labelWithString:LStr(@"Seeding Limits")];
    seedingHeader.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];

    NSButton* stopRatioButton = [NSButton checkboxWithTitle:LStr(@"Stop seeding at ratio:") target:self
                                                     action:@selector(setRatioStopSetting:)];
    self.fRatioStopField = [self limitFieldWithAction:@selector(setRatioStopLimit:)];

    NSButton* stopIdleButton = [NSButton checkboxWithTitle:LStr(@"Stop seeding if idle for:") target:self
                                                    action:@selector(setIdleStopSetting:)];
    self.fIdleStopField = [self limitFieldWithAction:@selector(setIdleStopLimit:)];
    NSTextField* minutesLabel = [ColorTextField labelWithString:LStr(@"minutes")];

    NSStackView* topRowStack = [NSStackView stackViewWithViews:@[ statusSelectedButton, remainingTimeButton ]];
    topRowStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    topRowStack.spacing = 12;

    NSArray<NSControl*>* allControls = @[
        statusSelectedButton,
        remainingTimeButton,
        limitDownButton,
        limitUpButton,
        stopRatioButton,
        stopIdleButton,
        self.fDownloadLimitField,
        self.fUploadLimitField,
        self.fRatioStopField,
        self.fIdleStopField,
        kbpsDownLabel,
        kbpsUpLabel,
        minutesLabel
    ];

    for (NSControl* control in allControls) {
        control.controlSize = NSControlSizeSmall;
        control.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeSmall]];
    }

    NSNumberFormatter* speedFormatter = [[NSNumberFormatter alloc] init];
    speedFormatter.allowsFloats = NO;
    speedFormatter.usesGroupingSeparator = NO;
    speedFormatter.minimumIntegerDigits = 1;
    speedFormatter.minimum = @0;
    speedFormatter.maximum = @99999;
    self.fDownloadLimitField.formatter = speedFormatter;
    self.fUploadLimitField.formatter = speedFormatter;

    NSNumberFormatter* idleFormatter = [[NSNumberFormatter alloc] init];
    idleFormatter.allowsFloats = NO;
    idleFormatter.usesGroupingSeparator = NO;
    idleFormatter.minimum = @1;
    idleFormatter.maximum = @40320;
    self.fIdleStopField.formatter = idleFormatter;

    NSNumberFormatter* floatFormatter = [[NSNumberFormatter alloc] init];
    floatFormatter.usesGroupingSeparator = NO;
    floatFormatter.minimumIntegerDigits = 1;
    floatFormatter.minimumFractionDigits = 2;
    floatFormatter.maximumFractionDigits = 2;
    floatFormatter.minimum = @0;
    floatFormatter.maximum = @99999;
    self.fRatioStopField.formatter = floatFormatter;

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ topRowStack, NSGridCell.emptyContentView, NSGridCell.emptyContentView ],
        @[ speedHeader, NSGridCell.emptyContentView, NSGridCell.emptyContentView ],
        @[ limitDownButton, self.fDownloadLimitField, kbpsDownLabel ],
        @[ limitUpButton, self.fUploadLimitField, kbpsUpLabel ],
        @[ seedingHeader, NSGridCell.emptyContentView, NSGridCell.emptyContentView ],
        @[ stopRatioButton, self.fRatioStopField, NSGridCell.emptyContentView ],
        @[ stopIdleButton, self.fIdleStopField, minutesLabel ]
    ]];

    [gridView mergeCellsInHorizontalRange:NSMakeRange(0, 3) verticalRange:NSMakeRange(0, 1)];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 5;
    gridView.columnSpacing = 8;
    gridView.rowAlignment = NSGridRowAlignmentFirstBaseline;
    gridView.xPlacement = NSGridCellPlacementLeading;

    [mainView addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.leadingAnchor constraintEqualToAnchor:mainView.leadingAnchor constant:12],
        [gridView.trailingAnchor constraintEqualToAnchor:mainView.trailingAnchor constant:-12],
        [gridView.topAnchor constraintEqualToAnchor:mainView.topAnchor constant:12],
        [gridView.bottomAnchor constraintEqualToAnchor:mainView.bottomAnchor constant:-12],

        [self.fDownloadLimitField.widthAnchor constraintEqualToConstant:45],
        [self.fUploadLimitField.widthAnchor constraintEqualToConstant:45],
        [self.fRatioStopField.widthAnchor constraintEqualToConstant:45],
        [self.fIdleStopField.widthAnchor constraintEqualToConstant:45]
    ]];

    NSUserDefaultsController* defaultsController = [NSUserDefaultsController sharedUserDefaultsController];

    [limitDownButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckDownload" options:nil];
    [limitUpButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];
    [stopRatioButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.RatioCheck" options:nil];
    [stopIdleButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.IdleLimitCheck" options:nil];
    [statusSelectedButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.DisplayStatusProgressSelected"
                       options:nil];
    [remainingTimeButton bind:NSValueBinding toObject:defaultsController withKeyPath:@"values.DisplaySmallStatusRegular"
                      options:@{ NSValueTransformerNameBindingOption : NSNegateBooleanTransformerName }];

    [self.fDownloadLimitField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckDownload" options:nil];
    [self.fUploadLimitField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];
    [self.fRatioStopField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.RatioCheck" options:nil];
    [self.fIdleStopField bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.IdleLimitCheck" options:nil];
    [kbpsDownLabel bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckDownload" options:nil];
    [kbpsUpLabel bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.CheckUpload" options:nil];
    [minutesLabel bind:NSEnabledBinding toObject:defaultsController withKeyPath:@"values.IdleLimitCheck" options:nil];

    [statusSelectedButton bind:NSHiddenBinding toObject:defaultsController withKeyPath:@"values.SmallView" options:nil];
    [remainingTimeButton bind:NSHiddenBinding toObject:defaultsController withKeyPath:@"values.SmallView"
                      options:@{ NSValueTransformerNameBindingOption : NSNegateBooleanTransformerName }];

    self.view = mainView;
}

- (NSTextField*)limitFieldWithAction:(SEL)action
{
    NSTextField* field = [NSTextField textFieldWithString:@""];
    field.target = self;
    field.action = action;
    field.delegate = self;
    field.alignment = NSTextAlignmentRight;
    return field;
}

- (void)configureContent
{
    self.fUploadLimitField.integerValue = [self.fDefaults integerForKey:@"UploadLimit"];
    self.fDownloadLimitField.integerValue = [self.fDefaults integerForKey:@"DownloadLimit"];

    self.fRatioStopField.floatValue = [self.fDefaults floatForKey:@"RatioLimit"];
    self.fIdleStopField.integerValue = [self.fDefaults integerForKey:@"IdleLimitMinutes"];

    [self.view setFrameSize:self.view.fittingSize];
}

- (IBAction)updatedDisplayString:(id)sender
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"RefreshTorrentTable" object:nil];
}

- (IBAction)setDownSpeedSetting:(id)sender
{
    tr_sessionLimitSpeed(self.fHandle, tr_direction::Down, [self.fDefaults boolForKey:@"CheckDownload"]);

    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setDownSpeedLimit:(id)sender
{
    NSInteger const limit = [sender integerValue];
    [self.fDefaults setInteger:limit forKey:@"DownloadLimit"];
    tr_sessionSetSpeedLimit_KBps(self.fHandle, tr_direction::Down, limit);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateSpeedLimitValuesOutsidePrefs" object:nil];
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setUpSpeedSetting:(id)sender
{
    tr_sessionLimitSpeed(self.fHandle, tr_direction::Up, [self.fDefaults boolForKey:@"CheckUpload"]);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateSpeedLimitValuesOutsidePrefs" object:nil];
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setUpSpeedLimit:(id)sender
{
    NSInteger const limit = [sender integerValue];
    [self.fDefaults setInteger:limit forKey:@"UploadLimit"];
    tr_sessionSetSpeedLimit_KBps(self.fHandle, tr_direction::Up, limit);

    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setRatioStopSetting:(id)sender
{
    tr_sessionSetRatioLimited(self.fHandle, [self.fDefaults boolForKey:@"RatioCheck"]);

    //reload main table for seeding progress
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setRatioStopLimit:(id)sender
{
    CGFloat const value = [sender floatValue];
    [self.fDefaults setFloat:value forKey:@"RatioLimit"];
    tr_sessionSetRatioLimit(self.fHandle, value);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateRatioStopValueOutsidePrefs" object:nil];

    //reload main table for seeding progress
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setIdleStopSetting:(id)sender
{
    tr_sessionSetIdleLimited(self.fHandle, [self.fDefaults boolForKey:@"IdleLimitCheck"]);

    //reload main table for remaining seeding time
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setIdleStopLimit:(id)sender
{
    NSInteger const value = [sender integerValue];
    [self.fDefaults setInteger:value forKey:@"IdleLimitMinutes"];
    tr_sessionSetIdleLimit(self.fHandle, value);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateIdleStopValueOutsidePrefs" object:nil];

    //reload main table for remaining seeding time
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialString = control.stringValue;

    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialString) {
        control.stringValue = self.fInitialString;
        self.fInitialString = nil;
    }
    return NO;
}

@end
