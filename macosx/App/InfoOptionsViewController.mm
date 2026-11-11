// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoOptionsViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "Utils.h"
#import "ColorTextField.h"
#import "InfoWindowController.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"InfoOptionsView", @"")

typedef NS_ENUM(NSInteger, OptionPopupType) {
    OptionPopupTypeGlobal = 0,
    OptionPopupTypeNoLimit = 1,
    OptionPopupTypeLimit = 2,
};

typedef NS_ENUM(NSUInteger, OptionPopupPriority) {
    OptionPopupPriorityHigh = 0,
    OptionPopupPriorityNormal = 1,
    OptionPopupPriorityLow = 2,
};

static NSInteger const kInvalidValue = -99;

static CGFloat const kStackViewInset = 12.0;
static CGFloat const kStackViewSpacing = 8.0;
static NSEdgeInsets const kStackViewEdgeInsets = NSEdgeInsetsMake(kStackViewInset, kStackViewInset, kStackViewInset, kStackViewInset);

@interface InfoOptionsViewController ()<NSTextFieldDelegate>

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;
@property(nonatomic) BOOL fSet;

// === UI Elements ===
@property(nonatomic) NSStackView* fOptionsStackView;

@property(nonatomic) NSGridView* fPriorityGrid;
@property(nonatomic) NSGridView* fSeedingGrid;

// Priority + Speed
@property(nonatomic) NSPopUpButton* fPriorityPopUp;
@property(nonatomic) NSButton* fUploadLimitCheck;
@property(nonatomic) NSButton* fDownloadLimitCheck;
@property(nonatomic) NSButton* fGlobalLimitCheck;
@property(nonatomic) NSTextField* fUploadLimitField;
@property(nonatomic) NSTextField* fDownloadLimitField;
@property(nonatomic) NSTextField* fUploadLimitLabel;
@property(nonatomic) NSTextField* fDownloadLimitLabel;

// Seeding
@property(nonatomic) NSPopUpButton* fRatioPopUp;
@property(nonatomic) NSPopUpButton* fIdlePopUp;
@property(nonatomic) NSButton* fRemoveSeedingCompleteCheck;
@property(nonatomic) NSTextField* fRatioLimitField;
@property(nonatomic) NSTextField* fIdleLimitField;
@property(nonatomic) NSTextField* fIdleLimitLabel;
@property(nonatomic) NSTextField* fRatioLimitGlobalLabel;
@property(nonatomic) NSTextField* fIdleLimitGlobalLabel;
@property(nonatomic) NSTextField* fPeersConnectLabel;
@property(nonatomic) NSTextField* fPeersConnectField;

@property(nonatomic, copy) NSString* fInitialString;

@property(nonatomic, readonly) CGFloat fHeightChange;
@property(nonatomic, readwrite) CGFloat fCurrentHeight;
@property(nonatomic, readonly) CGFloat fHorizLayoutHeight;
@property(nonatomic, readonly) CGFloat fHorizLayoutWidth;
@property(nonatomic, readonly) CGFloat fVertLayoutHeight;

@end

@implementation InfoOptionsViewController
#pragma mark - UI Helpers

- (NSView*)fPriorityView
{
    return self.fPriorityGrid;
}

