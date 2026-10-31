// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoActivityViewController.h"
#import "NSStringAdditions.h"
#import "PiecesView.h"
#import "Torrent.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"InfoActivityView", @"")

typedef NS_ENUM(NSUInteger, PiecesControlSegment) {
    PiecesControlSegmentProgress = 0,
    PiecesControlSegmentAvailable = 1,
};

static CGFloat const kStackViewInset = 12.0;
static CGFloat const kStackViewHorizontalSpacing = 20.0;
static CGFloat const kStackViewVerticalSpacing = 8.0;

@interface InfoActivityViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSTextField* fDateAddedField;
@property(nonatomic) NSTextField* fDateCompletedField;
@property(nonatomic) NSTextField* fDateActivityField;
@property(nonatomic) NSTextField* fStateField;
@property(nonatomic) NSTextField* fProgressField;
@property(nonatomic) NSTextField* fHaveField;
@property(nonatomic) NSTextField* fDownloadedTotalField;
@property(nonatomic) NSTextField* fUploadedTotalField;
@property(nonatomic) NSTextField* fFailedHashField;
@property(nonatomic) NSTextField* fRatioField;
@property(nonatomic) NSTextField* fDownloadTimeField;
@property(nonatomic) NSTextField* fSeedTimeField;
@property(nonatomic) NSTextView* fErrorMessageView;

@property(nonatomic) PiecesView* fPiecesView;
@property(nonatomic) NSSegmentedControl* fPiecesControl;

@property(nonatomic) NSStackView* fActivityStackView;
@property(nonatomic) NSView* fTransferView;
@property(nonatomic) NSView* fDatesView;
@property(nonatomic, readonly) CGFloat fHeightChange;
@property(nonatomic, readwrite) CGFloat fCurrentHeight;
@property(nonatomic, readonly) CGFloat fHorizLayoutHeight;
@property(nonatomic, readonly) CGFloat fHorizLayoutWidth;
@property(nonatomic, readonly) CGFloat fVertLayoutHeight;

@end

@implementation InfoActivityViewController

#pragma mark - Lifecycle

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        self.title = NSLocalizedString(@"Activity", "Inspector view -> title");
    }
    return self;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 358, 358)];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.view = root;

    self.fTransferView = [self makeTransferView];
    self.fDatesView = [self makeDatesView];

    self.fActivityStackView = [NSStackView stackViewWithViews:@[ self.fTransferView, self.fDatesView ]];
    self.fActivityStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
    self.fActivityStackView.alignment = NSLayoutAttributeLeading;
    self.fActivityStackView.distribution = NSStackViewDistributionGravityAreas;
    self.fActivityStackView.spacing = kStackViewVerticalSpacing;
    self.fActivityStackView.translatesAutoresizingMaskIntoConstraints = NO;
    self.fActivityStackView.detachesHiddenViews = YES;

    [root addSubview:self.fActivityStackView];

    [NSLayoutConstraint activateConstraints:@[
        [self.fActivityStackView.topAnchor constraintEqualToAnchor:root.topAnchor constant:kStackViewInset],
        [self.fActivityStackView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:kStackViewInset],
        [root.bottomAnchor constraintEqualToAnchor:self.fActivityStackView.bottomAnchor constant:kStackViewInset],
        [root.trailingAnchor constraintEqualToAnchor:self.fActivityStackView.trailingAnchor constant:kStackViewInset],
    ]];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self checkWindowSize];

    NSClickGestureRecognizer* gesture = [[NSClickGestureRecognizer alloc] initWithTarget:self
                                                                                  action:@selector(togglePiecesViewAvailability:)];
    [self.fPiecesView addGestureRecognizer:gesture];
}

#pragma mark - View factories

