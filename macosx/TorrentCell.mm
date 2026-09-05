// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "TorrentCellControlButton.h"
#import "TorrentCellActionButton.h"
#import "TorrentCellRevealButton.h"

// Layout Constants
// Considerations:
// Suffix _Size -> Size ( width x height )
// Suffix _Spacing -> Spacing between two elements inside container (most of them are in `self` view (torrent cell)).
// Suffix _Offset -> Offset to container (spacing between this view border and container border).
// Leading edge (group, icon, action button)
static CGFloat const kGroupIndicatorSize = 10.0;
static CGFloat const kGroupIndicatorToIconSpacing = 3.0;
static CGFloat const kIconSize = 36.0;
static CGFloat const kActionButtonSize = 16.0;

// Middle items (progress bar, title, status labels)
static CGFloat const kPriorityViewSize = 12.0;
static CGFloat const kIconToStackViewSpacing = 16.0;
static CGFloat const kStackViewTopOffset = 3.0;
static CGFloat const kMiddleVerticalStackSpacing = 1.0;
static CGFloat const kStatusFieldBottomOffset = -1.0; // inverted for constraints.
static CGFloat const kProgressBarHeight = 14.0;

// Trailing edge (control button, reveal button)
static CGFloat const kProgressBarToControlButtonSpacing = 14.0;
static CGFloat const kButtonSize = 14.0;
static CGFloat const kButtonsSpacing = 3.0;
static CGFloat const kRevealButtonTrailingOffset = -8.0; // inverted for constraints.

// Error status
static CGFloat const kErrorImageSize = 20.0;

@interface TorrentCell ()
@property(nonatomic, readonly) NSImageView* errorImageView;
@end

@implementation TorrentCell

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        [self configureViews];
        [self configurePriorities];
        [self setupConstraints];
    }
    return self;
}

- (void)configureViews
{
    auto groupIndicatorView = [[NSImageView alloc] init];

    auto iconView = [[NSImageView alloc] init];
    auto actionButton = [[TorrentCellActionButton alloc] init];

    auto stackView = [[NSStackView alloc] init];
    stackView.distribution = NSStackViewDistributionFill;
    stackView.spacing = 4;

    auto torrentTitleField = [[NSTextField alloc] init];
    torrentTitleField.textColor = NSColor.labelColor;
    torrentTitleField.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular];

    auto torrentPriorityView = [[NSImageView alloc] init];

    [stackView addArrangedSubview:torrentTitleField];
    [stackView addArrangedSubview:torrentPriorityView];

    auto torrentProgressField = [[NSTextField alloc] init];
    torrentProgressField.textColor = NSColor.secondaryLabelColor;
    torrentProgressField.font = [NSFont systemFontOfSize:10.0 weight:NSFontWeightRegular];

    auto torrentStatusField = [[NSTextField alloc] init];
    torrentStatusField.textColor = NSColor.secondaryLabelColor;
    torrentStatusField.font = [NSFont systemFontOfSize:10.0 weight:NSFontWeightRegular];

    auto torrentProgressBarView = [[NSView alloc] init];

    auto controlButton = [[TorrentCellControlButton alloc] init];
    auto revealButton = [[TorrentCellRevealButton alloc] init];

    for (NSImageView* imageView in @[ groupIndicatorView, iconView, torrentPriorityView ]) {
        imageView.imageScaling = NSImageScaleProportionallyDown;
    }

    for (NSTextField* textField in @[ torrentTitleField, torrentProgressField, torrentStatusField ]) {
        textField.editable = NO;
        textField.selectable = NO;
        textField.bordered = NO;
        textField.drawsBackground = NO;
        textField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    }

    for (NSButton* button in @[ actionButton, controlButton, revealButton ]) {
        button.imagePosition = NSImageOnly;
        button.imageScaling = NSImageScaleProportionallyDown;
        [button setButtonType:NSButtonTypeMomentaryPushIn];
        [button setBezelStyle:NSBezelStyleRegularSquare];
        button.bordered = NO;
    }

    for (NSView* view in @[
             groupIndicatorView,
             iconView,
             actionButton,
             stackView,
             torrentProgressField,
             torrentStatusField,
             torrentProgressBarView,
             controlButton,
             revealButton,
         ]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
    }

    self.fGroupIndicatorView = groupIndicatorView;
    self.fIconView = iconView;
    self.fActionButton = actionButton;
    self.fStackView = stackView;
    self.fTorrentTitleField = torrentTitleField;
    self.fTorrentPriorityView = torrentPriorityView;
    self.fTorrentProgressField = torrentProgressField;
    self.fTorrentStatusField = torrentStatusField;
    self.fTorrentProgressBarView = torrentProgressBarView;
    self.fControlButton = controlButton;
    self.fRevealButton = revealButton;

    actionButton.torrentCell = self;
    controlButton.torrentCell = self;
    revealButton.torrentCell = self;
}

