// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <libtransmission/transmission.h>
#import "PrefsTransfersManagementViewController.h"
#import "PrefsUI.h"
#import "ColorTextField.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"PreferencesTransfersTab", @"")

@interface PrefsTransfersManagementViewController ()<NSTextFieldDelegate>

@property(nonatomic) NSButton* fRatioCheckButton;
@property(nonatomic) NSTextField* fRatioStopField;
@property(nonatomic) NSButton* fIdleLimitCheckButton;
@property(nonatomic) NSTextField* fIdleStopField;
@property(nonatomic) ColorTextField* fIdleStopFieldSuffixLabel;
@property(nonatomic) NSButton* fRemoveWhenFinishSeedingButton;

@property(nonatomic) NSButton* fQueueDownloadButton;
@property(nonatomic) NSTextField* fQueueDownloadField;
@property(nonatomic) ColorTextField* fQueueDownloadFieldSuffixLabel;
@property(nonatomic) NSButton* fQueueSeedButton;
@property(nonatomic) NSTextField* fQueueSeedField;
@property(nonatomic) ColorTextField* fQueueSeedFieldSuffixLabel;
@property(nonatomic) NSButton* fCheckStalledButton;
@property(nonatomic) NSTextField* fStalledField;
@property(nonatomic) ColorTextField* fStalledFieldSuffixLabel;
@property(nonatomic) NSButton* fVerifyDownloadDataButton;

@property(nonatomic) NSButton* fPlayDownloadSoundButton;
@property(nonatomic) NSPopUpButton* fDownloadSoundPopUp;
@property(nonatomic) NSButton* fPlaySeedingSoundButton;
@property(nonatomic) NSPopUpButton* fSeedingSoundPopUp;

@property(nonatomic) NSButton* fDoneScriptEnabledButton;
@property(nonatomic) NSPopUpButton* fDoneScriptPopUp;

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@property(nonatomic, copy) NSString* fInitialTextFieldString;

@end

@implementation PrefsTransfersManagementViewController

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
    containerView.translatesAutoresizingMaskIntoConstraints = NO;
    NSView* contentView = [[NSView alloc] init];
    contentView.translatesAutoresizingMaskIntoConstraints = NO;
    [containerView addSubview:contentView];

    [self createUIElements];
    [self setupLayoutInView:contentView];

    [NSLayoutConstraint activateConstraints:@[
        [contentView.topAnchor constraintEqualToAnchor:containerView.topAnchor constant:20],
        [contentView.bottomAnchor constraintEqualToAnchor:containerView.bottomAnchor constant:-40],
        [contentView.leadingAnchor constraintEqualToAnchor:containerView.leadingAnchor constant:20],
        [contentView.trailingAnchor constraintEqualToAnchor:containerView.trailingAnchor constant:-20],
    ]];

    self.view = containerView;
}

