// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <cstdint> // int64_t, uint32_t, uint64_t
#include <locale>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/file.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>

#include "libtransmission-app/l10n.h"

#include "test-fixtures.h"

using namespace std::literals;
using namespace tr::app::l10n;
using namespace tr::app::l10n::detail;

namespace
{

struct Message {
    std::string_view original; // msgid, or msgid and msgid_plural separated by '\0'
    std::string_view translation; // msgstr, or each msgstr[n] separated by '\0'
};

// Builds the contents of a .mo file, as msgfmt writes it.
[[nodiscard]] std::vector<char> make_mo(std::vector<Message> const& messages, bool const swap_bytes = false)
{
    auto const put_u32 = [swap_bytes](std::vector<char>& out, uint32_t val) {
        if (swap_bytes) {
            val = ((val & 0xFFU) << 24U) | ((val & 0xFF00U) << 8U) | ((val >> 8U) & 0xFF00U) | (val >> 24U);
        }
        for (auto i = 0U; i < 4U; ++i) {
            out.push_back(static_cast<char>((val >> (8U * i)) & 0xFFU));
        }
    };

    auto const n_messages = static_cast<uint32_t>(std::size(messages));
    auto const originals = uint32_t{ 28U };
    auto const translations = originals + (8U * n_messages);
    auto offset = translations + (8U * n_messages);

    auto out = std::vector<char>{};
    put_u32(out, 0x950412DEU);
    put_u32(out, 0U); // revision
    put_u32(out, n_messages);
    put_u32(out, originals);
    put_u32(out, translations);
    put_u32(out, 0U); // hash table size
    put_u32(out, 0U); // hash table offset

    auto strings = std::string{};
    for (auto const member : { &Message::original, &Message::translation }) {
        for (auto const& message : messages) {
            auto const str = message.*member;
            put_u32(out, static_cast<uint32_t>(std::size(str)));
            put_u32(out, offset);
            strings += str;
            strings += '\0';
            offset += static_cast<uint32_t>(std::size(str) + 1U);
        }
    }

    out.insert(std::end(out), std::begin(strings), std::end(strings));
    return out;
}

auto constexpr RussianHeader = Message{
    ""sv,
    "Content-Type: text/plain; charset=UTF-8\n"
    "Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\n"sv
};

} // namespace

using L10nTest = SandboxedTest;

TEST_F(L10nTest, parseFields)
{
    auto fields = parse_fields("{count:L} of {total} {{literal}}"sv);
    ASSERT_TRUE(fields);
    ASSERT_EQ(2U, std::size(*fields));
    EXPECT_EQ("count"sv, (*fields)[0].name);
    EXPECT_EQ("L"sv, (*fields)[0].spec);
    EXPECT_EQ("total"sv, (*fields)[1].name);
    EXPECT_EQ(""sv, (*fields)[1].spec);

    fields = parse_fields("no fields"sv);
    ASSERT_TRUE(fields);
    EXPECT_TRUE(std::empty(*fields));

    EXPECT_FALSE(parse_fields("{}"sv));
    EXPECT_FALSE(parse_fields("{0}"sv));
    EXPECT_FALSE(parse_fields("{count"sv));
    EXPECT_FALSE(parse_fields("count}"sv));
    EXPECT_FALSE(parse_fields("{a{b}}"sv));
}

TEST_F(L10nTest, formatsTranslation)
{
    auto const loc = std::locale::classic();
    auto const* const source = "Created by {creator} on {date}";
    auto const args = std::array{ Arg{ "creator", "Mnemosaic"s }, Arg{ "date", "Tuesday"s } };

    EXPECT_EQ("Created by Mnemosaic on Tuesday"sv, format_translation(loc, source, args));
    EXPECT_EQ("Am Tuesday von Mnemosaic erstellt"sv, format_translation(loc, "Am {date} von {creator} erstellt", args));

    // Arguments are formatted verbatim, braces and all.
    auto const braces = std::array{ Arg{ "creator", "{date}"s }, Arg{ "date", "}{"s } };
    EXPECT_EQ("Created by {date} on }{"sv, format_translation(loc, source, braces));
}

TEST_F(L10nTest, formatsNumbers)
{
    auto const loc = std::locale::classic();
    auto const* const source = "{count:L} of {total}, {ratio:.2f}";
    auto const args = std::array{
        Arg{ "count", int64_t{ -12345 } },
        Arg{ "total", uint64_t{ 18446744073709551615ULL } },
        Arg{ "ratio", 0.5 },
    };

    EXPECT_EQ("-12345 of 18446744073709551615, 0.50"sv, format_translation(loc, source, args));
}

TEST_F(L10nTest, englishPluralForms)
{
    auto const plurals = PluralForms{};
    EXPECT_EQ(1U, plurals.index(0U));
    EXPECT_EQ(0U, plurals.index(1U));
    EXPECT_EQ(1U, plurals.index(2U));
}

