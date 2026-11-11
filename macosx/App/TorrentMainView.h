// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentTableView.h"

@interface TorrentMainView : NSView

@property(nonatomic, strong, readonly) NSScrollView* scrollView;
@property(nonatomic, strong, readonly) TorrentTableView* tableView;
@property(nonatomic, strong, readonly) NSView* bottomBar;
@property(nonatomic, strong, readonly) NSTextField* totalTorrentsField;

@property(nonatomic, strong, readonly) NSButton* globalActionsButton;
@property(nonatomic, strong, readonly) NSButton* speedLimitButton;
@property(nonatomic, strong, readonly) NSButton* clearCompletedButton;
@property(nonatomic, strong, readonly) NSBox* separator;

@end
