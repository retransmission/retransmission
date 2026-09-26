// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupCell.h"
#import "NSStringAdditions.h"

// Layout
// Leading Stack
static CGFloat const kIndicatorSize = 14.0;
static CGFloat const kLeadingOffset = 11.0;

// Trailing Stack
static CGFloat const kTrailingStackHeight = 16.0;
static CGFloat const kElementStackWidth = 60.0;
static CGFloat const kStackSpacing = 4.0;
static CGFloat const kIconSize = 12.0;
static CGFloat const kTrailingOffset = -5.0; // inverted for constraints.

@interface GroupCell ()
@property(nonatomic, readonly) NSStackView* fLeadingStackView;
@property(nonatomic, readonly) NSStackView* fTrailingStackView;

@property(nonatomic, readonly) NSImageView* fIndicatorView;
@property(nonatomic, readonly) NSTextField* fTitleField;

@property(nonatomic, readonly) NSStackView* fDownloadStack;
@property(nonatomic, readonly) NSStackView* fUploadStack;
@property(nonatomic, readonly) NSStackView* fRatioStack;

@property(nonatomic, readonly) NSImageView* fDownloadIconView;
@property(nonatomic, readonly) NSImageView* fUploadIconView;
@property(nonatomic, readonly) NSImageView* fRatioIconView;

@property(nonatomic, readonly) NSTextField* fDownloadField;
@property(nonatomic, readonly) NSTextField* fUploadField;
@property(nonatomic, readonly) NSTextField* fRatioField;

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

    auto downloadField = [NSTextField labelWithString:@""];
    auto uploadField = [NSTextField labelWithString:@""];
    auto ratioField = [NSTextField labelWithString:@""];

    for (NSTextField* field in @[ downloadField, uploadField, ratioField ]) {
        field.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
        field.textColor = NSColor.secondaryLabelColor;
        field.lineBreakMode = NSLineBreakByClipping;
    }

    auto downloadIconView = [[NSImageView alloc] init];
    downloadIconView.image = [NSImage imageNamed:@"DownArrowGroupTemplate"];
    downloadIconView.toolTip = NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");

    auto uploadIconView = [[NSImageView alloc] init];
    uploadIconView.image = [NSImage imageNamed:@"UpArrowGroupTemplate"];
    uploadIconView.toolTip = NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
    uploadIconView.image.accessibilityDescription = NSLocalizedString(@"UL", "Torrent -> status image");

    auto ratioIconView = [[NSImageView alloc] init];
    ratioIconView.image = [NSImage imageNamed:@"YingYangGroupTemplate"];
    ratioIconView.toolTip = NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip");
    ratioIconView.image.accessibilityDescription = NSLocalizedString(@"Ratio", "Torrent -> status image");

    for (NSImageView* view in @[ downloadIconView, uploadIconView, ratioIconView ]) {
        view.imageScaling = NSImageScaleProportionallyDown;
        view.contentTintColor = NSColor.secondaryLabelColor;
    }

    auto downloadStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
    downloadStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    downloadStack.spacing = 4.0;
    [downloadStack addArrangedSubview:downloadIconView];
    [downloadStack addArrangedSubview:downloadField];

    auto uploadStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
    uploadStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    uploadStack.spacing = 4.0;
    [uploadStack addArrangedSubview:uploadIconView];
    [uploadStack addArrangedSubview:uploadField];

    auto ratioStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
    ratioStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    ratioStack.spacing = 4.0;
    [ratioStack addArrangedSubview:ratioIconView];
    [ratioStack addArrangedSubview:ratioField];

    auto leadingStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [leadingStackView addArrangedSubview:indicatorView];
    [leadingStackView addArrangedSubview:titleField];

    auto trailingStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];

    [trailingStackView addArrangedSubview:downloadStack];
    [trailingStackView addArrangedSubview:uploadStack];
    [trailingStackView addArrangedSubview:ratioStack];

    for (NSView* view in @[ leadingStackView, trailingStackView ]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:view];
    }

    _fLeadingStackView = leadingStackView;
    _fTrailingStackView = trailingStackView;

    _fIndicatorView = indicatorView;
    _fTitleField = titleField;

    _fDownloadStack = downloadStack;
    _fUploadStack = uploadStack;
    _fRatioStack = ratioStack;

    _fDownloadIconView = downloadIconView;
    _fUploadIconView = uploadIconView;
    _fRatioIconView = ratioIconView;

    _fDownloadField = downloadField;
    _fUploadField = uploadField;
    _fRatioField = ratioField;
}

