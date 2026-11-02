// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

struct tr_session;

@interface PrefsRemoteTabViewController : NSViewController

- (instancetype)initWithHandle:(tr_session*)handle;

/// Replaces any saved remote access password with an unguessable one.
/// - returns: whether the new password is saved
+ (BOOL)saveUnguessableRPCPassword;

@end
