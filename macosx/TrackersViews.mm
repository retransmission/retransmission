// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/web-utils.h> //tr_addressIsIP()

#import "TrackersViews.h"
#import "TrackerNode.h"
#import "TrackerIconLoader.h"

static NSCache<NSString*, NSImage*>* fTrackerIconCache;
static NSMutableSet<NSString*>* fTrackerIconLoading;

@interface TrackerView ()

@property(nonatomic, strong) NSImageView* faviconView;
@property(nonatomic, strong) NSTextField* nameLabel;

@property(nonatomic, strong) NSTextField* lastAnnounceLabel;
@property(nonatomic, strong) NSTextField* nextAnnounceLabel;
@property(nonatomic, strong) NSTextField* lastScrapeLabel;

@property(nonatomic, strong) NSTextField* seederLabel;
@property(nonatomic, strong) NSTextField* leecherLabel;
@property(nonatomic, strong) NSTextField* downloadedLabel;

@property(nonatomic, strong, nullable) NSString* currentTrackedAddress;

@end

@implementation TrackerView

+ (void)initialize
{
    if (self == [TrackerView self]) {
        fTrackerIconCache = [[NSCache alloc] init];
        fTrackerIconCache.countLimit = 100;
        fTrackerIconLoading = [[NSMutableSet alloc] init];
    }
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        [self setupLayout];
    }
    return self;
}

- (nullable instancetype)initWithCoder:(NSCoder*)coder
{
    self = [super initWithCoder:coder];
    if (self) {
        [self setupLayout];
    }
    return self;
}

- (void)setupLayout
{
    self.faviconView = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil]];
    self.faviconView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.faviconView.widthAnchor constraintEqualToConstant:16.0],
        [self.faviconView.heightAnchor constraintEqualToConstant:16.0]
    ]];

    self.nameLabel = [NSTextField labelWithString:@""];
    self.nameLabel.font = [NSFont messageFontOfSize:12.0];
    self.nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;

    self.lastAnnounceLabel = [NSTextField labelWithString:@""];
    self.nextAnnounceLabel = [NSTextField labelWithString:@""];
    self.lastScrapeLabel = [NSTextField labelWithString:@""];

    self.seederLabel = [NSTextField labelWithString:@""];
    self.leecherLabel = [NSTextField labelWithString:@""];
    self.downloadedLabel = [NSTextField labelWithString:@""];

    NSArray* subLabels = @[
        self.lastAnnounceLabel,
        self.nextAnnounceLabel,
        self.lastScrapeLabel,
        self.seederLabel,
        self.leecherLabel,
        self.downloadedLabel
    ];
    for (NSTextField* label in subLabels) {
        label.font = [NSFont messageFontOfSize:9.5];
        label.textColor = NSColor.secondaryLabelColor;
        label.lineBreakMode = NSLineBreakByTruncatingTail;
    }

    self.seederLabel.alignment = NSTextAlignmentRight;
    self.leecherLabel.alignment = NSTextAlignmentRight;
    self.downloadedLabel.alignment = NSTextAlignmentRight;

    NSGridView* gridView = [NSGridView gridViewWithViews:@[
        @[ self.nameLabel, [NSGridCell emptyContentView] ],
        @[ self.lastAnnounceLabel, self.seederLabel ],
        @[ self.nextAnnounceLabel, self.leecherLabel ],
        @[ self.lastScrapeLabel, self.downloadedLabel ]
    ]];

    gridView.rowSpacing = 1.0;
    gridView.columnSpacing = 8.0;
    gridView.translatesAutoresizingMaskIntoConstraints = NO;

    [gridView columnAtIndex:0].xPlacement = NSGridCellPlacementFill;
    [gridView columnAtIndex:1].width = 95.0;
    [gridView columnAtIndex:1].xPlacement = NSGridCellPlacementFill;

    NSStackView* mainStack = [NSStackView stackViewWithViews:@[ self.faviconView, gridView ]];
    mainStack.spacing = 5.0;
    mainStack.alignment = NSLayoutAttributeTop;
    mainStack.distribution = NSStackViewDistributionFill;
    mainStack.translatesAutoresizingMaskIntoConstraints = NO;

    [self addSubview:mainStack];

    [NSLayoutConstraint activateConstraints:@[
        [mainStack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:5.0],
        [mainStack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-5.0],
        [mainStack.topAnchor constraintEqualToAnchor:self.topAnchor constant:2.0],
        [mainStack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-2.0]
    ]];
}

