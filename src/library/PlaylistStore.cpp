// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlaylistStore.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>

#include <algorithm>
#include <utility>

namespace linernotes::library {

namespace {

struct ItemRecord {
    qint64 trackId = 0;
    qint64 addedAt = 0;
};

core::Result<void> execWrite(QSqlQuery &q, const QString &detail = QString())
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

template <typename F>
auto inTransaction(Database &db, F &&body) -> decltype(body(std::declval<const QSqlDatabase &>()))
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();
    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction"),
            .detail = QString(),
        };
    }

    auto res = std::forward<F>(body)(conn);
    if (!res.ok()) {
        return res;
    }

    if (auto commitRes = tx.commit(); !commitRes.ok()) {
        return commitRes.error();
    }

    return res;
}

core::Result<QString> validatePlaylistName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return core::Error {
            .code = QString(errc::kPlaylistInvalid),
            .message = QStringLiteral("Playlist name cannot be empty"),
            .detail = name,
        };
    }
    return trimmed;
}

core::Result<PlaylistKind> getPlaylistKind(const QSqlDatabase &conn, qint64 playlistId)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT kind FROM playlists WHERE id = ?"));
    q.addBindValue(playlistId);
    if (!q.exec() || !q.next()) {
        return core::Error {
            .code = QString(errc::kPlaylistInvalid),
            .message = QStringLiteral("Playlist not found"),
            .detail = QString::number(playlistId),
        };
    }
    return (q.value(0).toString() == QStringLiteral("smart")) ? PlaylistKind::Smart
                                                              : PlaylistKind::Manual;
}

core::Result<void> requireKind(
    const QSqlDatabase &conn, qint64 id, PlaylistKind expected, const QString &message)
{
    auto kindRes = getPlaylistKind(conn, id);
    if (!kindRes.ok()) {
        return kindRes.error();
    }
    if (kindRes.value() != expected) {
        return core::Error {
            .code = QString(errc::kPlaylistInvalid),
            .message = message,
            .detail = QString::number(id),
        };
    }
    return { };
}

qint64 nextPlaylistPosition(const QSqlDatabase &conn)
{
    QSqlQuery posQuery(conn);
    if (posQuery.exec(QStringLiteral("SELECT MAX(position) FROM playlists")) && posQuery.next()) {
        if (!posQuery.isNull(0)) {
            return posQuery.value(0).toLongLong() + 1;
        }
    }
    return 0;
}

PlaylistInfo parsePlaylistRow(const QSqlQuery &q)
{
    const bool isSmart = (q.value(2).toString() == QStringLiteral("smart"));
    std::optional<SmartRule> rule;
    if (isSmart) {
        if (auto ruleRes = SmartRule::fromJson(q.value(3).toString()); ruleRes.ok()) {
            rule = ruleRes.value();
        }
    }
    return PlaylistInfo {
        .id = q.value(0).toLongLong(),
        .name = q.value(1).toString(),
        .kind = isSmart ? PlaylistKind::Smart : PlaylistKind::Manual,
        .rule = std::move(rule),
        .position = q.value(4).toInt(),
        .createdAt = q.value(5).toLongLong(),
        .updatedAt = q.value(6).toLongLong(),
    };
}

core::Result<QList<ItemRecord>> fetchItems(const QSqlDatabase &conn, qint64 playlistId)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT track_id, added_at FROM playlist_items WHERE playlist_id = ? "
                             "ORDER BY position ASC"));
    q.addBindValue(playlistId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(playlistId),
        };
    }

    QList<ItemRecord> items;
    while (q.next()) {
        items.append(ItemRecord {
            .trackId = q.value(0).toLongLong(),
            .addedAt = q.value(1).toLongLong(),
        });
    }
    return items;
}

core::Result<void> rewriteItems(
    const QSqlDatabase &conn, qint64 playlistId, const QList<ItemRecord> &items)
{
    QSqlQuery del(conn);
    del.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"));
    del.addBindValue(playlistId);
    if (auto res = execWrite(del, QString::number(playlistId)); !res.ok()) {
        return res;
    }

    if (!items.isEmpty()) {
        QSqlQuery ins(conn);
        ins.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position, "
                                   "added_at) VALUES (?, ?, ?, ?)"));
        for (int i = 0; i < items.size(); ++i) {
            ins.bindValue(0, playlistId);
            ins.bindValue(1, items.at(i).trackId);
            ins.bindValue(2, i);
            ins.bindValue(3, items.at(i).addedAt);
            if (auto res = execWrite(ins, QString::number(playlistId)); !res.ok()) {
                return res;
            }
        }
    }
    return { };
}

core::Result<void> updatePlaylistTimestamp(
    const QSqlDatabase &conn, qint64 playlistId, qint64 timestamp)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("UPDATE playlists SET updated_at = ? WHERE id = ?"));
    q.addBindValue(timestamp);
    q.addBindValue(playlistId);
    return execWrite(q, QString::number(playlistId));
}