- (NSTextField*)makeLabel:(NSString*)title
{
    NSTextField* label = [NSTextField labelWithString:title];
    label.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

- (NSTextField*)makeColoredLabel:(NSString*)title
{
    NSTextField* label = [ColorTextField labelWithString:title];
    label.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

- (NSTextField*)makeBoldLabel:(NSString*)title
{
    NSTextField* label = [NSTextField labelWithString:title];
    label.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

- (NSButton*)makeCheckbox:(NSString*)title
{
    NSButton* btn = [NSButton checkboxWithTitle:title target:nil action:nil];
    btn.controlSize = NSControlSizeSmall;
    btn.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    btn.allowsMixedState = YES;
    btn.translatesAutoresizingMaskIntoConstraints = NO;
    return btn;
}

- (NSPopUpButton*)makePopUpButton
{
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    popup.controlSize = NSControlSizeSmall;
    popup.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    popup.translatesAutoresizingMaskIntoConstraints = NO;
    return popup;
}

- (NSTextField*)makeNumberField
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field.controlSize = NSControlSizeSmall;
    field.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    field.alignment = NSTextAlignmentRight;
    field.cell.sendsActionOnEndEditing = YES;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterDecimalStyle;
    formatter.usesGroupingSeparator = NO;
    formatter.allowsFloats = NO;
    formatter.minimum = @0;
    formatter.maximum = @99999;
    field.formatter = formatter;

    [field.widthAnchor constraintEqualToConstant:50].active = YES;
    return field;
}

- (NSTextField*)makeFloatField
{
    NSTextField* field = [self makeNumberField];
    NSNumberFormatter* formatter = (NSNumberFormatter*)field.formatter;
    formatter.allowsFloats = YES;
    formatter.minimumFractionDigits = 2;
    formatter.maximumFractionDigits = 2;
    return field;
}

static inline NSStackView* mergeViews(NSArray<NSView*>* views)
{
    NSStackView* stackView = [NSStackView stackViewWithViews:views];
    stackView.alignment = NSLayoutAttributeFirstBaseline;
    return stackView;
}

- (instancetype)init
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        self.title = NSLocalizedString(@"Options", "Inspector view -> title");
    }
    return self;
}

- (void)loadView
{
    NSView* mainView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 380, 300)];
    mainView.translatesAutoresizingMaskIntoConstraints = NO;
    self.view = mainView;

    [self createUIElements];
    [self setupLayoutInView:mainView];
    [self setupBindings];
}

- (void)createUIElements
{
    // --- Priority section ---
    self.fPriorityPopUp = [self makePopUpButton];
    [self.fPriorityPopUp addItemWithTitle:LStr(@"High")];
    [self.fPriorityPopUp.itemArray[0] setImage:[NSImage imageNamed:@"PriorityHighTemplate"]];
    [self.fPriorityPopUp addItemWithTitle:LStr(@"Normal")];
    [self.fPriorityPopUp.itemArray[1] setImage:[NSImage imageNamed:@"PriorityNormalTemplate"]];
    [self.fPriorityPopUp addItemWithTitle:LStr(@"Low")];
    [self.fPriorityPopUp.itemArray[2] setImage:[NSImage imageNamed:@"PriorityLowTemplate"]];

    // --- Speed section ---
    self.fDownloadLimitCheck = [self makeCheckbox:LStr(@"Limit download speed:")];
    self.fDownloadLimitField = [self makeNumberField];
    self.fDownloadLimitLabel = [self makeColoredLabel:LStr(@"KB/s")];

    self.fUploadLimitCheck = [self makeCheckbox:LStr(@"Limit upload speed:")];
    self.fUploadLimitField = [self makeNumberField];
    self.fUploadLimitLabel = [self makeColoredLabel:LStr(@"KB/s")];

    self.fGlobalLimitCheck = [self makeCheckbox:LStr(@"Honor global limits")];

    // --- Seeding section ---
    self.fRatioPopUp = [self makePopUpButton];
    [self.fRatioPopUp addItemsWithTitles:@[ LStr(@"Use Global Settings"), LStr(@"Unlimited"), LStr(@"Stop Seeding at Ratio") ]];

    self.fRatioLimitField = [self makeFloatField];
    self.fRatioLimitGlobalLabel = [self makeLabel:@""];
    self.fRatioLimitGlobalLabel.textColor = [NSColor secondaryLabelColor];

    self.fIdlePopUp = [self makePopUpButton];
    [self.fIdlePopUp addItemsWithTitles:@[ LStr(@"Use Global Settings"), LStr(@"Unlimited"), LStr(@"Stop When Inactive") ]];

    self.fIdleLimitField = [self makeNumberField];
    NSNumberFormatter* idleFormatter = (NSNumberFormatter*)self.fIdleLimitField.formatter;
    idleFormatter.minimum = @1;
    idleFormatter.maximum = @40320;

    self.fIdleLimitLabel = [self makeLabel:LStr(@"minutes")];
    self.fIdleLimitGlobalLabel = [self makeLabel:@""];
    self.fIdleLimitGlobalLabel.textColor = [NSColor secondaryLabelColor];
    self.fRemoveSeedingCompleteCheck = [self makeCheckbox:LStr(@"Remove from the torrent list when seeding completes")];

    // --- Peers section ---
    self.fPeersConnectField = [self makeNumberField];
    NSNumberFormatter* peersFormatter = (NSNumberFormatter*)self.fPeersConnectField.formatter;
    peersFormatter.minimum = @1;
    peersFormatter.maximum = @9999;
    self.fPeersConnectLabel = [self makeColoredLabel:LStr(@"Maximum peers:")];
}

