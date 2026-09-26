// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/version.h>
#import "AboutWindowController.h"

@interface AboutWindowController ()<NSWindowDelegate>
@property(nonatomic) NSTextView* fTextView;
@property(nonatomic) NSTextField* fVersionField;
@property(nonatomic) NSTextField* fCopyrightField;
@property(nonatomic) NSButton* fLicenseButton;

@property(nonatomic) NSPanel* fLicenseSheet;
@property(nonatomic) NSTextView* fLicenseView;
@property(nonatomic) NSButton* fLicenseCloseButton;
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
        [self setupLicenseWindow];
        [self configureContent];
    }
    return self;
}

- (void)setupMainWindow
{
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 538, 337)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
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

    NSTextField* titleField = [NSTextField labelWithString:@"Transmission"];
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

    NSScrollView* creditsScrollView = [[NSScrollView alloc] init];
    creditsScrollView.hasHorizontalScroller = NO;
    creditsScrollView.hasVerticalScroller = YES;
    creditsScrollView.drawsBackground = NO;
    creditsScrollView.borderType = NSBezelBorder;
    creditsScrollView.translatesAutoresizingMaskIntoConstraints = NO;

    self.fTextView = [[NSTextView alloc] init];
    self.fTextView.editable = NO;
    self.fTextView.selectable = YES;
    self.fTextView.textColor = [NSColor textColor];
    self.fTextView.backgroundColor = [NSColor textBackgroundColor];

    creditsScrollView.documentView = self.fTextView;
    [contentView addSubview:creditsScrollView];

    self.fCopyrightField = [NSTextField labelWithString:@""];
    self.fCopyrightField.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    self.fCopyrightField.selectable = YES;
    self.fCopyrightField.focusRingType = NSFocusRingTypeNone;
    self.fCopyrightField.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:self.fCopyrightField];

    self.fLicenseButton = [NSButton buttonWithTitle:NSLocalizedString(@"License", "About window -> license button") target:self
                                             action:@selector(showLicense:)];
    self.fLicenseButton.bezelStyle = NSBezelStyleRounded;
    self.fLicenseButton.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:self.fLicenseButton];

    [NSLayoutConstraint activateConstraints:@[
        [iconView.widthAnchor constraintEqualToConstant:64],
        [iconView.heightAnchor constraintEqualToConstant:64],
        [iconView.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:12],

        [titleField.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],
        [titleField.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [titleField.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:2],

        [self.fVersionField.topAnchor constraintEqualToAnchor:titleField.bottomAnchor constant:8],
        [self.fVersionField.centerXAnchor constraintEqualToAnchor:titleField.centerXAnchor],

        [creditsScrollView.topAnchor constraintGreaterThanOrEqualToAnchor:self.fVersionField.bottomAnchor constant:12],
        [creditsScrollView.topAnchor constraintGreaterThanOrEqualToAnchor:iconView.bottomAnchor constant:8],
        [creditsScrollView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:-2],
        [creditsScrollView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:2],
        [creditsScrollView.heightAnchor constraintEqualToConstant:190],

        [self.fLicenseButton.topAnchor constraintEqualToAnchor:creditsScrollView.bottomAnchor constant:20],
        [self.fLicenseButton.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [self.fLicenseButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],

        [self.fCopyrightField.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [self.fCopyrightField.firstBaselineAnchor constraintEqualToAnchor:self.fLicenseButton.firstBaselineAnchor],
        [self.fLicenseButton.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.fCopyrightField.trailingAnchor constant:8]
    ]];

    [panel center];

    self.window = panel;
}

- (void)setupLicenseWindow
{
    self.fLicenseSheet = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 530, 331) styleMask:NSWindowStyleMaskTitled
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    NSView* sheetContentView = self.fLicenseSheet.contentView;

    NSScrollView* licenseScrollView = [[NSScrollView alloc] init];
    licenseScrollView.hasHorizontalScroller = NO;
    licenseScrollView.hasVerticalScroller = YES;
    licenseScrollView.drawsBackground = NO;
    licenseScrollView.borderType = NSBezelBorder;
    licenseScrollView.translatesAutoresizingMaskIntoConstraints = NO;

    self.fLicenseView = [[NSTextView alloc] init];
    self.fLicenseView.editable = NO;
    self.fLicenseView.selectable = YES;
    self.fLicenseView.textColor = [NSColor textColor];
    self.fLicenseView.backgroundColor = [NSColor textBackgroundColor];

    licenseScrollView.documentView = self.fLicenseView;
    [sheetContentView addSubview:licenseScrollView];

    self.fLicenseCloseButton = [NSButton buttonWithTitle:@"OK" target:self action:@selector(hideLicense:)];
    self.fLicenseCloseButton.bezelStyle = NSBezelStyleRounded;
    self.fLicenseCloseButton.keyEquivalent = @"\r";
    self.fLicenseCloseButton.translatesAutoresizingMaskIntoConstraints = NO;
    [sheetContentView addSubview:self.fLicenseCloseButton];

    [NSLayoutConstraint activateConstraints:@[
        [licenseScrollView.topAnchor constraintEqualToAnchor:sheetContentView.topAnchor constant:20],
        [licenseScrollView.leadingAnchor constraintEqualToAnchor:sheetContentView.leadingAnchor constant:20],
        [licenseScrollView.trailingAnchor constraintEqualToAnchor:sheetContentView.trailingAnchor constant:-20],

        [self.fLicenseCloseButton.topAnchor constraintEqualToAnchor:licenseScrollView.bottomAnchor constant:20],
        [self.fLicenseCloseButton.trailingAnchor constraintEqualToAnchor:sheetContentView.trailingAnchor constant:-20],
        [self.fLicenseCloseButton.bottomAnchor constraintEqualToAnchor:sheetContentView.bottomAnchor constant:-20],
        [self.fLicenseCloseButton.widthAnchor constraintGreaterThanOrEqualToConstant:80]
    ]];
}

- (void)configureContent
{
    self.fVersionField.stringValue = @(LONG_VERSION_STRING);
    self.fCopyrightField.stringValue = [NSBundle.mainBundle localizedStringForKey:@"NSHumanReadableCopyright" value:nil
                                                                            table:@"InfoPlist"];

    NSAttributedString* credits = [[NSAttributedString alloc]
               initWithURL:[NSBundle.mainBundle URLForResource:@"Credits" withExtension:@"rtf"]
                   options:@{ NSDocumentTypeDocumentAttribute : NSRTFTextDocumentType }
        documentAttributes:nil
                     error:nil];
    if (credits) {
        [self.fTextView.textStorage setAttributedString:credits];
    }
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    [self.window center];
}
- (void)windowWillClose:(NSNotification*)notification
{
    fAboutBoxInstance = nil;
}

- (void)showLicense:(id)sender
{
    NSString* licenseText = [NSString stringWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"COPYING" ofType:nil]
                                                  usedEncoding:nil
                                                         error:NULL];
    if (licenseText) {
        self.fLicenseView.string = licenseText;
    }
    self.fLicenseCloseButton.title = NSLocalizedString(@"OK", "About window -> license close button");
    if (self.fLicenseSheet) {
        [self.window beginSheet:self.fLicenseSheet completionHandler:nil];
    }
}

- (void)hideLicense:(id)sender
{
    [self.window endSheet:self.fLicenseSheet];
}

@end
