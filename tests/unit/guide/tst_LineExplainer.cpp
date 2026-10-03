// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <guide/LineExplainer.h>
#include <guide/LrcParser.h>

namespace {

using linernotes::guide::explainLinePromptVars;
using linernotes::guide::LineExplainRequest;
using linernotes::guide::LyricLine;
using linernotes::guide::ParsedLyrics;

class TstLineExplainer : public QObject {
    Q_OBJECT

private slots:
    void promptVarsBasicAndLine();
    void promptVarsLyricsSkippingEmptyLinesAndTranslations();
    void promptVarsContextMiddle();
    void promptVarsContextBoundaries();
    void promptVarsContextWithEmptyLines();
    void promptVarsInvalidIndex();
};

void TstLineExplainer::promptVarsBasicAndLine()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine {
            .timeMs = 1000, .text = QStringLiteral("First line"), .translation = QString() },
        LyricLine {
            .timeMs = 2000, .text = QStringLiteral("Second line"), .translation = QString() },
    };

    LineExplainRequest req {
        .trackId = 42,
        .lineIndex = 1,
        .title = QStringLiteral("Test Title"),
        .artist = QStringLiteral("Test Artist"),
        .album = QStringLiteral("Test Album"),
        .lyrics = lyrics,
        .translations = { },
    };

    const auto vars = explainLinePromptVars(req);
    QCOMPARE(vars.value(QStringLiteral("title")), QStringLiteral("Test Title"));
    QCOMPARE(vars.value(QStringLiteral("artist")), QStringLiteral("Test Artist"));
    QCOMPARE(vars.value(QStringLiteral("album")), QStringLiteral("Test Album"));
    QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Second line"));
}

void TstLineExplainer::promptVarsLyricsSkippingEmptyLinesAndTranslations()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("   "), .translation = QString() },
        LyricLine { .timeMs = 3000,
            .text = QStringLiteral("Line 3"),
            .translation = QStringLiteral("LRC 译文 3") },
        LyricLine { .timeMs = 4000, .text = QStringLiteral("Line 4"), .translation = QString() },
    };

    // Translations array overrides or supplies translations for lines
    QStringList translations = {
        QStringLiteral("LLM 译文 1"),
        QString(),
        QString(), // Empty in translations array -> fallback to line.translation
        QStringLiteral("LLM 译文 4"),
    };

    LineExplainRequest req {
        .trackId = 1,
        .lineIndex = 0,
        .title = QStringLiteral("Song"),
        .artist = QStringLiteral("Artist"),
        .album = QStringLiteral("Album"),
        .lyrics = lyrics,
        .translations = translations,
    };

    const auto vars = explainLinePromptVars(req);
    const QString lyricsVal = vars.value(QStringLiteral("lyrics"));
    const QStringList resultLines = lyricsVal.split(QLatin1Char('\n'));

    QCOMPARE(resultLines.size(), 3);
    QCOMPARE(resultLines.at(0), QStringLiteral("Line 1 / LLM 译文 1"));
    QCOMPARE(resultLines.at(1), QStringLiteral("Line 3 / LRC 译文 3"));
    QCOMPARE(resultLines.at(2), QStringLiteral("Line 4 / LLM 译文 4"));
}

void TstLineExplainer::promptVarsContextMiddle()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("Line 2"), .translation = QString() },
        LyricLine { .timeMs = 3000, .text = QStringLiteral("Line 3"), .translation = QString() },
        LyricLine { .timeMs = 4000, .text = QStringLiteral("Line 4"), .translation = QString() },
        LyricLine { .timeMs = 5000, .text = QStringLiteral("Line 5"), .translation = QString() },
        LyricLine { .timeMs = 6000, .text = QStringLiteral("Line 6"), .translation = QString() },
    };

    LineExplainRequest req {
        .trackId = 1,
        .lineIndex = 3, // Line 4
        .title = QStringLiteral("Song"),
        .artist = QStringLiteral("Artist"),
        .album = QStringLiteral("Album"),
        .lyrics = lyrics,
        .translations = { },
    };

    const auto vars = explainLinePromptVars(req);
    QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Line 4"));

    const QString contextVal = vars.value(QStringLiteral("context"));
    const QStringList ctxLines = contextVal.split(QLatin1Char('\n'));
    QCOMPARE(ctxLines.size(), 4);
    QCOMPARE(ctxLines.at(0), QStringLiteral("Line 2"));
    QCOMPARE(ctxLines.at(1), QStringLiteral("Line 3"));
    QCOMPARE(ctxLines.at(2), QStringLiteral("Line 5"));
    QCOMPARE(ctxLines.at(3), QStringLiteral("Line 6"));
}