- (void)setupLayoutInView:(NSView*)parent
{
    NSTextField* priorityTitle = [self makeBoldLabel:LStr(@"Priority")];
    NSTextField* priorityLabel = [self makeLabel:LStr(@"Torrent priority:")];
    NSTextField* speedTitle = [self makeBoldLabel:LStr(@"Speed")];
    NSTextField* seedingTitle = [self makeBoldLabel:LStr(@"Seeding Limits")];
    NSTextField* ratioLabel = [self makeLabel:LStr(@"Ratio:")];
    NSTextField* idleLabel = [self makeLabel:LStr(@"Idle:")];
    NSTextField* peersTitle = [self makeBoldLabel:LStr(@"Peer Connections")];

    // --- Priority + Speed Grid ---
    self.fPriorityGrid = [NSGridView gridViewWithViews:@[
        @[ priorityTitle ],
        @[ mergeViews(@[ priorityLabel, self.fPriorityPopUp ]) ],
        @[ speedTitle ],
        @[ mergeViews(@[ self.fDownloadLimitCheck, self.fDownloadLimitField, self.fDownloadLimitLabel ]) ],
        @[ mergeViews(@[ self.fUploadLimitCheck, self.fUploadLimitField, self.fUploadLimitLabel ]) ],
        @[ self.fGlobalLimitCheck ]
    ]];

    [self.fDownloadLimitCheck.widthAnchor constraintEqualToAnchor:self.fUploadLimitCheck.widthAnchor].active = YES;

    self.fPriorityGrid.rowSpacing = 6;
    self.fPriorityGrid.columnSpacing = 8;
    self.fPriorityGrid.rowAlignment = NSGridRowAlignmentFirstBaseline;

    // --- Seeding + Peers Grid ---
    self.fSeedingGrid = [NSGridView gridViewWithViews:@[
        @[ seedingTitle ],
        @[ mergeViews(@[ ratioLabel, self.fRatioPopUp, self.fRatioLimitField, self.fRatioLimitGlobalLabel ]) ],
        @[ mergeViews(
            @[ idleLabel, self.fIdlePopUp, self.fIdleLimitField, self.fIdleLimitLabel, self.fIdleLimitGlobalLabel ]) ],
        @[
            self.fRemoveSeedingCompleteCheck,
        ],
        @[ peersTitle ],
        @[ mergeViews(@[ self.fPeersConnectLabel, self.fPeersConnectField ]) ]
    ]];

    [ratioLabel.widthAnchor constraintEqualToAnchor:idleLabel.widthAnchor].active = YES;
    [self.fRatioPopUp.widthAnchor constraintEqualToAnchor:self.fIdlePopUp.widthAnchor].active = YES;

    self.fSeedingGrid.rowSpacing = 6;
    self.fSeedingGrid.columnSpacing = 8;
    self.fSeedingGrid.rowAlignment = NSGridRowAlignmentFirstBaseline;

    // --- Main StackView ---
    self.fOptionsStackView = [NSStackView stackViewWithViews:@[ self.fPriorityGrid, self.fSeedingGrid ]];
    self.fOptionsStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
    self.fOptionsStackView.alignment = NSLayoutAttributeLeading;
    self.fOptionsStackView.spacing = kStackViewSpacing;
    self.fOptionsStackView.edgeInsets = kStackViewEdgeInsets;
    self.fOptionsStackView.translatesAutoresizingMaskIntoConstraints = NO;

    [parent addSubview:self.fOptionsStackView];

    [NSLayoutConstraint activateConstraints:@[
        [self.fOptionsStackView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.fOptionsStackView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.fOptionsStackView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.fOptionsStackView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]
    ]];
}

