// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SmallTorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "TorrentTableView.h"
#import "TorrentCellControlButton.h"
#import "TorrentCellActionButton.h"
#import "TorrentCellRevealButton.h"

// Layout Constants
// Considerations:
// Suffix _Size -> Size ( width x height )
// Suffix _Spacing -> Spacing between two elements inside container (most of them are in `self` view (torrent cell)).
// Suffix _Offset -> Offset to container (spacing between this view border and container border).
// Leading edge (group, icon, action button)
static CGFloat const kGroupIndicatorSize = 6.0;
static CGFloat const kGroupIndicatorToIconSpacing = 8.0;
static CGFloat const kIconSize = 16.0;
static CGFloat const kActionButtonSize = 16.0;
static CGFloat const kIconToProgressBarSpacing = 15.0;

// Middle items (progress bar, title, priority labels)
static CGFloat const kPriorityViewSize = 12.0;
static CGFloat const kProgressBarHeight = 18.0;
static CGFloat const kProgressBarTrailingOffset = -5.0; // inverted for constraints.

// Trailing edge (control button, reveal button, status field)
static CGFloat const kStackViewToStatusFieldSpacing = 4.0;
static CGFloat const kTrailingOffset = -3.0; // inverted for constraints.
static CGFloat const kButtonSize = 14.0;
static CGFloat const kButtonsSpacing = 3.0;

@interface SmallTorrentCell ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@end

@implementation SmallTorrentCell

- (void)configureViews
{
    [super configureViews];
    // also set alignment as part of priorities ( text inside a textLabel ).
    self.fTorrentStatusField.alignment = NSTextAlignmentRight;

    // initially we set buttons hidden and reveal them on hover.
    self.fControlButton.hidden = YES;
    self.fRevealButton.hidden = YES;
}

// Layout
- (void)configurePriorities
{
    auto groupIndicatorView = self.fGroupIndicatorView;
    auto iconView = self.fIconView;
    auto torrentTitleField = self.fTorrentTitleField;
    auto torrentPriorityView = self.fTorrentPriorityView;
    auto torrentStatusField = self.fTorrentStatusField;

    // left items
    for (NSView* leftView in @[ groupIndicatorView, iconView ]) {
        [leftView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
        [leftView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationVertical];
    }

    /// middle items
    [torrentTitleField setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [torrentTitleField setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [torrentPriorityView setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [torrentPriorityView setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationVertical];

    [torrentStatusField setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [torrentStatusField setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [torrentStatusField setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                                 forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)setupConstraints
{
    auto groupIndicatorView = self.fGroupIndicatorView;
    auto iconView = self.fIconView;
    auto actionButton = self.fActionButton;
    auto torrentPriorityView = self.fTorrentPriorityView;
    auto stackView = self.fStackView;
    auto torrentStatusField = self.fTorrentStatusField;
    auto torrentProgressBarView = self.fTorrentProgressBarView;
    auto controlButton = self.fControlButton;
    auto revealButton = self.fRevealButton;

    for (NSView* view in @[
             groupIndicatorView,
             iconView,
             actionButton,
             torrentProgressBarView,
             stackView,
             torrentStatusField,
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

        // torrentProgressBarView
        [torrentProgressBarView.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:kIconToProgressBarSpacing],
        [torrentProgressBarView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:kProgressBarTrailingOffset],
        [torrentProgressBarView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [torrentProgressBarView.heightAnchor constraintEqualToConstant:kProgressBarHeight],

        // torrentPriorityView
        [torrentPriorityView.heightAnchor constraintEqualToConstant:kPriorityViewSize],
        [torrentPriorityView.widthAnchor constraintEqualToConstant:kPriorityViewSize],

        // stackView
        [stackView.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [stackView.topAnchor constraintEqualToAnchor:torrentProgressBarView.topAnchor],
        [stackView.bottomAnchor constraintEqualToAnchor:torrentProgressBarView.bottomAnchor],

        // torrentStatusField
        [torrentStatusField.leadingAnchor constraintEqualToAnchor:stackView.trailingAnchor constant:kStackViewToStatusFieldSpacing],
        [torrentStatusField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:kTrailingOffset],
        [torrentStatusField.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],

        // controlButton
        [controlButton.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [controlButton.widthAnchor constraintEqualToConstant:kButtonSize],
        [controlButton.heightAnchor constraintEqualToConstant:kButtonSize],

        // revealButton
        [revealButton.leadingAnchor constraintEqualToAnchor:controlButton.trailingAnchor constant:kButtonsSpacing],
        [revealButton.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:kTrailingOffset],
        [revealButton.centerYAnchor constraintEqualToAnchor:controlButton.centerYAnchor],
        [revealButton.widthAnchor constraintEqualToConstant:kButtonSize],
        [revealButton.heightAnchor constraintEqualToConstant:kButtonSize],
    ]];
}

// show fControlButton and fRevealButton
- (void)handleIsHovering:(BOOL)isHovering
{
    self.fControlButton.hidden = !isHovering;
    self.fRevealButton.hidden = !isHovering;
    self.fTorrentStatusField.hidden = isHovering;
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];

    [self handleIsHovering:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];

    [self handleIsHovering:NO];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (self.fTrackingArea != nil) {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSRect rect = self.bounds;

    NSTrackingAreaOptions options = (NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow);

    // Check if mouse is currently inside the bounds
    NSPoint mouseLocation = [self.window mouseLocationOutsideOfEventStream];
    NSPoint localPoint = [self convertPoint:mouseLocation fromView:nil];
    if (NSPointInRect(localPoint, self.bounds)) {
        options |= NSTrackingAssumeInside;
        [self handleIsHovering:YES];
    } else {
        [self handleIsHovering:NO];
    }

    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:rect options:options owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
