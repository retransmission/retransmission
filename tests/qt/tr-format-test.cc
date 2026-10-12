// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <utility>

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QString>
#include <QTest>

#include <fmt/format.h>

#include <libtransmission/utils.h>

#include "TrFormat.h"

namespace
{

// Canned translations, keyed by English text, or by context and English text as "context\x04text".
// The first form is for n == 1 and the second for every other n.
auto translations = QHash<QByteArray, QList<QByteArray>>{};

[[nodiscard]] char const* fakeGettext(char const* const msgid) noexcept
{
    auto const iter = translations.constFind(msgid);
    return iter != translations.cend() ? iter->front().constData() : msgid;
}

[[nodiscard]] char const* fakeNgettext(char const* const msgid, char const* const msgid_plural, uint64_t const n) noexcept
{
    auto const iter = translations.constFind(msgid);
    if (iter == translations.cend()) {
        return n == 1U ? msgid : msgid_plural;
    }

    return (n == 1U || iter->size() == 1 ? iter->front() : iter->back()).constData();
}

class TrFormatTest : public QObject
{
    Q_OBJECT

    static void translate(QByteArray const& source, QList<QByteArray> forms)
    {
        translations[source] = std::move(forms);
    }

private slots:
    void initTestCase()
    {
        tr_set_translator(fakeGettext, fakeNgettext);
    }

    void cleanupTestCase()
    {
        tr_set_translator(nullptr, nullptr);
    }

    void cleanup()
    {
        translations.clear();
        QLocale::setDefault(QLocale::c());
    }

    void translates_text()
    {
        QCOMPARE(TR_TEXT("Paused"), QStringLiteral("Paused"));

        translate("Paused", { "Pausiert" });
        QCOMPARE(TR_TEXT("Paused"), QStringLiteral("Pausiert"));
    }

    void translates_text_in_context()
    {
        translate("Seeding", { "Verteilt" });
        translate("Verb\x04Seeding", { "Verteilen" });

        QCOMPARE(TR_TEXT_C("Verb", "Seeding"), QStringLiteral("Verteilen"));
        QCOMPARE(TR_TEXT_C("Adjective", "Seeding"), QStringLiteral("Seeding"));
        QCOMPARE(TR_TEXT("Seeding"), QStringLiteral("Verteilt"));
    }

    void converts_mnemonics_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("expected");

        QTest::newRow("mnemonic") << QByteArray{ "_File" } << QStringLiteral("&File");
        QTest::newRow("mnemonic in a word") << QByteArray{ "Dese_lect All" } << QStringLiteral("Dese&lect All");
        QTest::newRow("literal ampersand") << QByteArray{ "Peers & _Seeds" } << QStringLiteral("Peers && &Seeds");
        QTest::newRow("literal underscore") << QByteArray{ "__init__ _Script" } << QStringLiteral("_init_ &Script");
        QTest::newRow("no mnemonic") << QByteArray{ "Statistics" } << QStringLiteral("Statistics");
        QTest::newRow("trailing underscore") << QByteArray{ "Name_" } << QStringLiteral("Name_");
        QTest::newRow("parenthesized") << QByteArray{ "ファイル(_F)" } << QStringLiteral("ファイル(&F)");
    }

