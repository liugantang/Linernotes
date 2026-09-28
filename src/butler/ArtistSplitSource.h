// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <butler/ArtistSplit.h>
#include <butler/TrackFieldTarget.h>
#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct ArtistSplitCandidate {
    int id = 0;
    QString original { };
    SplitDecision decision;
    QList<TrackFieldTarget> targets;
    QStringList contextAlbums;

    bool operator==(const ArtistSplitCandidate &) const = default;
};

struct ArtistSplitGroup {
    QString key { };
    QList<ArtistSplitCandidate> candidates;

    bool operator==(const ArtistSplitGroup &) const = default;
};

class ArtistSplitSource {
public:
    explicit ArtistSplitSource(library::Database &db);

    /// 扫描全库，返回待处理拆分项的分组 key（每 20 个值一组，紧凑 JSON 数组）。
    core::Result<QStringList> findItems() const;
    core::Result<ArtistSplitGroup> loadItem(const QString &key) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
