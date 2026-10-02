// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/SmartRule.h>
#include <nlq/Errors.h>
#include <nlq/NlqQuery.h>
#include <nlq/QueryRunner.h>

namespace {

using namespace linernotes;
using namespace linernotes::nlq;
namespace errc = linernotes::nlq::errc;

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
        qint64 durationMs = 200000, qint64 firstSeenAt = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, ?, ?, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
        q.addBindValue(firstSeenAt);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertAlbum(
        const QSqlDatabase &db, const QString &title, const QVariant &albumArtist = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, created_at) "
                           "VALUES (?, ?, ?, 1)"));
        q.addBindValue(QString(title + albumArtist.toString()));
        q.addBindValue(title);
        q.addBindValue(albumArtist);
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
        const QString &artist, const QString &album)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE effective_metadata SET title=?, artist=?, album=? WHERE track_id=?"));
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        q.addBindValue(trackId);
        q.exec();
    }

    static void addTrackArtist(const QSqlDatabase &db, qint64 trackId, qint64 artistId,
        const QString &role = QStringLiteral("artist"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_artists (track_id, artist_id, role, position) VALUES (?, ?, ?, 0)"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        q.addBindValue(role);
        q.exec();
    }

    static void insertPlayEvent(const QSqlDatabase &db, qint64 trackId, qint64 startedAt,
        qint64 playedMs, qint64 trackDurationMs, int completed, int skipped)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO play_events (track_id, started_at, played_ms, track_duration_ms, "
            "completed, skipped) VALUES (?, ?, ?, ?, ?, ?)"));
        q.addBindValue(trackId);
        q.addBindValue(startedAt);
        q.addBindValue(playedMs);
        q.addBindValue(trackDurationMs);
        q.addBindValue(completed);
        q.addBindValue(skipped);
        q.exec();
    }

    static void setTrackPlayStats(const QSqlDatabase &db, qint64 trackId, int playCount,
        int skipCount, const QVariant &lastPlayedAt)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO track_play_stats (track_id, play_count, skip_count, "
            "last_played_at) VALUES (?, ?, ?, ?)"));
        q.addBindValue(trackId);
        q.addBindValue(playCount);
        q.addBindValue(skipCount);
        q.addBindValue(lastPlayedAt);
        q.exec();
    }
};

class TstNlqQuery : public QObject {
    Q_OBJECT

private slots:
    void jsonRoundTrip();
    void jsonDefaults();
    void jsonErrorsAndClamping();
    void jsonSimilarToRoundTrip();
    void jsonSimilarToValidation();
    void runnerTrackWindowedPlayCount();
    void runnerAlbumAggregatedPlayCount();
    void runnerArtistAggregated();
    void runnerSqlInjectionSafe();
    void runnerMatchingTrackIdsReturnsAll();
};

void TstNlqQuery::jsonSimilarToRoundTrip()
{
    // 1. Current track seed
    {
        Query q;
        q.entity = Entity::Track;
        q.similarTo = SimilarTo {
            .current = true,
            .titles = { },
            .artists = { },
        };
        q.sortKey = SortKey::Default;
        q.limit = 50;

        const QJsonObject json = q.toJson();
        QVERIFY(json.contains(QStringLiteral("similarTo")));
        const QJsonObject stObj = json.value(QStringLiteral("similarTo")).toObject();
        QCOMPARE(stObj.value(QStringLiteral("current")).toBool(), true);
        QVERIFY(!stObj.contains(QStringLiteral("title")));
        QVERIFY(!stObj.contains(QStringLiteral("artist")));

        const auto res = Query::fromJson(json);
        QVERIFY(res.ok());
        QCOMPARE(res.value(), q);
    }

    // 2. Named track seed with array titles & artists
    {
        Query q;
        q.entity = Entity::Track;
        q.similarTo = SimilarTo {
            .current = false,
            .titles
            = { QStringLiteral("残酷な天使のテーゼ"), QStringLiteral("残酷天使的行动纲领") },
            .artists = { QStringLiteral("高橋洋子") },
        };
        q.sortKey = SortKey::Default;
        q.limit = 50;

        const QJsonObject json = q.toJson();
        QVERIFY(json.contains(QStringLiteral("similarTo")));
        const QJsonObject stObj = json.value(QStringLiteral("similarTo")).toObject();
        QVERIFY(!stObj.value(QStringLiteral("current")).toBool());
        QCOMPARE(stObj.value(QStringLiteral("title")).toArray().size(), 2);
        QCOMPARE(stObj.value(QStringLiteral("artist")).toArray().size(), 1);

        const auto res = Query::fromJson(json);
        QVERIFY(res.ok());
        QCOMPARE(res.value(), q);
    }

    // 3. fromJson with single-string title and artist
    {
        QJsonObject stObj;
        stObj.insert(QStringLiteral("title"), QStringLiteral("Single Title"));
        stObj.insert(QStringLiteral("artist"), QStringLiteral("Single Artist"));

        QJsonObject queryObj;
        queryObj.insert(QStringLiteral("entity"), QStringLiteral("track"));
        queryObj.insert(QStringLiteral("similarTo"), stObj);

        const auto res = Query::fromJson(queryObj);
        QVERIFY(res.ok());
        const auto &q = res.value();
        QCOMPARE(q.similarTo,
            std::optional<SimilarTo>(SimilarTo {
                .current = false,
                .titles = { QStringLiteral("Single Title") },
                .artists = { QStringLiteral("Single Artist") },
            }));
    }
}

