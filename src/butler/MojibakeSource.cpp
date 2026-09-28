// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MojibakeSource.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/Errors.h>
#include <butler/Mojibake.h>
#include <library/Database.h>
#include <library/EnumNames.h>

namespace linernotes::butler {

namespace {

using library::TagField;

QString tagFieldToRawKey(TagField field)
{
    switch (field) {
    case TagField::Title:
        return QStringLiteral("TITLE");
    case TagField::Artist:
        return QStringLiteral("ARTIST");
    case TagField::Album:
        return QStringLiteral("ALBUM");
    case TagField::AlbumArtist:
        return QStringLiteral("ALBUMARTIST");
    case TagField::Genre:
        return QStringLiteral("GENRE");
    case TagField::Composer:
        return QStringLiteral("COMPOSER");
    default:
        return { };
    }
}

const std::array<TagField, 6> kTextFields = {
    TagField::Title,
    TagField::Artist,
    TagField::Album,
    TagField::AlbumArtist,
    TagField::Genre,
    TagField::Composer,
};

QString fieldValueFromQuery(const QSqlQuery &q, TagField field)
{
    switch (field) {
    case TagField::Title:
        return q.value(2).toString();
    case TagField::Artist:
        return q.value(3).toString();
    case TagField::Album:
        return q.value(4).toString();
    case TagField::AlbumArtist:
        return q.value(5).toString();
    case TagField::Genre:
        return q.value(6).toString();
    case TagField::Composer:
        return q.value(7).toString();
    default:
        return { };
    }
}

core::Result<QSet<QPair<qint64, QString>>> fetchSkippedFields(const QSqlDatabase &conn)
{
    QSet<QPair<qint64, QString>> skipped;

    QSqlQuery qCorr(conn);
    if (!qCorr.exec(QStringLiteral("SELECT entity_id, field FROM corrections "
                                   "WHERE entity_type = 'track' AND status = 'pending'"))) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = qCorr.lastError().text(),
            .detail = QString(),
        };
    }
    while (qCorr.next()) {
        skipped.insert(qMakePair(qCorr.value(0).toLongLong(), qCorr.value(1).toString()));
    }

    QSqlQuery qOver(conn);
    if (!qOver.exec(QStringLiteral("SELECT track_id, field FROM user_overrides"))) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = qOver.lastError().text(),
            .detail = QString(),
        };
    }
    while (qOver.next()) {
        skipped.insert(qMakePair(qOver.value(0).toLongLong(), qOver.value(1).toString()));
    }

    return skipped;
}

QString buildGroupKey(const QString &dir, const QString &album)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("album"), album);
    obj.insert(QStringLiteral("dir"), dir);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

std::optional<QByteArray> fetchRawBytes(
    const QSqlDatabase &conn, qint64 trackId, TagField field, const QString &effectiveVal)
{
    const QString rawKey = tagFieldToRawKey(field);
    if (rawKey.isEmpty()) {
        return std::nullopt;
    }

    QSqlQuery rawQ(conn);
    rawQ.prepare(QStringLiteral("SELECT value, raw_bytes FROM raw_tags "
                                "WHERE track_id = ? AND key = ? "
                                "ORDER BY priority ASC, tag_type ASC, ordinal ASC "
                                "LIMIT 1"));
    rawQ.addBindValue(trackId);
    rawQ.addBindValue(rawKey);

    if (rawQ.exec() && rawQ.next()) {
        if (rawQ.value(0).toString() == effectiveVal && !rawQ.value(1).isNull()) {
            QByteArray bytes = rawQ.value(1).toByteArray();
            if (!bytes.isEmpty()) {
                return bytes;
            }
        }
    }
    return std::nullopt;
}

} // namespace

