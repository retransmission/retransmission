// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupCell.h"
#import "NSStringAdditions.h"

@interface GroupCell ()
@property(nonatomic, readonly) NSStackView* stackView;
@property(nonatomic, readonly) NSButton* downloadButton;
@property(nonatomic, readonly) NSButton* uploadButton;
@property(nonatomic, readonly) NSButton* ratioButton;
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

    auto titleField = [[NSTextField alloc] init];
    titleField.editable = NO;
    titleField.selectable = NO;
    titleField.bordered = NO;
    titleField.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
    titleField.drawsBackground = NO;
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
        button.imagePosition = NSImageLeft;
        button.bordered = NO;
        button.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
        button.contentTintColor = NSColor.secondaryLabelColor;
        button.lineBreakMode = NSLineBreakByClipping;
    }

    auto stackView = [[NSStackView alloc] initWithFrame:NSZeroRect];

    [stackView addArrangedSubview:downloadButton];
    [stackView addArrangedSubview:uploadButton];
    [stackView addArrangedSubview:ratioButton];

    for (NSView* view in @[ indicatorView, titleField, stackView ]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:view];
    }

    _indicatorView = indicatorView;
    _titleField = titleField;
    _downloadButton = downloadButton;
    _uploadButton = uploadButton;
    _ratioButton = ratioButton;
    _stackView = stackView;
}

- (void)setupConstraints
{
    [NSLayoutConstraint activateConstraints:@[
        // IndicatorView
        [self.indicatorView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:11],
        [self.indicatorView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.indicatorView.widthAnchor constraintEqualToConstant:14],
        [self.indicatorView.heightAnchor constraintEqualToConstant:14],

        // TitleField
        [self.titleField.leadingAnchor constraintEqualToAnchor:self.indicatorView.trailingAnchor constant:5],
        [self.titleField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

        // StackView
        [self.stackView.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.titleField.trailingAnchor],
        [self.stackView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-5],
        [self.stackView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.stackView.heightAnchor constraintEqualToConstant:16],
    ]];

    [self.titleField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                              forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    auto isEmphasized = backgroundStyle == NSBackgroundStyleEmphasized;
    self.titleField.textColor = isEmphasized ? NSColor.labelColor : NSColor.secondaryLabelColor;
}

- (void)setDownloadSpeed:(CGFloat)downloadSpeed uploadSpeed:(CGFloat)uploadSpeed ratio:(CGFloat)ratio
{
    _downloadButton.title = [NSString stringForSpeed:downloadSpeed];
    _uploadButton.title = [NSString stringForSpeed:uploadSpeed];
    _ratioButton.title = [NSString stringForRatio:ratio];
}

- (void)setDisplayRatio:(BOOL)displayRatio
{
    _downloadButton.hidden = displayRatio;
    _uploadButton.hidden = displayRatio;
    _ratioButton.hidden = !displayRatio;
}

- (void)setTooltipForTorrentsCount:(NSUInteger)count
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

@end