- (void)configurePriorities
{
    auto groupIndicatorView = self.fGroupIndicatorView;
    auto iconView = self.fIconView;
    auto actionButton = self.fActionButton;
    auto torrentTitleField = self.fTorrentTitleField;
    auto torrentPriorityView = self.fTorrentPriorityView;
    auto torrentProgressField = self.fTorrentProgressField;
    auto torrentStatusField = self.fTorrentStatusField;
    auto controlButton = self.fControlButton;
    auto revealButton = self.fRevealButton;

    // left items
    for (NSView* leftView in @[ groupIndicatorView, iconView ]) {
        [leftView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
        [leftView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationVertical];
    }

    [actionButton setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];

    /// middle items
    [torrentTitleField setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [torrentTitleField setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [torrentPriorityView setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [torrentPriorityView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationVertical];

    for (NSView* middleView in @[ torrentProgressField, torrentStatusField ]) {
        [middleView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
        [middleView setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    }

    // right items
    for (NSView* rightView in @[ controlButton, revealButton ]) {
        [rightView setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [rightView setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
        [rightView setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                            forOrientation:NSLayoutConstraintOrientationHorizontal];
    }
}

- (void)setupConstraints
{
    auto groupIndicatorView = self.fGroupIndicatorView;
    auto iconView = self.fIconView;
    auto actionButton = self.fActionButton;
    auto torrentPriorityView = self.fTorrentPriorityView;
    auto stackView = self.fStackView;
    auto torrentProgressField = self.fTorrentProgressField;
    auto torrentStatusField = self.fTorrentStatusField;
    auto torrentProgressBarView = self.fTorrentProgressBarView;
    auto controlButton = self.fControlButton;
    auto revealButton = self.fRevealButton;

    for (NSView* view in @[
             groupIndicatorView,
             iconView,
             actionButton,
             stackView,
             torrentProgressField,
             torrentStatusField,
             torrentProgressBarView,
             controlButton,
             revealButton,
         ]) {
        [self addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        // groupIndicatorView
        [groupIndicatorView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [groupIndicatorView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [groupIndicatorView.widthAnchor constraintEqualToConstant:kGroupIndicatorSize],
        [groupIndicatorView.heightAnchor constraintEqualToConstant:kGroupIndicatorSize],

        // iconView
        [iconView.leadingAnchor constraintEqualToAnchor:groupIndicatorView.trailingAnchor constant:kGroupIndicatorToIconSpacing],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:kIconSize],
        [iconView.heightAnchor constraintEqualToConstant:kIconSize],

        // actionButton
        [actionButton.centerXAnchor constraintEqualToAnchor:iconView.centerXAnchor],
        [actionButton.centerYAnchor constraintEqualToAnchor:iconView.centerYAnchor],
        [actionButton.widthAnchor constraintEqualToConstant:kActionButtonSize],
        [actionButton.heightAnchor constraintEqualToConstant:kActionButtonSize],

        // torrentPriorityView
        [torrentPriorityView.heightAnchor constraintEqualToConstant:kPriorityViewSize],
        [torrentPriorityView.widthAnchor constraintEqualToConstant:kPriorityViewSize],

        // stackView
        [stackView.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:kIconToStackViewSpacing],
        [stackView.trailingAnchor constraintLessThanOrEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [stackView.topAnchor constraintEqualToAnchor:self.topAnchor constant:kStackViewTopOffset],
        [stackView.leadingAnchor constraintEqualToAnchor:torrentProgressField.leadingAnchor],

        // torrentProgressField
        [torrentProgressField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentProgressField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentProgressField.topAnchor constraintEqualToAnchor:stackView.bottomAnchor constant:kMiddleVerticalStackSpacing],

        // torrentStatusField
        [torrentStatusField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentStatusField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentStatusField.topAnchor constraintEqualToAnchor:torrentProgressBarView.bottomAnchor constant:kMiddleVerticalStackSpacing],
        [torrentStatusField.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:kStatusFieldBottomOffset],

        // torrentProgressBarView
        [torrentProgressBarView.topAnchor constraintEqualToAnchor:torrentProgressField.bottomAnchor constant:kMiddleVerticalStackSpacing],
        [torrentProgressBarView.heightAnchor constraintEqualToConstant:kProgressBarHeight],

        // controlButton
        [controlButton.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor
                                                    constant:kProgressBarToControlButtonSpacing],
        [controlButton.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [controlButton.widthAnchor constraintEqualToConstant:kButtonSize],
        [controlButton.heightAnchor constraintEqualToConstant:kButtonSize],

        // revealButton
        [revealButton.leadingAnchor constraintEqualToAnchor:controlButton.trailingAnchor constant:kButtonsSpacing],
        [revealButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:kRevealButtonTrailingOffset],
        [revealButton.centerYAnchor constraintEqualToAnchor:controlButton.centerYAnchor],
        [revealButton.widthAnchor constraintEqualToConstant:kButtonSize],
        [revealButton.heightAnchor constraintEqualToConstant:kButtonSize],
    ]];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.fTorrentTableView) {
        Torrent* torrent = (Torrent*)self.objectValue;

        // draw progress bar
        NSRect barRect = self.fTorrentProgressBarView.frame;
        [ProgressBarView.sharedInstance drawBarInRect:barRect forTableView:self.fTorrentTableView withTorrent:torrent];
    }

    [super drawRect:dirtyRect];
}

- (void)setObjectValue:(id)objectValue
{
    [super setObjectValue:objectValue];
    [self setTorrentPriority:[(Torrent*)objectValue priority]];
}

- (void)setTorrentPriority:(tr_priority_t)priority
{
    BOOL const hasPriority = priority != TR_PRI_NORMAL;

    NSImage* const image = hasPriority ?
        [NSImage imageNamed:(priority == TR_PRI_HIGH ? @"PriorityHighTemplate" : @"PriorityLowTemplate")] :
        nil;

    NSStackViewVisibilityPriority const visibility = hasPriority ? NSStackViewVisibilityPriorityMustHold : NSStackViewVisibilityPriorityNotVisible;

    if (self.fTorrentPriorityView.image == image && [self.fStackView visibilityPriorityForView:self.fTorrentPriorityView] == visibility) {
        return;
    }

    self.fTorrentPriorityView.image = image;
    [self.fStackView setVisibilityPriority:visibility forView:self.fTorrentPriorityView];
}

// otherwise progress bar is inverted
- (BOOL)isFlipped
{
    return YES;
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    NSColor* priorityColor = backgroundStyle == NSBackgroundStyleEmphasized ? NSColor.whiteColor : NSColor.labelColor;
    self.fTorrentPriorityView.contentTintColor = priorityColor;
}

- (void)setAnyErrorOrWarning:(BOOL)errorOrWarning
{
    if (errorOrWarning) {
        if (_errorImageView == nil) {
            _errorImageView = [[NSImageView alloc] init];
            _errorImageView.imageScaling = NSImageScaleProportionallyDown;
            _errorImageView.image = [NSImage imageNamed:NSImageNameCaution];
            [self.fIconView addSubview:_errorImageView];
            _errorImageView.translatesAutoresizingMaskIntoConstraints = NO;

            [NSLayoutConstraint activateConstraints:@[
                [_errorImageView.leadingAnchor constraintEqualToAnchor:self.fIconView.centerXAnchor],
                [_errorImageView.topAnchor constraintEqualToAnchor:self.fIconView.centerYAnchor],
                [_errorImageView.widthAnchor constraintEqualToConstant:kErrorImageSize],
                [_errorImageView.heightAnchor constraintEqualToConstant:kErrorImageSize],
            ]];
        }
    }

    _errorImageView.hidden = !errorOrWarning;
}

@end
