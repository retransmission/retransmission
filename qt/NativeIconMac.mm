// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <utility>

#import <QtGui/QColor>
#import <QtGui/QImage>
#import <QtGui/QPixmap>

#import <AppKit/AppKit.h>

[[nodiscard]] bool hasSFSymbol(QString const symbol_name)
{
    return [NSImage imageWithSystemSymbolName:symbol_name.toNSString() accessibilityDescription:nil] != nil;
}

[[nodiscard]] QPixmap loadSFSymbol(QString const symbol_name, int const pixel_size, QColor const& color)
{
    if (pixel_size <= 0) {
        return {};
    }

    // Drain before returning: the autoreleased NSGraphicsContext retains `context`,
    // which must not outlive the pixels it draws into.
    @autoreleasepool {
        NSImage* symbol = [NSImage imageWithSystemSymbolName:symbol_name.toNSString() accessibilityDescription:nil];
        if (symbol == nil) {
            return {};
        }

        // Build the color in sRGB, the bitmap's color space, so its pixels equal the QColor's components.
        NSColor* const ns_color = [NSColor colorWithSRGBRed:color.redF() green:color.greenF() blue:color.blueF()
                                                      alpha:color.alphaF()];
        auto* const size_config = [NSImageSymbolConfiguration configurationWithPointSize:pixel_size weight:NSFontWeightRegular];
        auto* const color_config = [NSImageSymbolConfiguration configurationWithHierarchicalColor:ns_color];
        symbol = [symbol imageWithSymbolConfiguration:[size_config configurationByApplyingConfiguration:color_config]];

        // Center the symbol in a pixel_size square without changing its aspect ratio.
        // A square bitmap lets the icon engine map it 1:1 onto a square icon rect.
        auto const natural = symbol.size;
        if (natural.width <= 0 || natural.height <= 0) {
            return {};
        }
        auto const scale = pixel_size / std::max(natural.width, natural.height);
        auto const width = natural.width * scale;
        auto const height = natural.height * scale;

        auto image = QImage{ pixel_size, pixel_size, QImage::Format_ARGB32_Premultiplied };
        image.fill(Qt::transparent);

        // Let AppKit draw straight into the QImage's pixels.
        // Premultiplied alpha-first in host byte order is Format_ARGB32_Premultiplied's layout.
        // CGBitmapInfo{} keeps C++20 from warning about OR-ing two different enum types.
        auto constexpr BitmapInfo = CGBitmapInfo{ kCGImageAlphaPremultipliedFirst } | kCGBitmapByteOrder32Host;
        CGColorSpaceRef const color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef const context = CGBitmapContextCreate(image.bits(), pixel_size, pixel_size, 8, image.bytesPerLine(), color_space, BitmapInfo);
        CGColorSpaceRelease(color_space);
        if (context == nullptr) {
            return {};
        }

        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:context flipped:NO]];
        [symbol drawInRect:NSMakeRect((pixel_size - width) / 2, (pixel_size - height) / 2, width, height)];
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(context);

        return QPixmap::fromImage(std::move(image));
    }
}
