// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "CocoaCompatibility.h"

#import "NSImageAdditions.h"
#import "NSApplicationAdditions.h"

@implementation NSImage (NSImageAdditions)

static CGFloat const kIconSize = 16.0;
static CGFloat const kBorderWidth = 1.25;

+ (NSImage*)discIconWithColor:(NSColor*)color insetFactor:(CGFloat)insetFactor
{
    return [NSImage imageWithSize:NSMakeSize(kIconSize, kIconSize) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        //shape
        rect = NSInsetRect(rect, kBorderWidth / 2 + rect.size.width * insetFactor / 2, kBorderWidth / 2 + rect.size.height * insetFactor / 2);
        NSBezierPath* bp = [NSBezierPath bezierPathWithOvalInRect:rect];
        bp.lineWidth = kBorderWidth;

        //border
        CGFloat fractionOfBlendedColor = NSApp.darkMode ? 0.15 : 0.3;
        NSColor* borderColor = [color blendedColorWithFraction:fractionOfBlendedColor ofColor:NSColor.controlTextColor];
        [borderColor setStroke];
        [bp stroke];

        //inside
        [color setFill];
        [bp fill];

        return YES;
    }];
}

- (NSImage*)imageWithColor:(NSColor*)color
{
    NSImage* coloredImage = [self copy];

    [coloredImage lockFocus];

    [color set];

    NSSize const size = coloredImage.size;
    NSRectFillUsingOperation(NSMakeRect(0.0, 0.0, size.width, size.height), NSCompositingOperationSourceAtop);

    [coloredImage unlockFocus];

    return coloredImage;
}

@end

@implementation NSImage (Symbols)

+ (nullable instancetype)re_symbolNamed:(NSString*)name
{
    NSImage* image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
    [image setTemplate:YES];
    return image;
}

+ (nullable instancetype)re_symbolNamed:(NSString*)name pointSize:(CGFloat)pointSize weight:(NSFontWeight)weight
{
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithPointSize:pointSize weight:weight];
    NSImage* image = [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil] imageWithSymbolConfiguration:config];
    [image setTemplate:YES];
    return image;
}

+ (nullable instancetype)re_symbolNamed:(NSString*)name color:(NSColor*)color
{
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithHierarchicalColor:color];
    return [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil] imageWithSymbolConfiguration:config];
}

+ (instancetype)re_symbolNamed:(NSString*)name colors:(NSArray<NSColor*>*)colors
{
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithPaletteColors:colors];
    return [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil] imageWithSymbolConfiguration:config];
}

+ (nullable instancetype)re_multicolorSymbolNamed:(NSString*)name
{
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationPreferringMulticolor];
    return [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil] imageWithSymbolConfiguration:config];
}

@end

@implementation NSImage (Resources)

+ (NSImage*)highPriority
{
    return [self re_symbolNamed:@"arrowtriangle.up.fill" pointSize:16 weight:NSFontWeightRegular];
}

+ (NSImage*)normalPriority
{
    return [self re_symbolNamed:@"circle.fill" pointSize:13 weight:NSFontWeightRegular];
}

+ (NSImage*)lowPriority
{
    return [self re_symbolNamed:@"arrowtriangle.down.fill" pointSize:16 weight:NSFontWeightRegular];
}

+ (NSImage*)uploadArrow
{
    if (@available(macOS 14.0, *)) {
        return [self re_symbolNamed:@"arrowshape.up.fill"];
    } else {
        return [self re_symbolNamed:@"arrow.up"];
    }
}

+ (NSImage*)downloadArrow
{
    if (@available(macOS 14.0, *)) {
        return [self re_symbolNamed:@"arrowshape.down.fill"];
    } else {
        return [self re_symbolNamed:@"arrow.down"];
    }
}

@end
