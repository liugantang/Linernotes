// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LrcParser.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace linernotes::guide {

namespace {

struct RawTimestampedLine {
    qint64 timeMs = 0;
    QString text { };
};

qint64 parseFractionMs(const QString &fracStr)
{
    if (fracStr.size() == 1) {
        return fracStr.toLongLong() * 100;
    }
    if (fracStr.size() == 2) {
        return fracStr.toLongLong() * 10;
    }
    if (fracStr.size() == 3) {
        return fracStr.toLongLong();
    }
    return 0;
}

qint64 parseOffsetMs(const QString &text)
{
    static const QRegularExpression s_offsetRegex(QStringLiteral(R"(\[offset:\s*([+-]?\d+)\s*\])"),
        QRegularExpression::CaseInsensitiveOption);
    qint64 totalOffset = 0;
    auto it = s_offsetRegex.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        totalOffset += match.captured(1).toLongLong();
    }
    return totalOffset;
}

ParsedLyrics parseSyncedLyrics(const QList<RawTimestampedLine> &rawLines)
{
    auto sortedLines = rawLines;
    std::ranges::stable_sort(sortedLines, { }, &RawTimestampedLine::timeMs);

    QList<LyricLine> merged;
    merged.reserve(sortedLines.size());
    int runCount = 0;
    std::optional<qint64> prevTime;

    for (const auto &raw : sortedLines) {
        if (prevTime.has_value() && prevTime.value() == raw.timeMs) {
            ++runCount;
            if (runCount == 2) {
                merged.last().translation = raw.text;
                continue;
            }
            LyricLine line;
            line.timeMs = raw.timeMs;
            line.text = raw.text;
            merged.append(line);
        } else {
            prevTime = raw.timeMs;
            runCount = 1;
            LyricLine line;
            line.timeMs = raw.timeMs;
            line.text = raw.text;
            merged.append(line);
        }
    }

    return ParsedLyrics { .lines = merged, .synced = true };
}

ParsedLyrics parseUnsyncedLyrics(const QStringList &rawLines)
{
    QList<QString> trimmedLines;
    trimmedLines.reserve(rawLines.size());
    for (const QString &rawLine : rawLines) {
        QString s = rawLine;
        if (s.endsWith(QLatin1Char('\r'))) {
            s.chop(1);
        }
        trimmedLines.append(s.trimmed());
    }

    int firstNonEmpty = -1;
    int lastNonEmpty = -1;
    for (int i = 0; i < trimmedLines.size(); ++i) {
        if (!trimmedLines.at(i).isEmpty()) {
            if (firstNonEmpty == -1) {
                firstNonEmpty = i;
            }
            lastNonEmpty = i;
        }
    }

    QList<LyricLine> lines;
    if (firstNonEmpty != -1) {
        lines.reserve(lastNonEmpty - firstNonEmpty + 1);
        for (int i = firstNonEmpty; i <= lastNonEmpty; ++i) {
            LyricLine line;
            line.timeMs = std::nullopt;
            line.text = trimmedLines.at(i);
            lines.append(line);
        }
    }

    return ParsedLyrics { .lines = lines, .synced = false };
}

} // namespace

ParsedLyrics parseLyrics(const QString &text)
{
    if (text.isEmpty()) {
        return ParsedLyrics { };
    }

    const qint64 offsetMs = parseOffsetMs(text);

    static const QRegularExpression s_timeRegex(
        QStringLiteral(R"(\[(\d+):(\d{2})(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression s_metaRegex(QStringLiteral(R"(\[[a-zA-Z]+:[^\]]*\])"));

    const QStringList rawLines = text.split(QLatin1Char('\n'));
    QList<RawTimestampedLine> timestampedLines;
    bool hasAnyTimestamp = false;

    for (const QString &rawLine : rawLines) {
        QString lineStr = rawLine;
        if (lineStr.endsWith(QLatin1Char('\r'))) {
            lineStr.chop(1);
        }

        QList<qint64> lineTimes;
        auto it = s_timeRegex.globalMatch(lineStr);
        while (it.hasNext()) {
            const auto match = it.next();
            const qint64 minutes = match.captured(1).toLongLong();
            const qint64 seconds = match.captured(2).toLongLong();
            const qint64 fracMs = parseFractionMs(match.captured(3));
            const qint64 rawTimeMs = (((minutes * 60) + seconds) * 1000) + fracMs;
            const qint64 adjustedTimeMs = std::max<qint64>(0, rawTimeMs - offsetMs);
            lineTimes.append(adjustedTimeMs);
        }

        if (!lineTimes.isEmpty()) {
            hasAnyTimestamp = true;
            QString lyricText = lineStr;
            lyricText.remove(s_timeRegex);
            lyricText.remove(s_metaRegex);
            lyricText = lyricText.trimmed();

            for (const qint64 time : lineTimes) {
                timestampedLines.append(RawTimestampedLine {
                    .timeMs = time,
                    .text = lyricText,
                });
            }
        }
    }

    if (hasAnyTimestamp) {
        return parseSyncedLyrics(timestampedLines);
    }

    return parseUnsyncedLyrics(rawLines);
}

int lineIndexAt(const ParsedLyrics &lyrics, qint64 positionMs)
{
    if (!lyrics.synced || lyrics.lines.isEmpty()) {
        return -1;
    }

    const auto it = std::ranges::upper_bound(lyrics.lines, positionMs, { },
        [](const LyricLine &line) { return line.timeMs.value_or(0); });

    if (it == lyrics.lines.begin()) {
        return -1;
    }

    return static_cast<int>(std::distance(lyrics.lines.begin(), it) - 1);
}

} // namespace linernotes::guide
