// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "DragOverlayView.h"

// Layout

// Badge
static CGFloat const kBadgeWidth = 325.0;
static CGFloat const kBadgeHeight = 84.0;
static CGFloat const kBadgeCornerRadius = 15.0;

// MainStack
static CGFloat const kMainStackInset = 10.0;
static CGFloat const kMainStackSpacing = 5.0;

// Icon
static CGFloat const kIconSize = 64.0;

// TextStack
static CGFloat const kTextStackSpacing = 2.0;

@interface DragOverlayView ()

@property(nonatomic, readonly) NSImageView* fIconImageView;
@property(nonatomic, readonly) NSTextField* fTitleLabel;
@property(nonatomic, readonly) NSTextField* fSubtitleLabel;

@end

@implementation DragOverlayView

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        NSView* badgeContainer = [[NSView alloc] initWithFrame:NSZeroRect];
        badgeContainer.translatesAutoresizingMaskIntoConstraints = NO;
        badgeContainer.wantsLayer = YES;
        badgeContainer.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.75].CGColor;
        badgeContainer.layer.cornerRadius = kBadgeCornerRadius;
        [self addSubview:badgeContainer];

        _fIconImageView = [[NSImageView alloc] initWithFrame:NSZeroRect];
        _fIconImageView.imageScaling = NSImageScaleProportionallyUpOrDown;

        _fTitleLabel = [NSTextField labelWithString:@""];
        _fTitleLabel.font = [NSFont boldSystemFontOfSize:18.0];

        _fSubtitleLabel = [NSTextField labelWithString:@""];
        _fSubtitleLabel.font = [NSFont systemFontOfSize:14.0];

        for (NSTextField* label in @[ _fTitleLabel, _fSubtitleLabel ]) {
            label.textColor = NSColor.whiteColor;
            label.maximumNumberOfLines = 1;
            label.lineBreakMode = NSLineBreakByTruncatingMiddle;
        }

        NSStackView* textStackView = [NSStackView stackViewWithViews:@[ _fTitleLabel, _fSubtitleLabel ]];
        textStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
        textStackView.alignment = NSLayoutAttributeLeading;
        textStackView.spacing = kTextStackSpacing;

        // Prevent the vertical stack from stretching awkwardly in height
        [textStackView setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];

        NSStackView* mainStackView = [NSStackView stackViewWithViews:@[ _fIconImageView, textStackView ]];
        mainStackView.translatesAutoresizingMaskIntoConstraints = NO;
        mainStackView.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        mainStackView.alignment = NSLayoutAttributeCenterY;
        mainStackView.spacing = kMainStackSpacing;

        // AppKit NSStackView edgeInsets push views INWARD, so we use positive numbers here
        mainStackView.edgeInsets = NSEdgeInsetsMake(0, kMainStackInset, 0, kMainStackInset);

        [badgeContainer addSubview:mainStackView];

        [NSLayoutConstraint activateConstraints:@[
            [badgeContainer.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [badgeContainer.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

            [badgeContainer.widthAnchor constraintEqualToConstant:kBadgeWidth],
            [badgeContainer.heightAnchor constraintEqualToConstant:kBadgeHeight],

            [_fIconImageView.widthAnchor constraintEqualToConstant:kIconSize],
            [_fIconImageView.heightAnchor constraintEqualToConstant:kIconSize],

            [mainStackView.leadingAnchor constraintEqualToAnchor:badgeContainer.leadingAnchor],
            [mainStackView.trailingAnchor constraintEqualToAnchor:badgeContainer.trailingAnchor],
            [mainStackView.topAnchor constraintEqualToAnchor:badgeContainer.topAnchor],
            [mainStackView.bottomAnchor constraintEqualToAnchor:badgeContainer.bottomAnchor]
        ]];
    }
    return self;
}

- (void)setOverlay:(NSImage*)icon mainLine:(NSString*)mainLine subLine:(NSString*)subLine
{
    self.fIconImageView.image = icon;
    self.fTitleLabel.stringValue = mainLine ?: @"";
    self.fSubtitleLabel.stringValue = subLine ?: @"";
}

@end
