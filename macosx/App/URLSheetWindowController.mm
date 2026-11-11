// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "URLSheetWindowController.h"

@interface URLSheetWindowController ()<NSTextFieldDelegate>

@property(nonatomic, strong) NSTextField* fLabelField;
@property(nonatomic, strong) NSTextField* fTextField;
@property(nonatomic, strong) NSButton* fOpenButton;
@property(nonatomic, strong) NSButton* fCancelButton;

@end

@implementation URLSheetWindowController

- (instancetype)init
{
    self = [super initWithWindow:nil];
    if (self) {
        [self setupWindowAndLayout];
    }
    return self;
}

- (void)setupWindowAndLayout
{
    NSRect windowFrame = NSMakeRect(0, 0, 400, 177);
    NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    NSWindow* window = [[NSWindow alloc] initWithContentRect:windowFrame styleMask:styleMask backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.title = @"Window";
    window.allowsToolTipsWhenApplicationIsInactive = NO;
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(400, 177);
    window.frameAutosaveName = @"URLSheetWindow";
    self.window = window;

    NSView* contentView = window.contentView;

    self.fLabelField = [NSTextField labelWithString:NSLocalizedString(@"URL:", "URL sheet label")];
    self.fLabelField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fLabelField.cell.lineBreakMode = NSLineBreakByTruncatingTail;
    [contentView addSubview:self.fLabelField];

    self.fTextField = [[NSTextField alloc] init];
    self.fTextField.translatesAutoresizingMaskIntoConstraints = NO;
    self.fTextField.lineBreakMode = NSLineBreakByCharWrapping;
    self.fTextField.delegate = self;
    [self.fTextField setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
    [contentView addSubview:self.fTextField];

    self.fCancelButton = [NSButton buttonWithTitle:NSLocalizedString(@"Cancel", "URL sheet button") target:self
                                            action:@selector(openURLCancelEndSheet:)];
    self.fCancelButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fCancelButton.bezelStyle = NSBezelStyleRounded;
    self.fCancelButton.keyEquivalent = @"\e"; // Escape
    [contentView addSubview:self.fCancelButton];

    self.fOpenButton = [NSButton buttonWithTitle:NSLocalizedString(@"Open", "URL sheet button") target:self
                                          action:@selector(openURLEndSheet:)];
    self.fOpenButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.fOpenButton.bezelStyle = NSBezelStyleRounded;
    self.fOpenButton.enabled = NO;
    self.fOpenButton.keyEquivalent = @"\r"; // Enter
    [contentView addSubview:self.fOpenButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.fLabelField.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],
        [self.fLabelField.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [self.fLabelField.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],

        [self.fTextField.topAnchor constraintEqualToAnchor:self.fLabelField.bottomAnchor constant:10],
        [self.fTextField.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [self.fTextField.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [self.fTextField.bottomAnchor constraintEqualToAnchor:self.fOpenButton.topAnchor constant:-20],

        [self.fOpenButton.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [self.fOpenButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],
        [self.fOpenButton.widthAnchor constraintGreaterThanOrEqualToConstant:82],

        [self.fCancelButton.trailingAnchor constraintEqualToAnchor:self.fOpenButton.leadingAnchor constant:-12],
        [self.fCancelButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],

        [self.fCancelButton.widthAnchor constraintEqualToAnchor:self.fOpenButton.widthAnchor]
    ]];
}

- (void)openURLEndSheet:(id)sender
{
    [NSApp endSheet:self.window returnCode:1];
}

- (void)openURLCancelEndSheet:(id)sender
{
    [NSApp endSheet:self.window returnCode:0];
}

- (NSString*)urlString
{
    return self.fTextField.stringValue;
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    [self updateOpenButtonForURL:self.fTextField.stringValue];
}

#pragma mark - Private

- (void)updateOpenButtonForURL:(NSString*)string
{
    BOOL enable = YES;
    if ([string isEqualToString:@""]) {
        enable = NO;
    } else {
        NSRange prefixRange = [string rangeOfString:@"://"];
        if (prefixRange.location != NSNotFound && string.length == NSMaxRange(prefixRange)) {
            enable = NO;
        }
    }

    self.fOpenButton.enabled = enable;
}

@end
