// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class TranslationStore {
public:
    TranslationStore(library::Database &db, const core::Clock &clock);

    /// 原文 → 译文；只返回非空译文（7.10b 用）
    core::Result<QHash<QString, QString>> lookup(
        const QStringList &texts, const QString &targetLang = QStringLiteral("zh-Hans")) const;

    /// INSERT OR REPLACE，一个事务
    core::Result<void> save(const QHash<QString, QString> &translations, const QString &model,
        int promptVersion, const QString &targetLang = QStringLiteral("zh-Hans")) const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
