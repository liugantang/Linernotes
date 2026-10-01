// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <library/Database.h>
#include <library/Migrator.h>
#include <nlq/LibrarySummary.h>

namespace {

using namespace linernotes;
using namespace linernotes::nlq;

struct DbHelper {
    static qint64 insertRoot(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        if (!q.exec(
                QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES ('/m', 100);"))) {
            return 0;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        qint64 durationMs = 200000, const QVariant &missingSince = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "missing_since, first_seen_at, scanned_at) VALUES (?, ?, 1, 1, ?, "
                                 "?, 1000, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
        q.addBindValue(missingSince);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertAlbum(const QSqlDatabase &db, const QString &title,
        const QVariant &albumArtist = QVariant(), const QVariant &year = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO albums (grouping_key, title, album_artist, year, created_at) "
            "VALUES (?, ?, ?, ?, 1)"));
        q.addBindValue(QString(title + albumArtist.toString()));
        q.addBindValue(title);
        q.addBindValue(albumArtist);
        q.addBindValue(year);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertArtist(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1)"));
        q.addBindValue(name);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fileId, const QVariant &albumId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, album_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, 1, 1)"));
        q.addBindValue(fileId);
        q.addBindValue(albumId);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static void setMeta(const QSqlDatabase &db, qint64 trackId, const QString &title,
        const QString &artist, const QString &album, const QString &genre = QString(),
        const QVariant &year = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE effective_metadata SET title=?, artist=?, album=?, "
                                 "genre=?, year=? WHERE track_id=?"));
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        q.addBindValue(genre);
        q.addBindValue(year);
        q.addBindValue(trackId);
        q.exec();
    }

    static void addTrackArtist(const QSqlDatabase &db, qint64 trackId, qint64 artistId,
        const QString &role = QStringLiteral("artist"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role, position) "
                                 "VALUES (?, ?, ?, 0)"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        q.addBindValue(role);
        q.exec();
    }

    static void setTrackVersion(const QSqlDatabase &db, qint64 trackId, const QString &baseTitle,
        const QString &versionType)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO track_versions (track_id, base_title, version_type, "
                                 "unresolved, updated_at) "
                                 "VALUES (?, ?, ?, 0, 1)"));
        q.addBindValue(trackId);
        q.addBindValue(baseTitle);
        q.addBindValue(versionType);
        q.exec();
    }

    static void insertPlayEvent(const QSqlDatabase &db, qint64 trackId, qint64 startedAt)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO play_events (track_id, started_at, played_ms, "
                           "track_duration_ms, completed, skipped) VALUES (?, ?, 200000, 200000, "
                           "1, 0)"));
        q.addBindValue(trackId);
        q.addBindValue(startedAt);
        q.exec();
    }
};

class TstLibrarySummary : public QObject {
    Q_OBJECT

private slots:
    void buildSummarySmallLibrary();
    void buildSummaryEmptyLibrary();
    void renderSummaryBasic();
    void renderSummaryNoPlayEvents();
    void renderSummaryWinterJanuary();
};

