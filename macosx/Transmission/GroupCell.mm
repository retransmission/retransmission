// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupCell.h"

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
    self.fGroupIndicatorView = indicatorView;

    auto titleField = [[NSTextField alloc] init];
    titleField.textColor = NSColor.secondaryLabelColor;
    titleField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    self.fGroupTitleField = titleField;
    titleField.allowsExpansionToolTips = YES;

    auto downloadView = [[NSImageView alloc] init];
    downloadView.imageScaling = NSImageScaleProportionallyDown;
    self.fGroupDownloadView = downloadView;

    auto downloadField = [[NSTextField alloc] init];
    downloadField.textColor = NSColor.secondaryLabelColor;
    downloadField.lineBreakMode = NSLineBreakByClipping;
    self.fGroupDownloadField = downloadField;

    auto uploadAndRatioView = [[NSImageView alloc] init];
    uploadAndRatioView.imageScaling = NSImageScaleProportionallyDown;
    self.fGroupUploadAndRatioView = uploadAndRatioView;

    auto uploadAndRatioField = [[NSTextField alloc] init];
    uploadAndRatioField.textColor = NSColor.secondaryLabelColor;
    uploadAndRatioField.lineBreakMode = NSLineBreakByClipping;
    self.fGroupUploadAndRatioField = uploadAndRatioField;

    for (NSTextField* view in @[ titleField, downloadField, uploadAndRatioField ]) {
        view.editable = NO;
        view.selectable = NO;
        view.bordered = NO;
        view.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
        view.drawsBackground = NO;
    }

    for (NSView* view in @[ indicatorView, titleField, downloadView, downloadField, uploadAndRatioView, uploadAndRatioField ]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:view];
    }
}

- (void)setupConstraints
{
    [NSLayoutConstraint activateConstraints:@[
        // IndicatorView
        [self.fGroupIndicatorView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:11],
        [self.fGroupIndicatorView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fGroupIndicatorView.widthAnchor constraintEqualToConstant:14],
        [self.fGroupIndicatorView.heightAnchor constraintEqualToConstant:14],

        // TitleField
        [self.fGroupTitleField.leadingAnchor constraintEqualToAnchor:self.fGroupIndicatorView.trailingAnchor constant:5],
        [self.fGroupTitleField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

        // DownloadView
        [self.fGroupTitleField.trailingAnchor constraintEqualToAnchor:self.fGroupDownloadView.leadingAnchor],
        [self.fGroupDownloadView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fGroupDownloadView.widthAnchor constraintEqualToConstant:16],
        [self.fGroupDownloadView.heightAnchor constraintEqualToConstant:16],

        // DownloadField
        [self.fGroupDownloadView.trailingAnchor constraintEqualToAnchor:self.fGroupDownloadField.leadingAnchor],
        [self.fGroupDownloadField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fGroupDownloadField.widthAnchor constraintGreaterThanOrEqualToConstant:60],

        // UploadAndRatioView
        [self.fGroupUploadAndRatioView.leadingAnchor constraintEqualToAnchor:self.fGroupDownloadField.trailingAnchor constant:8],
        [self.fGroupUploadAndRatioView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fGroupUploadAndRatioView.widthAnchor constraintEqualToConstant:16],
        [self.fGroupUploadAndRatioView.heightAnchor constraintEqualToConstant:16],

        // UploadAndRatioField
        [self.fGroupUploadAndRatioField.leadingAnchor constraintEqualToAnchor:self.fGroupUploadAndRatioView.trailingAnchor],
        [self.fGroupUploadAndRatioField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-5],
        [self.fGroupUploadAndRatioField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.fGroupUploadAndRatioField.widthAnchor constraintGreaterThanOrEqualToConstant:60],
    ]];

    [self.fGroupTitleField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                                    forOrientation:NSLayoutConstraintOrientationHorizontal];

    [self.fGroupDownloadField setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1
                                         forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.fGroupUploadAndRatioField setContentHuggingPriority:NSLayoutPriorityDefaultLow + 1
                                               forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    auto isEmphasized = backgroundStyle == NSBackgroundStyleEmphasized;
    self.fGroupTitleField.textColor = isEmphasized ? NSColor.labelColor : NSColor.secondaryLabelColor;
}

@end
