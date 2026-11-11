// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface MessageLogLevelCellView : NSTableCellView

- (void)setImage:(NSImage*)image;

@end

@interface MessageLogTextCellView : NSTableCellView

- (void)configureForMultiline:(BOOL)isMultiline;
- (void)setText:(NSString*)text;

@end

NS_ASSUME_NONNULL_END