- (void)setupBindings
{
    self.fPriorityPopUp.target = self;
    self.fPriorityPopUp.action = @selector(setPriority:);
    self.fDownloadLimitCheck.target = self;
    self.fDownloadLimitCheck.action = @selector(setUseSpeedLimit:);
    self.fDownloadLimitField.target = self;
    self.fDownloadLimitField.action = @selector(setSpeedLimit:);
    self.fDownloadLimitField.delegate = self;
    self.fUploadLimitCheck.target = self;
    self.fUploadLimitCheck.action = @selector(setUseSpeedLimit:);
    self.fUploadLimitField.target = self;
    self.fUploadLimitField.action = @selector(setSpeedLimit:);
    self.fUploadLimitField.delegate = self;
    self.fGlobalLimitCheck.target = self;
    self.fGlobalLimitCheck.action = @selector(setUseGlobalSpeedLimit:);
    self.fRatioPopUp.target = self;
    self.fRatioPopUp.action = @selector(setRatioSetting:);
    self.fRatioLimitField.target = self;
    self.fRatioLimitField.action = @selector(setRatioLimit:);
    self.fRatioLimitField.delegate = self;
    self.fIdlePopUp.target = self;
    self.fIdlePopUp.action = @selector(setIdleSetting:);
    self.fIdleLimitField.target = self;
    self.fIdleLimitField.action = @selector(setIdleLimit:);
    self.fIdleLimitField.delegate = self;
    self.fRemoveSeedingCompleteCheck.target = self;
    self.fRemoveSeedingCompleteCheck.action = @selector(setRemoveWhenSeedingCompletes:);
    self.fPeersConnectField.target = self;
    self.fPeersConnectField.action = @selector(setPeersConnectLimit:);
    self.fPeersConnectField.delegate = self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    [self setGlobalLabels];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(setGlobalLabels) name:@"UpdateGlobalOptions" object:nil];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateOptionsNotification:)
                                               name:@"UpdateOptionsNotification"
                                             object:nil];
}

#pragma mark - Layout

- (CGFloat)fHorizLayoutHeight
{
    CGFloat h1 = self.fPriorityGrid.fittingSize.height;
    CGFloat h2 = self.fSeedingGrid.fittingSize.height;
    return MAX(h1, h2) + kStackViewInset * 2;
}

- (CGFloat)fHorizLayoutWidth
{
    return self.fPriorityGrid.fittingSize.width + self.fSeedingGrid.fittingSize.width + 2 * kStackViewInset + kStackViewSpacing; // insets + spacing
}

- (CGFloat)fVertLayoutHeight
{
    return self.fPriorityGrid.fittingSize.height + self.fSeedingGrid.fittingSize.height + 2 * kStackViewInset + kStackViewSpacing; // insets + spacing
}

- (CGFloat)fHeightChange
{
    return self.oldHeight - self.fCurrentHeight;
}

- (NSRect)viewRect
{
    NSRect viewRect = self.view.frame;

    CGFloat difference = self.fHeightChange;

    // Only the Inspector window resizes to fit this view;
    // the popover from TorrentTableView.mm keeps its height.
    if ([self.view.window.windowController isKindOfClass:InfoWindowController.class]) {
        viewRect.size.height -= difference;
    }

    return viewRect;
}

