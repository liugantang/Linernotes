// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace linernotes::guide {

struct LyricLine {
    std::optional<qint64> timeMs = std::nullopt; // 纯文本歌词为 nullopt
    QString text { };
    QString translation { }; // 同一时间戳的第二行
    bool operator==(const LyricLine &) const = default;
};

struct ParsedLyrics {
    QList<LyricLine> lines;
    bool synced = false;
    bool operator==(const ParsedLyrics &) const = default;
};

ParsedLyrics parseLyrics(const QString &text);

// 当前行：timeMs <= positionMs 的最后一行；第一行之前返回 -1；未同步歌词返回 -1
int lineIndexAt(const ParsedLyrics &lyrics, qint64 positionMs);

} // namespace linernotes::guide