TEST_F(L10nTest, pluralFormsMatchGettext)
{
    // The expected forms are from Python's gettext.c2py(),
    // which evaluates formulas as C does.
    static auto constexpr Counts = std::array<uint64_t, 17U>{ 0,  1,  2,  3,  4,   5,   7,   11,     12,
                                                              14, 21, 22, 25, 101, 102, 111, 1000000 };
    static auto constexpr Formulas = std::array<std::pair<std::string_view, std::array<size_t, std::size(Counts)>>, 6U>{ {
        { "nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);"sv,
          { 2, 0, 1, 1, 1, 2, 2, 2, 2, 2, 0, 1, 2, 0, 1, 2, 2 } },
        { "nplurals=6; plural=n==0 ? 0 : n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 : n%100>=11 && n%100<=99 ? 4 : 5;"sv,
          { 0, 1, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 4, 5 } },
        { "nplurals=5; plural=(n==1 ? 0 : n==2 ? 1 : n<7 ? 2 : n<11 ? 3 : 4);"sv,
          { 2, 0, 1, 2, 2, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 } },
        { "nplurals=4; plural=(n==1) ? 0 : (n==2) ? 1 : (n != 8 && n != 11) ? 2 : 3;"sv,
          { 2, 0, 1, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2 } },
        { "nplurals=2; plural=(n > 1);"sv, { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 } },
        { "nplurals=3; plural=n == 1 ? 0 : n != 0 && n % 1000000 == 0 ? 1 : 2;"sv,
          { 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1 } },
    } };

    for (auto const& [formula, expected] : Formulas) {
        auto const plurals = PluralForms::parse(formula);
        ASSERT_TRUE(plurals) << formula;
        for (size_t i = 0; i < std::size(Counts); ++i) {
            EXPECT_EQ(expected[i], plurals->index(Counts[i])) << formula << " n=" << Counts[i];
        }
    }
}

TEST_F(L10nTest, pluralFormsOutOfRange)
{
    // A form past the last picks form 0, and division by zero yields 0.
    auto plurals = PluralForms::parse("nplurals=2; plural=n;"sv);
    ASSERT_TRUE(plurals);
    EXPECT_EQ(1U, plurals->index(1U));
    EXPECT_EQ(0U, plurals->index(2U));

    plurals = PluralForms::parse("nplurals=2; plural=1 / (n - 1);"sv);
    ASSERT_TRUE(plurals);
    EXPECT_EQ(0U, plurals->index(1U));
    EXPECT_EQ(1U, plurals->index(2U));

    // C skips the division at 0, and evaluating it anyway must give the same form.
    plurals = PluralForms::parse("nplurals=3; plural=n==0 ? 0 : 10/n > 2 ? 1 : 2;"sv);
    ASSERT_TRUE(plurals);
    EXPECT_EQ(0U, plurals->index(0U));
    EXPECT_EQ(1U, plurals->index(1U));
    EXPECT_EQ(2U, plurals->index(5U));
}

TEST_F(L10nTest, pluralFormsRejectsBadFormulas)
{
    for (
        auto const formula : {
            "plural=(n != 1);"sv,
            "nplurals=2;"sv,
            "nplurals=0; plural=0;"sv,
            "nplurals=x; plural=0;"sv,
            "nplurals=2; plural=(n != 1;"sv,
            "nplurals=2; plural=n !;"sv,
            "nplurals=2; plural=n ? 1;"sv,
            "nplurals=2; plural=m;"sv,
            "nplurals=2; plural=n = 1;"sv,
            "nplurals=2; plural=99999999999999999999 > n;"sv,
        }) {
        EXPECT_FALSE(PluralForms::parse(formula)) << formula;
    }

    // Nesting deep enough to exhaust the stack is rejected, not evaluated.
    auto const deep = fmt::format("nplurals=2; plural={:s}n{:s};", std::string(10000U, '('), std::string(10000U, ')'));
    EXPECT_FALSE(PluralForms::parse(deep));
    EXPECT_FALSE(PluralForms::parse(fmt::format("nplurals=2; plural={:s}n;", std::string(10000U, '!'))));
}

