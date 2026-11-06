// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <locale>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include <libtransmission/string-utils.h>

#import "NSStringAdditions.h"
#import "NSDataAdditions.h"

namespace
{
// Formats localized numbers the way NSNumberFormatter does for the user's region, with its digits and separators.
// {fmt} hands every "L" field of a number to this facet first.
// A separator in one `char`, as std::numpunct<char> offers, could not hold French's narrow no-break space
// or Arabic's decimal separator.
class RegionFacet final : public fmt::format_facet<std::locale>
{
  public:
    RegionFacet()
    {
        for (size_t digits = 0; digits < std::size(formatters_); ++digits) {
            NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
            formatter.numberStyle = NSNumberFormatterDecimalStyle;
            formatter.minimumFractionDigits = digits;
            formatter.maximumFractionDigits = digits;
            formatters_[digits] = formatter;
        }
    }

  protected:
    [[nodiscard]] bool do_put(fmt::appender out, fmt::loc_value val, fmt::format_specs const& specs) const override
    {
        // Returning false leaves the value to {fmt}, which formats it unlocalized.
        if (specs.width != 0 || specs.sign() != fmt::sign::none || specs.alt()) {
            return false;
        }

        return val.visit([&](auto const value) -> bool {
            using T = std::remove_cv_t<decltype(value)>;
            auto const type = specs.type();

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, long long>) {
                auto const is_decimal = type == fmt::presentation_type::none || type == fmt::presentation_type::dec;
                return is_decimal && write(out, [formatters_[0] stringFromNumber:@(static_cast<long long>(value))]);
            } else if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T, unsigned long long>) {
                auto const is_decimal = type == fmt::presentation_type::none || type == fmt::presentation_type::dec;
                return is_decimal && write(out, [formatters_[0] stringFromNumber:@(static_cast<unsigned long long>(value))]);
            } else if constexpr (std::is_floating_point_v<T>) {
                auto const precision = specs.precision;
                if (type != fmt::presentation_type::fixed || precision < 0 || precision >= static_cast<int>(std::size(formatters_))) {
                    return false;
                }
                return write(out, [formatters_[precision] stringFromNumber:@(static_cast<double>(value))]);
            } else {
                return false;
            }
        });
    }

  private:
    static bool write(fmt::appender out, NSString* str)
    {
        for (char const* ch = str.UTF8String; ch != nullptr && *ch != '\0'; ++ch) {
            *out++ = *ch;
        }
        return true;
    }

    // with 0, 1, and 2 fraction digits, the precisions tr::Values formats
    std::array<NSNumberFormatter*, 3> formatters_;
};

// The user's region's number format, passed to each tr::Values to_string() and to_whole_string() call.
// Installing a locale globally can also change the C locale, and AppKit then draws SF Symbols
// at the wrong size whenever the C locale's decimal separator is a comma.
std::locale const& regionLocale()
{
    static auto const locale = std::locale{ std::locale::classic(), new RegionFacet{} };
    return locale;
}

// the region's percent pattern places the sign, e.g. "5%", "5 %", or "%5"
NSString* withPercentSign(std::string_view const digits)
{
    static NSString* prefix;
    static NSString* suffix;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
        formatter.numberStyle = NSNumberFormatterPercentStyle;
        prefix = formatter.positivePrefix;
        suffix = formatter.positiveSuffix;
    });

    return [NSString stringWithFormat:@"%@%@%@", prefix, tr_strv_to_utf8_nsstring(digits), suffix];
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

+ (NSString*)percentString:(tr::Values::Percent)progress
{
    return withPercentSign(progress.to_string(regionLocale()));
}

+ (NSString*)wholePercentString:(tr::Values::Percent)progress
{
    return withPercentSign(progress.to_whole_string(regionLocale()));
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
