// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/SmartRule.h>
#include <nlq/Errors.h>
#include <nlq/NlqQuery.h>
#include <nlq/QueryRunner.h>
#include <nlq/SimilarQuery.h>
#include <rec/Recommender.h>
#include <rec/SoundIndex.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlayCountRule;
using linernotes::library::SmartCondition;
using linernotes::library::SmartField;
using linernotes::library::SmartOp;
using linernotes::nlq::Entity;
using linernotes::nlq::Query;
using linernotes::nlq::QueryRunner;
using linernotes::nlq::resolveSimilarSeed;
using linernotes::nlq::runSimilarQuery;
using linernotes::nlq::SimilarTo;
using linernotes::rec::Recommender;
using linernotes::rec::SoundIndex;
using linernotes::test::ManualClock;
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

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                           "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, 200000, 1000, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                                 "VALUES (?, 1, 1)"));
        q.addBindValue(fileId);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static void setMeta(const QSqlDatabase &db, qint64 trackId, const QString &title,
        const QString &artist, const QString &album, const QVariant &year = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE effective_metadata SET title=?, artist=?, album=?, year=? WHERE track_id=?"));
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        q.addBindValue(year);
        q.addBindValue(trackId);
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

class TstSimilarQuery : public QObject {
    Q_OBJECT

private slots:
    void resolveSimilarSeedTitleCaseInsensitive();
    void resolveSimilarSeedArtistDisambiguation();
    void resolveSimilarSeedPlayCountTieBreak();
    void resolveSimilarSeedCurrentMissingAndPresent();
    void resolveSimilarSeedNotFound();
    void runSimilarQueryFilteringExclusionAndLimit();
};

void TstSimilarQuery::resolveSimilarSeedTitleCaseInsensitive()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_seed_title.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 root = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, root, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Cruel Angel's Thesis"),
        QStringLiteral("Yoko Takahashi"), QStringLiteral("EVA Album"));

    const qint64 f2 = DbHelper::insertFile(qDb, root, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(qDb, t2, QStringLiteral("Fly Me to the Moon"), QStringLiteral("Claire"),
        QStringLiteral("EVA Album"));

    SimilarTo st;
    st.current = false;
    st.titles = { QStringLiteral("CRUEL ANGEL'S THESIS") };

    const auto res = resolveSimilarSeed(db, st, std::nullopt);
    QVERIFY(res.ok());
    QCOMPARE(res.value(), t1);
}

void TstSimilarQuery::resolveSimilarSeedArtistDisambiguation()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_seed_artist.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 root = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, root, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Hello"), QStringLiteral("Artist Alpha"),
        QStringLiteral("Album Alpha"));

    const qint64 f2 = DbHelper::insertFile(qDb, root, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(qDb, t2, QStringLiteral("Hello"), QStringLiteral("Artist Beta"),
        QStringLiteral("Album Beta"));

    SimilarTo st;
    st.current = false;
    st.titles = { QStringLiteral("Hello") };
    st.artists = { QStringLiteral("Beta") };

    const auto res = resolveSimilarSeed(db, st, std::nullopt);
    QVERIFY(res.ok());
    QCOMPARE(res.value(), t2);
}

void TstSimilarQuery::resolveSimilarSeedPlayCountTieBreak()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_seed_tie.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 root = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, root, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Common"), QStringLiteral("Artist"), QStringLiteral("Album"));
    DbHelper::setTrackPlayStats(qDb, t1, 5, 0, 1000);

    const qint64 f2 = DbHelper::insertFile(qDb, root, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Common"), QStringLiteral("Artist"), QStringLiteral("Album"));
    DbHelper::setTrackPlayStats(qDb, t2, 20, 0, 1000);

    SimilarTo st;
    st.current = false;
    st.titles = { QStringLiteral("Common") };

    const auto res = resolveSimilarSeed(db, st, std::nullopt);
    QVERIFY(res.ok());
    QCOMPARE(res.value(), t2);
}

