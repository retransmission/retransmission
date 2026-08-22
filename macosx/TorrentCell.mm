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
        [groupIndicatorView.widthAnchor constraintEqualToConstant:10],
        [groupIndicatorView.heightAnchor constraintEqualToConstant:10],

        // iconView
        [iconView.leadingAnchor constraintEqualToAnchor:groupIndicatorView.trailingAnchor constant:3],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:36],
        [iconView.heightAnchor constraintEqualToConstant:36],

        // actionButton
        [actionButton.centerXAnchor constraintEqualToAnchor:iconView.centerXAnchor],
        [actionButton.centerYAnchor constraintEqualToAnchor:iconView.centerYAnchor],
        [actionButton.widthAnchor constraintEqualToConstant:16],
        [actionButton.heightAnchor constraintEqualToConstant:16],

        // torrentPriorityView
        [torrentPriorityView.heightAnchor constraintEqualToConstant:12],
        [torrentPriorityView.widthAnchor constraintEqualToConstant:12],

        // stackView
        [stackView.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:16],
        [stackView.trailingAnchor constraintLessThanOrEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [stackView.topAnchor constraintEqualToAnchor:self.topAnchor constant:3],
        [stackView.leadingAnchor constraintEqualToAnchor:torrentProgressField.leadingAnchor],

        // torrentProgressField
        [torrentProgressField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentProgressField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentProgressField.topAnchor constraintEqualToAnchor:stackView.bottomAnchor constant:1],

        // torrentStatusField
        [torrentStatusField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentStatusField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentStatusField.topAnchor constraintEqualToAnchor:torrentProgressBarView.bottomAnchor constant:1],
        [torrentStatusField.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-1],

        // torrentProgressBarView
        [torrentProgressBarView.topAnchor constraintEqualToAnchor:torrentProgressField.bottomAnchor constant:1],
        [torrentProgressBarView.heightAnchor constraintEqualToConstant:14],

        // controlButton
        [controlButton.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:14],
        [controlButton.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [controlButton.widthAnchor constraintEqualToConstant:14],
        [controlButton.heightAnchor constraintEqualToConstant:14],

        // revealButton
        [revealButton.leadingAnchor constraintEqualToAnchor:controlButton.trailingAnchor constant:3],
        [revealButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [revealButton.centerYAnchor constraintEqualToAnchor:controlButton.centerYAnchor],
        [revealButton.widthAnchor constraintEqualToConstant:14],
        [revealButton.heightAnchor constraintEqualToConstant:14],
    ]];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.fTorrentTableView) {
        Torrent* torrent = (Torrent*)self.objectValue;

        // draw progress bar
        NSRect barRect = self.fTorrentProgressBarView.frame;
        [ProgressBarView.sharedInstance drawBarInRect:barRect forTableView:self.fTorrentTableView withTorrent:torrent];

        // set priority icon
        if (torrent.priority != TR_PRI_NORMAL) {
            NSImage* priorityImage = [NSImage imageNamed:(torrent.priority == TR_PRI_HIGH ? @"PriorityHighTemplate" : @"PriorityLowTemplate")];

            self.fTorrentPriorityView.image = priorityImage;

            [self.fStackView setVisibilityPriority:NSStackViewVisibilityPriorityMustHold forView:self.fTorrentPriorityView];
        } else {
            [self.fStackView setVisibilityPriority:NSStackViewVisibilityPriorityNotVisible forView:self.fTorrentPriorityView];
        }
    }

    [super drawRect:dirtyRect];
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

@end
