// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <locale>
#include <optional>
#include <string>
#include <utility>

#include <libtransmission/string-utils.h>

#import "NSStringAdditions.h"
#import "NSDataAdditions.h"

namespace
{
class RegionNumpunct final : public std::numpunct<char>
{
public:
    RegionNumpunct(char const decimal_point, char const thousands_sep, std::string grouping)
        : decimal_point_{ decimal_point }
        , thousands_sep_{ thousands_sep }
        , grouping_{ std::move(grouping) }
    {
    }

private:
    [[nodiscard]] char do_decimal_point() const override
    {
        return decimal_point_;
    }

    [[nodiscard]] char do_thousands_sep() const override
    {
        return thousands_sep_;
    }

    [[nodiscard]] std::string do_grouping() const override
    {
        return grouping_;
    }

    char decimal_point_;
    char thousands_sep_;
    std::string grouping_;
};

// The user's region's number separators, passed to each tr::Values to_string() call.
// Installing a locale globally can also change the C locale, and AppKit then draws SF Symbols
// at the wrong size whenever the C locale's decimal separator is a comma.
std::locale const& regionLocale()
{
    static auto const locale = []
    {
        NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
        formatter.numberStyle = NSNumberFormatterDecimalStyle;

        // a separator longer than one byte falls back to "." or to no grouping
        auto const as_byte = [](NSString* str) -> std::optional<char>
        {
            if (str.length == 1 && [str characterAtIndex:0] < 0x80) {
                return static_cast<char>([str characterAtIndex:0]);
            }
            return {};
        };

        auto grouping = std::string{};
        auto const thousands_sep = as_byte(formatter.groupingSeparator);
        if (formatter.usesGroupingSeparator && thousands_sep && formatter.groupingSize > 0) {
            grouping += static_cast<char>(formatter.groupingSize);
            if (formatter.secondaryGroupingSize > 0) {
                grouping += static_cast<char>(formatter.secondaryGroupingSize);
            }
        }

        auto const decimal_point = as_byte(formatter.decimalSeparator).value_or('.');
        return std::locale{ std::locale::classic(),
                            new RegionNumpunct{ decimal_point, thousands_sep.value_or(','), std::move(grouping) } };
    }();

    return locale;
}
} // namespace

@interface NSString (Private)

+ (NSString*)stringForSpeed:(CGFloat)speed kb:(NSString*)kb mb:(NSString*)mb gb:(NSString*)gb;
+ (NSString*)stringForSpeedCompact:(CGFloat)speed kb:(NSString*)kb mb:(NSString*)mb gb:(NSString*)gb;

@end

@implementation NSString (NSStringAdditions)

+ (NSString*)ellipsis
{
    return @"\xE2\x80\xA6";
}

- (NSString*)stringByAppendingEllipsis
{
    return [self stringByAppendingString:NSString.ellipsis];
}

// Maximum supported localization is 9.22 EB, which is the maximum supported filesystem size by macOS, 8 EiB.
// https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/VolumeFormatComparison/VolumeFormatComparison.html
+ (NSString*)stringForFileSize:(uint64_t)size
{
    return [NSByteCountFormatter stringFromByteCount:size countStyle:NSByteCountFormatterCountStyleFile];
}

// Maximum supported localization is 9.22 EB, which is the maximum supported filesystem size by macOS, 8 EiB.
// https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/VolumeFormatComparison/VolumeFormatComparison.html
+ (NSString*)stringForFilePartialSize:(uint64_t)partialSize fullSize:(uint64_t)fullSize
{
    NSByteCountFormatter* fileSizeFormatter = [[NSByteCountFormatter alloc] init];

    NSString* fullSizeString = [fileSizeFormatter stringFromByteCount:fullSize];

    //figure out the magnitude of the two, since we can't rely on comparing the units because of localization and pluralization issues (for example, "1 byte of 2 bytes")
    BOOL partialUnitsSame;
    if (partialSize == 0) {
        partialUnitsSame = YES; //we want to just show "0" when we have no partial data, so always set to the same units
    } else {
        auto const magnitudePartial = static_cast<unsigned int>(log(partialSize) / log(1000));
        // we have to catch 0 with a special case, so might as well avoid the math for all of magnitude 0
        auto const magnitudeFull = static_cast<unsigned int>(fullSize < 1000 ? 0 : log(fullSize) / log(1000));
        partialUnitsSame = magnitudePartial == magnitudeFull;
    }

    fileSizeFormatter.includesUnit = !partialUnitsSame;
    NSString* partialSizeString = [fileSizeFormatter stringFromByteCount:partialSize];

    return [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "file size string"), partialSizeString, fullSizeString];
}

+ (NSString*)stringForSpeed:(CGFloat)speed
{
    return [self stringForSpeed:speed kb:NSLocalizedString(@"KB/s", "Transfer speed (kilobytes per second)")
                             mb:NSLocalizedString(@"MB/s", "Transfer speed (megabytes per second)")
                             gb:NSLocalizedString(@"GB/s", "Transfer speed (gigabytes per second)")];
}

+ (NSString*)stringForSpeedAbbrev:(CGFloat)speed
{
    return [self stringForSpeed:speed kb:@"K" mb:@"M" gb:@"G"];
}

