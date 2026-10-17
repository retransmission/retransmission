// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MessageLogsViews.h"

@implementation MessageLogLevelCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        NSImageView* imageView = [[NSImageView alloc] initWithFrame:NSZeroRect];
        imageView.translatesAutoresizingMaskIntoConstraints = NO;
        imageView.imageScaling = NSImageScaleProportionallyDown;
        imageView.imageAlignment = NSImageAlignTop;
        [self addSubview:imageView];
        self.imageView = imageView;

        [NSLayoutConstraint activateConstraints:@[
            [imageView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [imageView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [imageView.topAnchor constraintEqualToAnchor:self.topAnchor constant:1],
            [imageView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor]
        ]];
    }
    return self;
}

- (void)setImage:(NSImage*)image
{
    self.imageView.image = image;
}

@end

@implementation MessageLogTextCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        NSTextField* textField = [NSTextField labelWithString:@""];
        textField.translatesAutoresizingMaskIntoConstraints = NO;
        textField.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
        textField.allowsExpansionToolTips = YES;
        [self addSubview:textField];
        self.textField = textField;

        [NSLayoutConstraint activateConstraints:@[
            [textField.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:2],
            [textField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-2],
            [textField.topAnchor constraintEqualToAnchor:self.topAnchor constant:2],
            [textField.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-2]
        ]];

        [textField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                            forOrientation:NSLayoutConstraintOrientationHorizontal];
        [textField setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    }
    return self;
}

- (void)layout
{
    if (self.textField.usesSingleLineMode == NO) {
        CGFloat const width = MAX(NSWidth(self.bounds) - 4, 0);
        if (self.textField.preferredMaxLayoutWidth != width) {
            self.textField.preferredMaxLayoutWidth = width;
        }
    }

    [super layout];
}

- (void)configureForMultiline:(BOOL)isMultiline
{
    if (isMultiline) {
        self.textField.usesSingleLineMode = NO;
        self.textField.cell.wraps = YES;
        self.textField.lineBreakMode = NSLineBreakByWordWrapping;
    } else {
        self.textField.usesSingleLineMode = YES;
        self.textField.lineBreakMode = NSLineBreakByTruncatingTail;
    }
}

- (void)setText:(NSString*)text
{
    self.textField.stringValue = text ?: @"";
}

@end
