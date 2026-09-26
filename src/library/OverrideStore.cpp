// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "OverrideStore.h"

#include <QDateTime>
#include <QMetaEnum>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/SearchIndex.h>

#include <utility>

namespace linernotes::library {

namespace {

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

QString fieldToColumn(TagField field)
{
    switch (field) {
    case TagField::Title:
        return QStringLiteral("title");
    case TagField::Artist:
        return QStringLiteral("artist");
    case TagField::Album:
        return QStringLiteral("album");
    case TagField::AlbumArtist:
        return QStringLiteral("album_artist");
    case TagField::Genre:
        return QStringLiteral("genre");
    case TagField::Composer:
        return QStringLiteral("composer");
    case TagField::Year:
        return QStringLiteral("year");
    case TagField::TrackNumber:
        return QStringLiteral("track_number");
    case TagField::TrackTotal:
        return QStringLiteral("track_total");
    case TagField::DiscNumber:
        return QStringLiteral("disc_number");
    case TagField::DiscTotal:
        return QStringLiteral("disc_total");
    }
    return { };
}

core::Result<void> validateEdits(const QList<TagEdit> &edits)
{
    for (const auto &edit : edits) {
        if (!edit.value.has_value()) {
            continue;
        }
        const QString trimmed = edit.value->trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (edit.field == TagField::Year) {
            bool ok = false;
            const int y = trimmed.toInt(&ok);
            if (!ok || y < 1 || y > 9999 || trimmed.startsWith(u'+') || trimmed.startsWith(u'-')) {
                return core::Error {
                    .code = QString(errc::kTagOverrideInvalid),
                    .message = QStringLiteral("Year must be an integer between 1 and 9999"),
                    .detail = trimmed,
                };
            }
        } else if (edit.field == TagField::TrackNumber || edit.field == TagField::TrackTotal
            || edit.field == TagField::DiscNumber || edit.field == TagField::DiscTotal) {
            bool ok = false;
            const int n = trimmed.toInt(&ok);
            if (!ok || n < 0 || trimmed.startsWith(u'+') || trimmed.startsWith(u'-')) {
                return core::Error {
                    .code = QString(errc::kTagOverrideInvalid),
                    .message
                    = QStringLiteral("Track and disc numbers must be non-negative integers"),
                    .detail = trimmed,
                };
            }
        }
    }
    return { };
}

core::Result<void> writeOverrides(
    const QSqlDatabase &conn, const QList<qint64> &trackIds, const QList<TagEdit> &edits)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery deleteStmt(conn);
    deleteStmt.prepare(
        QStringLiteral("DELETE FROM user_overrides WHERE track_id = ? AND field = ?"));

    QSqlQuery upsertStmt(conn);
    upsertStmt.prepare(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(track_id, field) DO UPDATE SET "
        "value = excluded.value, updated_at = excluded.updated_at"));

    for (const qint64 trackId : trackIds) {
        for (const auto &edit : edits) {
            const QString col = fieldToColumn(edit.field);
            if (col.isEmpty()) {
                continue;
            }
            if (!edit.value.has_value()) {
                deleteStmt.bindValue(0, trackId);
                deleteStmt.bindValue(1, col);
                if (auto res = execWrite(deleteStmt, QString::number(trackId)); !res.ok()) {
                    return res;
                }
            } else {
                const QString trimmed = edit.value->trimmed();
                upsertStmt.bindValue(0, trackId);
                upsertStmt.bindValue(1, col);
                upsertStmt.bindValue(2, trimmed);
                upsertStmt.bindValue(3, now);
                upsertStmt.bindValue(4, now);
                if (auto res = execWrite(upsertStmt, QString::number(trackId)); !res.ok()) {
                    return res;
                }
            }
        }
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

} // namespace

OverrideStore::OverrideStore(Database &db)
    : m_db(db)
{
}

core::Result<QHash<TagField, QString>> OverrideStore::effectiveValues(qint64 trackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    // 按枚举顺序选出各字段列；数值列经 QVariant::toString() 转成十进制字符串，NULL 为空串
    const QMetaEnum meta = QMetaEnum::fromType<TagField>();
    QList<TagField> fields;
    QStringList columns;
    for (int i = 0; i < meta.keyCount(); ++i) {
        const auto field = static_cast<TagField>(meta.value(i));
        fields.append(field);
        columns.append(fieldToColumn(field));
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT %1 FROM effective_metadata WHERE track_id = ?")
            .arg(columns.join(QStringLiteral(", "))));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(trackId),
        };
    }

    QHash<TagField, QString> values;
    const bool found = q.next();
    for (int i = 0; i < fields.size(); ++i) {
        values.insert(fields.at(i), found ? q.value(i).toString() : QString());
    }

    return values;
}

core::Result<QSet<TagField>> OverrideStore::overriddenFields(qint64 trackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT field FROM user_overrides WHERE track_id = ?"));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(trackId),
        };
    }

    QSet<TagField> fields;
    while (q.next()) {
        const QString fieldStr = q.value(0).toString();
        if (const auto field = detail::enumFromName<TagField>(fieldStr, fieldToColumn);
            field.has_value()) {
            fields.insert(*field);
        }
    }
    return fields;
}

core::Result<void> OverrideStore::apply(const QList<qint64> &trackIds, const QList<TagEdit> &edits)
{
    if (trackIds.isEmpty() || edits.isEmpty()) {
        return { };
    }

    if (auto valRes = validateEdits(edits); !valRes.ok()) {
        return valRes;
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto res = writeOverrides(conn, trackIds, edits); !res.ok()) {
            return res;
        }
        return relinkTracks(conn, trackIds);
    });
}

} // namespace linernotes::library