    void converts_mnemonics()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, expected);

        translate("_File", { text });
        QCOMPARE(TR_MNEMONIC("_File"), expected);
    }

    void converts_english_mnemonics()
    {
        QCOMPARE(TR_MNEMONIC("Alternative Speed _Limits"), QStringLiteral("Alternative Speed &Limits"));
    }

    void translates_designer_text()
    {
        translate("Paused", { "Pausiert" });
        translate("_Add", { "_Hinzufügen" });
        translate("Verb\x04Seeding", { "Verteilen" });
        translate("Peers & Seeds", { "Peers & Seeds" });

        QCOMPARE(trqt::uiText("Paused", nullptr), QStringLiteral("Pausiert"));
        QCOMPARE(trqt::uiText("_Add", nullptr), QStringLiteral("&Hinzufügen"));
        QCOMPARE(trqt::uiText("_Edit", ""), QStringLiteral("&Edit"));
        QCOMPARE(trqt::uiText("Seeding", "Verb"), QStringLiteral("Verteilen"));

        // Text without a mnemonic keeps its "&", e.g. in a tooltip.
        QCOMPARE(trqt::uiText("Peers & Seeds", nullptr), QStringLiteral("Peers & Seeds"));
    }

    void formats_translation()
    {
        translate("Created by {creator} on {date}", { "Am {date} von {creator} erstellt" });

        auto const str = TR_FORMAT(
            "Created by {creator} on {date}",
            fmt::arg("creator", QStringLiteral("Mnemosaic")),
            fmt::arg("date", QStringLiteral("Tuesday")));
        QCOMPARE(str, QStringLiteral("Am Tuesday von Mnemosaic erstellt"));
    }

    // QString::arg() would replace the "%1" and "{date}" in the creator's name.
    void formats_arguments_verbatim()
    {
        auto const str = TR_FORMAT(
            "Created by {creator} on {date}",
            fmt::arg("creator", QStringLiteral("%1 {date} Ünïcode")),
            fmt::arg("date", QStringLiteral("Tuesday")));
        QCOMPARE(str, QStringLiteral("Created by %1 {date} Ünïcode on Tuesday"));
    }

    void formats_escaped_braces()
    {
        translate("{{{name}}} is {{set}}", { "{{{name}}} ist {{gesetzt}}" });

        QCOMPARE(TR_FORMAT("{{{name}}} is {{set}}", fmt::arg("name", "key")), QStringLiteral("{key} ist {gesetzt}"));
    }

    void falls_back_on_bad_translation_data()
    {
        QTest::addColumn<QByteArray>("translation");

        QTest::newRow("unknown field") << QByteArray{ "Erstellt von {author}" };
        QTest::newRow("positional field") << QByteArray{ "Erstellt von {0}" };
        QTest::newRow("automatic field") << QByteArray{ "Erstellt von {}" };
        QTest::newRow("unmatched open brace") << QByteArray{ "Erstellt von {creator" };
        QTest::newRow("unmatched close brace") << QByteArray{ "Erstellt von creator}" };
        QTest::newRow("nested field") << QByteArray{ "Erstellt von {creator:{width}}" };
        QTest::newRow("localized string") << QByteArray{ "Erstellt von {creator:L}" };
        QTest::newRow("spec not in source") << QByteArray{ "Erstellt von {creator:>20}" };
    }

    void falls_back_on_bad_translation()
    {
        QFETCH(QByteArray, translation);
        translate("Created by {creator}", { translation });

        QCOMPARE(TR_FORMAT("Created by {creator}", fmt::arg("creator", "Mnemosaic")), QStringLiteral("Created by Mnemosaic"));
    }

    void accepts_localized_number()
    {
        QLocale::setDefault(QLocale{ QLocale::German, QLocale::Germany });
        translate("{count} torrents", { "{count:L} Torrents" });

        QCOMPARE(TR_FORMAT("{count} torrents", fmt::arg("count", 12345)), QStringLiteral("12.345 Torrents"));
    }

    void shows_bad_source_unformatted()
    {
        QCOMPARE(TR_FORMAT("Created by {creator", fmt::arg("creator", "Mnemosaic")), QStringLiteral("Created by {creator"));
        QCOMPARE(TR_FORMAT("Created by {author}", fmt::arg("creator", "Mnemosaic")), QStringLiteral("Created by {author}"));
    }

    void picks_plural_form()
    {
        QLocale::setDefault(QLocale{ QLocale::German, QLocale::Germany });
        translate("{count:L} file", { "{count:L} Datei", "{count:L} Dateien" });

        for (auto const [count, expected] : { std::pair{ 1, "1 Datei" }, std::pair{ 1234, "1.234 Dateien" } }) {
            QCOMPARE(
                TR_FORMAT_N("{count:L} file", "{count:L} files", count, fmt::arg("count", count)),
                QString::fromUtf8(expected));
        }
    }

    void formats_english_plural_form()
    {
        QCOMPARE(
            TR_FORMAT_N("Remove torrent?", "Remove {count:L} torrents?", 1, fmt::arg("count", 1)),
            QStringLiteral("Remove torrent?"));
        QCOMPARE(
            TR_FORMAT_N("Remove torrent?", "Remove {count:L} torrents?", 1234, fmt::arg("count", 1234)),
            QStringLiteral("Remove 1234 torrents?"));
    }

    // A language's form for one can cover other counts, so it can show the count that its English lacks.
    void accepts_count_in_singular_form()
    {
        translate("Remove torrent?", { "Удалить {count:L} торрент?", "Удалить {count:L} торрентов?" });

        QCOMPARE(
            TR_FORMAT_N("Remove torrent?", "Remove {count:L} torrents?", 21, fmt::arg("count", 21)),
            QStringLiteral("Удалить 21 торрентов?"));
        QCOMPARE(
            TR_FORMAT_N("Remove torrent?", "Remove {count:L} torrents?", 1, fmt::arg("count", 1)),
            QStringLiteral("Удалить 1 торрент?"));
    }

    void formats_numbers_like_qlocale_data()
    {
        QTest::addColumn<QLocale>("locale");

        QTest::newRow("de_DE") << QLocale{ QLocale::German, QLocale::Germany };
        QTest::newRow("fr_FR, multi-byte separator") << QLocale{ QLocale::French, QLocale::France };
        QTest::newRow("hi_IN, 3;2 grouping") << QLocale{ QLocale::Hindi, QLocale::India };
        QTest::newRow("ar_EG, native digits") << QLocale{ QLocale::Arabic, QLocale::Egypt };
        QTest::newRow("es_ES, no grouping below 10,000") << QLocale{ QLocale::Spanish, QLocale::Spain };
    }

    void formats_numbers_like_qlocale()
    {
        QFETCH(QLocale, locale);
        QLocale::setDefault(locale);

        for (auto const value : { int64_t{ 1234 }, int64_t{ 1234567 }, int64_t{ -1234567 } }) {
            QCOMPARE(TR_FORMAT("{count:L}", fmt::arg("count", value)), locale.toString(static_cast<qlonglong>(value)));
        }

        auto constexpr Big = UINT64_MAX;
        QCOMPARE(TR_FORMAT("{count:L}", fmt::arg("count", Big)), locale.toString(static_cast<qulonglong>(Big)));
        QCOMPARE(TR_FORMAT("{ratio:.2Lf}", fmt::arg("ratio", 1234.5)), locale.toString(1234.5, 'f', 2));
    }

    void formats_numbers_for_the_current_default_locale()
    {
        QLocale::setDefault(QLocale{ QLocale::German, QLocale::Germany });
        QCOMPARE(TR_FORMAT("{count:L}", fmt::arg("count", 1234567)), QStringLiteral("1.234.567"));

        QLocale::setDefault(QLocale{ QLocale::English, QLocale::UnitedStates });
        QCOMPARE(TR_FORMAT("{count:L}", fmt::arg("count", 1234567)), QStringLiteral("1,234,567"));

        QCOMPARE(TR_FORMAT("{count}", fmt::arg("count", 1234567)), QStringLiteral("1234567"));
    }

    void splits_at_field()
    {
        auto const* const source = "{minutes:L} minute(s) ago";

        QCOMPARE(
            trqt::splitAtField("vor {minutes} Minuten", source, "minutes"),
            std::pair(QStringLiteral("vor "), QStringLiteral(" Minuten")));
        QCOMPARE(trqt::splitAtField("{{{minutes:L}}}", source, "minutes"), std::pair(QStringLiteral("{"), QStringLiteral("}")));
        QCOMPARE(
            trqt::splitAtField("vor {count} Minuten", source, "minutes"),
            std::pair(QString{}, QStringLiteral(" minute(s) ago")));
        QCOMPARE(
            trqt::splitAtField("vor {minutes} {minutes} Minuten", source, "minutes"),
            std::pair(QString{}, QStringLiteral(" minute(s) ago")));
    }
};

} // namespace

int main(int argc, char** argv)
{
    auto const app = QCoreApplication{ argc, argv };
    auto test = TrFormatTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "tr-format-test.moc"
