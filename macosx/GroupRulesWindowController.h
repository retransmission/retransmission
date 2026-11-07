// GroupRulesWindowController.h
// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface GroupRulesWindowController : NSWindowController

- (instancetype)initWithPredicate:(nullable NSPredicate*)predicate
                completionHandler:(void (^)(BOOL saved, NSPredicate* _Nullable finalPredicate))completionHandler;

@end

NS_ASSUME_NONNULL_END