void TstNlqQuery::jsonSimilarToValidation()
{
    // Album with similarTo is rejected
    {
        Query q;
        q.entity = Entity::Album;
        q.similarTo = SimilarTo { .current = true, .titles = { }, .artists = { } };
        const auto res = q.validate();
        QVERIFY(!res.ok());
    }

    // current = false with empty titles is rejected
    {
        Query q;
        q.entity = Entity::Track;
        q.similarTo = SimilarTo { .current = false, .titles = { }, .artists = { } };
        const auto res = q.validate();
        QVERIFY(!res.ok());
    }

    // current = false with valid titles is accepted
    {
        Query q;
        q.entity = Entity::Track;
        q.similarTo = SimilarTo {
            .current = false,
            .titles = { QStringLiteral("Valid Title") },
            .artists = { },
        };
        const auto res = q.validate();
        QVERIFY(res.ok());
    }
}

void TstNlqQuery::jsonRoundTrip()
{
    Query q;
    q.entity = Entity::Track;
    q.rule.match = library::SmartMatch::All;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::PlayCount,
            .op = library::SmartOp::Greater,
            .value = 3,
            .value2 = { },
        },
    };
    q.rule.playedFrom = QDate(2025, 12, 1);
    q.rule.playedTo = QDate(2026, 2, 28);
    q.sortKey = SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 20;

    const QJsonObject json = q.toJson();
    QCOMPARE(json.value(QStringLiteral("entity")).toString(), QStringLiteral("track"));
    QCOMPARE(json.value(QStringLiteral("match")).toString(), QStringLiteral("all"));
    QCOMPARE(json.value(QStringLiteral("playedFrom")).toString(), QStringLiteral("2025-12-01"));
    QCOMPARE(json.value(QStringLiteral("playedTo")).toString(), QStringLiteral("2026-02-28"));
    QCOMPARE(json.value(QStringLiteral("sort")).toString(), QStringLiteral("playCount"));
    QCOMPARE(json.value(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    QCOMPARE(json.value(QStringLiteral("limit")).toInt(), 20);

    const auto res = Query::fromJson(json);
    QVERIFY(res.ok());
    QCOMPARE(res.value(), q);
}

void TstNlqQuery::jsonDefaults()
{
    const QJsonObject emptyObj;
    const auto res = Query::fromJson(emptyObj);
    QVERIFY(res.ok());
    const auto &q = res.value();
    QCOMPARE(q.entity, Entity::Track);
    QCOMPARE(q.sortKey, SortKey::Default);
    QCOMPARE(q.sortOrder, Qt::DescendingOrder);
    QCOMPARE(q.limit, 50);
    QCOMPARE(q.rule.match, library::SmartMatch::All);
    QVERIFY(q.rule.conditions.isEmpty());
    QVERIFY(!q.rule.playedFrom.has_value());
    QVERIFY(!q.rule.playedTo.has_value());
}

void TstNlqQuery::jsonErrorsAndClamping()
{
    // Unknown entity
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("entity"), QStringLiteral("podcast"));
        const auto res = Query::fromJson(obj);
        QVERIFY(!res.ok());
    }

    // Unknown sort
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("sort"), QStringLiteral("popularity"));
        const auto res = Query::fromJson(obj);
        QVERIFY(!res.ok());
    }

    // Unknown order
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("order"), QStringLiteral("random_order"));
        const auto res = Query::fromJson(obj);
        QVERIFY(!res.ok());
    }

    // Limit 0 clamped to 1
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("limit"), 0);
        const auto res = Query::fromJson(obj);
        QVERIFY(res.ok());
        QCOMPARE(res.value().limit, 1);
    }

    // Limit 9999 clamped to 500
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("limit"), 9999);
        const auto res = Query::fromJson(obj);
        QVERIFY(res.ok());
        QCOMPARE(res.value().limit, 500);
    }
}