- (void)createUIElements
{
    self.fIdleStopFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"minutes")];
    self.fQueueDownloadFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"active torrents")];
    self.fQueueSeedFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"active torrents")];
    self.fStalledFieldSuffixLabel = [ColorTextField labelWithString:LStr(@"minutes")];

    self.fRatioCheckButton = [PrefsUI checkbox:LStr(@"Stop seeding at ratio:") target:self action:@selector(applyRatioSetting:)];
    self.fRatioStopField = [PrefsUI decimalInputFieldWithWidth:50 minimum:0 maximum:99999 minDigits:2 maxDigits:2];
    self.fRatioStopField.delegate = self;
    self.fRatioStopField.target = self;
    self.fRatioStopField.action = @selector(setRatioStop:);

    self.fIdleLimitCheckButton = [PrefsUI checkbox:LStr(@"Stop seeding if idle for:") target:self
                                            action:@selector(applyIdleStopSetting:)];
    self.fIdleStopField = [PrefsUI numberInputFieldWithWidth:41 minimum:1 maximum:40320];
    self.fIdleStopField.delegate = self;
    self.fIdleStopField.target = self;
    self.fIdleStopField.action = @selector(setIdleStop:);

    self.fRemoveWhenFinishSeedingButton = [PrefsUI checkbox:LStr(@"Remove from the torrent list when seeding completes") target:nil
                                                     action:nil];

    self.fQueueDownloadButton = [PrefsUI checkbox:LStr(@"Download with maximum of:") target:self action:@selector(setQueue:)];
    self.fQueueDownloadField = [PrefsUI numberInputFieldWithWidth:35 minimum:1 maximum:99999];
    self.fQueueDownloadField.delegate = self;
    self.fQueueDownloadField.target = self;
    self.fQueueDownloadField.action = @selector(setQueueNumber:);

    self.fQueueSeedButton = [PrefsUI checkbox:LStr(@"Seeding with maximum of:") target:self action:@selector(setQueue:)];
    self.fQueueSeedField = [PrefsUI numberInputFieldWithWidth:35 minimum:1 maximum:99999];
    self.fQueueSeedField.delegate = self;
    self.fQueueSeedField.target = self;
    self.fQueueSeedField.action = @selector(setQueueNumber:);

    self.fCheckStalledButton = [PrefsUI checkbox:LStr(@"Torrent is stalled when inactive for:") target:self
                                          action:@selector(setStalled:)];
    self.fStalledField = [PrefsUI numberInputFieldWithWidth:41 minimum:1 maximum:99999];
    self.fStalledField.delegate = self;
    self.fStalledField.target = self;
    self.fStalledField.action = @selector(setStalledMinutes:);

    self.fVerifyDownloadDataButton = [PrefsUI checkbox:LStr(@"Verify data when download completes") target:self
                                                action:@selector(setVerifyDataOnCompletion:)];

    self.fPlayDownloadSoundButton = [PrefsUI checkbox:LStr(@"When download completes:") target:nil action:nil];
    self.fDownloadSoundPopUp = [PrefsUI popUpButton];
    self.fDownloadSoundPopUp.target = self;
    self.fDownloadSoundPopUp.action = @selector(setSound:);

    self.fPlaySeedingSoundButton = [PrefsUI checkbox:LStr(@"When seeding completes:") target:nil action:nil];
    self.fSeedingSoundPopUp = [PrefsUI popUpButton];
    self.fSeedingSoundPopUp.target = self;
    self.fSeedingSoundPopUp.action = @selector(setSound:);

    self.fDoneScriptEnabledButton = [PrefsUI checkbox:LStr(@"When download completes, run script:") target:self
                                               action:@selector(setDoneScriptEnabled:)];
    self.fDoneScriptPopUp = [PrefsUI popUpButton];

    [self configurePopupButtons];
}