+ (NSString*)stringForSpeedAbbrevCompact:(CGFloat)speed
{
    return [self stringForSpeedCompact:speed kb:@"K" mb:@"M" gb:@"G"];
}

+ (NSString*)stringForRatio:(tr::Values::Ratio)ratio
{
    return tr_strv_to_utf8_nsstring(ratio.to_string(regionLocale()));
}

+ (NSString*)percentString:(CGFloat)progress longDecimals:(BOOL)longDecimals
{
    static NSNumberFormatter* longFormatter;
    static NSNumberFormatter* shortFormatter;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        longFormatter = [[NSNumberFormatter alloc] init];
        longFormatter.numberStyle = NSNumberFormatterPercentStyle;
        longFormatter.maximumFractionDigits = 2;
        shortFormatter = [[NSNumberFormatter alloc] init];
        shortFormatter.numberStyle = NSNumberFormatterPercentStyle;
        shortFormatter.maximumFractionDigits = 1;
    });
    if (progress >= 1.0) {
        return [shortFormatter stringFromNumber:@(1)];
    } else if (longDecimals) {
        return [longFormatter stringFromNumber:@(MIN(progress, 0.9999))];
    } else {
        return [shortFormatter stringFromNumber:@(MIN(progress, 0.999))];
    }
}

- (NSComparisonResult)compareNumeric:(NSString*)string
{
    NSStringCompareOptions const comparisonOptions = NSNumericSearch | NSForcedOrderingSearch;
    return [self compare:string options:comparisonOptions range:NSMakeRange(0, self.length) locale:NSLocale.currentLocale];
}

- (NSArray<NSString*>*)nonEmptyComponentsSeparatedByCharactersInSet:(NSCharacterSet*)separators
{
    NSMutableArray<NSString*>* components = [NSMutableArray array];
    for (NSString* evaluatedObject in [self componentsSeparatedByCharactersInSet:separators]) {
        if (evaluatedObject.length > 0) {
            [components addObject:evaluatedObject];
        }
    }
    return components;
}

+ (NSString*)convertedStringFromCString:(nonnull char const*)bytes
{
    // UTF-8 encoding
    NSString* fullPath = @(bytes);
    if (fullPath) {
        return fullPath;
    }
    // autodetection of the encoding (#3434)
    NSData* data = [NSData dataWithBytes:(void const*)bytes length:sizeof(unsigned char) * strlen(bytes)];
    [NSString stringEncodingForData:data encodingOptions:nil convertedString:&fullPath usedLossyConversion:nil];
    if (fullPath) {
        return fullPath;
    }
    // hexa encoding
    return data.hexString;
}

@end

@implementation NSString (Private)

+ (NSString*)stringForSpeed:(CGFloat)speed kb:(NSString*)kb mb:(NSString*)mb gb:(NSString*)gb
{
    if (speed < 999.95) // 0.0 KB/s to 999.9 KB/s
    {
        return [NSString localizedStringWithFormat:@"%.1f %@", speed, kb];
    }

    speed /= 1000.0;

    if (speed < 99.995) // 1.00 MB/s to 99.99 MB/s
    {
        return [NSString localizedStringWithFormat:@"%.2f %@", speed, mb];
    } else if (speed < 999.95) // 100.0 MB/s to 999.9 MB/s
    {
        return [NSString localizedStringWithFormat:@"%.1f %@", speed, mb];
    }

    speed /= 1000.0;

    if (speed < 99.995) // 1.00 GB/s to 99.99 GB/s
    {
        return [NSString localizedStringWithFormat:@"%.2f %@", speed, gb];
    }
    // 100.0 GB/s and above
    return [NSString localizedStringWithFormat:@"%.1f %@", speed, gb];
}

+ (NSString*)stringForSpeedCompact:(CGFloat)speed kb:(NSString*)kb mb:(NSString*)mb gb:(NSString*)gb
{
    if (speed < 99.95) // 0.0 KB/s to 99.9 KB/s
    {
        return [NSString localizedStringWithFormat:@"%.1f %@", speed, kb];
    }
    if (speed < 999.5) // 100 KB/s to 999 KB/s
    {
        return [NSString localizedStringWithFormat:@"%.0f %@", speed, kb];
    }

    speed /= 1000.0;

    if (speed < 9.995) // 1.00 MB/s to 9.99 MB/s
    {
        return [NSString localizedStringWithFormat:@"%.2f %@", speed, mb];
    }
    if (speed < 99.95) // 10.0 MB/s to 99.9 MB/s
    {
        return [NSString localizedStringWithFormat:@"%.1f %@", speed, mb];
    }
    if (speed < 999.5) // 100 MB/s to 999 MB/s
    {
        return [NSString localizedStringWithFormat:@"%.0f %@", speed, mb];
    }

    speed /= 1000.0;

    if (speed < 9.995) // 1.00 GB/s to 9.99 GB/s
    {
        return [NSString localizedStringWithFormat:@"%.2f %@", speed, gb];
    }
    if (speed < 99.95) // 10.0 GB/s to 99.9 GB/s
    {
        return [NSString localizedStringWithFormat:@"%.1f %@", speed, gb];
    }
    // 100 GB/s and above
    return [NSString localizedStringWithFormat:@"%.0f %@", speed, gb];
}

@end