void TstNlqQuery::runnerTrackWindowedPlayCount()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_track_win.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);

    // Track 1: 3 plays in Jan 2026, 0 outside
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"), 200000);
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Song 1"), QStringLiteral("Art"), QStringLiteral("Alb"));
    const qint64 inJan = QDateTime(QDate(2026, 1, 15), QTime(12, 0)).toMSecsSinceEpoch();
    DbHelper::insertPlayEvent(qDb, t1, inJan, 200000, 200000, 1, 0);
    DbHelper::insertPlayEvent(qDb, t1, inJan + 1000, 200000, 200000, 1, 0);
    DbHelper::insertPlayEvent(qDb, t1, inJan + 2000, 200000, 200000, 1, 0);

    // Track 2: 1 play in Jan 2026, 10 plays in 2025
    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"), 200000);
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Song 2"), QStringLiteral("Art"), QStringLiteral("Alb"));
    DbHelper::insertPlayEvent(qDb, t2, inJan, 200000, 200000, 1, 0);
    const qint64 in2025 = QDateTime(QDate(2025, 6, 1), QTime(12, 0)).toMSecsSinceEpoch();
    for (int i = 0; i < 10; ++i) {
        DbHelper::insertPlayEvent(qDb, t2, in2025 + (qint64 { i } * 1000), 200000, 200000, 1, 0);
    }

    // Track 3: 0 plays in Jan 2026, 20 plays in 2025
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"), 200000);
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("Song 3"), QStringLiteral("Art"), QStringLiteral("Alb"));
    for (int i = 0; i < 20; ++i) {
        DbHelper::insertPlayEvent(qDb, t3, in2025 + (qint64 { i } * 1000), 200000, 200000, 1, 0);
    }

    QueryRunner runner(db, library::PlayCountRule { });

    Query q;
    q.entity = Entity::Track;
    q.rule.playedFrom = QDate(2026, 1, 1);
    q.rule.playedTo = QDate(2026, 1, 31);
    q.sortKey = SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 2;

    const auto res = runner.run(q);
    QVERIFY(res.ok());
    const auto &ids = res.value();
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.at(0), t1);
    QCOMPARE(ids.at(1), t2);
}

void TstNlqQuery::runnerAlbumAggregatedPlayCount()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_album_agg.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 alb1 = DbHelper::insertAlbum(qDb, QStringLiteral("Album One"));
    const qint64 alb2 = DbHelper::insertAlbum(qDb, QStringLiteral("Album Two"));

    // Album 1: Track 1 ("Rock Hit", play_count 5), Track 2 ("Pop Hit", play_count 20)
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, alb1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Rock Hit"), QStringLiteral("Art"), QStringLiteral("Album One"));
    DbHelper::setTrackPlayStats(qDb, t1, 5, 0, 1000);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2, alb1);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Pop Hit"), QStringLiteral("Art"), QStringLiteral("Album One"));
    DbHelper::setTrackPlayStats(qDb, t2, 20, 0, 1000);

    // Album 2: Track 3 ("Rock Anthem", play_count 2), Track 4 ("Rock Ballad", play_count 1)
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3, alb2);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("Rock Anthem"), QStringLiteral("Art"), QStringLiteral("Album Two"));
    DbHelper::setTrackPlayStats(qDb, t3, 2, 0, 1000);

    const qint64 f4 = DbHelper::insertFile(qDb, r, QStringLiteral("4.mp3"));
    const qint64 t4 = DbHelper::insertTrack(qDb, f4, alb2);
    DbHelper::setMeta(
        qDb, t4, QStringLiteral("Rock Ballad"), QStringLiteral("Art"), QStringLiteral("Album Two"));
    DbHelper::setTrackPlayStats(qDb, t4, 1, 0, 1000);

    QueryRunner runner(db, library::PlayCountRule { });

    Query q;
    q.entity = Entity::Album;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Title,
            .op = library::SmartOp::Contains,
            .value = QStringLiteral("Rock"),
            .value2 = { },
        },
    };
    q.sortKey = SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 10;

    const auto res = runner.run(q);
    QVERIFY(res.ok());
    const auto &ids = res.value();
    QCOMPARE(ids.size(), 2);
    // Album 1 (only Rock Hit matched -> sum play_count 5) > Album 2 (sum play_count 2 + 1 = 3)
    QCOMPARE(ids.at(0), alb1);
    QCOMPARE(ids.at(1), alb2);
}

