// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <guide/LrcParser.h>

namespace {

using linernotes::guide::lineIndexAt;
using linernotes::guide::ParsedLyrics;
using linernotes::guide::parseLyrics;

class TstLrcParser : public QObject {
    Q_OBJECT

private slots:
    void expandsMultipleTimestamps();
    void parsesFractionsAndColonFormat();
    void appliesOffset();
    void ignoresMetadataTags();
    void mergesSameTimestampTranslations();
    void preservesEmptyLinesInPlainText();
    void lineIndexAtScenarios();
};

void TstLrcParser::expandsMultipleTimestamps()
{
    const QString lrc = QStringLiteral("[00:10.00][00:50.00] Repeated chorus\n");
    const ParsedLyrics parsed = parseLyrics(lrc);
    QVERIFY(parsed.synced);
    QCOMPARE(parsed.lines.size(), 2);
    QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(10000));
    QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Repeated chorus"));
    QCOMPARE(parsed.lines.at(0).translation, QString());
    QCOMPARE(parsed.lines.at(1).timeMs, std::optional<qint64>(50000));
    QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Repeated chorus"));
    QCOMPARE(parsed.lines.at(1).translation, QString());
}

void TstLrcParser::parsesFractionsAndColonFormat()
{
    const QString lrc = QStringLiteral("[01:02.3] One decimal\n"
                                       "[01:02.34] Two decimals\n"
                                       "[01:02.345] Three decimals\n"
                                       "[01:03:34] Colon format\n"
                                       "[100:05.10] Over 99 minutes\n");
    const ParsedLyrics parsed = parseLyrics(lrc);
    QVERIFY(parsed.synced);
    QCOMPARE(parsed.lines.size(), 5);
    QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(62300));
    QCOMPARE(parsed.lines.at(0).text, QStringLiteral("One decimal"));
    QCOMPARE(parsed.lines.at(1).timeMs, std::optional<qint64>(62340));
    QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Two decimals"));
    QCOMPARE(parsed.lines.at(2).timeMs, std::optional<qint64>(62345));
    QCOMPARE(parsed.lines.at(2).text, QStringLiteral("Three decimals"));
    QCOMPARE(parsed.lines.at(3).timeMs, std::optional<qint64>(63340));
    QCOMPARE(parsed.lines.at(3).text, QStringLiteral("Colon format"));
    QCOMPARE(parsed.lines.at(4).timeMs, std::optional<qint64>(6005100));
    QCOMPARE(parsed.lines.at(4).text, QStringLiteral("Over 99 minutes"));
}

void TstLrcParser::appliesOffset()
{
    // Positive offset (lyrics advance: time - offset)
    {
        const QString lrc
            = QStringLiteral("[offset: 500]\n[00:01.000] Line 1\n[00:00.300] Line 0\n");
        const ParsedLyrics parsed = parseLyrics(lrc);
        QVERIFY(parsed.synced);
        QCOMPARE(parsed.lines.size(), 2);
        // 300 - 500 = -200 -> clamped to 0
        QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(0));
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Line 0"));
        // 1000 - 500 = 500
        QCOMPARE(parsed.lines.at(1).timeMs, std::optional<qint64>(500));
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Line 1"));
    }
    // Negative offset (lyrics delayed: time - (-500) = time + 500)
    {
        const QString lrc = QStringLiteral("[offset:-500]\n[00:01.000] Line 1\n");
        const ParsedLyrics parsed = parseLyrics(lrc);
        QVERIFY(parsed.synced);
        QCOMPARE(parsed.lines.size(), 1);
        QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(1500));
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Line 1"));
    }
}

void TstLrcParser::ignoresMetadataTags()
{
    const QString lrc = QStringLiteral("[ar: Artist Name]\n"
                                       "[ti: Song Title]\n"
                                       "[al: Album Name]\n"
                                       "[by: Author]\n"
                                       "[length: 03:45]\n"
                                       "[00:05.00] Actual Lyric\n");
    const ParsedLyrics parsed = parseLyrics(lrc);
    QVERIFY(parsed.synced);
    QCOMPARE(parsed.lines.size(), 1);
    QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(5000));
    QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Actual Lyric"));
}