TEST_F(L10nTest, catalogLookups)
{
    for (auto const swap_bytes : { false, true }) {
        auto const catalog = Catalog::parse(make_mo(
            {
                RussianHeader,
                { "Paused"sv, "Пауза"sv },
                { "{count:L} file\0{count:L} files"sv, "{count:L} файл\0{count:L} файла\0{count:L} файлов"sv },
            },
            swap_bytes));
        ASSERT_TRUE(catalog);
        EXPECT_EQ(2U, catalog->size());

        EXPECT_STREQ("Пауза", catalog->gettext("Paused"sv));
        EXPECT_EQ(nullptr, catalog->gettext("Verifying"sv));

        EXPECT_STREQ("{count:L} файл", catalog->ngettext("{count:L} file"sv, 21U));
        EXPECT_STREQ("{count:L} файла", catalog->ngettext("{count:L} file"sv, 3U));
        EXPECT_STREQ("{count:L} файлов", catalog->ngettext("{count:L} file"sv, 11U));
        EXPECT_EQ(nullptr, catalog->ngettext("{count:L} torrent"sv, 1U));
    }
}

TEST_F(L10nTest, catalogDropsTranslationsThatDontFit)
{
    auto const catalog = Catalog::parse(make_mo(
        {
            { "{name} is done"sv, "{name} ist fertig"sv },
            { "{name} is paused"sv, "{nombre} ist pausiert"sv }, // a field the English lacks
            { "{name} is verifying"sv, "{name ist in Prüfung"sv }, // an unmatched brace
            { "{count} peer\0{count} peers"sv, "{count:L} Peer\0{count:L} Peers"sv }, // a spec the English lacks
            { "{count:L} seed\0{count:L} seeds"sv, "Ein Seed\0{count} Seeds"sv }, // dropping the field or its spec is fine
            { "{count:L} piece\0{count:L} pieces"sv, "{count:L} Stück\0"sv }, // an empty form
            { "Done"sv, "{Fertig}"sv }, // a field in a message without any
        }));
    ASSERT_TRUE(catalog);

    EXPECT_STREQ("{name} ist fertig", catalog->gettext("{name} is done"sv));
    EXPECT_EQ(nullptr, catalog->gettext("{name} is paused"sv));
    EXPECT_EQ(nullptr, catalog->gettext("{name} is verifying"sv));
    EXPECT_EQ(nullptr, catalog->ngettext("{count} peer"sv, 2U));
    EXPECT_STREQ("Ein Seed", catalog->ngettext("{count:L} seed"sv, 1U));
    EXPECT_STREQ("{count} Seeds", catalog->ngettext("{count:L} seed"sv, 2U));
    EXPECT_EQ(nullptr, catalog->ngettext("{count:L} piece"sv, 1U));
    EXPECT_EQ(nullptr, catalog->gettext("Done"sv));
}

TEST_F(L10nTest, catalogRejectsBadFiles)
{
    auto const good = make_mo({ { "Paused"sv, "Pausiert"sv } });
    ASSERT_TRUE(Catalog::parse(good));

    EXPECT_FALSE(Catalog::parse({}));

    auto bad_magic = good;
    bad_magic[0] = 0;
    EXPECT_FALSE(Catalog::parse(bad_magic));

    auto bad_revision = good;
    bad_revision[6] = 2; // major revision 2
    EXPECT_FALSE(Catalog::parse(bad_revision));

    // Each prefix cuts off a table or a string.
    for (size_t size = 0; size < std::size(good); ++size) {
        EXPECT_FALSE(Catalog::parse(std::vector<char>(std::begin(good), std::begin(good) + static_cast<std::ptrdiff_t>(size))))
            << size;
    }
}

TEST_F(L10nTest, languageCandidates)
{
    auto const candidates = [](std::vector<std::string> const& preferred) {
        return language_candidates(preferred);
    };

    using Names = std::vector<std::string>;
    EXPECT_EQ((Names{ "pt_BR", "pt" }), candidates({ "pt-BR" }));
    EXPECT_EQ((Names{ "de_DE", "de" }), candidates({ "de_DE.UTF-8" }));
    EXPECT_EQ((Names{ "ca_ES@valencia", "ca@valencia", "ca_ES", "ca" }), candidates({ "ca_ES.UTF-8@valencia" }));
    EXPECT_EQ((Names{ "sr_RS@latin", "sr@latin", "sr_RS", "sr" }), candidates({ "sr-Latn-RS" }));
    EXPECT_EQ((Names{ "zh_CN", "zh" }), candidates({ "zh-Hans-CN", "zh-CN", "zh-Hans", "zh" }));
    EXPECT_EQ((Names{ "zh_HK", "zh_TW", "zh" }), candidates({ "zh-Hant-HK" }));
    EXPECT_EQ((Names{ "pt_BR", "pt", "de" }), candidates({ "pt-BR", "de" }));

    // English ends the list, since its text is the catalogs' source.
    EXPECT_EQ((Names{ "en_GB" }), candidates({ "en-GB", "de" }));
    EXPECT_EQ((Names{}), candidates({ "en", "de" }));
    EXPECT_EQ((Names{}), candidates({ "C" }));
    EXPECT_EQ((Names{}), candidates({ "POSIX", "de" }));
    EXPECT_EQ((Names{ "de" }), candidates({ "de", "C" }));
}

