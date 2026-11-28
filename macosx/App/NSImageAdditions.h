// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface NSImage (NSImageAdditions)

+ (NSImage*)discIconWithColor:(NSColor*)color insetFactor:(CGFloat)insetFactor;
- (NSImage*)imageWithColor:(NSColor*)color;

@end

@interface NSImage (Symbols)

+ (instancetype)re_symbolNamed:(NSString*)name;
+ (instancetype)re_symbolNamed:(NSString*)name pointSize:(CGFloat)pointSize weight:(NSFontWeight)weight;
+ (instancetype)re_symbolNamed:(NSString*)name color:(NSColor*)color;
+ (instancetype)re_symbolNamed:(NSString*)name colors:(NSArray<NSColor*>*)colors;
+ (instancetype)re_multicolorSymbolNamed:(NSString*)name;

@end

@interface NSImage (Resources)

+ (NSImage*)highPriority;
+ (NSImage*)normalPriority;
+ (NSImage*)lowPriority;

+ (NSImage*)uploadArrow;
+ (NSImage*)downloadArrow;

@end
