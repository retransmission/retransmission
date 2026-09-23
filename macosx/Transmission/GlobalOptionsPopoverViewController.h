// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface GlobalOptionsPopoverViewController : NSViewController

- (instancetype)initWithHandle:(tr_session*)handle;

@end
