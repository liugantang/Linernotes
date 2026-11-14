// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <guide/Errors.h>
#include <guide/LrcParser.h>
#include <guide/LyricsTranslation.h>

namespace {

using linernotes::guide::LyricLine;
using linernotes::guide::lyricsHash;
using linernotes::guide::lyricsTranslationPromptVars;
using linernotes::guide::needsTranslation;
using linernotes::guide::ParsedLyrics;
using linernotes::guide::parseLyricsTranslation;
using linernotes::guide::translatableLineIndexes;
namespace errc = linernotes::guide::errc;

class TstLyricsTranslation : public QObject {
    Q_OBJECT

private slots:
    void parseLyricsTranslationEmptyLinesAndIndices();
    void parseLyricsTranslationOutOfBoundsAndDuplicates();
    void parseLyricsTranslationMissingThreshold();
    void needsTranslationScenarios();
    void promptVarsFormat();
    void hashConsistency();
};

void TstLyricsTranslation::parseLyricsTranslationEmptyLinesAndIndices()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine {
            .timeMs = 1000, .text = QStringLiteral("First line"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("   "), .translation = QString() },
        LyricLine {
            .timeMs = 3000, .text = QStringLiteral("Third line"), .translation = QString() },
    };

    const auto idxs = translatableLineIndexes(lyrics);
    QCOMPARE(idxs.size(), 2);
    QCOMPARE(idxs.at(0), 0);
    QCOMPARE(idxs.at(1), 2);

    QJsonObject json;
    QJsonArray linesArr;
    QJsonObject item1;
    item1.insert(QStringLiteral("id"), 1);
    item1.insert(QStringLiteral("translation"), QStringLiteral("第一行"));
    linesArr.append(item1);
    QJsonObject item2;
    item2.insert(QStringLiteral("id"), 2);
    item2.insert(QStringLiteral("translation"), QStringLiteral("第三行"));
    linesArr.append(item2);
    json.insert(QStringLiteral("lines"), linesArr);

    const auto res = parseLyricsTranslation(json, lyrics);
    QVERIFY(res.ok());
    const auto &transList = res.value();
    QCOMPARE(transList.size(), 3);
    QCOMPARE(transList.at(0), QStringLiteral("第一行"));
    QCOMPARE(transList.at(1), QString());
    QCOMPARE(transList.at(2), QStringLiteral("第三行"));
}

void TstLyricsTranslation::parseLyricsTranslationOutOfBoundsAndDuplicates()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("Line 2"), .translation = QString() },
    };

    QJsonObject json;
    QJsonArray linesArr;

    QJsonObject item1;
    item1.insert(QStringLiteral("id"), 1);
    item1.insert(QStringLiteral("translation"), QStringLiteral("译文一"));
    linesArr.append(item1);

    // Duplicate id 1 - should be ignored
    QJsonObject itemDup;
    itemDup.insert(QStringLiteral("id"), 1);
    itemDup.insert(QStringLiteral("translation"), QStringLiteral("重复忽略"));
    linesArr.append(itemDup);

    // Out of bounds id 0 and 99 - should be ignored
    QJsonObject itemOob0;
    itemOob0.insert(QStringLiteral("id"), 0);
    itemOob0.insert(QStringLiteral("translation"), QStringLiteral("越界0"));
    linesArr.append(itemOob0);

    QJsonObject itemOob99;
    itemOob99.insert(QStringLiteral("id"), 99);
    itemOob99.insert(QStringLiteral("translation"), QStringLiteral("越界99"));
    linesArr.append(itemOob99);

    QJsonObject item2;
    item2.insert(QStringLiteral("id"), 2);
    item2.insert(QStringLiteral("translation"), QStringLiteral("译文二"));
    linesArr.append(item2);

    json.insert(QStringLiteral("lines"), linesArr);

    const auto res = parseLyricsTranslation(json, lyrics);
    QVERIFY(res.ok());
    const auto &transList = res.value();
    QCOMPARE(transList.size(), 2);
    QCOMPARE(transList.at(0), QStringLiteral("译文一"));
    QCOMPARE(transList.at(1), QStringLiteral("译文二"));
}

