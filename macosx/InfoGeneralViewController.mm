// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoGeneralViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define LStr(key) NSLocalizedStringFromTable(key, @"InfoGeneralView", @"")

@interface InfoGeneralViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSTextField* fPiecesField;
@property(nonatomic) NSTextField* fHashField;
@property(nonatomic) NSTextField* fSecureField;
@property(nonatomic) NSTextField* fDataLocationField;
@property(nonatomic) NSTextField* fLastDataLocationField;
@property(nonatomic) NSTextField* fLastDataLabel;
@property(nonatomic) NSTextField* fCreatorField;
@property(nonatomic) NSTextField* fDateCreatedField;

@property(nonatomic) NSTextView* fCommentView;

@property(nonatomic) NSButton* fRevealDataButton;

@end

@implementation InfoGeneralViewController
- (NSTextField*)createLabel:(NSString*)title isBold:(BOOL)isBold
{
    NSTextField* label = [NSTextField labelWithString:title];
    label.font = isBold ? [NSFont boldSystemFontOfSize:[NSFont smallSystemFontSize]] :
                          [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    label.alignment = NSTextAlignmentRight;
    return label;
}

- (NSTextField*)createValueField
{
    NSTextField* field = [[NSTextField alloc] init];
    field.editable = NO;
    field.selectable = YES;
    field.drawsBackground = NO;
    field.bezeled = NO;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [field setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow - 1
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        self.title = NSLocalizedString(@"Information", "Inspector view -> title");
    }
    return self;
}

- (void)loadView
{
    self.view = [[NSView alloc] initWithFrame:NSZeroRect];
    self.view.translatesAutoresizingMaskIntoConstraints = NO;

    self.fPiecesField = [self createValueField];
    self.fHashField = [self createValueField];
    self.fSecureField = [self createValueField];
    self.fCreatorField = [self createValueField];
    self.fDateCreatedField = [self createValueField];
    NSDateFormatter* dateCreatedFormatter = [[NSDateFormatter alloc] init];
    dateCreatedFormatter.dateStyle = NSDateFormatterLongStyle;
    dateCreatedFormatter.timeStyle = NSDateFormatterShortStyle;
    self.fDateCreatedField.formatter = dateCreatedFormatter;

    NSScrollView* commentScroll = [NSTextView scrollableTextView];
    commentScroll.borderType = NSBezelBorder;

    self.fCommentView = commentScroll.documentView;
    self.fCommentView.editable = NO;
    self.fCommentView.richText = NO;
    self.fCommentView.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [commentScroll.heightAnchor constraintEqualToConstant:58].active = YES;

    self.fDataLocationField = [self createValueField];
    self.fDataLocationField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    self.fLastDataLabel = [self createLabel:LStr(@"Was:") isBold:NO];
    self.fLastDataLocationField = [self createValueField];
    self.fLastDataLocationField.lineBreakMode = NSLineBreakByTruncatingMiddle;

    for (NSTextField* textField in @[
             self.fPiecesField,
             self.fHashField,
             self.fSecureField,
             self.fCreatorField,
             self.fDateCreatedField,
             self.fDataLocationField,
             self.fLastDataLocationField
         ]) {
        textField.placeholderString = LStr(@"N/A");
    }

    self.fRevealDataButton = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"magnifyingglass" accessibilityDescription:nil]
                                                target:self
                                                action:@selector(revealDataFile:)];
    self.fRevealDataButton.bordered = NO;
    [self.fRevealDataButton.widthAnchor constraintEqualToConstant:14].active = YES;
    [self.fRevealDataButton.heightAnchor constraintEqualToConstant:14].active = YES;

    NSStackView* locationStack = [NSStackView stackViewWithViews:@[
        self.fDataLocationField,
        self.fRevealDataButton,
        self.fLastDataLabel,
        self.fLastDataLocationField
    ]];
    locationStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    locationStack.spacing = 8;

    NSTextField* detailsTitle = [self createLabel:LStr(@"Details") isBold:YES];
    NSTextField* locationTitle = [self createLabel:LStr(@"Location") isBold:YES];

    NSArray* rows = @[
        @[ detailsTitle, [NSGridCell emptyContentView] ],
        @[ [self createLabel:LStr(@"Pieces:") isBold:NO], self.fPiecesField ],
        @[ [self createLabel:LStr(@"Hash:") isBold:NO], self.fHashField ],
        @[ [self createLabel:LStr(@"Privacy:") isBold:NO], self.fSecureField ],
        @[ [self createLabel:LStr(@"Creator:") isBold:NO], self.fCreatorField ],
        @[ [self createLabel:LStr(@"Created On:") isBold:NO], self.fDateCreatedField ],
        @[ [self createLabel:LStr(@"Comment:") isBold:NO], commentScroll ],
        @[ [NSGridCell emptyContentView], [NSGridCell emptyContentView] ],
        @[ locationTitle, [NSGridCell emptyContentView] ],
        @[ [self createLabel:LStr(@"Location:") isBold:NO], locationStack ]
    ];

    NSGridView* mainGrid = [NSGridView gridViewWithViews:rows];
    mainGrid.translatesAutoresizingMaskIntoConstraints = NO;
    mainGrid.rowSpacing = 4;
    mainGrid.columnSpacing = 8;

    // Left column has trailing alignment
    [mainGrid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;

    // But Details and Location are titles. They should have leading alignment
    [mainGrid cellForView:detailsTitle].xPlacement = NSGridCellPlacementLeading;
    [mainGrid cellForView:locationTitle].xPlacement = NSGridCellPlacementLeading;

    // The comment box has no intrinsic width, so it fills its cell
    [mainGrid cellForView:commentScroll].xPlacement = NSGridCellPlacementFill;

    // Section headers span both columns so they don't widen the label column
    [[mainGrid cellForView:detailsTitle].row mergeCellsInRange:NSMakeRange(0, 2)];
    [[mainGrid cellForView:locationTitle].row mergeCellsInRange:NSMakeRange(0, 2)];

    [self.view addSubview:mainGrid];

    [NSLayoutConstraint activateConstraints:@[
        [mainGrid.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:12],
        [mainGrid.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-12],
        [mainGrid.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:12],
        [mainGrid.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-12]
    ]];

    [self.view setFrameSize:self.view.fittingSize];
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

    if (self.fTorrents.count != 1) {
        return;
    }

    Torrent* torrent = self.fTorrents[0];

    NSString* location = torrent.dataLocation;
    NSString* lastKnownDataLocation = torrent.lastKnownDataLocation;

    self.fDataLocationField.stringValue = location ? location.stringByAbbreviatingWithTildeInPath : @"";
    self.fDataLocationField.toolTip = location ? location : @"";

    self.fLastDataLabel.hidden = location ? YES : NO;
    self.fLastDataLocationField.hidden = location ? YES : NO;
    self.fLastDataLocationField.stringValue = location ? @"" : lastKnownDataLocation.stringByAbbreviatingWithTildeInPath ?: @"";
    self.fLastDataLocationField.toolTip = location ? @"" : lastKnownDataLocation;

    self.fRevealDataButton.hidden = location ? NO : YES;
}