- (NSView*)makeTransferView
{
    NSView* container = [[NSView alloc] initWithFrame:NSZeroRect];
    container.translatesAutoresizingMaskIntoConstraints = NO;

    NSGridView* grid = [self makeTransferGrid];
    NSView* piecesColumn = [self makePiecesColumn];
    NSView* errorRow = [self makeErrorRow];

    grid.translatesAutoresizingMaskIntoConstraints = NO;
    piecesColumn.translatesAutoresizingMaskIntoConstraints = NO;
    errorRow.translatesAutoresizingMaskIntoConstraints = NO;

    [container addSubview:grid];
    [container addSubview:piecesColumn];
    [container addSubview:errorRow];

    [NSLayoutConstraint activateConstraints:@[
        // grid — top-left
        [grid.topAnchor constraintEqualToAnchor:container.topAnchor],
        [grid.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],

        // pieces — right of grid
        [piecesColumn.topAnchor constraintEqualToAnchor:container.topAnchor constant:16.0],
        [piecesColumn.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [piecesColumn.leadingAnchor constraintGreaterThanOrEqualToAnchor:grid.trailingAnchor constant:8.0],

        // error — full width
        [errorRow.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [errorRow.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [errorRow.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-12.0],

        // the error row sits directly under the taller of the grid and the pieces column
        [errorRow.topAnchor constraintGreaterThanOrEqualToAnchor:grid.bottomAnchor constant:8.0],
        [errorRow.topAnchor constraintGreaterThanOrEqualToAnchor:piecesColumn.bottomAnchor constant:8.0],
    ]];

    NSLayoutConstraint* errorBelowGrid = [errorRow.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:8.0];
    errorBelowGrid.priority = NSLayoutPriorityDefaultHigh - 1;
    errorBelowGrid.active = YES;

    [container setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [container setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];

    return container;
}

- (NSView*)makeDatesView
{
    NSView* container = [[NSView alloc] initWithFrame:NSZeroRect];
    container.translatesAutoresizingMaskIntoConstraints = NO;

    NSGridView* grid = [self makeDatesGrid];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:grid];

    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:container.topAnchor],
        [grid.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [grid.trailingAnchor constraintLessThanOrEqualToAnchor:container.trailingAnchor],
        [grid.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
        [container.widthAnchor constraintGreaterThanOrEqualToConstant:139.0],
    ]];

    [container setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [container setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];

    [container.heightAnchor constraintEqualToConstant:128.0].active = YES;

    return container;
}

#pragma mark - Transfer pieces

- (NSGridView*)makeTransferGrid
{
    NSTextField* header = [self makeSectionLabel:LStr(@"Transfer")];

    NSTextField* stateLabel = [self makeCaptionLabel:LStr(@"State:")];
    NSTextField* progressLabel = [self makeCaptionLabel:LStr(@"Progress:")];
    NSTextField* haveLabel = [self makeCaptionLabel:LStr(@"Have:")];
    NSTextField* downloadedLabel = [self makeCaptionLabel:LStr(@"Downloaded:")];
    NSTextField* uploadedLabel = [self makeCaptionLabel:LStr(@"Uploaded:")];
    NSTextField* failedLabel = [self makeCaptionLabel:LStr(@"Failed DL:")];
    NSTextField* ratioLabel = [self makeCaptionLabel:LStr(@"Ratio:")];

    self.fStateField = [self makeValueField];
    self.fProgressField = [self makeValueField];
    self.fHaveField = [self makeValueField];
    self.fDownloadedTotalField = [self makeValueField];
    self.fUploadedTotalField = [self makeValueField];
    self.fFailedHashField = [self makeValueField];
    self.fRatioField = [self makeValueField];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ header ],
        @[ stateLabel, self.fStateField ],
        @[ progressLabel, self.fProgressField ],
        @[ haveLabel, self.fHaveField ],
        @[ downloadedLabel, self.fDownloadedTotalField ],
        @[ uploadedLabel, self.fUploadedTotalField ],
        @[ failedLabel, self.fFailedHashField ],
        @[ ratioLabel, self.fRatioField ],
    ]];

    grid.rowSpacing = 2.0;
    grid.columnSpacing = 8.0;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;

    [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(0, 1)];
    [grid rowAtIndex:0].topPadding = 0.0;

    for (NSInteger r = 1; r < grid.numberOfRows; r++) {
        NSGridCell* labelCell = [grid cellAtColumnIndex:0 rowIndex:r];
        labelCell.xPlacement = NSGridCellPlacementTrailing;
    }
    for (NSInteger r = 1; r < grid.numberOfRows; r++) {
        NSGridCell* valueCell = [grid cellAtColumnIndex:1 rowIndex:r];
        valueCell.xPlacement = NSGridCellPlacementFill;
    }

    return grid;
}