MojibakeSource::MojibakeSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QStringList> MojibakeSource::findGroups() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto skippedRes = fetchSkippedFields(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();

    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT t.id, f.path, em.title, em.artist, em.album, "
                                       "em.album_artist, em.genre, em.composer "
                                       "FROM tracks t "
                                       "JOIN files f ON f.id = t.file_id "
                                       "JOIN effective_metadata em ON em.track_id = t.id "
                                       "ORDER BY f.path ASC");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QMap<QPair<QString, QString>, QString> groupsMap;

    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString filePath = q.value(1).toString();
        const QString dir = QFileInfo(filePath).path();
        const QString album = q.value(4).toString();

        bool hasSuspect = false;
        for (const auto field : kTextFields) {
            const QString val = fieldValueFromQuery(q, field);
            if (val.isEmpty()) {
                continue;
            }
            const QString col = library::tagFieldToColumn(field);
            if (skipped.contains(qMakePair(trackId, col))) {
                continue;
            }
            if (looksLikeMojibake(val) || looksIrreparable(val)) {
                hasSuspect = true;
                break;
            }
        }

        if (hasSuspect) {
            const auto keyPair = qMakePair(dir, album);
            if (!groupsMap.contains(keyPair)) {
                groupsMap.insert(keyPair, buildGroupKey(dir, album));
            }
        }
    }

    return groupsMap.values();
}

core::Result<MojibakeGroup> MojibakeSource::loadGroup(const QString &key) const
{
    QJsonParseError parseErr { };
    const QJsonDocument doc = QJsonDocument::fromJson(key.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidKey),
            .message = QStringLiteral("Failed to parse group key JSON"),
            .detail = key,
        };
    }

    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("dir")) || !obj.contains(QStringLiteral("album"))) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidKey),
            .message = QStringLiteral("Group key JSON missing 'dir' or 'album'"),
            .detail = key,
        };
    }

    const QString dir = obj.value(QStringLiteral("dir")).toString();
    const QString album = obj.value(QStringLiteral("album")).toString();

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto skippedRes = fetchSkippedFields(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT t.id, f.path, em.title, em.artist, em.album, em.album_artist, "
                             "em.genre, em.composer "
                             "FROM tracks t "
                             "JOIN files f ON f.id = t.file_id "
                             "JOIN effective_metadata em ON em.track_id = t.id "
                             "WHERE f.path LIKE ? "
                             "ORDER BY f.path ASC, t.id ASC"));
    q.addBindValue(QString(dir + QStringLiteral("/%")));

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = q.lastError().text(),
            .detail = key,
        };
    }

    MojibakeGroup group;
    group.key = key;
    group.directory = dir;

    while (q.next()) {
        const QString filePath = q.value(1).toString();
        if (QFileInfo(filePath).path() != dir) {
            continue;
        }
        const QString trackAlbum = q.value(4).toString();
        if (trackAlbum != album) {
            continue;
        }

        const qint64 trackId = q.value(0).toLongLong();
        MojibakeTrack track;
        track.trackId = trackId;
        track.filePath = filePath;

        for (const auto field : kTextFields) {
            const QString val = fieldValueFromQuery(q, field);
            if (val.isEmpty()) {
                continue;
            }
            const QString col = library::tagFieldToColumn(field);
            const bool isSkipped = skipped.contains(qMakePair(trackId, col));
            const bool isSuspect = !isSkipped && (looksLikeMojibake(val) || looksIrreparable(val));

            if (isSuspect) {
                const auto rawBytes = fetchRawBytes(conn, trackId, field, val);
                track.suspects.append(MojibakeField {
                    .field = field,
                    .value = val,
                    .rawBytes = rawBytes,
                });
            } else {
                track.readable.append(MojibakeField {
                    .field = field,
                    .value = val,
                    .rawBytes = std::nullopt,
                });
            }
        }

        group.tracks.append(track);
    }

    if (group.tracks.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMojibakeGroupNotFound),
            .message = QStringLiteral("No tracks found for group key"),
            .detail = key,
        };
    }

    return group;
}

} // namespace linernotes::butler
