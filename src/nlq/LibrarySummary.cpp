// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibrarySummary.h"

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimeZone>

#include <library/EnumNames.h>
#include <library/Errors.h>

#include <array>

namespace linernotes::nlq {

namespace {

core::Result<void> populateTrackCountsAndYears(const QSqlDatabase &qDb, LibrarySummary &summary)
{
    QSqlQuery q(qDb);
    if (!q.exec(QStringLiteral(
            "SELECT COUNT(*), MIN(year), MAX(year) FROM track_sort WHERE visible = 1;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query track counts and years"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        summary.trackCount = q.value(0).toInt();
        if (!q.value(1).isNull()) {
            summary.minYear = q.value(1).toInt();
        }
        if (!q.value(2).isNull()) {
            summary.maxYear = q.value(2).toInt();
        }
    }

    return { };
}

core::Result<void> populateAlbumAndArtistCounts(const QSqlDatabase &qDb, LibrarySummary &summary)
{
    QSqlQuery albQ(qDb);
    if (!albQ.exec(QStringLiteral("SELECT COUNT(DISTINCT album_id) FROM track_sort WHERE visible = "
                                  "1 AND album_id IS NOT NULL;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query album count"),
            .detail = albQ.lastError().text(),
        };
    }
    if (albQ.next()) {
        summary.albumCount = albQ.value(0).toInt();
    }

    QSqlQuery artQ(qDb);
    const QString artSql = QStringLiteral(
        "SELECT COUNT(*) FROM artists ar "
        "WHERE ("
        "  EXISTS (SELECT 1 FROM track_artists ta JOIN track_sort ts ON ta.track_id = ts.track_id "
        "WHERE ta.artist_id = ar.id AND ta.role = 'artist' AND ts.visible = 1)"
        "  OR EXISTS (SELECT 1 FROM album_artists aa JOIN track_sort ts ON aa.album_id = "
        "ts.album_id WHERE aa.artist_id = ar.id AND ts.visible = 1)"
        ");");
    if (!artQ.exec(artSql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query artist count"),
            .detail = artQ.lastError().text(),
        };
    }
    if (artQ.next()) {
        summary.artistCount = artQ.value(0).toInt();
    }

    return { };
}

core::Result<void> populatePlayEvents(
    const QSqlDatabase &qDb, const QString &timeZoneId, LibrarySummary &summary)
{
    QSqlQuery q(qDb);
    if (!q.exec(QStringLiteral(
            "SELECT COUNT(*), MIN(started_at), MAX(started_at) FROM play_events;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query play events"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        const int count = q.value(0).toInt();
        summary.playEventCount = count;
        if (count > 0 && !q.value(1).isNull() && !q.value(2).isNull()) {
            const qint64 minStarted = q.value(1).toLongLong();
            const qint64 maxStarted = q.value(2).toLongLong();
            QTimeZone tz(timeZoneId.toUtf8());
            if (!tz.isValid()) {
                tz = QTimeZone::systemTimeZone();
            }
            summary.firstPlayed = QDateTime::fromMSecsSinceEpoch(minStarted, tz).date();
            summary.lastPlayed = QDateTime::fromMSecsSinceEpoch(maxStarted, tz).date();
        }
    }

    return { };
}

core::Result<void> populateTopGenres(const QSqlDatabase &qDb, LibrarySummary &summary)
{
    QSqlQuery q(qDb);
    const QString sql
        = QStringLiteral("SELECT genre, COUNT(*) AS cnt FROM track_sort "
                         "WHERE visible = 1 AND genre IS NOT NULL AND trim(genre) != '' "
                         "GROUP BY genre "
                         "ORDER BY cnt DESC, genre ASC "
                         "LIMIT 20;");
    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query top genres"),
            .detail = q.lastError().text(),
        };
    }

    while (q.next()) {
        summary.topGenres.append(qMakePair(q.value(0).toString(), q.value(1).toInt()));
    }

    return { };
}

core::Result<void> populateLanguages(const QSqlDatabase &qDb, LibrarySummary &summary)
{
    QSqlQuery q(qDb);
    const QString sql = QStringLiteral(
        "SELECT linernotes_lang(ts.title, ts.album, ts.artist) AS lang, COUNT(*) AS cnt "
        "FROM track_sort ts "
        "WHERE ts.visible = 1 "
        "GROUP BY lang "
        "ORDER BY cnt DESC, lang ASC;");
    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query languages"),
            .detail = q.lastError().text(),
        };
    }

    while (q.next()) {
        const auto optLang = library::trackLanguageFromString(q.value(0).toString());
        if (optLang.has_value()) {
            summary.languages.append(qMakePair(optLang.value(), q.value(1).toInt()));
        }
    }

    return { };
}

core::Result<void> populateVersionTypes(const QSqlDatabase &qDb, LibrarySummary &summary)
{
    QSqlQuery q(qDb);
    const QString sql
        = QStringLiteral("SELECT COALESCE(tv.version_type, 'studio') AS vt, COUNT(*) AS cnt "
                         "FROM track_sort ts "
                         "LEFT JOIN track_versions tv ON tv.track_id = ts.track_id "
                         "WHERE ts.visible = 1 "
                         "GROUP BY vt "
                         "ORDER BY cnt DESC, vt ASC;");
    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query version types"),
            .detail = q.lastError().text(),
        };
    }

    while (q.next()) {
        const auto optVt = library::versionTypeFromString(q.value(0).toString());
        if (optVt.has_value()) {
            summary.versionTypes.append(qMakePair(optVt.value(), q.value(1).toInt()));
        }
    }

    return { };
}