void TstLibrarySummary::buildSummarySmallLibrary()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_summary.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 r = DbHelper::insertRoot(qDb);

    // Artists
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("宇多田ヒカル"));
    const qint64 art2 = DbHelper::insertArtist(qDb, QStringLiteral("周杰伦"));
    const qint64 art3 = DbHelper::insertArtist(qDb, QStringLiteral("The Beatles"));
    const qint64 art4 = DbHelper::insertArtist(qDb, QStringLiteral("Hidden Artist"));

    // Albums
    const qint64 alb1 = DbHelper::insertAlbum(
        qDb, QStringLiteral("First Love"), QStringLiteral("宇多田ヒカル"), 1999);
    const qint64 alb2
        = DbHelper::insertAlbum(qDb, QStringLiteral("范特西"), QStringLiteral("周杰伦"), 2001);
    const qint64 alb3 = DbHelper::insertAlbum(
        qDb, QStringLiteral("Abbey Road"), QStringLiteral("The Beatles"), 1969);
    const qint64 alb4 = DbHelper::insertAlbum(
        qDb, QStringLiteral("Hidden Album"), QStringLiteral("Hidden Artist"), 2020);

    // Tracks
    // Track 1: visible, J-Pop, 1999, Japanese (ja), studio (no track_versions entry)
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, alb1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Automatic"), QStringLiteral("宇多田ヒカル"),
        QStringLiteral("First Love"), QStringLiteral("J-Pop"), 1999);
    DbHelper::addTrackArtist(qDb, t1, art1);

    // Track 2: visible, J-Pop, 1999, Japanese (ja), remix
    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2, alb1);
    DbHelper::setMeta(qDb, t2, QStringLiteral("First Love (Remix)"), QStringLiteral("宇多田ヒカル"),
        QStringLiteral("First Love"), QStringLiteral("J-Pop"), 1999);
    DbHelper::addTrackArtist(qDb, t2, art1);
    DbHelper::setTrackVersion(qDb, t2, QStringLiteral("First Love"), QStringLiteral("remix"));

    // Track 3: visible, Pop, 2001, Chinese (zh), studio (no track_versions entry)
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3, alb2);
    DbHelper::setMeta(qDb, t3, QStringLiteral("简单爱"), QStringLiteral("周杰伦"),
        QStringLiteral("范特西"), QStringLiteral("Pop"), 2001);
    DbHelper::addTrackArtist(qDb, t3, art2);

    // Track 4: visible, Rock, 1969, Western (western), live
    const qint64 f4 = DbHelper::insertFile(qDb, r, QStringLiteral("4.mp3"));
    const qint64 t4 = DbHelper::insertTrack(qDb, f4, alb3);
    DbHelper::setMeta(qDb, t4, QStringLiteral("Come Together"), QStringLiteral("The Beatles"),
        QStringLiteral("Abbey Road"), QStringLiteral("Rock"), 1969);
    DbHelper::addTrackArtist(qDb, t4, art3);
    DbHelper::setTrackVersion(qDb, t4, QStringLiteral("Come Together"), QStringLiteral("live"));

    // Track 5: missing (missing_since set), Metal, 2020, Western (western), demo
    const qint64 f5 = DbHelper::insertFile(qDb, r, QStringLiteral("5.mp3"), 200000, 12345);
    const qint64 t5 = DbHelper::insertTrack(qDb, f5, alb4);
    DbHelper::setMeta(qDb, t5, QStringLiteral("Ghost Song"), QStringLiteral("Hidden Artist"),
        QStringLiteral("Hidden Album"), QStringLiteral("Metal"), 2020);
    DbHelper::addTrackArtist(qDb, t5, art4);
    DbHelper::setTrackVersion(qDb, t5, QStringLiteral("Ghost Song"), QStringLiteral("demo"));

    // Play Events
    // Event 1: 2026-09-25 10:00:00 UTC
    const qint64 pe1Time
        = QDateTime(QDate(2026, 9, 25), QTime(10, 0), QTimeZone::utc()).toMSecsSinceEpoch();
    DbHelper::insertPlayEvent(qDb, t1, pe1Time);

    // Event 2: 2026-10-01 12:00:00 UTC
    const qint64 pe2Time
        = QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::utc()).toMSecsSinceEpoch();
    DbHelper::insertPlayEvent(qDb, t3, pe2Time);

    const QDate today(2026, 10, 1);
    const QString timeZoneId = QStringLiteral("Asia/Shanghai");
    const auto res = buildLibrarySummary(db, today, timeZoneId);
    QVERIFY(res.ok());
    const auto &summary = res.value();

    QCOMPARE(summary.today, today);
    QCOMPARE(summary.timeZoneId, timeZoneId);
    QCOMPARE(summary.trackCount, 4);
    QCOMPARE(summary.albumCount, 3);
    QCOMPARE(summary.artistCount, 3);
    QCOMPARE(summary.minYear, std::optional<int>(1969));
    QCOMPARE(summary.maxYear, std::optional<int>(2001));
    QCOMPARE(summary.playEventCount, 2);
    QCOMPARE(summary.firstPlayed, std::optional<QDate>(QDate(2026, 9, 25)));
    QCOMPARE(summary.lastPlayed, std::optional<QDate>(QDate(2026, 10, 1)));

    // Top Genres
    QCOMPARE(summary.topGenres.size(), 3);
    QCOMPARE(summary.topGenres.at(0).first, QStringLiteral("J-Pop"));
    QCOMPARE(summary.topGenres.at(0).second, 2);

    // Languages
    QCOMPARE(summary.languages.size(), 3);
    QCOMPARE(summary.languages.at(0).first, library::TrackLanguage::Japanese);
    QCOMPARE(summary.languages.at(0).second, 2);

    // Versions
    QCOMPARE(summary.versionTypes.size(), 3);
    QCOMPARE(summary.versionTypes.at(0).first, library::VersionType::Studio);
    QCOMPARE(summary.versionTypes.at(0).second, 2);
}