core::Result<qint64> insertPlaylistRow(const QSqlDatabase &conn, const QString &name,
    const QString &kind, const QVariant &rule, qint64 now)
{
    const qint64 nextPos = nextPlaylistPosition(conn);
    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("INSERT INTO playlists (name, kind, rule, position, created_at, updated_at) "
                       "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(name);
    q.addBindValue(kind);
    q.addBindValue(rule);
    q.addBindValue(nextPos);
    q.addBindValue(now);
    q.addBindValue(now);
    if (auto res = execWrite(q, name); !res.ok()) {
        return res.error();
    }
    return q.lastInsertId().toLongLong();
}

QList<ItemRecord> reorderTrackItems(
    const QList<ItemRecord> &items, const QList<qint64> &trackIds, int beforePosition)
{
    QHash<qint64, ItemRecord> itemMap;
    for (const auto &it : items) {
        itemMap.insert(it.trackId, it);
    }

    QList<ItemRecord> movedItems;
    QSet<qint64> addedToMoved;
    for (const qint64 tid : trackIds) {
        if (itemMap.contains(tid) && !addedToMoved.contains(tid)) {
            movedItems.append(itemMap.value(tid));
            addedToMoved.insert(tid);
        }
    }

    if (movedItems.isEmpty()) {
        return items;
    }

    const int total = static_cast<int>(items.size());
    const int clampedPos = std::clamp(beforePosition, 0, total);
    std::optional<qint64> targetAnchorTrackId;
    for (int i = clampedPos; i < total; ++i) {
        if (!addedToMoved.contains(items.at(i).trackId)) {
            targetAnchorTrackId = items.at(i).trackId;
            break;
        }
    }

    QList<ItemRecord> remaining;
    for (const auto &it : items) {
        if (!addedToMoved.contains(it.trackId)) {
            remaining.append(it);
        }
    }

    int insertIdx = static_cast<int>(remaining.size());
    if (targetAnchorTrackId.has_value()) {
        for (int i = 0; i < remaining.size(); ++i) {
            if (remaining.at(i).trackId == targetAnchorTrackId.value()) {
                insertIdx = i;
                break;
            }
        }
    }

    for (int i = 0; i < movedItems.size(); ++i) {
        remaining.insert(insertIdx + i, movedItems.at(i));
    }
    return remaining;
}

} // namespace

PlaylistStore::PlaylistStore(Database &db)
    : m_db(db)
{
}

core::Result<QList<PlaylistInfo>> PlaylistStore::list() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery q(connRes.value());
    if (!q.exec(QStringLiteral(
            "SELECT id, name, kind, rule, position, created_at, updated_at FROM playlists ORDER BY "
            "position ASC"))) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<PlaylistInfo> result;
    while (q.next()) {
        result.append(parsePlaylistRow(q));
    }
    return result;
}

core::Result<std::optional<PlaylistInfo>> PlaylistStore::playlist(qint64 id) const
{
    if (id <= 0) {
        return std::optional<PlaylistInfo> { std::nullopt };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("SELECT id, name, kind, rule, position, created_at, updated_at FROM "
                             "playlists WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(id),
        };
    }

    if (!q.next()) {
        return std::optional<PlaylistInfo> { std::nullopt };
    }

    return std::make_optional(parsePlaylistRow(q));
}

core::Result<qint64> PlaylistStore::createManual(const QString &name, const QList<qint64> &trackIds)
{
    auto nameRes = validatePlaylistName(name);
    if (!nameRes.ok()) {
        return nameRes.error();
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<qint64> {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto insertRes
            = insertPlaylistRow(conn, nameRes.value(), QStringLiteral("manual"), QVariant(), now);
        if (!insertRes.ok()) {
            return insertRes.error();
        }
        const qint64 playlistId = insertRes.value();

        QList<qint64> uniqueTrackIds;
        uniqueTrackIds.reserve(trackIds.size());
        QSet<qint64> seen;
        for (const qint64 tid : trackIds) {
            if (!seen.contains(tid)) {
                seen.insert(tid);
                uniqueTrackIds.append(tid);
            }
        }

        if (!uniqueTrackIds.isEmpty()) {
            QList<ItemRecord> items;
            items.reserve(uniqueTrackIds.size());
            for (const qint64 tid : uniqueTrackIds) {
                items.append(ItemRecord { .trackId = tid, .addedAt = now });
            }
            if (auto res = rewriteItems(conn, playlistId, items); !res.ok()) {
                return res.error();
            }
        }
        return playlistId;
    });
}

core::Result<qint64> PlaylistStore::createSmart(const QString &name, const SmartRule &rule)
{
    auto nameRes = validatePlaylistName(name);
    if (!nameRes.ok()) {
        return nameRes.error();
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<qint64> {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        return insertPlaylistRow(
            conn, nameRes.value(), QStringLiteral("smart"), rule.toJson(), now);
    });
}

core::Result<void> PlaylistStore::rename(qint64 id, const QString &name)
{
    auto nameRes = validatePlaylistName(name);
    if (!nameRes.ok()) {
        return nameRes.error();
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto kindRes = getPlaylistKind(conn, id); !kindRes.ok()) {
            return kindRes.error();
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("UPDATE playlists SET name = ?, updated_at = ? WHERE id = ?"));
        q.addBindValue(nameRes.value());
        q.addBindValue(now);
        q.addBindValue(id);
        return execWrite(q, QString::number(id));
    });
}