QString renderTodayLine(const LibrarySummary &summary)
{
    static constexpr std::array<const char *, 7> kWeekdays
        = { "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日" };
    const int dayOfWeek = summary.today.dayOfWeek();
    QString weekdayStr;
    if (dayOfWeek >= 1 && dayOfWeek <= 7) {
        weekdayStr = QString::fromUtf8(kWeekdays.at(dayOfWeek - 1));
    }
    return QStringLiteral("今天：%1（%2），时区 %3")
        .arg(summary.today.toString(QStringLiteral("yyyy-MM-dd")), weekdayStr, summary.timeZoneId);
}

QString renderSeasonLine(const LibrarySummary &summary)
{
    const int year = summary.today.year();
    const int month = summary.today.month();
    const QString winterLabel
        = (month == 1 || month == 2) ? QStringLiteral("今年冬天") : QStringLiteral("去年冬天");
    const QDate winterStart(year - 1, 12, 1);
    const QDate winterEnd(year, 2, QDate(year, 2, 1).daysInMonth());

    return QStringLiteral(
        "季节按北半球：春 3–5 月，夏 6–8 月，秋 9–11 月，冬 12–次年 2 月（“%1”指 %2 至 %3）")
        .arg(winterLabel, winterStart.toString(QStringLiteral("yyyy-MM-dd")),
            winterEnd.toString(QStringLiteral("yyyy-MM-dd")));
}

QString renderLibraryCountsLine(const LibrarySummary &summary)
{
    QString line = QStringLiteral("曲库：%1 首，%2 张专辑，%3 位艺人")
                       .arg(summary.trackCount)
                       .arg(summary.albumCount)
                       .arg(summary.artistCount);

    if (summary.minYear.has_value() && summary.maxYear.has_value()) {
        if (summary.minYear.value() == summary.maxYear.value()) {
            line += QStringLiteral("；年份 %1").arg(summary.minYear.value());
        } else {
            line += QStringLiteral("；年份 %1–%2")
                        .arg(summary.minYear.value())
                        .arg(summary.maxYear.value());
        }
    }

    return line;
}

QString renderPlayRecordsLine(const LibrarySummary &summary)
{
    if (summary.playEventCount > 0 && summary.firstPlayed.has_value()
        && summary.lastPlayed.has_value()) {
        return QStringLiteral("播放记录：%1 至 %2，共 %3 次")
            .arg(summary.firstPlayed->toString(QStringLiteral("yyyy-MM-dd")),
                summary.lastPlayed->toString(QStringLiteral("yyyy-MM-dd")))
            .arg(summary.playEventCount);
    }
    return QStringLiteral("播放记录：无");
}

QString renderTopGenresLine(const LibrarySummary &summary)
{
    if (summary.topGenres.isEmpty()) {
        return QStringLiteral("流派（曲目数）：无");
    }
    QStringList items;
    items.reserve(summary.topGenres.size());
    for (const auto &pair : summary.topGenres) {
        items.append(QStringLiteral("%1 %2").arg(pair.first).arg(pair.second));
    }
    return QStringLiteral("流派（曲目数）：%1").arg(items.join(QStringLiteral("、")));
}

QString renderLanguagesLine(const LibrarySummary &summary)
{
    if (summary.languages.isEmpty()) {
        return QStringLiteral("语言（曲目数）：无");
    }
    QStringList items;
    items.reserve(summary.languages.size());
    for (const auto &pair : summary.languages) {
        items.append(QStringLiteral("%1 %2")
                .arg(library::trackLanguageToString(pair.first))
                .arg(pair.second));
    }
    return QStringLiteral("语言（曲目数）：%1").arg(items.join(QStringLiteral("、")));
}

QString renderVersionTypesLine(const LibrarySummary &summary)
{
    if (summary.versionTypes.isEmpty()) {
        return QStringLiteral("版本（曲目数）：无");
    }
    QStringList items;
    items.reserve(summary.versionTypes.size());
    for (const auto &pair : summary.versionTypes) {
        items.append(
            QStringLiteral("%1 %2").arg(library::versionTypeToString(pair.first)).arg(pair.second));
    }
    return QStringLiteral("版本（曲目数）：%1").arg(items.join(QStringLiteral("、")));
}

} // namespace

core::Result<LibrarySummary> buildLibrarySummary(
    library::Database &db, QDate today, const QString &timeZoneId)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = QString(library::errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const QSqlDatabase &qDb = connRes.value();

    LibrarySummary summary;
    summary.today = today;
    summary.timeZoneId = timeZoneId;

    if (const auto res = populateTrackCountsAndYears(qDb, summary); !res.ok()) {
        return res.error();
    }
    if (const auto res = populateAlbumAndArtistCounts(qDb, summary); !res.ok()) {
        return res.error();
    }
    if (const auto res = populatePlayEvents(qDb, timeZoneId, summary); !res.ok()) {
        return res.error();
    }
    if (const auto res = populateTopGenres(qDb, summary); !res.ok()) {
        return res.error();
    }
    if (const auto res = populateLanguages(qDb, summary); !res.ok()) {
        return res.error();
    }
    if (const auto res = populateVersionTypes(qDb, summary); !res.ok()) {
        return res.error();
    }

    return summary;
}

QString renderLibrarySummary(const LibrarySummary &summary)
{
    const QStringList lines {
        renderTodayLine(summary),
        renderSeasonLine(summary),
        renderLibraryCountsLine(summary),
        renderPlayRecordsLine(summary),
        renderTopGenresLine(summary),
        renderLanguagesLine(summary),
        renderVersionTypesLine(summary),
    };
    return lines.join(QLatin1Char('\n'));
}

} // namespace linernotes::nlq
