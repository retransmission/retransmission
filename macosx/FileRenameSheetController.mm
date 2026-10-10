// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.
// Created by Mitchell Livingston on 1/20/13.

#import "FileRenameSheetController.h"
#import "FileListNode.h"
#import "Torrent.h"

@interface FileRenameSheetController ()<NSWindowDelegate, NSTextFieldDelegate>

@property(nonatomic, strong) NSTextField* labelField;
@property(nonatomic, strong) NSTextField* inputField;
@property(nonatomic, strong) NSButton* renameButton;
@property(nonatomic, strong) NSButton* cancelButton;

@property(nonatomic) Torrent* torrent;
@property(nonatomic) FileListNode* node;

@property(nonatomic, copy) NSString* originalName;

@end

@implementation FileRenameSheetController

+ (void)presentSheetForTorrent:(Torrent*)torrent
                modalForWindow:(NSWindow*)window
             completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(torrent != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController* renamer = [[FileRenameSheetController alloc] initWithTorrent:torrent node:nil];

    [self presentSheetForRenamer:renamer modalForWindow:window completionHandler:completionHandler];
}

+ (void)presentSheetForFileListNode:(FileListNode*)node
                     modalForWindow:(NSWindow*)window
                  completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(node != nil);
    NSParameterAssert(window != nil);

    FileRenameSheetController* renamer = [[FileRenameSheetController alloc] initWithTorrent:node.torrent node:node];

    [self presentSheetForRenamer:renamer modalForWindow:window completionHandler:completionHandler];
}

+ (void)presentSheetForRenamer:(FileRenameSheetController*)renamer
                modalForWindow:(NSWindow*)window
             completionHandler:(void (^)(BOOL))completionHandler
{
    __block FileRenameSheetController* strongRenamer = renamer;
    [window beginSheet:renamer.window completionHandler:^(NSModalResponse returnCode) {
        completionHandler(returnCode == NSModalResponseOK);
        strongRenamer = nil;
    }];
}

- (instancetype)initWithTorrent:(Torrent*)torrent node:(FileListNode*)node
{
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 400, 120)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"Window";
    window.minSize = NSMakeSize(300, 120);
    window.maxSize = NSMakeSize(9999, 120);
    window.frameAutosaveName = @"RenameFileWindow";

    self = [super initWithWindow:window];
    if (self) {
        _torrent = torrent;
        _node = node;
        [self setupMainWindow];
        [self configureContent];
    }
    return self;
}

- (void)setupMainWindow
{
    self.window.delegate = self;
    NSView* contentView = self.window.contentView;

    _labelField = [NSTextField labelWithString:@""];
    _labelField.translatesAutoresizingMaskIntoConstraints = NO;
    _labelField.cell.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [_labelField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                          forOrientation:NSLayoutConstraintOrientationHorizontal];
    [contentView addSubview:_labelField];

    _inputField = [NSTextField textFieldWithString:@""];
    _inputField.translatesAutoresizingMaskIntoConstraints = NO;
    _inputField.delegate = self;
    [contentView addSubview:_inputField];

    _cancelButton = [NSButton buttonWithTitle:NSLocalizedString(@"Cancel", "rename sheet button") target:self
                                       action:@selector(cancelRename:)];
    _cancelButton.bezelStyle = NSBezelStyleRounded;
    _cancelButton.keyEquivalent = @"\e";
    _cancelButton.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_cancelButton];

    _renameButton = [NSButton buttonWithTitle:NSLocalizedString(@"Rename", "rename sheet button") target:self
                                       action:@selector(rename:)];
    _renameButton.bezelStyle = NSBezelStyleRounded;
    _renameButton.keyEquivalent = @"\r";
    _renameButton.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_renameButton];

    [NSLayoutConstraint activateConstraints:@[
        [_labelField.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20],
        [_labelField.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [_labelField.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],

        [_inputField.topAnchor constraintEqualToAnchor:_labelField.bottomAnchor constant:12],
        [_inputField.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20],
        [_inputField.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],

        [_renameButton.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],
        [_renameButton.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20],
        [_renameButton.topAnchor constraintEqualToAnchor:_inputField.bottomAnchor constant:16],

        [_cancelButton.trailingAnchor constraintEqualToAnchor:_renameButton.leadingAnchor constant:-12],
        [_cancelButton.firstBaselineAnchor constraintEqualToAnchor:_renameButton.firstBaselineAnchor],

        [_cancelButton.widthAnchor constraintEqualToAnchor:_renameButton.widthAnchor],
        [_renameButton.widthAnchor constraintGreaterThanOrEqualToConstant:90]
    ]];
}

- (void)configureContent
{
    self.originalName = self.node.name ?: self.torrent.name;
    NSString* label = [NSString stringWithFormat:NSLocalizedString(@"Rename the file \"%@\":", "rename sheet label"), self.originalName];
    self.labelField.stringValue = label;

    self.inputField.stringValue = self.originalName;
    self.renameButton.enabled = NO;

    [self.window setInitialFirstResponder:self.inputField];
}

- (void)rename:(id)sender
{
    void (^completionHandler)(BOOL) = ^(BOOL didRename) {
        if (didRename) {
            [NSApp endSheet:self.window returnCode:NSModalResponseOK];
        } else {
#warning more thorough error
            NSBeep();
        }
    };

    if (self.node) {
        [self.torrent renameFileNode:self.node withName:self.inputField.stringValue completionHandler:completionHandler];
    } else {
        [self.torrent renameTorrent:self.inputField.stringValue completionHandler:completionHandler];
    }
}

- (void)cancelRename:(id)sender
{
    [NSApp endSheet:self.window returnCode:NSModalResponseCancel];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    self.renameButton.enabled = ![self.inputField.stringValue isEqualToString:@""] &&
        ![self.inputField.stringValue isEqualToString:self.originalName];
}

@end
