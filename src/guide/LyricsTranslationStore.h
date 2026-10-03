// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringList>

#include <core/Clock.h>
#include <core/Result.h>
#include <library/Database.h>

#include <optional>

namespace linernotes::guide {

class LyricsTranslationStore {
public:
    explicit LyricsTranslationStore(library::Database &db, const core::Clock &clock);

    // 只有 lyrics_hash 相同才返回（歌词改了旧译文作废）
    [[nodiscard]] core::Result<std::optional<QStringList>> get(qint64 trackId,
        const QString &lyricsHash, const QString &targetLang = QStringLiteral("zh-Hans")) const;

    core::Result<void> save(qint64 trackId, const QString &lyricsHash, const QStringList &lines,
        const QString &model, int promptVersion,
        const QString &targetLang = QStringLiteral("zh-Hans")) const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::guide
