// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <guide/GuideEnums.h>

namespace linernotes::guide {

struct Lyrics {
    QString text { }; // 原样文本（LRC 或纯文本），不在这里解析
    LyricsSource source = LyricsSource::Lrclib;
    bool synced = false; // 文本中存在 [mm:ss.xx] 形式的时间戳
    bool operator==(const Lyrics &) const = default;
};

struct LyricsResult {
    qint64 trackId = 0;
    LyricsStatus status = LyricsStatus::NotFound;
    Lyrics lyrics { }; // 仅 status == Found 时有意义
    bool operator==(const LyricsResult &) const = default;
};

bool looksSynced(const QString &text); // 任一行以 [数字+:数字...] 时间戳开头

} // namespace linernotes::guide