void TstLibrarySummary::buildSummaryEmptyLibrary()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_summary_empty.db")));
    QVERIFY(db.open(library::Migrator()).ok());

    const QDate today(2026, 10, 1);
    const QString timeZoneId = QStringLiteral("Asia/Shanghai");
    const auto res = buildLibrarySummary(db, today, timeZoneId);
    QVERIFY(res.ok());
    const auto &summary = res.value();

    QCOMPARE(summary.trackCount, 0);
    QCOMPARE(summary.albumCount, 0);
    QCOMPARE(summary.artistCount, 0);
    QVERIFY(!summary.minYear.has_value());
    QVERIFY(!summary.maxYear.has_value());
    QCOMPARE(summary.playEventCount, 0);
    QVERIFY(!summary.firstPlayed.has_value());
    QVERIFY(!summary.lastPlayed.has_value());
    QVERIFY(summary.topGenres.isEmpty());
    QVERIFY(summary.languages.isEmpty());
    QVERIFY(summary.versionTypes.isEmpty());
}

void TstLibrarySummary::renderSummaryBasic()
{
    LibrarySummary summary;
    summary.today = QDate(2026, 10, 1);
    summary.timeZoneId = QStringLiteral("Asia/Shanghai");
    summary.trackCount = 15536;
    summary.albumCount = 2270;
    summary.artistCount = 2100;
    summary.minYear = 1965;
    summary.maxYear = 2026;
    summary.firstPlayed = QDate(2026, 9, 25);
    summary.lastPlayed = QDate(2026, 10, 1);
    summary.playEventCount = 342;
    summary.topGenres = {
        qMakePair(QStringLiteral("J-Pop"), 3200),
        qMakePair(QStringLiteral("Anime"), 2100),
    };
    summary.languages = {
        qMakePair(library::TrackLanguage::Japanese, 9000),
        qMakePair(library::TrackLanguage::Chinese, 3000),
        qMakePair(library::TrackLanguage::Western, 2500),
        qMakePair(library::TrackLanguage::Korean, 200),
        qMakePair(library::TrackLanguage::Other, 36),
    };
    summary.versionTypes = {
        qMakePair(library::VersionType::Studio, 14638),
        qMakePair(library::VersionType::Remix, 869),
    };

    const QString rendered = renderLibrarySummary(summary);
    QVERIFY(rendered.contains(QStringLiteral("今天：2026-10-01（星期四），时区 Asia/Shanghai")));
    QVERIFY(rendered.contains(QStringLiteral("2025-12-01 至 2026-02-28")));
    QVERIFY(rendered.contains(QStringLiteral("“去年冬天”指 2025-12-01 至 2026-02-28")));
    QVERIFY(rendered.contains(
        QStringLiteral("曲库：15536 首，2270 张专辑，2100 位艺人；年份 1965–2026")));
    QVERIFY(rendered.contains(QStringLiteral("播放记录：2026-09-25 至 2026-10-01，共 342 次")));
    QVERIFY(rendered.contains(QStringLiteral("流派（曲目数）：J-Pop 3200、Anime 2100")));
    QVERIFY(rendered.contains(
        QStringLiteral("语言（曲目数）：ja 9000、zh 3000、western 2500、ko 200、other 36")));
    QVERIFY(rendered.contains(QStringLiteral("版本（曲目数）：studio 14638、remix 869")));
}

void TstLibrarySummary::renderSummaryNoPlayEvents()
{
    LibrarySummary summary;
    summary.today = QDate(2026, 10, 1);
    summary.timeZoneId = QStringLiteral("Asia/Shanghai");
    summary.trackCount = 0;
    summary.albumCount = 0;
    summary.artistCount = 0;
    summary.playEventCount = 0;

    const QString rendered = renderLibrarySummary(summary);
    QVERIFY(rendered.contains(QStringLiteral("播放记录：无")));
    QVERIFY(rendered.contains(QStringLiteral("流派（曲目数）：无")));
    QVERIFY(rendered.contains(QStringLiteral("语言（曲目数）：无")));
    QVERIFY(rendered.contains(QStringLiteral("版本（曲目数）：无")));
}

void TstLibrarySummary::renderSummaryWinterJanuary()
{
    LibrarySummary summary;
    summary.today = QDate(2026, 1, 15);
    summary.timeZoneId = QStringLiteral("Asia/Shanghai");

    const QString rendered = renderLibrarySummary(summary);
    QVERIFY(rendered.contains(QStringLiteral("“今年冬天”指 2025-12-01 至 2026-02-28")));
}

} // namespace

QTEST_MAIN(TstLibrarySummary)
#include "tst_LibrarySummary.moc"
