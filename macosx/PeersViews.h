// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface PeerProgressIndicatorView : NSTableCellView
- (void)updateProgress:(float)progressValue isSeed:(BOOL)isSeed showText:(BOOL)showText;
@end

@interface PeerTextView : NSTableCellView
- (void)updateText:(NSString*)text;
@end

@interface PeerEncryptionView : NSTableCellView
- (void)updateEncrypted:(BOOL)encrypted;
@end