- (void)checkLayout
{
    if (NSWidth(self.view.window.frame) >= self.fHorizLayoutWidth + 1) {
        self.fOptionsStackView.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        self.fCurrentHeight = self.fHorizLayoutHeight;
    } else {
        self.fOptionsStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
        self.fCurrentHeight = self.fVertLayoutHeight;
    }
}

- (void)checkWindowSize
{
    self.oldHeight = self.fCurrentHeight;
    [self updateWindowLayout];
}

- (void)updateWindowLayout
{
    // Only the Inspector window resizes to fit this view;
    // the popover from TorrentTableView.mm keeps its height.
    if ([self.view.window.windowController isKindOfClass:InfoWindowController.class]) {
        [self checkLayout];

        CGFloat difference = self.fHeightChange;

        NSRect windowRect = self.view.window.frame;
        windowRect.origin.y += difference;
        windowRect.size.height -= difference;

        self.view.window.minSize = NSMakeSize(self.view.window.minSize.width, NSHeight(windowRect));
        self.view.window.maxSize = NSMakeSize(FLT_MAX, NSHeight(windowRect));

        self.view.frame = [self viewRect];
        [self.view.window setFrame:windowRect display:YES animate:YES];
    } else {
        // set popover width
        NSRect rect = self.view.frame;
        rect.size.width = self.fOptionsStackView.fittingSize.width;
        self.view.frame = rect;
    }
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet) {
        [self setupInfo];
    }

    self.fSet = YES;
}