- (NSView*)makePiecesColumn
{
    NSView* column = [[NSView alloc] initWithFrame:NSZeroRect];
    column.translatesAutoresizingMaskIntoConstraints = NO;

    self.fPiecesView = [[PiecesView alloc] initWithFrame:NSZeroRect];
    self.fPiecesView.translatesAutoresizingMaskIntoConstraints = NO;

    self.fPiecesControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    self.fPiecesControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.fPiecesControl.segmentCount = 2;
    self.fPiecesControl.segmentStyle = NSSegmentStyleRounded;
    self.fPiecesControl.controlSize = NSControlSizeMini;
    self.fPiecesControl.trackingMode = NSSegmentSwitchTrackingSelectAny;
    [self.fPiecesControl setLabel:LStr(@"Progress") forSegment:0];
    [self.fPiecesControl setLabel:LStr(@"Available") forSegment:1];
    self.fPiecesControl.target = self;
    self.fPiecesControl.action = @selector(setPiecesView:);

    [column addSubview:self.fPiecesView];
    [column addSubview:self.fPiecesControl];

    [NSLayoutConstraint activateConstraints:@[
        [self.fPiecesView.topAnchor constraintEqualToAnchor:column.topAnchor],
        [self.fPiecesView.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
        [self.fPiecesView.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
        [self.fPiecesView.widthAnchor constraintEqualToConstant:93.0],
        [self.fPiecesView.heightAnchor constraintEqualToConstant:93.0],

        [self.fPiecesControl.topAnchor constraintEqualToAnchor:self.fPiecesView.bottomAnchor constant:2.0],
        [self.fPiecesControl.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
        [self.fPiecesControl.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
        [self.fPiecesControl.bottomAnchor constraintEqualToAnchor:column.bottomAnchor],
        [self.fPiecesControl.widthAnchor constraintEqualToAnchor:self.fPiecesView.widthAnchor],
    ]];

    return column;
}

- (NSView*)makeErrorRow
{
    NSView* row = [[NSView alloc] initWithFrame:NSZeroRect];
    row.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField* errorLabel = [self makeCaptionLabel:LStr(@"Error:")];

    NSScrollView* scroll = [NSTextView scrollableTextView];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.borderType = NSBezelBorder;
    scroll.autohidesScrollers = YES;

    self.fErrorMessageView = scroll.documentView;
    self.fErrorMessageView.editable = NO;
    self.fErrorMessageView.richText = NO;
    self.fErrorMessageView.importsGraphics = NO;

    [row addSubview:errorLabel];
    [row addSubview:scroll];

    [NSLayoutConstraint activateConstraints:@[
        [errorLabel.leadingAnchor constraintEqualToAnchor:row.leadingAnchor],
        [errorLabel.topAnchor constraintEqualToAnchor:row.topAnchor],

        [scroll.leadingAnchor constraintEqualToAnchor:errorLabel.trailingAnchor constant:8.0],
        [scroll.trailingAnchor constraintEqualToAnchor:row.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:row.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:row.bottomAnchor],
        [scroll.heightAnchor constraintEqualToConstant:58.0],
        [scroll.widthAnchor constraintGreaterThanOrEqualToConstant:247.0],
    ]];

    return row;
}

#pragma mark - Dates grid

- (NSGridView*)makeDatesGrid
{
    NSTextField* datesHeader = [self makeSectionLabel:LStr(@"Dates")];
    NSTextField* timeHeader = [self makeSectionLabel:LStr(@"Time Elapsed")];

    NSTextField* addedLabel = [self makeCaptionLabel:LStr(@"Added:")];
    NSTextField* completedLabel = [self makeCaptionLabel:LStr(@"Completed:")];
    NSTextField* activityLabel = [self makeCaptionLabel:LStr(@"Last activity:")];
    NSTextField* downloadingLabel = [self makeCaptionLabel:LStr(@"Downloading:")];
    NSTextField* seedingLabel = [self makeCaptionLabel:LStr(@"Seeding:")];

    self.fDateAddedField = [self makeDateValueField];
    self.fDateCompletedField = [self makeDateValueField];
    self.fDateActivityField = [self makeDateValueField];
    self.fDownloadTimeField = [self makeValueField];
    self.fSeedTimeField = [self makeValueField];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ datesHeader ],
        @[ addedLabel, self.fDateAddedField ],
        @[ completedLabel, self.fDateCompletedField ],
        @[ activityLabel, self.fDateActivityField ],
        @[ timeHeader ],
        @[ downloadingLabel, self.fDownloadTimeField ],
        @[ seedingLabel, self.fSeedTimeField ],
    ]];

    grid.rowSpacing = 2.0;
    grid.columnSpacing = 8.0;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;

    [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(0, 1)];
    [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(4, 1)];

    [grid rowAtIndex:4].topPadding = 18.0;

    for (NSInteger r = 0; r < grid.numberOfRows; r++) {
        NSView* label = [grid cellAtColumnIndex:0 rowIndex:r].contentView;
        if (label == datesHeader || label == timeHeader)
            continue;
        [grid cellAtColumnIndex:0 rowIndex:r].xPlacement = NSGridCellPlacementTrailing;
        [grid cellAtColumnIndex:1 rowIndex:r].xPlacement = NSGridCellPlacementFill;
    }

    return grid;
}

#pragma mark - Field helpers

- (NSTextField*)makeSectionLabel:(NSString*)title
{
    NSTextField* tf = [NSTextField labelWithString:title];
    tf.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    return tf;
}

- (NSTextField*)makeCaptionLabel:(NSString*)title
{
    NSTextField* tf = [NSTextField labelWithString:title];
    tf.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    tf.alignment = NSTextAlignmentRight;
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    [tf setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [tf setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    return tf;
}

- (NSTextField*)makeValueField
{
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSZeroRect];
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    tf.editable = NO;
    tf.bezeled = NO;

    tf.drawsBackground = NO;
    tf.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    tf.lineBreakMode = NSLineBreakByTruncatingTail;
    tf.placeholderString = LStr(@"N/A");
    [tf setContentHuggingPriority:249 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [tf setContentHuggingPriority:750 forOrientation:NSLayoutConstraintOrientationVertical];
    return tf;
}

- (NSTextField*)makeDateValueField
{
    NSTextField* tf = [self makeValueField];
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.dateStyle = NSDateFormatterLongStyle;
    formatter.timeStyle = NSDateFormatterShortStyle;
    formatter.doesRelativeDateFormatting = YES;
    tf.formatter = formatter;
    return tf;
}

- (CGFloat)fHorizLayoutHeight
{
    return self.fTransferView.fittingSize.height + 2 * kStackViewInset;
}

- (CGFloat)fHorizLayoutWidth
{
    return self.fTransferView.fittingSize.width + self.fDatesView.fittingSize.width + (2 * kStackViewInset) + kStackViewHorizontalSpacing;
}

- (CGFloat)fVertLayoutHeight
{
    return self.fTransferView.fittingSize.height + self.fDatesView.fittingSize.height + (2 * kStackViewInset) + kStackViewVerticalSpacing;
}

- (CGFloat)fHeightChange
{
    return self.oldHeight - self.fCurrentHeight;
}

- (NSRect)viewRect
{
    NSRect viewRect = self.view.frame;

    CGFloat difference = self.fHeightChange;
    viewRect.size.height -= difference;

    return viewRect;
}

- (void)checkLayout
{
    if (NSWidth(self.view.window.frame) >= self.fHorizLayoutWidth + 1) {
        self.fActivityStackView.orientation = NSUserInterfaceLayoutOrientationHorizontal;

        //add some padding between views in horizontal layout
        self.fActivityStackView.spacing = kStackViewHorizontalSpacing;
        self.fCurrentHeight = self.fHorizLayoutHeight;
    } else {
        self.fActivityStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
        self.fActivityStackView.spacing = kStackViewVerticalSpacing;
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
    [self checkLayout];

    CGFloat difference = self.fHeightChange;

    NSRect windowRect = self.view.window.frame;
    windowRect.origin.y += difference;
    windowRect.size.height -= difference;

    self.view.window.minSize = NSMakeSize(self.view.window.minSize.width, NSHeight(windowRect));
    self.view.window.maxSize = NSMakeSize(FLT_MAX, NSHeight(windowRect));

    self.view.frame = [self viewRect];
    [self.view.window setFrame:windowRect display:YES animate:YES];
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

    NSInteger const numberSelected = self.fTorrents.count;
    if (numberSelected == 0) {
        return;
    }

    uint64_t have = 0;
    uint64_t haveVerified = 0;
    uint64_t downloadedTotal = 0;
    uint64_t uploadedTotal = 0;
    uint64_t failedHash = 0;
    NSDate* lastActivity = nil;
    for (Torrent* torrent in self.fTorrents) {
        have += torrent.haveTotal;
        haveVerified += torrent.haveVerified;
        downloadedTotal += torrent.downloadedTotal;
        uploadedTotal += torrent.uploadedTotal;
        failedHash += torrent.failedHash;

        NSDate* nextLastActivity;
        if ((nextLastActivity = torrent.dateActivity)) {
            lastActivity = lastActivity ? [lastActivity laterDate:nextLastActivity] : nextLastActivity;
        }
    }

    if (have == 0) {
        self.fHaveField.stringValue = [NSString stringForFileSize:0];
    } else {
        NSString* verifiedString = [NSString stringWithFormat:NSLocalizedString(@"%@ verified", "Inspector -> Activity tab -> have"),
                                                              [NSString stringForFileSize:haveVerified]];
        if (have == haveVerified) {
            self.fHaveField.stringValue = verifiedString;
        } else {
            self.fHaveField.stringValue = [NSString stringWithFormat:@"%@ (%@)", [NSString stringForFileSize:have], verifiedString];
        }
    }

    self.fDownloadedTotalField.stringValue = [NSString stringForFileSize:downloadedTotal];
    self.fUploadedTotalField.stringValue = [NSString stringForFileSize:uploadedTotal];
    self.fFailedHashField.stringValue = [NSString stringForFileSize:failedHash];

    self.fDateActivityField.objectValue = lastActivity;

    if (numberSelected == 1) {
        Torrent* torrent = self.fTorrents[0];

        self.fStateField.stringValue = torrent.stateString;

        NSString* progressString = [NSString percentString:torrent.progress];
        if (torrent.folder) {
            NSString* progressSelectedString = [NSString
                stringWithFormat:NSLocalizedString(@"%@ selected", "Inspector -> Activity tab -> progress"),
                                 [NSString percentString:torrent.progressDone]];
            progressString = [progressString stringByAppendingFormat:@" (%@)", progressSelectedString];
        }
        self.fProgressField.stringValue = progressString;

        self.fRatioField.stringValue = [NSString stringForRatio:torrent.ratio];

        NSString* errorMessage = torrent.errorMessage;
        if (![errorMessage isEqualToString:self.fErrorMessageView.string])
            self.fErrorMessageView.string = errorMessage;

        self.fDateCompletedField.objectValue = torrent.dateCompleted;

        //uses a relative date, so can't be set once
        self.fDateAddedField.objectValue = torrent.dateAdded;

        static NSDateComponentsFormatter* timeFormatter;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            timeFormatter = [NSDateComponentsFormatter new];
            timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleShort;
            timeFormatter.allowedUnits = NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond;
            timeFormatter.zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDropLeading;
        });

        self.fDownloadTimeField.stringValue = [timeFormatter stringFromTimeInterval:torrent.secondsDownloading];
        self.fSeedTimeField.stringValue = [timeFormatter stringFromTimeInterval:torrent.secondsSeeding];

        [self.fPiecesView updateView];
    } else if (numberSelected > 1) {
        self.fRatioField.stringValue = [NSString stringForRatio:tr::Values::Ratio{ uploadedTotal, downloadedTotal }];
    }
}

- (BOOL)shouldTogglePiecesViewAvailability
{
    return self.fPiecesView.torrent != nil;
}

- (void)setPiecesViewAvailability:(BOOL)availability
{
    [NSUserDefaults.standardUserDefaults setBool:availability forKey:@"PiecesViewShowAvailability"];
    [self syncPiecesControl:availability];
    [self.fPiecesView updateView];
}

- (void)syncPiecesControl:(BOOL)availability
{
    [self.fPiecesControl setSelected:availability forSegment:PiecesControlSegmentAvailable];
    [self.fPiecesControl setSelected:!availability forSegment:PiecesControlSegmentProgress];
}

- (IBAction)setPiecesView:(id)sender
{
    BOOL const availability = [sender selectedSegment] == PiecesControlSegmentAvailable;
    [self setPiecesViewAvailability:availability];
}

- (void)togglePiecesViewAvailability:(id)sender
{
    if (self.shouldTogglePiecesViewAvailability) {
        BOOL const availability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
        [self setPiecesViewAvailability:!availability];
    }
}

- (void)clearView
{
    [self.fPiecesView clearView];
}

#pragma mark - Private

- (void)setupInfo
{
    NSUInteger const count = self.fTorrents.count;
    if (count != 1) {
        if (count == 0) {
            self.fHaveField.stringValue = @"";
            self.fDownloadedTotalField.stringValue = @"";
            self.fUploadedTotalField.stringValue = @"";
            self.fFailedHashField.stringValue = @"";
            self.fDateActivityField.objectValue = @""; //using [field setStringValue: @""] causes "December 31, 1969 7:00 PM" to be displayed, at least on 10.7.3
            self.fRatioField.stringValue = @"";
        }

        self.fStateField.stringValue = @"";
        self.fProgressField.stringValue = @"";

        self.fErrorMessageView.string = @"";

        //using [field setStringValue: @""] causes "December 31, 1969 7:00 PM" to be displayed, at least on 10.7.3
        self.fDateAddedField.objectValue = @"";
        self.fDateCompletedField.objectValue = @"";

        self.fDownloadTimeField.stringValue = @"";
        self.fSeedTimeField.stringValue = @"";

        self.fPiecesView.torrent = nil;
    } else {
        self.fPiecesView.torrent = self.fTorrents[0];
    }

    BOOL const enabled = self.shouldTogglePiecesViewAvailability;
    self.fPiecesControl.enabled = enabled;
    if (enabled) {
        [self syncPiecesControl:[NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"]];
    } else {
        [self.fPiecesControl setSelected:NO forSegment:PiecesControlSegmentAvailable];
        [self.fPiecesControl setSelected:NO forSegment:PiecesControlSegmentProgress];
    }

    self.fSet = YES;
}

@end
