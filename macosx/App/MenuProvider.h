// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

typedef NS_ENUM(NSUInteger, SortTag) {
    SortTagOrder = 0,
    SortTagDate = 1,
    SortTagName = 2,
    SortTagProgress = 3,
    SortTagState = 4,
    SortTagTracker = 5,
    SortTagActivity = 6,
    SortTagSize = 7,
    SortTagETA = 8
};

typedef NS_ENUM(NSUInteger, SortOrderTag) { SortOrderTagAscending = 0, SortOrderTagDescending = 1 };

@interface MenuProvider : NSObject
- (instancetype)initWithController:(id)controller;
- (NSMenu*)buildMainMenu:(id)sparkleController;
@end
