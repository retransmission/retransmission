// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PeersViews.h"
#import "NSStringAdditions.h"

@interface PeerProgressIndicatorView ()
@property(nonatomic, strong) NSTextField* textProgressView;
@property(nonatomic, strong) NSLevelIndicator* levelIndicator;
@property(nonatomic, strong) NSImageView* checkImageView;
@end

@implementation PeerProgressIndicatorView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        _textProgressView = [[NSTextField alloc] initWithFrame:frameRect];
        _textProgressView.editable = NO;
        _textProgressView.selectable = NO;
        _textProgressView.bordered = NO;
        _textProgressView.drawsBackground = NO;
        _textProgressView.alignment = NSTextAlignmentRight;
        _textProgressView.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];

        _levelIndicator = [[NSLevelIndicator alloc] initWithFrame:frameRect];
        _levelIndicator.levelIndicatorStyle = NSLevelIndicatorStyleContinuousCapacity;
        _levelIndicator.editable = NO;
        _levelIndicator.criticalValue = 0.3F;
        _levelIndicator.warningValue = 0.7F;
        _levelIndicator.maxValue = 1.0F;

        _checkImageView = [[NSImageView alloc] initWithFrame:frameRect];
        _checkImageView.imageScaling = NSImageScaleProportionallyDown;
        _checkImageView.imageAlignment = NSImageAlignCenter;
        _checkImageView.image = [NSImage imageNamed:@"CompleteCheck"];

        [self addSubview:_textProgressView];
        [self addSubview:_levelIndicator];
        [self addSubview:_checkImageView];

        _textProgressView.translatesAutoresizingMaskIntoConstraints = NO;
        _levelIndicator.translatesAutoresizingMaskIntoConstraints = NO;
        _checkImageView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [_textProgressView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_textProgressView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_textProgressView.topAnchor constraintEqualToAnchor:self.topAnchor],
            [_textProgressView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],

            [_levelIndicator.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:2],
            [_levelIndicator.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-2],
            [_levelIndicator.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_levelIndicator.heightAnchor constraintEqualToAnchor:self.heightAnchor],

            [_checkImageView.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [_checkImageView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_checkImageView.widthAnchor constraintEqualToAnchor:self.heightAnchor],
            [_checkImageView.heightAnchor constraintEqualToAnchor:self.heightAnchor],
        ]];
    }
    return self;
}

- (void)updateProgress:(float)progressValue isSeed:(BOOL)isSeed showText:(BOOL)showText
{
    if (showText) {
        self.textProgressView.stringValue = [NSString percentString:progressValue longDecimals:NO];
    } else {
        self.levelIndicator.floatValue = progressValue;
    }

    self.textProgressView.hidden = showText == NO;
    self.levelIndicator.hidden = showText;
    self.checkImageView.hidden = showText || !isSeed;
}

@end

@implementation PeerTextView
- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        NSTextField* textFieldView = [[NSTextField alloc] initWithFrame:frameRect];
        textFieldView.editable = NO;
        textFieldView.selectable = NO;
        textFieldView.bordered = NO;
        textFieldView.drawsBackground = NO;
        textFieldView.alignment = NSTextAlignmentLeft;
        textFieldView.lineBreakMode = NSLineBreakByTruncatingTail;
        textFieldView.allowsExpansionToolTips = YES;
        textFieldView.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
        [self addSubview:textFieldView];
        self.textField = textFieldView;

        textFieldView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [textFieldView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [textFieldView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [textFieldView.topAnchor constraintEqualToAnchor:self.topAnchor],
            [textFieldView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}

- (void)updateText:(NSString*)text
{
    self.textField.stringValue = text;
}

@end

@interface PeerEncryptionView ()
@property(nonatomic, strong) NSImageView* iconView;
@end

@implementation PeerEncryptionView
- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect]) {
        _iconView = [[NSImageView alloc] initWithFrame:frameRect];
        _iconView.imageScaling = NSImageScaleProportionallyDown;
        _iconView.imageAlignment = NSImageAlignCenter;
        _iconView.image = [NSImage imageWithSystemSymbolName:@"lock.fill" accessibilityDescription:nil];

        [self addSubview:_iconView];

        _iconView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [_iconView.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [_iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_iconView.widthAnchor constraintEqualToAnchor:self.heightAnchor],
            [_iconView.heightAnchor constraintEqualToAnchor:self.heightAnchor],
        ]];
    }
    return self;
}

- (void)updateEncrypted:(BOOL)encrypted
{
    self.iconView.hidden = encrypted == NO;
}
@end