- (void)updateOptions
{
    if (self.fTorrents.count == 0) {
        return;
    }

    //get bandwidth info
    NSEnumerator* enumerator = [self.fTorrents objectEnumerator];
    Torrent* torrent = [enumerator nextObject]; //first torrent

    NSInteger uploadUseSpeedLimit = [torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff;
    NSUInteger uploadSpeedLimit = [torrent speedLimit:YES];
    BOOL multipleUploadSpeedLimits = NO;
    NSInteger downloadUseSpeedLimit = [torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff;
    NSUInteger downloadSpeedLimit = [torrent speedLimit:NO];
    BOOL multipleDownloadSpeedLimits = NO;
    NSInteger globalUseSpeedLimit = torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff;

    while (
        (torrent = [enumerator nextObject]) &&
        (uploadUseSpeedLimit != NSControlStateValueMixed || !multipleUploadSpeedLimits || downloadUseSpeedLimit != NSControlStateValueMixed ||
         !multipleDownloadSpeedLimits || globalUseSpeedLimit != NSControlStateValueMixed)) {
        if (uploadUseSpeedLimit != NSControlStateValueMixed &&
            uploadUseSpeedLimit != ([torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff)) {
            uploadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (!multipleUploadSpeedLimits && uploadSpeedLimit != [torrent speedLimit:YES]) {
            multipleUploadSpeedLimits = YES;
        }

        if (downloadUseSpeedLimit != NSControlStateValueMixed &&
            downloadUseSpeedLimit != ([torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff)) {
            downloadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (!multipleDownloadSpeedLimits && downloadSpeedLimit != [torrent speedLimit:NO]) {
            multipleDownloadSpeedLimits = YES;
        }

        if (globalUseSpeedLimit != NSControlStateValueMixed &&
            globalUseSpeedLimit != (torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff)) {
            globalUseSpeedLimit = NSControlStateValueMixed;
        }
    }

    //set upload view
    self.fUploadLimitCheck.state = uploadUseSpeedLimit;
    self.fUploadLimitCheck.enabled = YES;

    self.fUploadLimitLabel.enabled = uploadUseSpeedLimit == NSControlStateValueOn;
    self.fUploadLimitField.enabled = uploadUseSpeedLimit == NSControlStateValueOn;
    if (!multipleUploadSpeedLimits) {
        self.fUploadLimitField.integerValue = uploadSpeedLimit;
    } else {
        self.fUploadLimitField.stringValue = @"";
    }

    //set download view
    self.fDownloadLimitCheck.state = downloadUseSpeedLimit;
    self.fDownloadLimitCheck.enabled = YES;

    self.fDownloadLimitLabel.enabled = downloadUseSpeedLimit == NSControlStateValueOn;
    self.fDownloadLimitField.enabled = downloadUseSpeedLimit == NSControlStateValueOn;
    if (!multipleDownloadSpeedLimits) {
        self.fDownloadLimitField.integerValue = downloadSpeedLimit;
    } else {
        self.fDownloadLimitField.stringValue = @"";
    }

    //set global check
    self.fGlobalLimitCheck.state = globalUseSpeedLimit;
    self.fGlobalLimitCheck.enabled = YES;

    //get ratio and idle info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger checkRatio = torrent.ratioSetting;
    NSInteger checkIdle = torrent.idleSetting;
    NSInteger removeWhenFinishSeeding = torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff;
    CGFloat ratioLimit = torrent.ratioLimit;
    BOOL multipleRatioLimits = NO;
    NSUInteger idleLimit = torrent.idleLimitMinutes;
    BOOL multipleIdleLimits = NO;

    while (
        (torrent = [enumerator nextObject]) &&
        (checkRatio != kInvalidValue || !multipleRatioLimits || checkIdle != kInvalidValue || !multipleIdleLimits)) {
        if (checkRatio != kInvalidValue && checkRatio != torrent.ratioSetting) {
            checkRatio = kInvalidValue;
        }

        if (!multipleRatioLimits && !isRatioEqual(ratioLimit, torrent.ratioLimit)) {
            multipleRatioLimits = YES;
        }

        if (checkIdle != kInvalidValue && checkIdle != torrent.idleSetting) {
            checkIdle = kInvalidValue;
        }

        if (!multipleIdleLimits && idleLimit != torrent.idleLimitMinutes) {
            multipleIdleLimits = YES;
        }

        if (removeWhenFinishSeeding != NSControlStateValueMixed &&
            removeWhenFinishSeeding != (torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff)) {
            removeWhenFinishSeeding = NSControlStateValueMixed;
        }
    }

    //set ratio view
    NSInteger index;
    if (checkRatio == TR_RATIOLIMIT_SINGLE) {
        index = OptionPopupTypeLimit;
    } else if (checkRatio == TR_RATIOLIMIT_UNLIMITED) {
        index = OptionPopupTypeNoLimit;
    } else if (checkRatio == TR_RATIOLIMIT_GLOBAL) {
        index = OptionPopupTypeGlobal;
    } else {
        index = -1;
    }
    [self.fRatioPopUp selectItemAtIndex:index];
    self.fRatioPopUp.enabled = YES;

    self.fRatioLimitField.hidden = checkRatio != TR_RATIOLIMIT_SINGLE;
    if (!multipleRatioLimits) {
        self.fRatioLimitField.floatValue = ratioLimit;
    } else {
        self.fRatioLimitField.stringValue = @"";
    }

    self.fRatioLimitGlobalLabel.hidden = checkRatio != TR_RATIOLIMIT_GLOBAL;

    //set idle view
    if (checkIdle == TR_IDLELIMIT_SINGLE) {
        index = OptionPopupTypeLimit;
    } else if (checkIdle == TR_IDLELIMIT_UNLIMITED) {
        index = OptionPopupTypeNoLimit;
    } else if (checkIdle == TR_IDLELIMIT_GLOBAL) {
        index = OptionPopupTypeGlobal;
    } else {
        index = -1;
    }
    [self.fIdlePopUp selectItemAtIndex:index];
    self.fIdlePopUp.enabled = YES;

    self.fIdleLimitField.hidden = checkIdle != TR_IDLELIMIT_SINGLE;
    if (!multipleIdleLimits) {
        self.fIdleLimitField.integerValue = idleLimit;
    } else {
        self.fIdleLimitField.stringValue = @"";
    }
    self.fIdleLimitLabel.hidden = checkIdle != TR_IDLELIMIT_SINGLE;

    self.fIdleLimitGlobalLabel.hidden = checkIdle != TR_IDLELIMIT_GLOBAL;

    //set remove transfer when seeding finishes
    self.fRemoveSeedingCompleteCheck.state = removeWhenFinishSeeding;
    self.fRemoveSeedingCompleteCheck.enabled = YES;

    //get priority info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger priority = torrent.priority;

    while ((torrent = [enumerator nextObject]) && priority != kInvalidValue) {
        if (priority != torrent.priority) {
            priority = kInvalidValue;
        }
    }

    //set priority view
    if (priority == TR_PRI_HIGH) {
        index = OptionPopupPriorityHigh;
    } else if (priority == TR_PRI_NORMAL) {
        index = OptionPopupPriorityNormal;
    } else if (priority == TR_PRI_LOW) {
        index = OptionPopupPriorityLow;
    } else {
        index = -1;
    }
    [self.fPriorityPopUp selectItemAtIndex:index];
    self.fPriorityPopUp.enabled = YES;

    //get peer info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger maxPeers = torrent.maxPeerConnect;

    while ((torrent = [enumerator nextObject])) {
        if (maxPeers != torrent.maxPeerConnect) {
            maxPeers = kInvalidValue;
            break;
        }
    }

    //set peer view
    self.fPeersConnectField.enabled = YES;
    self.fPeersConnectLabel.enabled = YES;
    if (maxPeers != kInvalidValue) {
        self.fPeersConnectField.integerValue = maxPeers;
    } else {
        self.fPeersConnectField.stringValue = @"";
    }

    [self checkWindowSize];
}

- (void)setUseSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitCheck;

    if (((NSButton*)sender).state == NSControlStateValueMixed) {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const limit = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents) {
        [torrent setUseSpeedLimit:limit upload:upload];
    }

    NSTextField* field = upload ? self.fUploadLimitField : self.fDownloadLimitField;
    field.enabled = limit;
    if (limit) {
        [field selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    NSTextField* label = upload ? self.fUploadLimitLabel : self.fDownloadLimitLabel;
    label.enabled = limit;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setUseGlobalSpeedLimit:(id)sender
{
    if (((NSButton*)sender).state == NSControlStateValueMixed) {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const limit = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents) {
        torrent.usesGlobalSpeedLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitField;
    NSInteger const limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents) {
        [torrent setSpeedLimit:limit upload:upload];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRatioSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem]) {
    case OptionPopupTypeLimit:
        setting = TR_RATIOLIMIT_SINGLE;
        single = YES;
        break;
    case OptionPopupTypeNoLimit:
        setting = TR_RATIOLIMIT_UNLIMITED;
        break;
    case OptionPopupTypeGlobal:
        setting = TR_RATIOLIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in ratio popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents) {
        torrent.ratioSetting = static_cast<tr_ratiolimit>(setting);
    }

    self.fRatioLimitField.hidden = !single;
    if (single) {
        [self.fRatioLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fRatioLimitGlobalLabel.hidden = setting != TR_RATIOLIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
    [self checkWindowSize];
}

- (void)setRatioLimit:(id)sender
{
    CGFloat const limit = [sender floatValue];

    for (Torrent* torrent in self.fTorrents) {
        torrent.ratioLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setIdleSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem]) {
    case OptionPopupTypeLimit:
        setting = TR_IDLELIMIT_SINGLE;
        single = YES;
        break;
    case OptionPopupTypeNoLimit:
        setting = TR_IDLELIMIT_UNLIMITED;
        break;
    case OptionPopupTypeGlobal:
        setting = TR_IDLELIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in idle popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents) {
        torrent.idleSetting = static_cast<tr_idlelimit>(setting);
    }

    self.fIdleLimitField.hidden = !single;
    self.fIdleLimitLabel.hidden = !single;
    if (single) {
        [self.fIdleLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fIdleLimitGlobalLabel.hidden = setting != TR_IDLELIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
    [self checkWindowSize];
}

- (void)setIdleLimit:(id)sender
{
    NSUInteger const limit = [sender integerValue];

    for (Torrent* torrent in self.fTorrents) {
        torrent.idleLimitMinutes = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRemoveWhenSeedingCompletes:(id)sender
{
    if (((NSButton*)sender).state == NSControlStateValueMixed) {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const enable = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents) {
        torrent.removeWhenFinishSeeding = enable;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem]) {
    case OptionPopupPriorityHigh:
        priority = TR_PRI_HIGH;
        break;
    case OptionPopupPriorityNormal:
        priority = TR_PRI_NORMAL;
        break;
    case OptionPopupPriorityLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in priority popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents) {
        torrent.priority = priority;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPeersConnectLimit:(id)sender
{
    NSInteger limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents) {
        torrent.maxPeerConnect = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
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

#pragma mark - Private

- (void)setupInfo
{
    if (self.fTorrents.count == 0) {
        self.fUploadLimitCheck.enabled = NO;
        self.fUploadLimitCheck.state = NSControlStateValueOff;
        self.fUploadLimitField.enabled = NO;
        self.fUploadLimitLabel.enabled = NO;
        self.fUploadLimitField.stringValue = @"";

        self.fDownloadLimitCheck.enabled = NO;
        self.fDownloadLimitCheck.state = NSControlStateValueOff;
        self.fDownloadLimitField.enabled = NO;
        self.fDownloadLimitLabel.enabled = NO;
        self.fDownloadLimitField.stringValue = @"";

        self.fGlobalLimitCheck.enabled = NO;
        self.fGlobalLimitCheck.state = NSControlStateValueOff;

        self.fPriorityPopUp.enabled = NO;
        [self.fPriorityPopUp selectItemAtIndex:-1];

        self.fRatioPopUp.enabled = NO;
        [self.fRatioPopUp selectItemAtIndex:-1];
        self.fRatioLimitField.hidden = YES;
        self.fRatioLimitField.stringValue = @"";
        self.fRatioLimitGlobalLabel.hidden = YES;

        self.fIdlePopUp.enabled = NO;
        [self.fIdlePopUp selectItemAtIndex:-1];
        self.fIdleLimitField.hidden = YES;
        self.fIdleLimitField.stringValue = @"";
        self.fIdleLimitLabel.hidden = YES;
        self.fIdleLimitGlobalLabel.hidden = YES;

        self.fRemoveSeedingCompleteCheck.enabled = NO;
        self.fRemoveSeedingCompleteCheck.state = NSControlStateValueOff;

        self.fPeersConnectField.enabled = NO;
        self.fPeersConnectField.stringValue = @"";
        self.fPeersConnectLabel.enabled = NO;
        [self checkWindowSize];
    } else {
        [self updateOptions];
    }
}

- (void)setGlobalLabels
{
    NSString* global = [NSUserDefaults.standardUserDefaults boolForKey:@"RatioCheck"] ?
        [NSString stringForRatio:tr::Values::Ratio::from_double([NSUserDefaults.standardUserDefaults floatForKey:@"RatioLimit"])] :
        NSLocalizedString(@"disabled", "Info options -> global setting");
    self.fRatioLimitGlobalLabel.stringValue = global;

    //idle field
    NSString* globalIdle;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"IdleLimitCheck"]) {
        NSInteger const globalMin = [NSUserDefaults.standardUserDefaults integerForKey:@"IdleLimitMinutes"];
        globalIdle = globalMin == 1 ?
            NSLocalizedString(@"1 minute", "Info options -> global setting") :
            [NSString localizedStringWithFormat:NSLocalizedString(@"%ld minutes", "Info options -> global setting"), globalMin];
    } else {
        globalIdle = NSLocalizedString(@"disabled", "Info options -> global setting");
    }
    self.fIdleLimitGlobalLabel.stringValue = globalIdle;

    [self checkWindowSize];
}

- (void)updateOptionsNotification:(NSNotification*)notification
{
    if (notification.object != self) {
        [self updateOptions];
    }
}

@end
