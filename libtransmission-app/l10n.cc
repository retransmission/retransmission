// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef> // size_t
#include <cstdint> // uint32_t, uint64_t
#include <cstring> // std::memcpy()
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/file.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>

#include "libtransmission-app/l10n.h"

using namespace std::literals;

namespace tr::app::l10n
{
namespace
{

[[nodiscard]] constexpr bool is_identifier(std::string_view const str) noexcept
{
    auto const is_start = [](char const ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    auto const is_rest = [&is_start](char const ch) {
        return is_start(ch) || (ch >= '0' && ch <= '9');
    };

    return !std::empty(str) && is_start(str.front()) && std::all_of(std::begin(str) + 1, std::end(str), is_rest);
}

// Whether {fmt} can format `translation` with the arguments that its English text takes.
[[nodiscard]] bool fits(std::string_view const translation, std::span<Field const> const english_fields)
{
    auto const fields = parse_fields(translation);
    if (!fields) {
        return false;
    }

    return std::ranges::all_of(*fields, [&english_fields](Field const& field) {
        return std::ranges::any_of(english_fields, [&field](Field const& english) {
            return english.name == field.name && (std::empty(field.spec) || english.spec == field.spec);
        });
    });
}

// Splits `str` at each `delimiter`.
[[nodiscard]] std::vector<std::string_view> split(std::string_view str, char const delimiter)
{
    auto parts = std::vector<std::string_view>{};
    for (;;) {
        auto const pos = str.find(delimiter);
        parts.push_back(str.substr(0, pos));
        if (pos == std::string_view::npos) {
            return parts;
        }
        str.remove_prefix(pos + 1U);
    }
}

[[nodiscard]] constexpr uint32_t byteswap(uint32_t const val) noexcept
{
    return ((val & 0xFFU) << 24U) | ((val & 0xFF00U) << 8U) | ((val >> 8U) & 0xFF00U) | (val >> 24U);
}

// Returns each message of a .mo file as its English text and its translation,
// or nullopt if `contents` isn't a .mo file of a revision this reads.
// https://www.gnu.org/software/gettext/manual/html_node/MO-Files.html
[[nodiscard]] std::optional<std::vector<std::pair<std::string_view, std::string_view>>> read_mo(std::string_view const contents)
{
    static auto constexpr Magic = uint32_t{ 0x950412DEU };

    auto swap = false;
    auto const u32 = [&contents, &swap](uint64_t const offset) noexcept -> std::optional<uint32_t> {
        if (offset + sizeof(uint32_t) > std::size(contents)) {
            return {};
        }

        auto val = uint32_t{};
        std::memcpy(&val, std::data(contents) + offset, sizeof(val));
        return swap ? byteswap(val) : val;
    };

    // A table entry is a string's length and offset.
    // The string ends with a '\0' that the length doesn't count.
    auto const string = [&contents, &u32](uint64_t const entry) noexcept -> std::optional<std::string_view> {
        auto const length = u32(entry);
        auto const offset = u32(entry + 4U);
        if (!length || !offset) {
            return {};
        }

        if (auto const end = uint64_t{ *offset } + *length; end >= std::size(contents) || contents[end] != '\0') {
            return {};
        }

        return contents.substr(*offset, *length);
    };

    auto const magic = u32(0U);
    if (!magic || (*magic != Magic && byteswap(*magic) != Magic)) {
        return {};
    }
    swap = *magic != Magic;

    // Revision 1 adds tables of system-dependent strings,
    // which this skips, and keeps the tables that this reads.
    auto const revision = u32(4U);
    auto const n_messages = u32(8U);
    auto const originals = u32(12U);
    auto const translations = u32(16U);
    if (!revision || (*revision >> 16U) > 1U || !n_messages || !originals || !translations) {
        return {};
    }

    auto messages = std::vector<std::pair<std::string_view, std::string_view>>{};
    for (uint64_t idx = 0U; idx < *n_messages; ++idx) {
        auto const original = string(*originals + (idx * 8U));
        auto const translation = string(*translations + (idx * 8U));
        if (!original || !translation) {
            return {};
        }

        messages.emplace_back(*original, *translation);
    }

    return messages;
}

// Returns the value of the header field `key`, e.g. "Plural-Forms".
[[nodiscard]] std::optional<std::string_view> header_field(std::string_view const header, std::string_view const key)
{
    for (auto const line : split(header, '\n')) {
        if (auto const colon = line.find(':'); colon != std::string_view::npos && tr_strv_strip(line.substr(0, colon)) == key) {
            return tr_strv_strip(line.substr(colon + 1U));
        }
    }

    return {};
}

// Messages from several catalogs, most preferred first,
// e.g. Brazilian Portuguese, then Portuguese for messages it lacks.
class Translations
{
public:
    explicit Translations(std::vector<detail::Catalog> catalogs)
        : catalogs_{ std::move(catalogs) }
    {
    }

    [[nodiscard]] char const* gettext(char const* const msgid) const noexcept
    {
        auto const key = std::string_view{ msgid };
        for (auto const& catalog : catalogs_) {
            if (auto const* const translation = catalog.gettext(key); translation != nullptr) {
                return translation;
            }
        }

        return msgid;
    }

    [[nodiscard]] char const* ngettext(char const* const msgid, char const* const msgid_plural, uint64_t const n) const noexcept
    {
        auto const key = std::string_view{ msgid };
        for (auto const& catalog : catalogs_) {
            if (auto const* const translation = catalog.ngettext(key, n); translation != nullptr) {
                return translation;
            }
        }

        return n == 1U ? msgid : msgid_plural;
    }

private:
    std::vector<detail::Catalog> catalogs_;
};

auto current_translations = std::atomic<Translations const*>{};

// Every Translations ever used stays alive until exit,
// because _() hands out pointers into them.
auto all_translations_mutex = std::mutex{};
auto all_translations = std::deque<Translations>{};

void use_translations(std::vector<detail::Catalog> catalogs)
{
    if (std::empty(catalogs)) {
        tr_set_translator(nullptr, nullptr);
        return;
    }

    auto const lock = std::scoped_lock{ all_translations_mutex };
    current_translations = &all_translations.emplace_back(std::move(catalogs));
    tr_set_translator(
        [](char const* const msgid) noexcept { return current_translations.load()->gettext(msgid); },
        [](char const* const msgid, char const* const msgid_plural, uint64_t const n) noexcept {
            return current_translations.load()->ngettext(msgid, msgid_plural, n);
        });
}

} // namespace

// ---

std::optional<std::vector<Field>> parse_fields(std::string_view const text)
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
        if (!is_identifier(field.name)) {
            return {};
        }

