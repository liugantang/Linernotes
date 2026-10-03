// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LocalLyrics.h"

#include "Errors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringDecoder>
#include <QStringTokenizer>

namespace linernotes::guide {

namespace {

QString decodeLyricsBytes(const QByteArray &rawBytes)
{
    QByteArray data = rawBytes;
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }

    // 1. Try UTF-8
    QStringDecoder utf8Decoder(QStringDecoder::Utf8);
    QString utf8Result = utf8Decoder(data);
    if (!utf8Decoder.hasError() && !utf8Result.contains(QChar::ReplacementCharacter)) {
        return utf8Result;
    }

    // 2. Try GB18030
    QStringDecoder gbDecoder("GB18030");
    if (gbDecoder.isValid()) {
        QString gbResult = gbDecoder(data);
        if (!gbDecoder.hasError() && !gbResult.contains(QChar::ReplacementCharacter)) {
            return gbResult;
        }
    }

    // 3. Try Shift_JIS
    QStringDecoder sjisDecoder("Shift_JIS");
    if (sjisDecoder.isValid()) {
        QString sjisResult = sjisDecoder(data);
        if (!sjisDecoder.hasError() && !sjisResult.contains(QChar::ReplacementCharacter)) {
            return sjisResult;
        }
    }

    // Fallback: UTF-8 result
    return utf8Result;
}

} // namespace

core::Result<std::optional<QString>> embeddedLyrics(library::Database &db, qint64 trackId)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT value FROM raw_tags "
                             "WHERE track_id = ? AND key IN ('LYRICS', 'UNSYNCED LYRICS') "
                             "ORDER BY priority ASC, ordinal ASC "
                             "LIMIT 1;"));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query embedded lyrics"),
            .detail = q.lastError().text(),
        };
    }
    if (!q.next()) {
        return std::optional<QString>(std::nullopt);
    }
    return std::optional<QString>(q.value(0).toString());
}

core::Result<bool> isCueTrack(library::Database &db, qint64 trackId)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT cue_index FROM tracks WHERE id = ?;"));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query cue index"),
            .detail = q.lastError().text(),
        };
    }
    if (!q.next()) {
        return false;
    }
    return !q.value(0).isNull();
}

bool isUsableLyrics(const QString &text)
{
    int count = 0;
    for (const auto line : QStringTokenizer { text, QLatin1Char('\n') }) {
        if (!line.trimmed().isEmpty()) {
            ++count;
            if (count >= 3) {
                return true;
            }
        }
    }
    return false;
}

std::optional<QString> sidecarLyrics(const QString &audioPath)
{
    if (audioPath.isEmpty()) {
        return std::nullopt;
    }

    const QFileInfo fi(audioPath);
    const QDir dir = fi.dir();
    const QString baseName = fi.completeBaseName();

    QString lrcPath;
    const QString lrcCandidate = dir.filePath(baseName + QStringLiteral(".lrc"));
    const QString lrcUpperCandidate = dir.filePath(baseName + QStringLiteral(".LRC"));

    if (QFile::exists(lrcCandidate)) {
        lrcPath = lrcCandidate;
    } else if (QFile::exists(lrcUpperCandidate)) {
        lrcPath = lrcUpperCandidate;
    } else {
        const auto entries = dir.entryInfoList({ baseName + QStringLiteral(".*") }, QDir::Files);
        for (const auto &entry : entries) {
            if (entry.completeBaseName() == baseName
                && entry.suffix().compare(QStringLiteral("lrc"), Qt::CaseInsensitive) == 0) {
                lrcPath = entry.absoluteFilePath();
                break;
            }
        }
    }

    if (lrcPath.isEmpty()) {
        return std::nullopt;
    }

    QFile file(lrcPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    const QByteArray rawBytes = file.readAll();
    return decodeLyricsBytes(rawBytes);
}

} // namespace linernotes::guide