- (void)setupLayoutInView:(NSView*)parent
{
    NSTextField* ratioHint = [PrefsUI smallLabel:LStr(@"Ratio is amount uploaded to amount downloaded")];
    NSTextField* newlyHint = [PrefsUI smallLabel:LStr(@"Applies to newly added torrents")];

    NSStackView* ratioStack = [NSStackView stackViewWithViews:@[ self.fRatioCheckButton, self.fRatioStopField ]];
    ratioStack.spacing = 6;
    ratioStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* idleStack = [NSStackView
        stackViewWithViews:@[ self.fIdleLimitCheckButton, self.fIdleStopField, self.fIdleStopFieldSuffixLabel ]];
    idleStack.spacing = 6;
    idleStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* qDownStack = [NSStackView
        stackViewWithViews:@[ self.fQueueDownloadButton, self.fQueueDownloadField, self.fQueueDownloadFieldSuffixLabel ]];
    qDownStack.spacing = 6;
    qDownStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* qSeedStack = [NSStackView
        stackViewWithViews:@[ self.fQueueSeedButton, self.fQueueSeedField, self.fQueueSeedFieldSuffixLabel ]];
    qSeedStack.spacing = 6;
    qSeedStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* stalledStack = [NSStackView
        stackViewWithViews:@[ self.fCheckStalledButton, self.fStalledField, self.fStalledFieldSuffixLabel ]];
    stalledStack.spacing = 6;
    stalledStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* soundDownStack = [NSStackView stackViewWithViews:@[ self.fPlayDownloadSoundButton, self.fDownloadSoundPopUp ]];
    soundDownStack.spacing = 6;
    soundDownStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* soundSeedStack = [NSStackView stackViewWithViews:@[ self.fPlaySeedingSoundButton, self.fSeedingSoundPopUp ]];
    soundSeedStack.spacing = 6;
    soundSeedStack.alignment = NSLayoutAttributeCenterY;

    NSStackView* scriptStack = [NSStackView stackViewWithViews:@[ self.fDoneScriptEnabledButton, self.fDoneScriptPopUp ]];
    scriptStack.spacing = 6;
    scriptStack.alignment = NSLayoutAttributeCenterY;

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ [PrefsUI sectionTitle:LStr(@"Limits:")] ],
        @[ [PrefsUI indentedView:ratioStack] ],
        @[ [PrefsUI indentedView:ratioHint level:2] ],
        @[ [PrefsUI indentedView:idleStack] ],
        @[ [PrefsUI indentedView:self.fRemoveWhenFinishSeedingButton] ],
        @[ [PrefsUI indentedView:newlyHint level:2] ],

        @[ [PrefsUI sectionTitle:LStr(@"Queues:")] ],
        @[ [PrefsUI indentedView:qDownStack] ],
        @[ [PrefsUI indentedView:qSeedStack] ],
        @[ [PrefsUI indentedView:stalledStack] ],
        @[ [PrefsUI indentedView:self.fVerifyDownloadDataButton] ],

        @[ [PrefsUI sectionTitle:LStr(@"Play sound:")] ],
        @[ [PrefsUI indentedView:soundDownStack] ],
        @[ [PrefsUI indentedView:soundSeedStack] ],

        @[ [PrefsUI sectionTitle:LStr(@"Call script:")] ],
        @[ [PrefsUI indentedView:scriptStack] ]
    ]];

    gridView.translatesAutoresizingMaskIntoConstraints = NO;
    gridView.rowSpacing = 10;
    gridView.columnSpacing = 8;
    gridView.xPlacement = NSGridCellPlacementLeading;
    gridView.yPlacement = NSGridCellPlacementCenter;

    [gridView rowAtIndex:6].topPadding = 14;
    [gridView rowAtIndex:11].topPadding = 14;
    [gridView rowAtIndex:14].topPadding = 14;

    [parent addSubview:gridView];

    [NSLayoutConstraint activateConstraints:@[
        [gridView.topAnchor constraintEqualToAnchor:parent.topAnchor],
        [gridView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        [gridView.leadingAnchor constraintGreaterThanOrEqualToAnchor:parent.leadingAnchor],
        [gridView.trailingAnchor constraintLessThanOrEqualToAnchor:parent.trailingAnchor],
        [gridView.centerXAnchor constraintEqualToAnchor:parent.centerXAnchor]
    ]];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    NSUserDefaultsController* dc = [NSUserDefaultsController sharedUserDefaultsController];

    // Limits
    [self.fRatioCheckButton bind:NSValueBinding toObject:dc withKeyPath:@"values.RatioCheck" options:nil];
    [self.fRatioStopField bind:NSValueBinding toObject:dc withKeyPath:@"values.RatioLimit" options:nil];
    [self.fRatioStopField bind:NSEnabledBinding toObject:dc withKeyPath:@"values.RatioCheck" options:nil];

    [self.fIdleLimitCheckButton bind:NSValueBinding toObject:dc withKeyPath:@"values.IdleLimitCheck" options:nil];
    [self.fIdleStopField bind:NSValueBinding toObject:dc withKeyPath:@"values.IdleLimitMinutes" options:nil];
    [self.fIdleStopField bind:NSEnabledBinding toObject:dc withKeyPath:@"values.IdleLimitCheck" options:nil];
    [self.fIdleStopFieldSuffixLabel bind:NSEnabledBinding toObject:dc withKeyPath:@"values.IdleLimitCheck" options:nil];

    [self.fRemoveWhenFinishSeedingButton bind:NSValueBinding toObject:dc withKeyPath:@"values.RemoveWhenFinishSeeding" options:nil];

    // Queues
    [self.fQueueDownloadButton bind:NSValueBinding toObject:dc withKeyPath:@"values.Queue" options:nil];
    [self.fQueueDownloadField bind:NSValueBinding toObject:dc withKeyPath:@"values.QueueDownloadNumber" options:nil];
    [self.fQueueDownloadField bind:NSEnabledBinding toObject:dc withKeyPath:@"values.Queue" options:nil];
    [self.fQueueDownloadFieldSuffixLabel bind:NSEnabledBinding toObject:dc withKeyPath:@"values.Queue" options:nil];

    [self.fQueueSeedButton bind:NSValueBinding toObject:dc withKeyPath:@"values.QueueSeed" options:nil];
    [self.fQueueSeedField bind:NSValueBinding toObject:dc withKeyPath:@"values.QueueSeedNumber" options:nil];
    [self.fQueueSeedField bind:NSEnabledBinding toObject:dc withKeyPath:@"values.QueueSeed" options:nil];
    [self.fQueueSeedFieldSuffixLabel bind:NSEnabledBinding toObject:dc withKeyPath:@"values.QueueSeed" options:nil];

    [self.fCheckStalledButton bind:NSValueBinding toObject:dc withKeyPath:@"values.CheckStalled" options:nil];
    [self.fStalledField bind:NSValueBinding toObject:dc withKeyPath:@"values.StalledMinutes" options:nil];
    [self.fStalledField bind:NSEnabledBinding toObject:dc withKeyPath:@"values.CheckStalled" options:nil];
    [self.fStalledFieldSuffixLabel bind:NSEnabledBinding toObject:dc withKeyPath:@"values.CheckStalled" options:nil];

    [self.fVerifyDownloadDataButton bind:NSValueBinding toObject:dc withKeyPath:@"values.VerifyDataOnCompletion" options:nil];

    // Sounds
    [self.fPlayDownloadSoundButton bind:NSValueBinding toObject:dc withKeyPath:@"values.PlayDownloadSound" options:nil];
    [self.fPlaySeedingSoundButton bind:NSValueBinding toObject:dc withKeyPath:@"values.PlaySeedingSound" options:nil];

    [self.fDownloadSoundPopUp bind:NSContentValuesBinding toObject:self withKeyPath:@"sounds" options:nil];
    [self.fDownloadSoundPopUp bind:NSSelectedValueBinding toObject:dc withKeyPath:@"values.DownloadSound" options:nil];
    [self.fDownloadSoundPopUp bind:NSEnabledBinding toObject:dc withKeyPath:@"values.PlayDownloadSound" options:nil];

    [self.fSeedingSoundPopUp bind:NSContentValuesBinding toObject:self withKeyPath:@"sounds" options:nil];
    [self.fSeedingSoundPopUp bind:NSSelectedValueBinding toObject:dc withKeyPath:@"values.SeedingSound" options:nil];
    [self.fSeedingSoundPopUp bind:NSEnabledBinding toObject:dc withKeyPath:@"values.PlaySeedingSound" options:nil];

    // Call script
    [self.fDoneScriptEnabledButton bind:NSValueBinding toObject:dc withKeyPath:@"values.DoneScriptEnabled" options:nil];
    [self.fDoneScriptPopUp bind:NSEnabledBinding toObject:dc withKeyPath:@"values.DoneScriptEnabled" options:nil];
}