        fields.push_back(field);
        pos = end;
    }

    return fields;
}

namespace detail
{

// --- PluralForms

namespace
{
// Evaluates a Plural-Forms formula for one count,
// with C's operators and precedence, as gettext does.
// It evaluates both sides of &&, || and ?:, unlike C.
// The results still match C's, because division and modulo by zero yield 0.
class PluralFormula
{
public:
    PluralFormula(std::string_view const text, uint64_t const n) noexcept
        : text_{ text }
        , n_{ n }
    {
    }

    // Returns the formula's value, or nullopt if `text` isn't a valid formula.
    [[nodiscard]] std::optional<uint64_t> evaluate() noexcept
    {
        auto const value = conditional(0U);
        skip_space();
        return pos_ == std::size(text_) ? value : std::nullopt;
    }

private:
    struct BinaryOp {
        std::string_view token;
        int precedence;
        uint64_t (*apply)(uint64_t lhs, uint64_t rhs) noexcept;
    };

    // Lowest precedence first. Each "<=" or ">=" precedes its one-character prefix.
    static auto constexpr BinaryOps = std::array<BinaryOp, 13U>{ {
        { .token = "||"sv,
          .precedence = 0,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a != 0U || b != 0U ? 1U : 0U; } },
        { .token = "&&"sv,
          .precedence = 1,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a != 0U && b != 0U ? 1U : 0U; } },
        { .token = "=="sv,
          .precedence = 2,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a == b ? 1U : 0U; } },
        { .token = "!="sv,
          .precedence = 2,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a != b ? 1U : 0U; } },
        { .token = "<="sv,
          .precedence = 3,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a <= b ? 1U : 0U; } },
        { .token = ">="sv,
          .precedence = 3,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a >= b ? 1U : 0U; } },
        { .token = "<"sv,
          .precedence = 3,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a < b ? 1U : 0U; } },
        { .token = ">"sv,
          .precedence = 3,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a > b ? 1U : 0U; } },
        { .token = "+"sv, .precedence = 4, .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a + b; } },
        { .token = "-"sv, .precedence = 4, .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a - b; } },
        { .token = "*"sv, .precedence = 5, .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return a * b; } },
        { .token = "/"sv,
          .precedence = 5,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return b != 0U ? a / b : 0U; } },
        { .token = "%"sv,
          .precedence = 5,
          .apply = [](uint64_t a, uint64_t b) noexcept -> uint64_t { return b != 0U ? a % b : 0U; } },
    } };
    static auto constexpr MaxPrecedence = 5;

    // Real formulas nest a few levels deep. This bounds the recursion.
    static auto constexpr MaxDepth = size_t{ 32U };

    // condition ? if_true : if_false, which groups right to left.
    [[nodiscard]] std::optional<uint64_t> conditional(size_t const depth) noexcept
    {
        if (depth > MaxDepth) {
            return {};
        }

        auto const condition = binary(0, depth);
        if (!condition || !consume("?"sv)) {
            return condition;
        }

        auto const if_true = conditional(depth + 1U);
        if (!if_true || !consume(":"sv)) {
            return {};
        }

        auto const if_false = conditional(depth + 1U);
        if (!if_false) {
            return {};
        }

        return *condition != 0U ? if_true : if_false;
    }

    // Binary operators of `precedence` or higher, which group left to right.
    [[nodiscard]] std::optional<uint64_t> binary(int const precedence, size_t const depth) noexcept
    {
        if (precedence > MaxPrecedence) {
            return unary(depth);
        }

        auto lhs = binary(precedence + 1, depth);
        while (lhs) {
            skip_space();
            auto const op = std::ranges::find_if(BinaryOps, [this, precedence](BinaryOp const& candidate) {
                return candidate.precedence == precedence && text_.substr(pos_).starts_with(candidate.token);
            });
            if (op == std::end(BinaryOps)) {
                break;
            }

            pos_ += std::size(op->token);
            auto const rhs = binary(precedence + 1, depth);
            if (!rhs) {
                return {};
            }

            lhs = op->apply(*lhs, *rhs);
        }

        return lhs;
    }

    // "!x", "(x)", "n", or a number.
    [[nodiscard]] std::optional<uint64_t> unary(size_t const depth) noexcept
    {
        if (depth > MaxDepth) {
            return {};
        }

        if (consume("!"sv)) {
            auto const arg = unary(depth + 1U);
            return arg ? std::optional<uint64_t>{ *arg == 0U ? 1U : 0U } : std::nullopt;
        }

        if (consume("("sv)) {
            auto const inner = conditional(depth + 1U);
            return inner && consume(")"sv) ? inner : std::nullopt;
        }

        if (consume("n"sv)) {
            return n_;
        }

        auto rest = std::string_view{};
        auto const number = tr_num_parse<uint64_t>(text_.substr(pos_), &rest);
        pos_ = std::size(text_) - std::size(rest);
        return number;
    }

    void skip_space() noexcept
    {
        while (pos_ < std::size(text_) && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n')) {
            ++pos_;
        }
    }

    [[nodiscard]] bool consume(std::string_view const token) noexcept
    {
        skip_space();
        if (!text_.substr(pos_).starts_with(token)) {
            return false;
        }

        pos_ += std::size(token);
        return true;
    }

    std::string_view text_;
    uint64_t n_;
    size_t pos_ = 0U;
};
} // namespace

