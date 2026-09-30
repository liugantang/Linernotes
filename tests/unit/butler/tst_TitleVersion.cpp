// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/ArtistName.h>
#include <butler/TitleVersion.h>
#include <library/EnumNames.h>
#include <library/LibraryEnums.h>

#include <array>
#include <optional>

namespace {

using linernotes::butler::classifySuffix;
using linernotes::butler::exactKey;
using linernotes::butler::resolveTitle;
using linernotes::butler::splitSuffixes;
using linernotes::butler::SuffixClass;
using linernotes::butler::suffixKey;
using linernotes::butler::SuffixRole;
using linernotes::butler::TitleSuffix;
using linernotes::butler::TitleVersion;
using linernotes::library::VersionType;
using linernotes::library::versionTypeFromString;
using linernotes::library::versionTypeToString;

class TstTitleVersion : public QObject {
    Q_OBJECT

private slots:
    void splitSuffixesTest_data();
    void splitSuffixesTest();

    void classifySuffixTest_data();
    void classifySuffixTest();

    void resolveTitleTest();
    void versionTypeRoundtrip();
    void suffixKeyTest();
};

void TstTitleVersion::splitSuffixesTest_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QStringList>("expectedTexts");

    QTest::newRow("Extended Mix") << QStringLiteral("Snow halation (Extended Mix)")
                                  << QStringList { QStringLiteral("Extended Mix") };

    QTest::newRow("Multiple suffixes")
        << QStringLiteral("Song (Live) [2011 Remaster]")
        << QStringList { QStringLiteral("2011 Remaster"), QStringLiteral("Live") };

    QTest::newRow("Dash Instrumental") << QStringLiteral("Wake up! - Instrumental")
                                       << QStringList { QStringLiteral("Instrumental") };

    QTest::newRow("Dash without spaces (Re-Boot)") << QStringLiteral("Re-Boot") << QStringList { };

    QTest::newRow("Tilde suffix") << QStringLiteral("Every Best Single 2 ～Early period～")
                                  << QStringList { QStringLiteral("Early period") };

    QTest::newRow("Whole title is brackets") << QStringLiteral("(Intro)") << QStringList { };

    QTest::newRow("Fullwidth brackets") << QStringLiteral("极上スマイル（I-1club Ver.）")
                                        << QStringList { QStringLiteral("I-1club Ver.") };
}

void TstTitleVersion::splitSuffixesTest()
{
    QFETCH(QString, title);
    QFETCH(QStringList, expectedTexts);

    const QList<TitleSuffix> suffixes = splitSuffixes(title);
    QStringList actualTexts;
    actualTexts.reserve(suffixes.size());
    for (const auto &s : suffixes) {
        actualTexts.append(s.text);
    }

    QCOMPARE(actualTexts, expectedTexts);
}

void TstTitleVersion::classifySuffixTest_data()
{
    QTest::addColumn<QString>("suffixText");
    QTest::addColumn<int>("expectedRole");
    QTest::addColumn<int>("expectedType");

    auto addRow = [](const char *tag, const QString &text, SuffixRole role, VersionType type) {
        QTest::newRow(tag) << text << static_cast<int>(role) << static_cast<int>(type);
    };

    addRow("Extended Mix", QStringLiteral("Extended Mix"), SuffixRole::Version, VersionType::Remix);
    addRow("Instrumental", QStringLiteral("Instrumental"), SuffixRole::Version,
        VersionType::Instrumental);
    addRow(
        "off vocal", QStringLiteral("off vocal"), SuffixRole::Version, VersionType::Instrumental);
    addRow("オリジナル・カラオケ", QStringLiteral("オリジナル・カラオケ"), SuffixRole::Version,
        VersionType::Instrumental);
    addRow("TVサイズ", QStringLiteral("TVサイズ"), SuffixRole::Version, VersionType::Edit);
    addRow("TV size", QStringLiteral("TV size"), SuffixRole::Version, VersionType::Edit);
    addRow("Live at PACIFICO YOKOHAMA 2014/9/29",
        QStringLiteral("Live at PACIFICO YOKOHAMA 2014/9/29"), SuffixRole::Version,
        VersionType::Live);
    addRow("LIVE#10", QStringLiteral("LIVE#10"), SuffixRole::Version, VersionType::Live);
    addRow("2014武道館 LIVE ICHIGO ver.", QStringLiteral("2014武道館 LIVE ICHIGO ver."),
        SuffixRole::Version, VersionType::Live);
    addRow("Acoustic ver.", QStringLiteral("Acoustic ver."), SuffixRole::Version,
        VersionType::Acoustic);
    addRow("Piano", QStringLiteral("Piano"), SuffixRole::Version, VersionType::Acoustic);
    addRow("2011 Remaster", QStringLiteral("2011 Remaster"), SuffixRole::Version,
        VersionType::Remaster);
    addRow("English ver.", QStringLiteral("English ver."), SuffixRole::Version,
        VersionType::Alternate);
    addRow("album version", QStringLiteral("album version"), SuffixRole::Version,
        VersionType::Alternate);
    addRow("feat. 初音ミク", QStringLiteral("feat. 初音ミク"), SuffixRole::Annotation,
        VersionType::Studio);
    addRow(
        "Bonus Track", QStringLiteral("Bonus Track"), SuffixRole::Annotation, VersionType::Studio);
    addRow(
        "96kHz/24bit", QStringLiteral("96kHz/24bit"), SuffixRole::Annotation, VersionType::Studio);
    addRow(
        "CV:高木美佑", QStringLiteral("CV:高木美佑"), SuffixRole::Annotation, VersionType::Studio);
    addRow("Elite", QStringLiteral("Elite"), SuffixRole::Unknown, VersionType::Studio);
    addRow("Forget about my love", QStringLiteral("Forget about my love"), SuffixRole::Unknown,
        VersionType::Studio);
    addRow("Maki Mix", QStringLiteral("Maki Mix"), SuffixRole::Version, VersionType::Remix);
    addRow("Eurobeat Remix", QStringLiteral("Eurobeat Remix"), SuffixRole::Version,
        VersionType::Remix);
}

