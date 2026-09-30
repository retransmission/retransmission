// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TrFormat.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QLocale>

namespace trqt
{
namespace
{

// Formats localized numbers the way QLocale does, with its digits and separators.
// {fmt} hands every "L" field of a number to this facet first.
// A separator in one `char`, as std::numpunct<char> offers, could not hold
// French's narrow no-break space or Arabic's thousands separator.
class QLocaleFacet final : public fmt::format_facet<std::locale>
{
public:
    explicit QLocaleFacet(QLocale const& qlocale)
        : qlocale_{ qlocale }
    {
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
            auto const is_decimal = type == fmt::presentation_type::none || type == fmt::presentation_type::dec;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, long long>) {
                return is_decimal && write(out, qlocale_.toString(static_cast<qlonglong>(value)));
            } else if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T, unsigned long long>) {
                return is_decimal && write(out, qlocale_.toString(static_cast<qulonglong>(value)));
            } else if constexpr (std::is_floating_point_v<T>) {
                auto const precision = specs.precision;
                auto const as_double = static_cast<double>(value);
                switch (type) {
                case fmt::presentation_type::none:
                    return write(
                        out,
                        precision < 0 ? qlocale_.toString(as_double, 'g', QLocale::FloatingPointShortest) :
                                        qlocale_.toString(as_double, 'g', precision));
                case fmt::presentation_type::fixed:
                    return write(out, qlocale_.toString(as_double, 'f', precision < 0 ? 6 : precision));
                case fmt::presentation_type::exp:
                    return write(out, qlocale_.toString(as_double, specs.upper() ? 'E' : 'e', precision < 0 ? 6 : precision));
                case fmt::presentation_type::general:
                    return write(out, qlocale_.toString(as_double, specs.upper() ? 'G' : 'g', precision < 0 ? 6 : precision));
                default:
                    return false;
                }
            } else {
                return false;
            }
        });
    }

private:
    static bool write(fmt::appender out, QString const& str)
    {
        for (char const ch : str.toUtf8()) {
            *out++ = ch;
        }
        return true;
    }

    QLocale qlocale_;
};

struct Field {
    std::string_view name;
    std::string_view spec;
};

[[nodiscard]] constexpr bool isIdentifier(std::string_view const str) noexcept
{
    auto const is_start = [](char const ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    auto const is_rest = [&is_start](char const ch) {
        return is_start(ch) || (ch >= '0' && ch <= '9');
    };

    return !std::empty(str) && is_start(str.front()) && std::all_of(std::begin(str) + 1, std::end(str), is_rest);
}

// Returns the replacement fields in `text`, or nullopt if it has a brace that isn't
// an escaped brace or a named field.
// {fmt} accepts more, e.g. positional fields,
// but translations have no use for them.
[[nodiscard]] std::optional<std::vector<Field>> parseFields(std::string_view const text)
{
    auto fields = std::vector<Field>{};

    for (size_t pos = 0; pos < std::size(text); ++pos) {
        auto const ch = text[pos];
        if (ch != '{' && ch != '}') {
            continue;
        }

        if (pos + 1 < std::size(text) && text[pos + 1] == ch) {
            ++pos; // an escaped "{{" or "}}"
            continue;
        }

        auto const end = text.find_first_of("{}", pos + 1);
        if (ch == '}' || end == std::string_view::npos || text[end] != '}') {
            return {};
        }

        auto const body = text.substr(pos + 1, end - pos - 1);
        auto const colon = body.find(':');
        auto const field = Field{ .name = body.substr(0, colon),
                                  .spec = colon == std::string_view::npos ? std::string_view{} : body.substr(colon + 1) };
        if (!isIdentifier(field.name)) {
            return {};
        }

        fields.push_back(field);
        pos = end;
    }

    return fields;
}

// Whether {fmt} can format `text` with these arguments without reporting an error.
// Every field must name an argument,
// and its spec must be empty, "L" for a number, or a spec that `source_fields` uses for that name.
[[nodiscard]] bool fits(
    std::string_view const text,
    std::span<Field const> const source_fields,
    std::span<format_detail::ArgInfo const> const arg_infos)
{
    auto const fields = parseFields(text);
    if (!fields) {
        return false;
    }

    return std::ranges::all_of(*fields, [&](Field const& field) {
        auto const arg = std::ranges::find_if(arg_infos, [&field](auto const& info) { return info.name == field.name; });
        if (arg == std::end(arg_infos)) {
            return false;
        }

        if (std::empty(field.spec) || (field.spec == "L" && arg->is_number)) {
            return true;
        }

        return std::ranges::any_of(source_fields, [&field](Field const& source_field) {
            return source_field.name == field.name && source_field.spec == field.spec;
        });
    });
}

[[nodiscard]] std::string_view toStringView(QByteArray const& bytes) noexcept
{
    return { bytes.constData(), static_cast<size_t>(bytes.size()) };
}

// Warns once per source string, since views format the same text on every repaint.
void warnBadTranslation(QString const& translation, char const* source)
{
    thread_local auto warned = std::set<char const*>{};
    if (warned.insert(source).second) {
        qWarning().noquote() << "Ignoring translation" << translation << "of" << source
                             << "because its fields don't fit the arguments";
    }
}

} // namespace