- (void)revealDataFile:(id)sender
{
    Torrent* torrent = self.fTorrents[0];
    NSString* location = torrent.dataLocation;
    if (!location) {
        return;
    }

    NSURL* file = [NSURL fileURLWithPath:location];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
}

#pragma mark - Private

- (void)setupInfo
{
    self.fLastDataLabel.hidden = YES;
    self.fLastDataLocationField.hidden = YES;

    if (self.fTorrents.count == 1) {
        Torrent* torrent = self.fTorrents[0];

        // Associated Press Style: "Use a semicolon to clarify a series that includes a number of commas."
        NSString* piecesString = !torrent.magnet ?
            [NSString localizedStringWithFormat:@"%ld; %@", torrent.pieceCount, [NSString stringForFileSize:torrent.pieceSize]] :
            @"";
        self.fPiecesField.stringValue = piecesString;

        NSString* hashString = torrent.hashString;
        self.fHashField.stringValue = hashString;
        self.fHashField.toolTip = hashString;
        self.fSecureField.stringValue = torrent.privateTorrent ?
            NSLocalizedString(@"Private to this tracker -- DHT and PEX disabled", "Inspector -> private torrent") :
            NSLocalizedString(@"Public torrent", "Inspector -> private torrent");

        NSString* commentString = torrent.comment;
        self.fCommentView.string = commentString;

        NSString* creatorString = torrent.creator;
        self.fCreatorField.stringValue = creatorString;
        self.fDateCreatedField.objectValue = torrent.dateCreated;
    } else {
        self.fPiecesField.stringValue = @"";
        self.fHashField.stringValue = @"";
        self.fHashField.toolTip = nil;
        self.fSecureField.stringValue = @"";
        self.fCommentView.string = @"";

        self.fCreatorField.stringValue = @"";
        self.fDateCreatedField.stringValue = @"";

        self.fDataLocationField.stringValue = @"";
        self.fDataLocationField.toolTip = nil;

        self.fRevealDataButton.hidden = YES;
    }

    self.fSet = YES;
}

@end
