// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentTableView.h"

@interface GroupCell : NSTableCellView

@property(nonatomic) NSImageView* indicatorView;
@property(nonatomic) NSTextField* titleField;

- (void)setDownloadSpeed:(CGFloat)downloadSpeed uploadSpeed:(CGFloat)uploadSpeed ratio:(CGFloat)ratio;
- (void)setDisplayRatio:(BOOL)displayRatio;
- (void)setTooltipForTorrentsCount:(NSUInteger)count;

@end
