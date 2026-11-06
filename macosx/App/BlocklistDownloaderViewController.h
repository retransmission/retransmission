// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

struct tr_session;

@interface BlocklistDownloaderViewController : NSObject

- (instancetype)initWithSessionHandle:(struct tr_session*)sessionHandle;

- (void)showAsSheetModalForWindow:(NSWindow*)parentWindow completionHandler:(void (^)(NSString* _Nullable error))handler;

@end

NS_ASSUME_NONNULL_END
