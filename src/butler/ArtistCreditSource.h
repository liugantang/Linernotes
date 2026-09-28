// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <butler/TrackFieldTarget.h>
#include <core/Result.h>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class ArtistCreditSource {
public:
    explicit ArtistCreditSource(library::Database &db);

    /// promptVersion：当前提示词版本；缓存中 prompt_version 小于它的值视为未解析。
    core::Result<QStringList> findItems(int promptVersion) const;
    /// 某个字段值需要修正的目标（effective 值等于 value 的曲目的 artist / album_artist 字段）。
    core::Result<QList<TrackFieldTarget>> targetsFor(const QString &value) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
