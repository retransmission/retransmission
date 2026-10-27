// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
@class Controller;

NS_ASSUME_NONNULL_BEGIN

@interface TorrentContextMenuProvider : NSObject

+ (BOOL)isGroupsSubmenu:(NSMenu*)menu;
+ (BOOL)isShareSubmenu:(NSMenu*)menu;

+ (NSMenu*)makeRowContextMenuWithTarget:(Controller*)target;
+ (NSMenu*)makeNoRowContextMenuWithTarget:(Controller*)target;

@end

NS_ASSUME_NONNULL_END
