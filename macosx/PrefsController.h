// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

struct tr_session;

@interface PrefsController : NSWindowController<NSToolbarDelegate>

@property(nonatomic, readonly) NSArray<NSString*>* sounds;

/// - returns: number of minutes
+ (int)dateToTimeSum:(NSDate*)date;

- (instancetype)initWithHandle:(tr_session*)handle;

- (void)rpcUpdatePrefs;

@end
