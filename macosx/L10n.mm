// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "L10n.h"

#include <locale>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/string-utils.h>
#include <libtransmission/utils.h>

namespace
{

// Formats localized numbers the way NSString's localized formats do, with the digits and separators of the user's region.
// {fmt} hands every "L" field of a number to this facet first.
// A separator in one `char`, as std::numpunct<char> offers, could not hold
// French's narrow no-break space or Arabic's thousands separator.
class LocalizedNumberFacet final : public fmt::format_facet<std::locale>
{
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
            auto const is_decimal = type == fmt::presentation_type::none || type == fmt::presentation_type::dec;

            // libtransmission formats numbers on its own threads, which have no autorelease pool.
            @autoreleasepool {
                if constexpr (std::is_same_v<T, int> || std::is_same_v<T, long long>) {
                    return is_decimal && write(out, [NSString localizedStringWithFormat:@"%lld", static_cast<long long>(value)]);
                } else if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T, unsigned long long>) {
                    return is_decimal &&
                        write(out, [NSString localizedStringWithFormat:@"%llu", static_cast<unsigned long long>(value)]);
                } else if constexpr (std::is_floating_point_v<T>) {
                    auto const precision = specs.precision < 0 ? 6 : specs.precision;
                    return type == fmt::presentation_type::fixed &&
                        write(out, [NSString localizedStringWithFormat:@"%.*f", precision, static_cast<double>(value)]);
                } else {
                    return false;
                }
            }
        });
    }

  private:
    static bool write(fmt::appender out, NSString* const str)
    {
        auto const* const utf8 = str.UTF8String;
        if (utf8 == nullptr) {
            return false;
        }

        for (auto const ch : std::string_view{ utf8 }) {
            *out++ = ch;
        }
        return true;
    }
};

[[nodiscard]] NSString* formatTranslation(char const* const translation, std::initializer_list<tr::app::l10n::Arg> const args)
{
    // The global locale has the facet that TRSetUpLocalization() installed.
    return tr_strv_to_utf8_nsstring(tr::app::l10n::format_translation(std::locale{}, translation, { std::data(args), std::size(args) }));
}

} // namespace

void TRSetUpLocalization()
{
    // libtransmission and libtransmission-app pass {fmt} no locale, so it uses the global one, as TR_FORMAT does.
    // An app launched from Finder or the Dock gets no LANG, so the environment's locale alone would group no digits.
    // std::locale owns its facets and deletes them along with its last copy.
    tr_locale_set_global("");
    tr_locale_set_global(std::locale{ std::locale{}, new LocalizedNumberFacet{} });

    @autoreleasepool {
        NSBundle* const bundle = NSBundle.mainBundle;
        auto filenames = std::vector<std::string>{};

        for (NSString* localization in bundle.preferredLocalizations) {
            // The English text is in the code, so it is preferred over every language after it.
            if ([localization isEqualToString:@"en"] || [localization isEqualToString:@"Base"]) {
                break;
            }

            // Each catalog is named after the gettext domain, which is the app's name.
            NSString* const path = [bundle pathForResource:@TR_PROJ_APPNAME ofType:@"mo" inDirectory:nil
                                           forLocalization:localization];
            if (path != nil) {
                filenames.emplace_back(path.fileSystemRepresentation);
            }
        }

        tr::app::l10n::use_catalog_files(filenames);
    }
}

NSString* TRText(char const* const msgid)
{
    return tr_strv_to_utf8_nsstring(tr_gettext(msgid));
}

NSString* TRTextInContext(char const* const context, char const* const msgid)
{
    return tr_strv_to_utf8_nsstring(tr_pgettext(context, msgid));
}

NSString* TRMnemonicText(char const* const msgid)
{
    return tr_strv_to_utf8_nsstring(tr::app::l10n::strip_mnemonic(tr_gettext(msgid)));
}

NSString* TRFormat(char const* const msgid, std::initializer_list<tr::app::l10n::Arg> const args)
{
    return formatTranslation(tr_gettext(msgid), args);
}

NSString* TRFormatPlural(char const* const msgid, char const* const msgid_plural, uint64_t const n, std::initializer_list<tr::app::l10n::Arg> const args)
{
    return formatTranslation(tr_ngettext(msgid, msgid_plural, n), args);
}
