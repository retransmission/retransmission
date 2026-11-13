// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

struct tr_session;

@interface PrefsNetworkTabViewController : NSViewController

- (instancetype)initWithHandle:(tr_session*)handle;

/// Saves the session's bind interface, as an RPC client may have changed it, and refreshes the tab.
- (void)updateBindInterfaceFromSession;

@end