void TstLineExplainer::promptVarsContextBoundaries()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("Line 2"), .translation = QString() },
        LyricLine { .timeMs = 3000, .text = QStringLiteral("Line 3"), .translation = QString() },
    };

    // 1. First line (index 0): 0 lines before, 2 lines after
    {
        LineExplainRequest req {
            .trackId = 1,
            .lineIndex = 0,
            .title = QStringLiteral("Song"),
            .artist = QStringLiteral("Artist"),
            .album = QStringLiteral("Album"),
            .lyrics = lyrics,
            .translations = { },
        };
        const auto vars = explainLinePromptVars(req);
        QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Line 1"));
        const QString contextVal = vars.value(QStringLiteral("context"));
        const QStringList ctxLines = contextVal.split(QLatin1Char('\n'));
        QCOMPARE(ctxLines.size(), 2);
        QCOMPARE(ctxLines.at(0), QStringLiteral("Line 2"));
        QCOMPARE(ctxLines.at(1), QStringLiteral("Line 3"));
    }

    // 2. Last line (index 2): 2 lines before, 0 lines after
    {
        LineExplainRequest req {
            .trackId = 1,
            .lineIndex = 2,
            .title = QStringLiteral("Song"),
            .artist = QStringLiteral("Artist"),
            .album = QStringLiteral("Album"),
            .lyrics = lyrics,
            .translations = { },
        };
        const auto vars = explainLinePromptVars(req);
        QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Line 3"));
        const QString contextVal = vars.value(QStringLiteral("context"));
        const QStringList ctxLines = contextVal.split(QLatin1Char('\n'));
        QCOMPARE(ctxLines.size(), 2);
        QCOMPARE(ctxLines.at(0), QStringLiteral("Line 1"));
        QCOMPARE(ctxLines.at(1), QStringLiteral("Line 2"));
    }

    // 3. Single line song (index 0)
    {
        ParsedLyrics singleLineLyrics;
        singleLineLyrics.lines = {
            LyricLine {
                .timeMs = 1000, .text = QStringLiteral("Only line"), .translation = QString() },
        };
        LineExplainRequest req {
            .trackId = 1,
            .lineIndex = 0,
            .title = QStringLiteral("Song"),
            .artist = QStringLiteral("Artist"),
            .album = QStringLiteral("Album"),
            .lyrics = singleLineLyrics,
            .translations = { },
        };
        const auto vars = explainLinePromptVars(req);
        QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Only line"));
        QCOMPARE(vars.value(QStringLiteral("context")), QString());
    }
}

void TstLineExplainer::promptVarsContextWithEmptyLines()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
        LyricLine { .timeMs = 2000, .text = QStringLiteral("   "), .translation = QString() },
        LyricLine { .timeMs = 3000, .text = QStringLiteral("Line 3"), .translation = QString() },
        LyricLine { .timeMs = 4000, .text = QStringLiteral("Line 4"), .translation = QString() },
        LyricLine { .timeMs = 5000, .text = QStringLiteral("\t"), .translation = QString() },
        LyricLine { .timeMs = 6000, .text = QStringLiteral("Line 6"), .translation = QString() },
        LyricLine { .timeMs = 7000, .text = QStringLiteral("Line 7"), .translation = QString() },
    };

    LineExplainRequest req {
        .trackId = 1,
        .lineIndex = 3, // Line 4
        .title = QStringLiteral("Song"),
        .artist = QStringLiteral("Artist"),
        .album = QStringLiteral("Album"),
        .lyrics = lyrics,
        .translations = { },
    };

    const auto vars = explainLinePromptVars(req);
    QCOMPARE(vars.value(QStringLiteral("line")), QStringLiteral("Line 4"));

    const QString contextVal = vars.value(QStringLiteral("context"));
    const QStringList ctxLines = contextVal.split(QLatin1Char('\n'));
    QCOMPARE(ctxLines.size(), 4);
    QCOMPARE(ctxLines.at(0), QStringLiteral("Line 1"));
    QCOMPARE(ctxLines.at(1), QStringLiteral("Line 3"));
    QCOMPARE(ctxLines.at(2), QStringLiteral("Line 6"));
    QCOMPARE(ctxLines.at(3), QStringLiteral("Line 7"));
}

void TstLineExplainer::promptVarsInvalidIndex()
{
    ParsedLyrics lyrics;
    lyrics.lines = {
        LyricLine { .timeMs = 1000, .text = QStringLiteral("Line 1"), .translation = QString() },
    };

    LineExplainRequest req {
        .trackId = 1,
        .lineIndex = -1,
        .title = QStringLiteral("Song"),
        .artist = QStringLiteral("Artist"),
        .album = QStringLiteral("Album"),
        .lyrics = lyrics,
        .translations = { },
    };

    const auto vars = explainLinePromptVars(req);
    QCOMPARE(vars.value(QStringLiteral("line")), QString());
    QCOMPARE(vars.value(QStringLiteral("context")), QString());
}

} // namespace

QTEST_GUILESS_MAIN(TstLineExplainer)

#include "tst_LineExplainer.moc"
