// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface TRGroup : NSObject<NSSecureCoding>

@property(nonatomic, assign) NSInteger groupIndex;
@property(nonatomic, copy, null_resettable) NSString* name;
@property(nonatomic, strong, null_resettable) NSColor* color;

/** Runtime-only cache for the rendered group icon. Ignored during serialization. */
@property(nonatomic, strong, nullable) NSImage* icon;

@property(nonatomic, assign) BOOL usesCustomDownloadLocation;
@property(nonatomic, copy, nullable) NSString* customDownloadLocation;

@property(nonatomic, assign) BOOL usesAutoGroupRules;
@property(nonatomic, strong, nullable) NSPredicate* autoGroupRules;

- (instancetype)initWithIndex:(NSInteger)index name:(nullable NSString*)name color:(nullable NSColor*)color;

@end

NS_ASSUME_NONNULL_END
