// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/macros.h>
#include <libtransmission/version.h>
#import "AboutWindowController.h"

@interface AboutWindowController ()<NSWindowDelegate>
@property(nonatomic) NSTextField* fVersionField;
@property(nonatomic) NSTextField* fCopyrightField;
@end

@implementation AboutWindowController
static AboutWindowController* fAboutBoxInstance = nil;

+ (AboutWindowController*)aboutController
{
    if (!fAboutBoxInstance) {
        fAboutBoxInstance = [[self alloc] initWithWindow:nil];
    }
    return fAboutBoxInstance;
}

- (instancetype)initWithWindow:(NSWindow*)window
{
    self = [super initWithWindow:window];
    if (self) {
        [self setupMainWindow];
        [self configureContent];
        [self.window setContentSize:self.window.contentView.fittingSize];
        [self.window center];
    }
    return self;
}

- (void)setupMainWindow
{
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSZeroRect styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.restorable = NO;
    panel.releasedWhenClosed = NO;
    panel.hidesOnDeactivate = NO;
    panel.tabbingMode = NSWindowTabbingModeDisallowed;
    panel.delegate = self;

    NSView* contentView = panel.contentView;

    NSImageView* iconView = [NSImageView imageViewWithImage:[NSImage imageNamed:NSImageNameApplicationIcon]];
    iconView.imageScaling = NSImageScaleAxesIndependently;
    iconView.animates = YES;
    iconView.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:iconView];

    NSTextField* titleField = [NSTextField labelWithString:@TR_PROJ_APPNAME_CAPITALIZED];
    titleField.font = [NSFont systemFontOfSize:24 weight:NSFontWeightBold];
    titleField.selectable = YES;
    titleField.focusRingType = NSFocusRingTypeNone;
    titleField.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:titleField];

    self.fVersionField = [NSTextField labelWithString:@""];
    self.fVersionField.font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    self.fVersionField.alignment = NSTextAlignmentCenter;
    self.fVersionField.selectable = YES;
    self.fVersionField.focusRingType = NSFocusRingTypeNone;
    self.fVersionField.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:self.fVersionField];

    NSAttributedString* creditsLink = [[NSAttributedString alloc] initWithString:@TR_PROJ_URL_CREDITS attributes:@{
        NSLinkAttributeName : [NSURL URLWithString:@TR_PROJ_URL_CREDITS],
        NSFontAttributeName : [NSFont systemFontOfSize:[NSFont systemFontSize]]
    }];
    NSTextField* creditsField = [NSTextField labelWithAttributedString:creditsLink];
    // NSTextField renders and follows links only with both of these set
    creditsField.selectable = YES;
    creditsField.allowsEditingTextAttributes = YES;
    creditsField.focusRingType = NSFocusRingTypeNone;
    creditsField.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:creditsField];

    self.fCopyrightField = [NSTextField labelWithString:@""];
    self.fCopyrightField.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    self.fCopyrightField.selectable = YES;
    self.fCopyrightField.focusRingType = NSFocusRingTypeNone;
    self.fCopyrightField.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:self.fCopyrightField];

    [NSLayoutConstraint activateConstraints:@[
        [iconView.widthAnchor constraintEqualToConstant:64],
        [iconView.heightAnchor constraintEqualToConstant:64],
        [iconView.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:12],
        [iconView.leadingAnchor constraintGreaterThanOrEqualToAnchor:contentView.leadingAnchor constant:20],

        [titleField.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],
        [titleField.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [titleField.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:8],

        [self.fVersionField.topAnchor constraintEqualToAnchor:titleField.bottomAnchor constant:8],
        [self.fVersionField.centerXAnchor constraintEqualToAnchor:titleField.centerXAnchor],

        [creditsField.topAnchor constraintGreaterThanOrEqualToAnchor:self.fVersionField.bottomAnchor constant:12],
        [creditsField.topAnchor constraintGreaterThanOrEqualToAnchor:iconView.bottomAnchor constant:8],
        [creditsField.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [creditsField.leadingAnchor constraintGreaterThanOrEqualToAnchor:contentView.leadingAnchor constant:20],

        [self.fCopyrightField.topAnchor constraintEqualToAnchor:creditsField.bottomAnchor constant:20],
        [self.fCopyrightField.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [self.fCopyrightField.leadingAnchor constraintGreaterThanOrEqualToAnchor:contentView.leadingAnchor constant:20],
        [self.fCopyrightField.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20]
    ]];

    self.window = panel;
}

- (void)configureContent
{
    self.fVersionField.stringValue = @(LONG_VERSION_STRING);
    self.fCopyrightField.stringValue = [NSBundle.mainBundle localizedStringForKey:@"NSHumanReadableCopyright" value:nil
                                                                            table:@"InfoPlist"];
}

- (void)windowWillClose:(NSNotification*)notification
{
    fAboutBoxInstance = nil;
}

@end