- (void)setupConstraints
{
    [NSLayoutConstraint activateConstraints:@[
        [self.fLeadingStackView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:kLeadingOffset],
        [self.fLeadingStackView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fIndicatorView.widthAnchor constraintEqualToConstant:kIndicatorSize],
        [self.fIndicatorView.heightAnchor constraintEqualToConstant:kIndicatorSize],

        [self.fTrailingStackView.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.fTitleField.trailingAnchor],
        [self.fTrailingStackView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:kTrailingOffset],
        [self.fTrailingStackView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fTrailingStackView.heightAnchor constraintEqualToConstant:kTrailingStackHeight],

        [self.fDownloadIconView.widthAnchor constraintEqualToConstant:kIconSize],
        [self.fDownloadIconView.heightAnchor constraintEqualToConstant:kIconSize],
        [self.fUploadIconView.widthAnchor constraintEqualToConstant:kIconSize],
        [self.fUploadIconView.heightAnchor constraintEqualToConstant:kIconSize],
        [self.fRatioIconView.widthAnchor constraintEqualToConstant:kIconSize],
        [self.fRatioIconView.heightAnchor constraintEqualToConstant:kIconSize],

        [self.fDownloadStack.widthAnchor constraintGreaterThanOrEqualToConstant:kElementStackWidth],
        [self.fUploadStack.widthAnchor constraintGreaterThanOrEqualToConstant:kElementStackWidth],
        [self.fRatioStack.widthAnchor constraintGreaterThanOrEqualToConstant:kElementStackWidth]
    ]];

    [self.fTitleField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                               forOrientation:NSLayoutConstraintOrientationHorizontal];

    [self.fDownloadStack setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.fUploadStack setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.fRatioStack setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    auto isEmphasized = backgroundStyle == NSBackgroundStyleEmphasized;
    auto color = isEmphasized ? NSColor.labelColor : NSColor.secondaryLabelColor;

    self.fTitleField.textColor = color;

    self.fDownloadIconView.contentTintColor = color;
    self.fUploadIconView.contentTintColor = color;
    self.fRatioIconView.contentTintColor = color;

    self.fDownloadField.textColor = color;
    self.fUploadField.textColor = color;
    self.fRatioField.textColor = color;
}

- (void)updateImage:(NSImage*)image
{
    self.fIndicatorView.image = image;
}

- (void)updateTitle:(NSString*)title
{
    self.fTitleField.stringValue = title;
}

- (void)updateDownloadSpeed:(CGFloat)downloadSpeed
                uploadSpeed:(CGFloat)uploadSpeed
                      ratio:(CGFloat)ratio
               displayRatio:(BOOL)displayRatio
{
    self.fDownloadStack.hidden = displayRatio;
    self.fUploadStack.hidden = displayRatio;
    self.fRatioStack.hidden = !displayRatio;

    if (displayRatio) {
        self.fRatioField.stringValue = [NSString stringForRatio:ratio];
    } else {
        self.fDownloadField.stringValue = [NSString stringForSpeed:downloadSpeed];
        self.fUploadField.stringValue = [NSString stringForSpeed:uploadSpeed];
    }
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

- (BOOL)isPointInStatusArea:(NSPoint)pointInCell
{
    return pointInCell.x >= NSMinX(self.fTrailingStackView.frame);
}

@end
