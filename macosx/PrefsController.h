// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PortChecker.h"
#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface PrefsController : NSWindowController<NSToolbarDelegate, PortCheckerDelegate>

@property(nonatomic, readonly) NSArray<NSString*>* sounds;

/// - returns: number of minutes
+ (int)dateToTimeSum:(NSDate*)date;

/// Replaces any saved remote access password with an unguessable one.
/// - returns: whether the new password is saved
+ (BOOL)saveUnguessableRPCPassword;

- (instancetype)initWithHandle:(tr_session*)handle;

- (void)rpcUpdatePrefs;

- (void)updateBlocklistFields;

@end