std::locale const& fmtLocale()
{
    // QLocale::setDefault() can change QLocale{} at any time, so compare on every call.
    thread_local auto cache = std::optional<std::pair<QLocale, std::locale>>{};

    if (auto const qlocale = QLocale{}; !cache || cache->first != qlocale) {
        auto locale = std::locale{ std::locale::classic(), new QLocaleFacet{ qlocale } };
        cache.emplace(qlocale, std::move(locale));
    }

    return cache->second;
}

std::pair<QString, QString> splitAtField(QString const& translation, char const* const source, std::string_view const name)
{
    // Unescapes "{{" and "}}", which parseFields() has checked come in pairs.
    auto const unescape = [](std::string_view const text) {
        auto str = std::string{};
        for (size_t pos = 0; pos < std::size(text); ++pos) {
            str += text[pos];
            pos += (text[pos] == '{' || text[pos] == '}') ? 1U : 0U;
        }
        return QString::fromStdString(str);
    };

    auto const split = [&](std::string_view const text) -> std::optional<std::pair<QString, QString>> {
        auto const fields = parseFields(text);
        if (!fields || std::size(*fields) != 1U || fields->front().name != name) {
            return {};
        }

        // The field's name is a view into `text`, so it marks where the field starts and ends.
        auto const field_begin = static_cast<size_t>(fields->front().name.data() - text.data()) - 1U;
        auto const field_end = text.find('}', field_begin) + 1U;
        return std::pair{ unescape(text.substr(0, field_begin)), unescape(text.substr(field_end)) };
    };

    auto const utf8 = translation.toUtf8();
    if (auto result = split(toStringView(utf8)); result) {
        return *std::move(result);
    }

    warnBadTranslation(translation, source);
    return split(source).value_or(std::pair{ QString::fromUtf8(source), QString{} });
}

QString format_detail::formatTranslation(
    QString const& translation,
    char const* const source,
    std::span<ArgInfo const> const arg_infos,
    fmt::format_args const args)
{
    auto const source_fields = parseFields(source).value_or(std::vector<Field>{});
    auto const format = [&](std::string_view const text) {
        return QString::fromStdString(fmt::vformat(fmtLocale(), text, args));
    };

    if (auto const utf8 = translation.toUtf8(); fits(toStringView(utf8), source_fields, arg_infos)) {
        return format(toStringView(utf8));
    }

    warnBadTranslation(translation, source);

    // A source that doesn't fit is a bug in the calling code. Show it unformatted.
    return fits(source, source_fields, arg_infos) ? format(source) : QString::fromUtf8(source);
}

} // namespace trqt