void TstLyricsTranslation::parseLyricsTranslationMissingThreshold()
{
    ParsedLyrics lyrics;
    for (int i = 0; i < 60; ++i) {
        lyrics.lines.append(LyricLine {
            .timeMs = i * 1000,
            .text = QStringLiteral("Line %1").arg(i + 1),
            .translation = QString(),
        });
    }

    // 1. Missing 1 line out of 60: 1 <= ceil(60 * 0.02) = 2 -> Should NOT error
    {
        QJsonObject json;
        QJsonArray linesArr;
        for (int i = 0; i < 59; ++i) {
            QJsonObject item;
            item.insert(QStringLiteral("id"), i + 1);
            item.insert(QStringLiteral("translation"), QStringLiteral("Trans %1").arg(i + 1));
            linesArr.append(item);
        }
        json.insert(QStringLiteral("lines"), linesArr);

        const auto res = parseLyricsTranslation(json, lyrics);
        QVERIFY(res.ok());
        QCOMPARE(res.value().size(), 60);
        QCOMPARE(res.value().at(0), QStringLiteral("Trans 1"));
        QCOMPARE(res.value().at(58), QStringLiteral("Trans 59"));
        QCOMPARE(res.value().at(59), QString());
    }

    // 2. Missing 3 lines out of 60: 3 > ceil(60 * 0.02) = 2 -> Should error with
    // kTranslationMisaligned
    {
        QJsonObject json;
        QJsonArray linesArr;
        for (int i = 0; i < 57; ++i) {
            QJsonObject item;
            item.insert(QStringLiteral("id"), i + 1);
            item.insert(QStringLiteral("translation"), QStringLiteral("Trans %1").arg(i + 1));
            linesArr.append(item);
        }
        json.insert(QStringLiteral("lines"), linesArr);

        const auto res = parseLyricsTranslation(json, lyrics);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, errc::kTranslationMisaligned);
    }
}

void TstLyricsTranslation::needsTranslationScenarios()
{
    // Empty lyrics -> false
    {
        ParsedLyrics lyrics;
        QCOMPARE(needsTranslation(lyrics), false);
    }

    // Chinese lyrics -> false
    {
        ParsedLyrics lyrics;
        lyrics.lines = {
            LyricLine { .timeMs = 1000,
                .text = QStringLiteral("海阔天空 狂风暴雨"),
                .translation = QString() },
            LyricLine { .timeMs = 2000,
                .text = QStringLiteral("冷风吹过我的脸庞"),
                .translation = QString() },
        };
        QCOMPARE(needsTranslation(lyrics), false);
    }

    // Japanese lyrics -> true
    {
        ParsedLyrics lyrics;
        lyrics.lines = {
            LyricLine { .timeMs = 1000,
                .text = QStringLiteral("君の前前前世から僕は"),
                .translation = QString() },
            LyricLine { .timeMs = 2000,
                .text = QStringLiteral("君を探しはじめたよ"),
                .translation = QString() },
        };
        QCOMPARE(needsTranslation(lyrics), true);
    }

    // Japanese lyrics with existing LRC translation -> false
    {
        ParsedLyrics lyrics;
        lyrics.lines = {
            LyricLine { .timeMs = 1000,
                .text = QStringLiteral("君の前前前世から僕は"),
                .translation = QStringLiteral("从你的前前前世起") },
            LyricLine { .timeMs = 2000,
                .text = QStringLiteral("君を探しはじめたよ"),
                .translation = QString() },
        };
        QCOMPARE(needsTranslation(lyrics), false);
    }
}

void TstLyricsTranslation::promptVarsFormat()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine {
            .timeMs = 1000, .text = QStringLiteral("First line"), .translation = QString() },
        LyricLine {
            .timeMs = 2000, .text = QStringLiteral("Second line"), .translation = QString() },
    };

    const auto vars = lyricsTranslationPromptVars(
        lyrics, QStringLiteral("Song Title"), QStringLiteral("Artist Name"));
    QCOMPARE(vars.value(QStringLiteral("title")), QStringLiteral("Song Title"));
    QCOMPARE(vars.value(QStringLiteral("artist")), QStringLiteral("Artist Name"));
    const QString linesVal = vars.value(QStringLiteral("lines"));
    QVERIFY(linesVal.contains(QStringLiteral("ID 1: First line")));
    QVERIFY(linesVal.contains(QStringLiteral("ID 2: Second line")));
}

void TstLyricsTranslation::hashConsistency()
{
    const QString text = QStringLiteral("[00:01.00] Hello\n[00:02.00] World");
    const QString hash1 = lyricsHash(text);
    const QString hash2 = lyricsHash(text);
    QCOMPARE(hash1, hash2);
    QVERIFY(!hash1.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstLyricsTranslation)

#include "tst_LyricsTranslation.moc"