void TstTitleVersion::classifySuffixTest()
{
    QFETCH(QString, suffixText);
    QFETCH(int, expectedRole);
    QFETCH(int, expectedType);

    const SuffixClass result = classifySuffix(suffixText);
    QCOMPARE(static_cast<int>(result.role), expectedRole);
    QCOMPARE(static_cast<int>(result.type), expectedType);
}

void TstTitleVersion::resolveTitleTest()
{
    // 1. "Song (Live) [2011 Remaster]" -> base "Song", type Live (priority higher than Remaster)
    {
        const TitleVersion res
            = resolveTitle(QStringLiteral("Song (Live) [2011 Remaster]"), classifySuffix);
        QCOMPARE(res.baseTitle, QStringLiteral("Song"));
        QCOMPARE(res.type, VersionType::Live);
        QCOMPARE(res.unresolved, false);
    }

    // 2. "Song (feat. GUMI)" -> base "Song", Studio
    {
        const TitleVersion res = resolveTitle(QStringLiteral("Song (feat. GUMI)"), classifySuffix);
        QCOMPARE(res.baseTitle, QStringLiteral("Song"));
        QCOMPARE(res.type, VersionType::Studio);
        QCOMPARE(res.unresolved, false);
    }

    // 3. "Song (Elite)" -> base "Song (Elite)", unresolved
    {
        const TitleVersion res = resolveTitle(QStringLiteral("Song (Elite)"), classifySuffix);
        QCOMPARE(res.baseTitle, QStringLiteral("Song (Elite)"));
        QCOMPARE(res.type, VersionType::Studio);
        QCOMPARE(res.unresolved, true);
    }

    // 4. "Song (Elite) (Instrumental)" -> base "Song (Elite)", Instrumental, unresolved
    {
        const TitleVersion res
            = resolveTitle(QStringLiteral("Song (Elite) (Instrumental)"), classifySuffix);
        QCOMPARE(res.baseTitle, QStringLiteral("Song (Elite)"));
        QCOMPARE(res.type, VersionType::Instrumental);
        QCOMPARE(res.unresolved, true);
    }

    // 5. Lambda treating Elite as TitlePart: "Song (Elite) (Instrumental)" -> base "Song (Elite)",
    // Instrumental, not unresolved
    {
        auto classifyWithTitlePart = [](const QString &text) {
            if (text == QStringLiteral("Elite")) {
                return SuffixClass { .role = SuffixRole::TitlePart, .type = VersionType::Studio };
            }
            return classifySuffix(text);
        };
        const TitleVersion res
            = resolveTitle(QStringLiteral("Song (Elite) (Instrumental)"), classifyWithTitlePart);
        QCOMPARE(res.baseTitle, QStringLiteral("Song (Elite)"));
        QCOMPARE(res.type, VersionType::Instrumental);
        QCOMPARE(res.unresolved, false);
    }
}

void TstTitleVersion::versionTypeRoundtrip()
{
    const std::array<VersionType, 9> types = {
        VersionType::Studio,
        VersionType::Live,
        VersionType::Remaster,
        VersionType::Acoustic,
        VersionType::Remix,
        VersionType::Demo,
        VersionType::Instrumental,
        VersionType::Edit,
        VersionType::Alternate,
    };

    for (VersionType vt : types) {
        const QString str = versionTypeToString(vt);
        QVERIFY(!str.isEmpty());
        const auto parsed = versionTypeFromString(str);
        QVERIFY(parsed.has_value());
        if (!parsed.has_value()) {
            return;
        }
        QCOMPARE(*parsed, vt);
    }

    QCOMPARE(versionTypeToString(VersionType::Studio), QStringLiteral("studio"));
    QCOMPARE(versionTypeToString(VersionType::Live), QStringLiteral("live"));
    QCOMPARE(versionTypeToString(VersionType::Remaster), QStringLiteral("remaster"));
    QCOMPARE(versionTypeToString(VersionType::Acoustic), QStringLiteral("acoustic"));
    QCOMPARE(versionTypeToString(VersionType::Remix), QStringLiteral("remix"));
    QCOMPARE(versionTypeToString(VersionType::Demo), QStringLiteral("demo"));
    QCOMPARE(versionTypeToString(VersionType::Instrumental), QStringLiteral("instrumental"));
    QCOMPARE(versionTypeToString(VersionType::Edit), QStringLiteral("edit"));
    QCOMPARE(versionTypeToString(VersionType::Alternate), QStringLiteral("alternate"));

    QCOMPARE(versionTypeFromString(QStringLiteral("unknown_type")), std::nullopt);
}

void TstTitleVersion::suffixKeyTest()
{
    const QString text = QStringLiteral("Extended Mix");
    QCOMPARE(suffixKey(text), exactKey(text));
}

} // namespace

QTEST_GUILESS_MAIN(TstTitleVersion)
#include "tst_TitleVersion.moc"
