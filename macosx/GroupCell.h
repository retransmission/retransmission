// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentTableView.h"

@interface GroupCell : NSTableCellView

- (void)updateImage:(NSImage*)image;
- (void)updateTitle:(NSString*)title;
- (void)updateDownloadSpeed:(CGFloat)downloadSpeed uploadSpeed:(CGFloat)uploadSpeed ratio:(CGFloat)ratio;
- (void)updateDisplayRatio:(BOOL)displayRatio;
- (void)updateTooltipForTorrentsCount:(NSUInteger)count;

@property(nonatomic, readonly) CGRect frameForTitle;

@end