void TstNlqQuery::runnerArtistAggregated()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_artist_agg.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("Artist Alpha"));
    const qint64 art2 = DbHelper::insertArtist(qDb, QStringLiteral("Artist Beta"));

    // Artist 1: t1 duration 100s, t2 duration 200s -> total 300s
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"), 100000);
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Song 1"), QStringLiteral("Artist Alpha"), QStringLiteral("Alb"));
    DbHelper::addTrackArtist(qDb, t1, art1);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"), 200000);
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Song 2"), QStringLiteral("Artist Alpha"), QStringLiteral("Alb"));
    DbHelper::addTrackArtist(qDb, t2, art1);

    // Artist 2: t3 duration 500s -> total 500s
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"), 500000);
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("Song 3"), QStringLiteral("Artist Beta"), QStringLiteral("Alb"));
    DbHelper::addTrackArtist(qDb, t3, art2);

    QueryRunner runner(db, library::PlayCountRule { });

    Query q;
    q.entity = Entity::Artist;
    q.sortKey = SortKey::Duration;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 10;

    const auto res = runner.run(q);
    QVERIFY(res.ok());
    const auto &ids = res.value();
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.at(0), art2);
    QCOMPARE(ids.at(1), art1);
}

void TstNlqQuery::runnerSqlInjectionSafe()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_injection.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Ordinary Song"), QStringLiteral("Art"), QStringLiteral("Alb"));

    QueryRunner runner(db, library::PlayCountRule { });

    Query q;
    q.entity = Entity::Track;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Title,
            .op = library::SmartOp::Contains,
            .value = QStringLiteral("x' OR 1=1 --"),
            .value2 = { },
        },
    };

    const auto res = runner.run(q);
    QVERIFY(res.ok());
    QVERIFY(res.value().isEmpty());
}

void TstNlqQuery::runnerMatchingTrackIdsReturnsAll()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_matching_ids.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);

    QSet<qint64> expectedIds;
    expectedIds.reserve(600);

    {
        library::Transaction tx(qDb);
        for (int i = 0; i < 600; ++i) {
            const qint64 f
                = DbHelper::insertFile(qDb, r, QStringLiteral("song_%1.mp3").arg(i), 200000);
            const qint64 t = DbHelper::insertTrack(qDb, f);
            DbHelper::setMeta(qDb, t, QStringLiteral("Song %1").arg(i), QStringLiteral("Artist"),
                QStringLiteral("Album"));
            expectedIds.insert(t);
        }
        QVERIFY(tx.commit().ok());
    }

    QueryRunner runner(db, library::PlayCountRule { });

    // 1. Without conditions (matching all visible tracks, ignoring limit)
    {
        Query q;
        q.entity = Entity::Track;
        q.limit = 10;

        const auto res = runner.matchingTrackIds(q);
        QVERIFY(res.ok());
        QCOMPARE(res.value().size(), 600);
        QCOMPARE(res.value(), expectedIds);

        // runner.run still respects limit
        const auto runRes = runner.run(q);
        QVERIFY(runRes.ok());
        QCOMPARE(runRes.value().size(), 10);
    }

    // 2. Entity other than Track is rejected
    {
        Query albumQuery;
        albumQuery.entity = Entity::Album;
        const auto albumRes = runner.matchingTrackIds(albumQuery);
        QVERIFY(!albumRes.ok());
        QCOMPARE(albumRes.error().code, QString(errc::kQueryInvalid));
    }
}

} // namespace

QTEST_MAIN(TstNlqQuery)
#include "tst_NlqQuery.moc"
