// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/values.h>

#import "TorrentTableView.h"

@interface GroupCell : NSTableCellView

- (void)updateImage:(NSImage*)image;
- (void)updateTitle:(NSString*)title;
- (void)updateDownloadSpeed:(CGFloat)downloadSpeed
                uploadSpeed:(CGFloat)uploadSpeed
                      ratio:(tr::Values::Ratio)ratio
               displayRatio:(BOOL)displayRatio;
- (void)updateTooltipForTorrentsCount:(NSUInteger)count;

- (BOOL)isPointInStatusArea:(NSPoint)pointInCell;

@end