void TstLrcParser::mergesSameTimestampTranslations()
{
    // 2 lines with same timestamp -> 2nd merged as translation
    {
        const QString lrc = QStringLiteral("[00:01.00] Original line\n"
                                           "[00:01.00] Translated line\n"
                                           "[00:05.00] Next original\n"
                                           "[00:05.00] Next translated\n");
        const ParsedLyrics parsed = parseLyrics(lrc);
        QVERIFY(parsed.synced);
        QCOMPARE(parsed.lines.size(), 2);
        QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(1000));
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Original line"));
        QCOMPARE(parsed.lines.at(0).translation, QStringLiteral("Translated line"));
        QCOMPARE(parsed.lines.at(1).timeMs, std::optional<qint64>(5000));
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Next original"));
        QCOMPARE(parsed.lines.at(1).translation, QStringLiteral("Next translated"));
    }
    // 3 or more lines with same timestamp -> 2nd merged into 1st, rest are separate
    {
        const QString lrc = QStringLiteral("[00:02.00] Line 1\n"
                                           "[00:02.00] Line 2 Translation\n"
                                           "[00:02.00] Line 3 Extra\n"
                                           "[00:02.00] Line 4 Extra\n");
        const ParsedLyrics parsed = parseLyrics(lrc);
        QVERIFY(parsed.synced);
        QCOMPARE(parsed.lines.size(), 3);
        QCOMPARE(parsed.lines.at(0).timeMs, std::optional<qint64>(2000));
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Line 1"));
        QCOMPARE(parsed.lines.at(0).translation, QStringLiteral("Line 2 Translation"));
        QCOMPARE(parsed.lines.at(1).timeMs, std::optional<qint64>(2000));
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Line 3 Extra"));
        QCOMPARE(parsed.lines.at(1).translation, QString());
        QCOMPARE(parsed.lines.at(2).timeMs, std::optional<qint64>(2000));
        QCOMPARE(parsed.lines.at(2).text, QStringLiteral("Line 4 Extra"));
        QCOMPARE(parsed.lines.at(2).translation, QString());
    }
}

void TstLrcParser::preservesEmptyLinesInPlainText()
{
    const QString plain = QStringLiteral("\n\r\n"
                                         "   Verse 1 Line 1   \r\n"
                                         "Verse 1 Line 2\n"
                                         "\n"
                                         "   \n"
                                         "Verse 2 Line 1\n"
                                         "\n\n\n");
    const ParsedLyrics parsed = parseLyrics(plain);
    QVERIFY(!parsed.synced);
    QCOMPARE(parsed.lines.size(), 5);
    QCOMPARE(parsed.lines.at(0).timeMs, std::nullopt);
    QCOMPARE(parsed.lines.at(0).text, QStringLiteral("Verse 1 Line 1"));
    QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Verse 1 Line 2"));
    QCOMPARE(parsed.lines.at(2).text, QString());
    QCOMPARE(parsed.lines.at(3).text, QString());
    QCOMPARE(parsed.lines.at(4).text, QStringLiteral("Verse 2 Line 1"));
}

void TstLrcParser::lineIndexAtScenarios()
{
    const QString lrc = QStringLiteral("[00:02.00] Line 1\n"
                                       "[00:04.00] Line 2\n"
                                       "[00:06.00] Line 3\n");
    const ParsedLyrics parsed = parseLyrics(lrc);
    // Before first line (< 2000)
    QCOMPARE(lineIndexAt(parsed, 0), -1);
    QCOMPARE(lineIndexAt(parsed, 1999), -1);
    // Exactly at timestamps
    QCOMPARE(lineIndexAt(parsed, 2000), 0);
    QCOMPARE(lineIndexAt(parsed, 4000), 1);
    QCOMPARE(lineIndexAt(parsed, 6000), 2);
    // Between timestamps
    QCOMPARE(lineIndexAt(parsed, 3000), 0);
    QCOMPARE(lineIndexAt(parsed, 5500), 1);
    // After last line
    QCOMPARE(lineIndexAt(parsed, 6001), 2);
    QCOMPARE(lineIndexAt(parsed, 100000), 2);

    // Unsynced lyrics returns -1
    const ParsedLyrics plain = parseLyrics(QStringLiteral("Plain line 1\nPlain line 2"));
    QCOMPARE(lineIndexAt(plain, 5000), -1);

    // Empty lyrics returns -1
    const ParsedLyrics empty;
    QCOMPARE(lineIndexAt(empty, 5000), -1);
}

} // namespace

QTEST_GUILESS_MAIN(TstLrcParser)

#include "tst_LrcParser.moc"