void TstSimilarQuery::resolveSimilarSeedCurrentMissingAndPresent()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_seed_cur.db")));
    QVERIFY(db.open(Migrator()).ok());

    SimilarTo st;
    st.current = true;

    // Missing current track
    const auto resMissing = resolveSimilarSeed(db, st, std::nullopt);
    QVERIFY(!resMissing.ok());
    QCOMPARE(resMissing.error().code, QString(errc::kSimilarSeedMissing));

    // Invalid id <= 0
    const auto resInvalid = resolveSimilarSeed(db, st, -1);
    QVERIFY(!resInvalid.ok());
    QCOMPARE(resInvalid.error().code, QString(errc::kSimilarSeedMissing));

    // Valid current track id
    const auto resPresent = resolveSimilarSeed(db, st, 123);
    QVERIFY(resPresent.ok());
    QCOMPARE(resPresent.value(), 123);
}

void TstSimilarQuery::resolveSimilarSeedNotFound()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_seed_nf.db")));
    QVERIFY(db.open(Migrator()).ok());

    SimilarTo st;
    st.current = false;
    st.titles = { QStringLiteral("Nonexistent Song") };

    const auto res = resolveSimilarSeed(db, st, std::nullopt);
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, QString(errc::kSimilarSeedNotFound));
    QVERIFY(res.error().message.contains(QStringLiteral("Nonexistent Song")));
}

void TstSimilarQuery::runSimilarQueryFilteringExclusionAndLimit()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_similar_run.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 root = DbHelper::insertRoot(qDb);

    // Track 1: Seed track
    const qint64 f1 = DbHelper::insertFile(qDb, root, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Seed Song"), QStringLiteral("Artist A"),
        QStringLiteral("Album 1"), 2020);
    DbHelper::setTrackPlayStats(qDb, t1, 10, 0, 1000);

    // Track 2: Same artist & year, playCount = 5
    const qint64 f2 = DbHelper::insertFile(qDb, root, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(qDb, t2, QStringLiteral("Track 2"), QStringLiteral("Artist A"),
        QStringLiteral("Album 1"), 2020);
    DbHelper::setTrackPlayStats(qDb, t2, 5, 0, 1000);

    // Track 3: Same artist & year, playCount = 1
    const qint64 f3 = DbHelper::insertFile(qDb, root, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(qDb, t3, QStringLiteral("Track 3"), QStringLiteral("Artist A"),
        QStringLiteral("Album 1"), 2020);
    DbHelper::setTrackPlayStats(qDb, t3, 1, 0, 1000);

    // Track 4: Different artist, playCount = 15
    const qint64 f4 = DbHelper::insertFile(qDb, root, QStringLiteral("4.mp3"));
    const qint64 t4 = DbHelper::insertTrack(qDb, f4);
    DbHelper::setMeta(qDb, t4, QStringLiteral("Track 4"), QStringLiteral("Artist B"),
        QStringLiteral("Album 2"), 2010);
    DbHelper::setTrackPlayStats(qDb, t4, 15, 0, 1000);

    ManualClock clock(10000);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);
    QueryRunner runner(db, PlayCountRule { });

    // 1. Query with condition playCount > 2 and limit = 1
    {
        Query q;
        q.entity = Entity::Track;
        q.rule.conditions = {
            SmartCondition {
                .field = SmartField::PlayCount,
                .op = SmartOp::Greater,
                .value = 2,
                .value2 = { },
            },
        };
        q.limit = 1;

        const auto res = runSimilarQuery(runner, recommender, q, t1);
        QVERIFY(res.ok());
        const auto &results = res.value();
        QCOMPARE(results.size(), 1);
        QVERIFY(!results.contains(t1)); // Seed track must be excluded
        // Only t2 (playCount 5) or t4 (playCount 15) match condition; t1 is seed; t2 has same
        // artist/album as seed so higher score
        QCOMPARE(results.at(0), t2);
    }

    // 2. Query with limit = 10, no condition -> returns other tracks, excludes seed t1
    {
        Query q;
        q.entity = Entity::Track;
        q.limit = 10;

        const auto res = runSimilarQuery(runner, recommender, q, t1);
        QVERIFY(res.ok());
        const auto &results = res.value();
        QVERIFY(!results.isEmpty());
        QVERIFY(!results.contains(t1));
        QVERIFY(results.size() <= 10);
    }
}

} // namespace

QTEST_MAIN(TstSimilarQuery)
#include "tst_SimilarQuery.moc"
