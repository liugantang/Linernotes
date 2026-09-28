// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct MojibakeField {
    library::TagField field = library::TagField::Title;
    QString value { };
    std::optional<QByteArray> rawBytes;

    bool operator==(const MojibakeField &) const = default;
};

struct MojibakeTrack {
    qint64 trackId = 0;
    QString filePath { };
    QList<MojibakeField> suspects;
    QList<MojibakeField> readable;

    bool operator==(const MojibakeTrack &) const = default;
};

struct MojibakeGroup {
    QString key { };
    QString directory { };
    QList<MojibakeTrack> tracks;

    bool operator==(const MojibakeGroup &) const = default;
};

class MojibakeSource {
public:
    explicit MojibakeSource(library::Database &db);

    /// 扫描全库，返回至少有一首曲目含可疑字段的分组 key（按目录排序）。
    core::Result<QStringList> findGroups() const;
    core::Result<MojibakeGroup> loadGroup(const QString &key) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
