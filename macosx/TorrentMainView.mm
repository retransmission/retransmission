// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentMainView.h"

@implementation TorrentMainView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        [self setupSubviews];
        [self setupLayout];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder*)coder
{
    self = [super initWithCoder:coder];
    if (self) {
        [self setupSubviews];
        [self setupLayout];
    }
    return self;
}

- (void)setupSubviews
{
    _scrollView = [[NSScrollView alloc] init];
    _scrollView.borderType = NSNoBorder;
    _scrollView.autohidesScrollers = YES;
    _scrollView.hasHorizontalScroller = NO;
    _scrollView.hasVerticalScroller = YES;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_scrollView];

    _tableView = [[TorrentTableView alloc] init];
    _tableView.intercellSpacing = NSMakeSize(2, 3);
    _tableView.allowsExpansionToolTips = YES;

    NSTableColumn* torrentColumn = [[NSTableColumn alloc] initWithIdentifier:@"Torrent"];
    torrentColumn.editable = NO;
    torrentColumn.minWidth = 16;
    torrentColumn.maxWidth = 3000;
    torrentColumn.resizingMask = NSTableColumnAutoresizingMask;
    [_tableView addTableColumn:torrentColumn];
    _tableView.outlineTableColumn = torrentColumn;

    _scrollView.documentView = _tableView;

    _bottomBar = [[NSView alloc] init];
    _bottomBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_bottomBar];

    _separator = [[NSBox alloc] init];
    _separator.boxType = NSBoxSeparator;
    _separator.translatesAutoresizingMaskIntoConstraints = NO;
    [_bottomBar addSubview:_separator];

    _totalTorrentsField = [NSTextField labelWithString:@"Torrents"];
    _totalTorrentsField.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    _totalTorrentsField.alignment = NSTextAlignmentCenter;
    _totalTorrentsField.textColor = [NSColor controlTextColor];
    _totalTorrentsField.translatesAutoresizingMaskIntoConstraints = NO;
    [_bottomBar addSubview:_totalTorrentsField];

    _globalActionsButton = [self createButtonWithImage:@"EllipsisTemplate" label:@"Global Actions"];
    [_bottomBar addSubview:_globalActionsButton];

    _speedLimitButton = [self createButtonWithImage:@"TortoiseTemplate" label:@"Speed Limit"];
    [_speedLimitButton setButtonType:NSButtonTypeToggle];
    [_bottomBar addSubview:_speedLimitButton];
    _clearCompletedButton = [self createButtonWithImage:@"CleanupTemplate" label:@"Clear Completed"];
    [_bottomBar addSubview:_clearCompletedButton];
}

- (NSButton*)createButtonWithImage:(NSString*)imageName label:(NSString*)label
{
    NSButton* button = [[NSButton alloc] init];
    [button setButtonType:NSButtonTypeMomentaryChange];
    button.bezelStyle = NSBezelStyleTexturedSquare;
    button.bordered = NO;
    button.image = [NSImage imageNamed:imageName];
    button.imagePosition = NSImageOnly;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.accessibilityLabel = label;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    return button;
}

- (void)setupLayout
{
    // Yields to the required 16pt inset Controller adds on macOS 26.
    NSLayoutConstraint* globalActionsLeading = [_globalActionsButton.leadingAnchor constraintEqualToAnchor:_bottomBar.leadingAnchor
                                                                                                  constant:8];
    globalActionsLeading.priority = NSLayoutPriorityDefaultHigh;

    [NSLayoutConstraint activateConstraints:@[
        [_scrollView.topAnchor constraintEqualToAnchor:self.safeAreaLayoutGuide.topAnchor],
        [_scrollView.leadingAnchor constraintEqualToAnchor:self.safeAreaLayoutGuide.leadingAnchor],
        [_scrollView.trailingAnchor constraintEqualToAnchor:self.safeAreaLayoutGuide.trailingAnchor],
        [_scrollView.bottomAnchor constraintEqualToAnchor:_bottomBar.topAnchor],
        [_bottomBar.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [_bottomBar.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [_bottomBar.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        [_bottomBar.heightAnchor constraintEqualToConstant:24],
        [_separator.topAnchor constraintEqualToAnchor:_bottomBar.topAnchor],
        [_separator.leadingAnchor constraintEqualToAnchor:_bottomBar.leadingAnchor],
        [_separator.trailingAnchor constraintEqualToAnchor:_bottomBar.trailingAnchor],

        globalActionsLeading,
        [_globalActionsButton.centerYAnchor constraintEqualToAnchor:_bottomBar.centerYAnchor],
        [_globalActionsButton.widthAnchor constraintEqualToConstant:18],
        [_globalActionsButton.heightAnchor constraintEqualToConstant:18],

        [_speedLimitButton.leadingAnchor constraintEqualToAnchor:_globalActionsButton.trailingAnchor constant:8],
        [_speedLimitButton.centerYAnchor constraintEqualToAnchor:_bottomBar.centerYAnchor],
        [_speedLimitButton.widthAnchor constraintEqualToConstant:18],
        [_speedLimitButton.heightAnchor constraintEqualToConstant:18],

        [_clearCompletedButton.leadingAnchor constraintEqualToAnchor:_speedLimitButton.trailingAnchor constant:8],
        [_clearCompletedButton.centerYAnchor constraintEqualToAnchor:_bottomBar.centerYAnchor],
        [_clearCompletedButton.widthAnchor constraintEqualToConstant:18],
        [_clearCompletedButton.heightAnchor constraintEqualToConstant:18],

        [_totalTorrentsField.centerXAnchor constraintEqualToAnchor:_bottomBar.centerXAnchor],
        [_totalTorrentsField.centerYAnchor constraintEqualToAnchor:_bottomBar.centerYAnchor]
    ]];
}

@end
