// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <map>
#include <utility>

#include <QCoreApplication>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTranslator>

#include <fmt/format.h>

#include "TrFormat.h"

namespace
{

// Serves canned translations, keyed by source text, in every context.
// The first form is for n == 1 and the second for every other n.
class FakeTranslator final : public QTranslator
{
public:
    std::map<QString, QStringList> translations;

    [[nodiscard]] bool isEmpty() const override
    {
        return translations.empty();
    }

    [[nodiscard]] QString translate(
        char const* /*context*/,
        char const* source_text,
        char const* /*disambiguation*/,
        int const n) const override
    {
        auto const iter = translations.find(QString::fromUtf8(source_text));
        if (iter == translations.end()) {
            return {};
        }

        auto const& forms = iter->second;
        return forms.value(n == 1 || forms.size() == 1 ? 0 : 1);
    }
};

class TrFormatTest : public QObject
{
    Q_OBJECT

    FakeTranslator translator_;

    void translate(QString const& source, QStringList forms)
    {
        translator_.translations[source] = std::move(forms);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::installTranslator(&translator_);
    }

    void cleanup()
    {
        translator_.translations.clear();
        QLocale::setDefault(QLocale::c());
    }

    void formats_translation()
    {
        translate(QStringLiteral("Created by {creator} on {date}"), { QStringLiteral("Am {date} von {creator} erstellt") });

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
        translate(QStringLiteral("{{{name}}} is {{set}}"), { QStringLiteral("{{{name}}} ist {{gesetzt}}") });

        QCOMPARE(TR_FORMAT("{{{name}}} is {{set}}", fmt::arg("name", "key")), QStringLiteral("{key} ist {gesetzt}"));
    }

    void falls_back_on_bad_translation_data()
    {
        QTest::addColumn<QString>("translation");

        QTest::newRow("unknown field") << QStringLiteral("Erstellt von {author}");
        QTest::newRow("positional field") << QStringLiteral("Erstellt von {0}");
        QTest::newRow("automatic field") << QStringLiteral("Erstellt von {}");
        QTest::newRow("unmatched open brace") << QStringLiteral("Erstellt von {creator");
        QTest::newRow("unmatched close brace") << QStringLiteral("Erstellt von creator}");
        QTest::newRow("nested field") << QStringLiteral("Erstellt von {creator:{width}}");
        QTest::newRow("localized string") << QStringLiteral("Erstellt von {creator:L}");
        QTest::newRow("spec not in source") << QStringLiteral("Erstellt von {creator:>20}");
    }

    void falls_back_on_bad_translation()
    {
        QFETCH(QString, translation);
        translate(QStringLiteral("Created by {creator}"), { translation });

        QCOMPARE(TR_FORMAT("Created by {creator}", fmt::arg("creator", "Mnemosaic")), QStringLiteral("Created by Mnemosaic"));
    }

    void accepts_localized_number()
    {
        QLocale::setDefault(QLocale{ QLocale::German, QLocale::Germany });
        translate(QStringLiteral("{count} torrents"), { QStringLiteral("{count:L} Torrents") });

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
        translate(
            QStringLiteral("{count:L} file(s)"),
            { QStringLiteral("{count:L} Datei"), QStringLiteral("{count:L} Dateien") });

        for (auto const [count, expected] : { std::pair{ 1, "1 Datei" }, std::pair{ 1234, "1.234 Dateien" } }) {
            QCOMPARE(TR_FORMAT_N("{count:L} file(s)", count, fmt::arg("count", count)), QString::fromUtf8(expected));
        }
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
            trqt::splitAtField(QStringLiteral("vor {minutes} Minuten"), source, "minutes"),
            std::pair(QStringLiteral("vor "), QStringLiteral(" Minuten")));
        QCOMPARE(
            trqt::splitAtField(QStringLiteral("{{{minutes:L}}}"), source, "minutes"),
            std::pair(QStringLiteral("{"), QStringLiteral("}")));
        QCOMPARE(
            trqt::splitAtField(QStringLiteral("vor {count} Minuten"), source, "minutes"),
            std::pair(QString{}, QStringLiteral(" minute(s) ago")));
        QCOMPARE(
            trqt::splitAtField(QStringLiteral("vor {minutes} {minutes} Minuten"), source, "minutes"),
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