#pragma mark - Configure
- (void)configurePopupButtons
{
    // Call script
    NSMenuItem* scriptPathItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [scriptPathItem bind:NSTitleBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
             withKeyPath:@"values.DoneScriptPath"
                 options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToPathTransformer" }];
    [scriptPathItem bind:NSImageBinding toObject:[NSUserDefaultsController sharedUserDefaultsController]
             withKeyPath:@"values.DoneScriptPath"
                 options:@{ NSValueTransformerNameBindingOption : @"ExpandedPathToIconTransformer" }];
    [self.fDoneScriptPopUp.menu addItem:scriptPathItem];

    [self.fDoneScriptPopUp.menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* scriptOtherItem = [[NSMenuItem alloc] initWithTitle:LStr(@"Other…") action:@selector(doneScriptSheetShow:)
                                                      keyEquivalent:@""];
    scriptOtherItem.target = self;
    [self.fDoneScriptPopUp.menu addItem:scriptOtherItem];
}

#pragma mark - Actions & Logic
- (void)applyRatioSetting:(id)sender
{
    tr_sessionSetRatioLimited(self.fHandle, [self.fDefaults boolForKey:@"RatioCheck"]);
    tr_sessionSetRatioLimit(self.fHandle, [self.fDefaults floatForKey:@"RatioLimit"]);
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateUI" object:nil];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (void)setRatioStop:(id)sender
{
    [self.fDefaults setFloat:[sender floatValue] forKey:@"RatioLimit"];
    [self applyRatioSetting:nil];
}

- (void)applyIdleStopSetting:(id)sender
{
    tr_sessionSetIdleLimited(self.fHandle, [self.fDefaults boolForKey:@"IdleLimitCheck"]);
    tr_sessionSetIdleLimit(self.fHandle, [self.fDefaults integerForKey:@"IdleLimitMinutes"]);
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateUI" object:nil];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (void)setIdleStop:(id)sender
{
    [self.fDefaults setInteger:[sender integerValue] forKey:@"IdleLimitMinutes"];
    [self applyIdleStopSetting:nil];
}

- (void)setQueue:(id)sender
{
    tr_sessionSetQueueEnabled(self.fHandle, tr_direction::Down, [self.fDefaults boolForKey:@"Queue"]);
    tr_sessionSetQueueEnabled(self.fHandle, tr_direction::Up, [self.fDefaults boolForKey:@"QueueSeed"]);
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateTorrentsState" object:nil];
}

- (void)setQueueNumber:(id)sender
{
    NSInteger const number = [sender integerValue];
    BOOL const seed = sender == self.fQueueSeedField;
    [self.fDefaults setInteger:number forKey:seed ? @"QueueSeedNumber" : @"QueueDownloadNumber"];
    tr_sessionSetQueueSize(self.fHandle, seed ? tr_direction::Up : tr_direction::Down, number);
}

- (void)setStalled:(id)sender
{
    tr_sessionSetQueueStalledEnabled(self.fHandle, [self.fDefaults boolForKey:@"CheckStalled"]);
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateUI" object:nil];
}

- (void)setStalledMinutes:(id)sender
{
    int const min = [sender intValue];
    [self.fDefaults setInteger:min forKey:@"StalledMinutes"];
    tr_sessionSetQueueStalledMinutes(self.fHandle, min);
    [[NSNotificationCenter defaultCenter] postNotificationName:@"UpdateUI" object:nil];
}

- (void)setVerifyDataOnCompletion:(id)sender
{
    bool enabled = [self.fDefaults boolForKey:@"VerifyDataOnCompletion"];
    tr_sessionSetCompleteVerifyEnabled(self.fHandle, enabled);
}

- (NSArray<NSString*>*)sounds
{
    NSMutableArray* sounds = [NSMutableArray array];
    NSArray* directories = NSSearchPathForDirectoriesInDomains(NSAllLibrariesDirectory, NSUserDomainMask | NSLocalDomainMask | NSSystemDomainMask, YES);

    for (NSString* directory in directories) {
        NSString* soundDir = [directory stringByAppendingPathComponent:@"Sounds"];
        BOOL isDirectory;
        if ([[NSFileManager defaultManager] fileExistsAtPath:soundDir isDirectory:&isDirectory] && isDirectory) {
            NSArray* directoryContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:soundDir error:NULL];
            for (NSString* sound in directoryContents) {
                NSString* soundName = sound.stringByDeletingPathExtension;
                if ([NSSound soundNamed:soundName]) {
                    [sounds addObject:soundName];
                }
            }
        }
    }
    return sounds;
}

- (void)setSound:(id)sender
{
    NSSound* sound = [NSSound soundNamed:[sender titleOfSelectedItem]];
    if (sound) {
        [sound play];
    }
}

- (void)setDoneScriptEnabled:(id)sender
{
    if ([self.fDefaults boolForKey:@"DoneScriptEnabled"] &&
        ![NSFileManager.defaultManager fileExistsAtPath:[self.fDefaults stringForKey:@"DoneScriptPath"]]) {
        [self.fDefaults setBool:NO forKey:@"DoneScriptEnabled"];
        [self doneScriptSheetShow:sender];
    }
    tr_sessionSetScriptEnabled(self.fHandle, TR_SCRIPT_ON_TORRENT_DONE, [self.fDefaults boolForKey:@"DoneScriptEnabled"]);
}

- (void)doneScriptSheetShow:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.prompt = NSLocalizedString(@"Select", "Preferences -> Open panel prompt");

    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            NSString* filePath = panel.URLs[0].path;
            [self.fDefaults setObject:filePath forKey:@"DoneScriptPath"];
            tr_sessionSetScript(self.fHandle, TR_SCRIPT_ON_TORRENT_DONE, filePath.fileSystemRepresentation);
            [self.fDefaults setBool:YES forKey:@"DoneScriptEnabled"];
            tr_sessionSetScriptEnabled(self.fHandle, TR_SCRIPT_ON_TORRENT_DONE, YES);
        }
        [self.fDoneScriptPopUp selectItemAtIndex:0];
    }];
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
@end
