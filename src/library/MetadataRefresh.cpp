// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MetadataRefresh.h"

#include <QSqlError>

#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/SearchIndex.h>

namespace linernotes::library::detail {

core::Result<void> execWrite(QSqlQuery &q, const QString &detail)
{
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = detail,
        };
    }
    return { };
}

core::Result<void> relinkTracks(const QSqlDatabase &conn, const QList<qint64> &trackIds)
{
    EntityLinker linker(conn);
    for (const qint64 trackId : trackIds) {
        if (auto res = linker.linkTrack(trackId); !res.ok()) {
            return res;
        }
    }

    if (auto res = linker.removeOrphans(); !res.ok()) {
        return res.error();
    }

    SearchIndex searchIndex(conn);
    if (auto res = searchIndex.flushDirty(); !res.ok()) {
        return res.error();
    }

    return { };
}

} // namespace linernotes::library::detail
