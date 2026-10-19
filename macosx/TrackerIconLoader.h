// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface TrackerIconLoader : NSObject

+ (instancetype)sharedInstance;
- (void)fetchIconForAddress:(NSString*)addressString completion:(void (^)(NSImage* image))completion;

@end