core::Result<void> PlaylistStore::setRule(qint64 id, const SmartRule &rule)
{
    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto res = requireKind(conn, id, PlaylistKind::Smart,
                QStringLiteral("Cannot set rule on a non-smart playlist"));
            !res.ok()) {
            return res;
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("UPDATE playlists SET rule = ?, updated_at = ? WHERE id = ?"));
        q.addBindValue(rule.toJson());
        q.addBindValue(now);
        q.addBindValue(id);
        return execWrite(q, QString::number(id));
    });
}

core::Result<void> PlaylistStore::remove(qint64 id)
{
    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT position FROM playlists WHERE id = ?"));
        q.addBindValue(id);
        if (!q.exec() || !q.next()) {
            return core::Error {
                .code = QString(errc::kPlaylistInvalid),
                .message = QStringLiteral("Playlist not found"),
                .detail = QString::number(id),
            };
        }
        const int pos = q.value(0).toInt();

        QSqlQuery delPl(conn);
        delPl.prepare(QStringLiteral("DELETE FROM playlists WHERE id = ?"));
        delPl.addBindValue(id);
        if (auto res = execWrite(delPl, QString::number(id)); !res.ok()) {
            return res;
        }

        QSqlQuery updatePos(conn);
        updatePos.prepare(
            QStringLiteral("UPDATE playlists SET position = position - 1 WHERE position > ?"));
        updatePos.addBindValue(pos);
        return execWrite(updatePos, QString::number(pos));
    });
}

core::Result<int> PlaylistStore::addTracks(
    qint64 id, const QList<qint64> &trackIds, std::optional<int> beforePosition)
{
    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<int> {
        if (auto res = requireKind(conn, id, PlaylistKind::Manual,
                QStringLiteral("Cannot add tracks to a smart playlist"));
            !res.ok()) {
            return res.error();
        }

        auto itemsRes = fetchItems(conn, id);
        if (!itemsRes.ok()) {
            return itemsRes.error();
        }

        auto items = itemsRes.value();
        QSet<qint64> existing;
        for (const auto &it : items) {
            existing.insert(it.trackId);
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<ItemRecord> toAdd;
        for (const qint64 tid : trackIds) {
            if (!existing.contains(tid)) {
                existing.insert(tid);
                toAdd.append(ItemRecord { .trackId = tid, .addedAt = now });
            }
        }

        if (toAdd.isEmpty()) {
            return 0;
        }

        const int total = static_cast<int>(items.size());
        const int insertIdx
            = beforePosition.has_value() ? std::clamp(beforePosition.value(), 0, total) : total;
        for (int i = 0; i < toAdd.size(); ++i) {
            items.insert(insertIdx + i, toAdd.at(i));
        }

        if (auto rewriteRes = rewriteItems(conn, id, items); !rewriteRes.ok()) {
            return rewriteRes.error();
        }

        if (auto stampRes = updatePlaylistTimestamp(conn, id, now); !stampRes.ok()) {
            return stampRes.error();
        }

        return static_cast<int>(toAdd.size());
    });
}

core::Result<void> PlaylistStore::removeTracks(qint64 id, const QList<qint64> &trackIds)
{
    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto res = requireKind(conn, id, PlaylistKind::Manual,
                QStringLiteral("Cannot remove tracks from a smart playlist"));
            !res.ok()) {
            return res.error();
        }

        if (trackIds.isEmpty()) {
            return { };
        }

        auto itemsRes = fetchItems(conn, id);
        if (!itemsRes.ok()) {
            return itemsRes.error();
        }

        const QSet<qint64> toRemove(trackIds.begin(), trackIds.end());
        QList<ItemRecord> remaining;
        for (const auto &it : itemsRes.value()) {
            if (!toRemove.contains(it.trackId)) {
                remaining.append(it);
            }
        }

        if (auto rewriteRes = rewriteItems(conn, id, remaining); !rewriteRes.ok()) {
            return rewriteRes.error();
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        return updatePlaylistTimestamp(conn, id, now);
    });
}

core::Result<void> PlaylistStore::moveTracks(
    qint64 id, const QList<qint64> &trackIds, int beforePosition)
{
    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto res = requireKind(conn, id, PlaylistKind::Manual,
                QStringLiteral("Cannot move tracks in a smart playlist"));
            !res.ok()) {
            return res.error();
        }

        if (trackIds.isEmpty()) {
            return { };
        }

        auto itemsRes = fetchItems(conn, id);
        if (!itemsRes.ok()) {
            return itemsRes.error();
        }

        const auto &items = itemsRes.value();
        if (items.isEmpty()) {
            return { };
        }

        const QList<ItemRecord> reordered = reorderTrackItems(items, trackIds, beforePosition);

        if (auto rewriteRes = rewriteItems(conn, id, reordered); !rewriteRes.ok()) {
            return rewriteRes.error();
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        return updatePlaylistTimestamp(conn, id, now);
    });
}

} // namespace linernotes::library
