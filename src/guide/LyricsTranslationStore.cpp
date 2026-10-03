// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsTranslationStore.h"

#include "Errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace linernotes::guide {

LyricsTranslationStore::LyricsTranslationStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<std::optional<QStringList>> LyricsTranslationStore::get(
    qint64 trackId, const QString &lyricsHash, const QString &targetLang) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT lyrics_hash, lines FROM lyrics_translations "
                             "WHERE track_id = ? AND target_lang = ?;"));
    q.addBindValue(trackId);
    q.addBindValue(targetLang);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query lyrics translation"),
            .detail = q.lastError().text(),
        };
    }

    if (!q.next()) {
        return std::optional<QStringList>(std::nullopt);
    }

    const QString storedHash = q.value(0).toString();
    if (storedHash != lyricsHash) {
        return std::optional<QStringList>(std::nullopt);
    }

    const QString linesJson = q.value(1).toString();
    const auto doc = QJsonDocument::fromJson(linesJson.toUtf8());
    if (!doc.isArray()) {
        return std::optional<QStringList>(std::nullopt);
    }

    const QJsonArray arr = doc.array();
    QStringList result;
    result.reserve(arr.size());
    for (const auto &elem : arr) {
        result.append(elem.toString());
    }

    return std::optional<QStringList>(result);
}

core::Result<void> LyricsTranslationStore::save(qint64 trackId, const QString &lyricsHash,
    const QStringList &lines, const QString &model, int promptVersion,
    const QString &targetLang) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QJsonArray arr;
    for (const auto &line : lines) {
        arr.append(line);
    }
    const QJsonDocument doc(arr);
    const QString linesJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    const qint64 now = m_clock.nowMs();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT INTO lyrics_translations (track_id, lyrics_hash, target_lang, "
                             "lines, model, prompt_version, created_at) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?) "
                             "ON CONFLICT(track_id, target_lang) DO UPDATE SET "
                             "lyrics_hash = excluded.lyrics_hash, lines = excluded.lines, "
                             "model = excluded.model, prompt_version = excluded.prompt_version, "
                             "created_at = excluded.created_at;"));
    q.addBindValue(trackId);
    q.addBindValue(lyricsHash);
    q.addBindValue(targetLang);
    q.addBindValue(linesJson);
    q.addBindValue(model);
    q.addBindValue(promptVersion);
    q.addBindValue(now);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to save lyrics translation"),
            .detail = q.lastError().text(),
        };
    }

    return { };
}

} // namespace linernotes::guide