std::optional<PluralForms> PluralForms::parse(std::string_view const plural_forms)
{
    auto n_forms = std::optional<size_t>{};
    auto formula = std::optional<std::string_view>{};

    for (auto const part : split(plural_forms, ';')) {
        auto const equals = part.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }

        auto const key = tr_strv_strip(part.substr(0, equals));
        auto const value = tr_strv_strip(part.substr(equals + 1U));
        if (key == "nplurals"sv) {
            auto rest = std::string_view{};
            if (auto const count = tr_num_parse<size_t>(value, &rest); count && *count > 0U && std::empty(rest)) {
                n_forms = count;
            }
        } else if (key == "plural"sv && PluralFormula{ value, 0U }.evaluate()) {
            // Since no count makes evaluation fail, a formula valid for 0 is valid for every count.
            formula = value;
        }
    }

    if (!n_forms || !formula) {
        return {};
    }

    auto plurals = PluralForms{};
    plurals.formula_ = *formula;
    plurals.n_forms_ = *n_forms;
    return plurals;
}

size_t PluralForms::index(uint64_t const n) const noexcept
{
    if (std::empty(formula_)) {
        return n == 1U ? 0U : 1U;
    }

    auto const form = PluralFormula{ formula_, n }.evaluate().value_or(0U);
    return form < n_forms_ ? static_cast<size_t>(form) : 0U;
}

// --- Catalog