#pragma mark - Configuration
- (void)configureWithNode:(TrackerNode*)node
{
    self.nameLabel.stringValue = node.host ?: @"";

    self.lastAnnounceLabel.stringValue = node.lastAnnounceStatusString ?: @"";
    self.nextAnnounceLabel.stringValue = node.nextAnnounceStatusString ?: @"";
    self.lastScrapeLabel.stringValue = node.lastScrapeStatusString ?: @"";

    self.seederLabel.stringValue = [NSString
        stringWithFormat:@"%@: %@", NSLocalizedString(@"Seeders", @"tracker peer stat"), [self stringForCount:node.totalSeeders]];
    self.leecherLabel.stringValue = [NSString
        stringWithFormat:@"%@: %@", NSLocalizedString(@"Leechers", @"tracker peer stat"), [self stringForCount:node.totalLeechers]];
    self.downloadedLabel.stringValue = [NSString
        stringWithFormat:@"%@: %@", NSLocalizedString(@"Downloaded", @"tracker peer stat"), [self stringForCount:node.totalDownloaded]];

    auto trackedAddress = node.fullAnnounceAddress;
    self.currentTrackedAddress = trackedAddress;

    __weak TrackerView* weakSelf = self;
    [[TrackerIconLoader sharedInstance] fetchIconForAddress:trackedAddress completion:^(NSImage* image) {
        if ([weakSelf.currentTrackedAddress isEqualToString:trackedAddress]) {
            weakSelf.faviconView.image = image;
        }
    }];
}

- (NSString*)stringForCount:(NSInteger)count
{
    return count != -1 ? [NSString localizedStringWithFormat:@"%ld", count] : NSLocalizedString(@"N/A", nil);
}

@end

@interface TrackerTierView ()
@end

@implementation TrackerTierView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        auto tierLabel = [NSTextField labelWithString:@""];
        tierLabel.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
        tierLabel.textColor = [NSColor labelColor];

        self.textField = tierLabel;

        [self addSubview:tierLabel];

        tierLabel.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [tierLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8.0],
            [tierLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8.0],
            [tierLabel.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        ]];
    }
    return self;
}

- (void)setTier:(NSString*)tier
{
    self.textField.stringValue = tier;
}

@end

@interface TrackerInputView ()<NSTextFieldDelegate>
@end

@implementation TrackerInputView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        auto textFieldView = [[NSTextField alloc] initWithFrame:NSZeroRect];
        textFieldView.font = [NSFont systemFontOfSize:12.0];
        textFieldView.placeholderString = NSLocalizedString(@"Enter tracker announce URL…", @"Inspector -> tracker table");
        textFieldView.bezeled = YES;
        textFieldView.bezelStyle = NSTextFieldSquareBezel;
        textFieldView.editable = YES;
        textFieldView.selectable = YES;
        textFieldView.delegate = self;

        _textFieldView = textFieldView;
        [self addSubview:textFieldView];

        textFieldView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [textFieldView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8.0],
            [textFieldView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8.0],
            [textFieldView.topAnchor constraintEqualToAnchor:self.topAnchor],
            [textFieldView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}

#pragma mark - NSTextFieldDelegate

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    if (commandSelector == @selector(insertNewline:)) {
        NSString* trimmedAddress = [control.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (trimmedAddress.length > 0) {
            [self.delegate trackerInputView:self didCommitAddress:trimmedAddress];
        } else {
            [self handleCancel];
        }
        return YES;
    }

    if (commandSelector == @selector(cancelOperation:)) {
        [self handleCancel];
        return YES;
    }

    return NO;
}

- (void)handleCancel
{
    if ([self.delegate respondsToSelector:@selector(trackerInputViewDidCancel:)]) {
        [self.delegate trackerInputViewDidCancel:self];
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)obj
{
    NSString* trimmedAddress = [self.textFieldView.stringValue
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmedAddress.length > 0) {
        [self.delegate trackerInputView:self didCommitAddress:trimmedAddress];
    } else {
        [self handleCancel];
    }
}

@end
