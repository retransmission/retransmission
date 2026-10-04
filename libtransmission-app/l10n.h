// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <cstdint> // int64_t, uint64_t
#include <locale>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// Translations from compiled gettext catalogs (.mo files),
// for clients that don't use libintl.
namespace tr::app::l10n
{

// A replacement field in a {fmt} format string, e.g. {count:L}.
struct Field {
    std::string_view name;
    std::string_view spec;
};

// Returns the replacement fields in `text`,
// or nullopt if it has a brace that isn't an escaped brace or a named field.
// {fmt} accepts more, e.g. positional fields,
// but translations have no use for them.
[[nodiscard]] std::optional<std::vector<Field>> parse_fields(std::string_view text);

// A named argument for format_translation(): text or a number.
struct Arg {
    char const* name;
    std::variant<std::string, int64_t, uint64_t, double> value;
};

// Formats `translation` with {fmt} named arguments, taking "L" fields' number formatting from `locale`.
// It is for code that can't call {fmt}'s templates, such as Objective-C and Swift.
// {fmt} is built without exceptions and aborts on a format string that doesn't fit its arguments;
// a catalog holds no such translation, because Catalog::parse() drops them.
[[nodiscard]] std::string format_translation(std::locale const& locale, char const* translation, std::span<Arg const> args);

// Returns `text` without the catalog's mnemonic markers, for a client whose controls have no mnemonics:
// "_File" becomes "File", and "__" becomes "_".
// Chinese, Japanese and Korean translations mark a Latin letter in parentheses after the text,
// which goes away whole: "ファイル(_F)" becomes "ファイル". Its parentheses may be ASCII or full-width.
[[nodiscard]] std::string strip_mnemonic(std::string_view text);

// Loads `<dir>/<language>/LC_MESSAGES/<domain>.mo` for each candidate language,
// from the first of `dirs` that has it,
// and sets libtransmission's translator to look up each message
// in the most preferred catalog that has it, else to show English.
// Returns the languages whose catalogs it loaded.
std::vector<std::string> use_catalogs(
    std::span<std::string const> dirs,
    std::string_view domain,
    std::span<std::string const> preferred_languages);

// Like use_catalogs(), for a client that keeps its catalogs somewhere else,
// such as the language folders of a macOS bundle.
// Loads each file that is a catalog, most preferred first, and returns how many it loaded.
size_t use_catalog_files(std::span<std::string const> filenames);

// The parts of use_catalogs(), public for tests.
// Clients call use_catalogs() instead.
namespace detail
{

// Picks a message's plural form for a count, with a catalog's Plural-Forms formula.
class PluralForms
{
public:
    // English's rule: form 0 for 1, form 1 for every other count.
    PluralForms() = default;

    // Parses a Plural-Forms value, e.g. "nplurals=2; plural=(n != 1);".
    [[nodiscard]] static std::optional<PluralForms> parse(std::string_view plural_forms);

    // Returns the form for `n`, or form 0 if the formula picks one past the last.
    // Division and modulo by zero yield 0.
    [[nodiscard]] size_t index(uint64_t n) const noexcept;

private:
    std::string formula_; // empty for English's rule
    size_t n_forms_ = 2U;
};

// One compiled gettext catalog, i.e. the contents of a .mo file.
class Catalog
{
public:
    // A copy's index would point into the original's contents, so a catalog moves but doesn't copy.
    Catalog(Catalog const&) = delete;
    Catalog& operator=(Catalog const&) = delete;
    Catalog(Catalog&&) = default;
    Catalog& operator=(Catalog&&) = default;
    ~Catalog() = default;

    // Parses a .mo file's contents.
    // Drops each translation whose {fmt} fields don't fit its English text,
    // because {fmt} is built without exceptions and aborts on a bad format string.
    // A translation fits if it parses and uses only fields that its English uses,
    // each with no spec or with a spec that its English uses for that field.
    [[nodiscard]] static std::optional<Catalog> parse(std::vector<char> mo_contents);

    // Returns the translation of `msgid`, or nullptr if the catalog has none.
    [[nodiscard]] char const* gettext(std::string_view msgid) const noexcept;

    // Returns the plural form for `n` of `msgid`'s translation, or nullptr if the catalog has none.
    [[nodiscard]] char const* ngettext(std::string_view msgid, uint64_t n) const noexcept;

    [[nodiscard]] size_t size() const noexcept
    {
        return std::size(translations_);
    }

private:
    Catalog() = default;

    std::vector<char> contents_;
    PluralForms plural_forms_;

    // A message's English singular, and its translated forms separated by '\0'.
    // Both point into `contents_`.
    std::unordered_map<std::string_view, std::string_view> translations_;
};

// Returns the catalog names to look for, most preferred first,
// given a list of languages in the order the user prefers them,
// e.g. { "pt-BR", "de_DE.UTF-8" } -> { "pt_BR", "pt", "de_DE", "de" }.
// English, "C" or "POSIX" ends the list,
// since the English text is preferred over every language after it.
[[nodiscard]] std::vector<std::string> language_candidates(std::span<std::string const> preferred_languages);

} // namespace detail

} // namespace tr::app::l10n