std::optional<Catalog> Catalog::parse(std::vector<char> mo_contents)
{
    auto catalog = Catalog{};
    catalog.contents_ = std::move(mo_contents);

    auto const messages = read_mo({ std::data(catalog.contents_), std::size(catalog.contents_) });
    if (!messages) {
        return {};
    }

    catalog.translations_.reserve(std::size(*messages));
    for (auto const& [original, translation] : *messages) {
        // The header is the translation of "".
        if (std::empty(original)) {
            if (auto const plural_forms = header_field(translation, "Plural-Forms"sv); plural_forms) {
                catalog.plural_forms_ = PluralForms::parse(*plural_forms).value_or(PluralForms{});
            }
            continue;
        }

        // A plural message is its singular and plural, separated by '\0'.
        // Its translation is every form, separated by '\0'.
        auto english_fields = std::vector<Field>{};
        for (auto const english : split(original, '\0')) {
            auto const fields = parse_fields(english).value_or(std::vector<Field>{});
            english_fields.insert(std::end(english_fields), std::begin(fields), std::end(fields));
        }

        auto const forms = split(translation, '\0');
        if (std::ranges::all_of(forms, [&](auto const form) { return !std::empty(form) && fits(form, english_fields); })) {
            catalog.translations_.try_emplace(original.substr(0, original.find('\0')), translation);
        }
    }

    return catalog;
}

char const* Catalog::gettext(std::string_view const msgid) const noexcept
{
    auto const iter = translations_.find(msgid);
    return iter != std::end(translations_) ? std::data(iter->second) : nullptr;
}

char const* Catalog::ngettext(std::string_view const msgid, uint64_t const n) const noexcept
{
    auto const iter = translations_.find(msgid);
    if (iter == std::end(translations_)) {
        return nullptr;
    }

    auto const forms = iter->second;
    auto pos = size_t{ 0U };
    for (auto idx = plural_forms_.index(n); idx > 0U; --idx) {
        pos = forms.find('\0', pos);
        if (pos == std::string_view::npos) {
            return std::data(forms); // the catalog has fewer forms than its formula picks from
        }
        ++pos;
    }

    return std::data(forms) + pos;
}

// ---

std::vector<std::string> language_candidates(std::span<std::string const> const preferred_languages)
{
    auto candidates = std::vector<std::string>{};
    auto const add = [&candidates](std::string candidate) {
        if (std::ranges::find(candidates, candidate) == std::end(candidates)) {
            candidates.push_back(std::move(candidate));
        }
    };

    for (auto name : preferred_languages) {
        // POSIX names look like "ll_CC.codeset@modifier", BCP 47 names like "ll-Script-CC".
        std::ranges::replace(name, '-', '_');

        auto modifier = std::string{};
        if (auto const at = name.find('@'); at != std::string::npos) {
            modifier = name.substr(at);
            name.resize(at);
        }

        if (auto const dot = name.find('.'); dot != std::string::npos) {
            name.resize(dot);
        }

        auto const subtags = split(name, '_');
        auto const language = std::string{ subtags.front() };
        if (std::empty(language) || language == "C"sv || language == "POSIX"sv) {
            break;
        }

        auto script = std::string_view{};
        auto territories = std::vector<std::string>{};
        for (auto const subtag : std::span{ subtags }.subspan(1U)) {
            if (std::size(subtag) == 4U) {
                script = subtag;
            } else if (!std::empty(subtag)) {
                territories.emplace_back(subtag);
            }
        }

        // Catalogs name Chinese scripts by territory, and Serbian's Latin script by modifier.
        if (language == "zh"sv && script == "Hans"sv) {
            territories.emplace_back("CN");
        } else if (language == "zh"sv && script == "Hant"sv) {
            territories.emplace_back("TW");
        } else if (language == "sr"sv && script == "Latn"sv && std::empty(modifier)) {
            modifier = "@latin";
        }

        // gettext's order: ll_CC@modifier, ll@modifier, ll_CC, ll
        if (!std::empty(modifier)) {
            for (auto const& territory : territories) {
                add(fmt::format("{:s}_{:s}{:s}", language, territory, modifier));
            }
            add(language + modifier);
        }

        for (auto const& territory : territories) {
            add(fmt::format("{:s}_{:s}", language, territory));
        }

        if (language == "en"sv) {
            break;
        }

        add(language);
    }

    return candidates;
}

} // namespace detail

// ---

std::vector<std::string> use_catalogs(
    std::span<std::string const> const dirs,
    std::string_view const domain,
    std::span<std::string const> const preferred_languages)
{
    auto catalogs = std::vector<detail::Catalog>{};
    auto languages = std::vector<std::string>{};

    for (auto const& language : detail::language_candidates(preferred_languages)) {
        for (auto const& dir : dirs) {
            auto const filename = tr_pathbuf{ dir, '/', language, "/LC_MESSAGES/"sv, domain, ".mo"sv };
            auto contents = std::vector<char>{};
            if (!tr_sys_path_exists(filename) || !tr_file_read(filename, contents)) {
                continue;
            }

            if (auto catalog = detail::Catalog::parse(std::move(contents)); catalog) {
                catalogs.push_back(*std::move(catalog));
                languages.push_back(language);
                break;
            }
        }
    }

    use_translations(std::move(catalogs));
    return languages;
}

} // namespace tr::app::l10n
