// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentMainView.h"

@interface TorrentMainWindow : NSWindow

@property(nonatomic, strong, readonly) TorrentMainView* mainView;

- (instancetype)initWindowWithContentView:(TorrentMainView*)contentView;

@end
