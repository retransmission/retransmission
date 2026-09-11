// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupCell.h"
#import "NSStringAdditions.h"

// Layout
// Leading Stack
static CGFloat const kIndicatorSize = 14.0;
static NSEdgeInsets const kLeadingInsets = NSEdgeInsetsMake(0, 11, 0, 0);

// Trailing Stack
static CGFloat const kTrailingStackHeight = 16.0;
static CGFloat const kButtonWidth = 60.0;
static NSEdgeInsets const kTrailingInsets = NSEdgeInsetsMake(1, 0, 1, 5);

@interface GroupCell ()
@property(nonatomic, readonly) NSStackView* fLeadingStackView;
@property(nonatomic, readonly) NSStackView* fTrailingStackView;
@property(nonatomic, readonly) NSImageView* fIndicatorView;
@property(nonatomic, readonly) NSTextField* fTitleField;
@property(nonatomic, readonly) NSButton* fDownloadButton;
@property(nonatomic, readonly) NSButton* fUploadButton;
@property(nonatomic, readonly) NSButton* fRatioButton;
@end

@implementation GroupCell

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        [self configureViews];
        [self setupConstraints];
    }

    return self;
}

- (void)configureViews
{
    auto indicatorView = [[NSImageView alloc] init];
    indicatorView.imageScaling = NSImageScaleProportionallyDown;

    auto titleField = [NSTextField labelWithString:@""];
    titleField.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
    titleField.textColor = NSColor.secondaryLabelColor;
    titleField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    titleField.allowsExpansionToolTips = YES;

    auto downloadButton = [NSButton buttonWithTitle:@"" image:[NSImage imageNamed:@"DownArrowGroupTemplate"] target:nil action:nil];
    downloadButton.toolTip = NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");

    auto uploadButton = [NSButton buttonWithTitle:@"" image:[NSImage imageNamed:@"UpArrowGroupTemplate"] target:nil action:nil];
    uploadButton.toolTip = NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
    uploadButton.image.accessibilityDescription = NSLocalizedString(@"UL", "Torrent -> status image");

    auto ratioButton = [NSButton buttonWithTitle:@"" image:[NSImage imageNamed:@"YingYangGroupTemplate"] target:nil action:nil];
    ratioButton.toolTip = NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip");
    ratioButton.image.accessibilityDescription = NSLocalizedString(@"Ratio", "Torrent -> status image");

    for (NSButton* button in @[ downloadButton, uploadButton, ratioButton ]) {
        button.imageScaling = NSImageScaleProportionallyDown;
        button.imagePosition = NSImageLeading;
        button.bordered = NO;
        button.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
        button.contentTintColor = NSColor.secondaryLabelColor;
        button.lineBreakMode = NSLineBreakByClipping;
    }

    auto leadingStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [leadingStackView addArrangedSubview:indicatorView];
    [leadingStackView addArrangedSubview:titleField];
    leadingStackView.edgeInsets = kLeadingInsets;

    auto trailingStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];

    [trailingStackView addArrangedSubview:downloadButton];
    [trailingStackView addArrangedSubview:uploadButton];
    [trailingStackView addArrangedSubview:ratioButton];
    trailingStackView.edgeInsets = kTrailingInsets;

    for (NSView* view in @[ leadingStackView, trailingStackView ]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:view];
    }

    _fIndicatorView = indicatorView;
    _fTitleField = titleField;
    _fDownloadButton = downloadButton;
    _fUploadButton = uploadButton;
    _fRatioButton = ratioButton;
    _fLeadingStackView = leadingStackView;
    _fTrailingStackView = trailingStackView;
}

- (void)setupConstraints
{
    [NSLayoutConstraint activateConstraints:@[
        [self.fLeadingStackView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [self.fLeadingStackView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fIndicatorView.widthAnchor constraintEqualToConstant:kIndicatorSize],
        [self.fIndicatorView.heightAnchor constraintEqualToConstant:kIndicatorSize],

        [self.fTrailingStackView.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.fTitleField.trailingAnchor],
        [self.fTrailingStackView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [self.fTrailingStackView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fTrailingStackView.heightAnchor constraintEqualToConstant:kTrailingStackHeight],

        [self.fDownloadButton.widthAnchor constraintGreaterThanOrEqualToConstant:kButtonWidth],
        [self.fUploadButton.widthAnchor constraintGreaterThanOrEqualToConstant:kButtonWidth],
        [self.fRatioButton.widthAnchor constraintGreaterThanOrEqualToConstant:kButtonWidth]
    ]];

    [self.fTitleField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                               forOrientation:NSLayoutConstraintOrientationHorizontal];

    [self.fDownloadButton setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1
                                     forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.fUploadButton setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.fRatioButton setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    auto isEmphasized = backgroundStyle == NSBackgroundStyleEmphasized;
    auto color = isEmphasized ? NSColor.labelColor : NSColor.secondaryLabelColor;

    self.fTitleField.textColor = color;
    self.fDownloadButton.contentTintColor = color;
    self.fUploadButton.contentTintColor = color;
    self.fRatioButton.contentTintColor = color;
}

- (void)updateImage:(NSImage*)image
{
    self.fIndicatorView.image = image;
}

- (void)updateTitle:(NSString*)title
{
    self.fTitleField.stringValue = title;
}

- (void)updateDownloadSpeed:(CGFloat)downloadSpeed uploadSpeed:(CGFloat)uploadSpeed ratio:(CGFloat)ratio
{
    self.fDownloadButton.title = [NSString stringForSpeed:downloadSpeed];
    self.fUploadButton.title = [NSString stringForSpeed:uploadSpeed];
    self.fRatioButton.title = [NSString stringForRatio:ratio];
}

- (void)updateDisplayRatio:(BOOL)displayRatio
{
    self.fDownloadButton.hidden = displayRatio;
    self.fUploadButton.hidden = displayRatio;
    self.fRatioButton.hidden = !displayRatio;
}

- (void)updateTooltipForTorrentsCount:(NSUInteger)count
{
    NSString* tooltipGroup;
    if (count == 1) {
        tooltipGroup = NSLocalizedString(@"1 transfer", "Torrent table -> group row -> tooltip");
    } else {
        tooltipGroup = NSLocalizedString(@"%lu transfers", "Torrent table -> group row -> tooltip");
        tooltipGroup = [NSString localizedStringWithFormat:tooltipGroup, count];
    }
    self.toolTip = tooltipGroup;
}

- (CGRect)frameForTitle
{
    return self.fLeadingStackView.frame;
}

@end