TEST_F(L10nTest, useCatalogs)
{
    auto const write_mo =
        [this](std::string_view const dir, std::string_view const language, std::vector<Message> const& messages) {
            auto const path = fmt::format("{:s}/{:s}/{:s}/LC_MESSAGES", sandbox_dir(), dir, language);
            ASSERT_TRUE(tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0700));
            ASSERT_TRUE(tr_file_save(fmt::format("{:s}/domain.mo", path), make_mo(messages)));
        };

    write_mo("a"sv, "pt_BR"sv, { { "Paused"sv, "Pausado (pt_BR)"sv }, { "Verb\x04Seeding"sv, "Semeando"sv } });
    write_mo("b"sv, "pt_BR"sv, { { "Paused"sv, "Pausado (b)"sv } });
    write_mo(
        "b"sv,
        "pt"sv,
        { { "Paused"sv, "Pausado (pt)"sv }, { "Verifying"sv, "A verificar"sv }, { "kB/s"sv, "kB/s (pt)"sv } });

    auto const dirs = std::vector<std::string>{ fmt::format("{:s}/a", sandbox_dir()), fmt::format("{:s}/b", sandbox_dir()) };
    auto const languages = use_catalogs(dirs, "domain"sv, std::vector<std::string>{ "pt-BR", "fr" });
    EXPECT_EQ((std::vector<std::string>{ "pt_BR", "pt" }), languages);

    // Each message comes from the most preferred catalog that has it, else it stays English.
    EXPECT_STREQ("Pausado (pt_BR)", _("Paused"));
    EXPECT_STREQ("A verificar", _("Verifying"));
    EXPECT_STREQ("Seeding", _("Seeding"));
    EXPECT_STREQ("{count} files", tr_ngettext("{count} file", "{count} files", 2));
    EXPECT_STREQ("Semeando", tr_pgettext("Verb", "Seeding"));
    EXPECT_EQ("10 kB/s (pt)", (tr::Values::Speed{ 10, tr::Values::Speed::Units::KByps }.to_string()));

    // Without a catalog, everything is English.
    EXPECT_TRUE(std::empty(use_catalogs(dirs, "domain"sv, std::vector<std::string>{ "de" })));
    EXPECT_STREQ("Paused", _("Paused"));
    EXPECT_EQ("10 kB/s", (tr::Values::Speed{ 10, tr::Values::Speed::Units::KByps }.to_string()));
}

TEST_F(L10nTest, stripMnemonic)
{
    static constexpr auto Tests = std::array<std::pair<std::string_view, std::string_view>, 13U>{ {
        { ""sv, ""sv },
        { "Paused"sv, "Paused"sv },
        { "_File"sv, "File"sv },
        { "Open _URL…"sv, "Open URL…"sv },
        { "Pass_word:"sv, "Password:"sv },
        { "Append \"._part\" to incomplete files' names"sv, "Append \".part\" to incomplete files' names"sv },
        { "snake__case"sv, "snake_case"sv },
        { "trailing_"sv, "trailing_"sv },
        { "ファイル(_F)"sv, "ファイル"sv },
        { "開く(_O)…"sv, "開く…"sv },
        { "文件 (_F)"sv, "文件"sv },
        { "場所（_L）:"sv, "場所:"sv },
        { "Size (_bytes)"sv, "Size (bytes)"sv },
    } };

    for (auto const& [text, expected] : Tests) {
        EXPECT_EQ(expected, strip_mnemonic(text)) << text;
    }
}

TEST_F(L10nTest, useCatalogFiles)
{
    auto const write_mo = [this](std::string_view const name, std::vector<Message> const& messages) {
        auto filename = fmt::format("{:s}/{:s}", sandbox_dir(), name);
        EXPECT_TRUE(tr_file_save(filename, make_mo(messages)));
        return filename;
    };

    auto const filenames = std::vector<std::string>{
        write_mo("pt-BR.mo"sv, { { "Paused"sv, "Pausado (pt-BR)"sv } }),
        fmt::format("{:s}/missing.mo", sandbox_dir()),
        write_mo("pt.mo"sv, { { "Paused"sv, "Pausado (pt)"sv }, { "Verifying"sv, "A verificar"sv } }),
    };
    EXPECT_EQ(2U, use_catalog_files(filenames));

    // Each message comes from the most preferred catalog that has it, else it stays English.
    EXPECT_STREQ("Pausado (pt-BR)", _("Paused"));
    EXPECT_STREQ("A verificar", _("Verifying"));
    EXPECT_STREQ("Seeding", _("Seeding"));

    // Without a catalog, everything is English.
    EXPECT_EQ(0U, use_catalog_files({}));
    EXPECT_STREQ("Paused", _("Paused"));
}
