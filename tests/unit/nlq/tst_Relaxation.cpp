// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>
#include <nlq/QueryRunner.h>
#include <nlq/Relaxation.h>

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

    static qint64 insertFile(
        const QSqlDatabase &db, qint64 rootId, const QString &path, qint64 durationMs = 200000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, ?, 1000, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
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
        const QString &artist, const QString &album)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE effective_metadata SET title=?, artist=?, album=? "
                                 "WHERE track_id=?"));
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
        q.prepare(QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role, position) "
                                 "VALUES (?, ?, ?, 0)"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        q.addBindValue(role);
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

class TstRelaxation : public QObject {
    Q_OBJECT

private slots:
    void relaxationRemoveCondition();
    void relaxationPlayWindowBeforeHistory();
};

void TstRelaxation::relaxationRemoveCondition()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_relaxation_cond.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 r = DbHelper::insertRoot(qDb);

    // Artists
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("宇多田ヒカル"));
    const qint64 art2 = DbHelper::insertArtist(qDb, QStringLiteral("周杰伦"));

    // Albums
    const qint64 alb1 = DbHelper::insertAlbum(
        qDb, QStringLiteral("First Love"), QStringLiteral("宇多田ヒカル"), 1999);
    const qint64 alb2
        = DbHelper::insertAlbum(qDb, QStringLiteral("范特西"), QStringLiteral("周杰伦"), 2001);

    // 2 Japanese tracks
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, alb1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Automatic"), QStringLiteral("宇多田ヒカル"),
        QStringLiteral("First Love"));
    DbHelper::addTrackArtist(qDb, t1, art1);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2, alb1);
    DbHelper::setMeta(qDb, t2, QStringLiteral("First Love"), QStringLiteral("宇多田ヒカル"),
        QStringLiteral("First Love"));
    DbHelper::addTrackArtist(qDb, t2, art1);

    // 1 Chinese track
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3, alb2);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("简单爱"), QStringLiteral("周杰伦"), QStringLiteral("范特西"));
    DbHelper::addTrackArtist(qDb, t3, art2);

    // No favorites in database

    const QueryRunner runner(db, library::PlayCountRule { });

    // Query: language is ja AND favorite isTrue
    Query query;
    query.entity = Entity::Track;
    query.rule.match = library::SmartMatch::All;
    query.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Language,
            .op = library::SmartOp::Is,
            .value = QStringLiteral("ja"),
            .value2 = { },
        },
        library::SmartCondition {
            .field = library::SmartField::Favorite,
            .op = library::SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        },
    };

    const auto initialRun = runner.run(query);
    QVERIFY(initialRun.ok());
    QVERIFY(initialRun.value().isEmpty());

    const auto analysisRes = analyzeEmptyResult(db, runner, query);
    QVERIFY(analysisRes.ok());
    const auto &analysis = analysisRes.value();

    QCOMPARE(analysis.relaxations.size(), 1);
    QCOMPARE(analysis.relaxations.at(0).kind, RelaxKind::RemoveCondition);
    QCOMPARE(analysis.relaxations.at(0).conditionIndex, 1);
    QCOMPARE(analysis.relaxations.at(0).resultCount, 2);
    QCOMPARE(analysis.windowBeforeHistory, false);
}

void TstRelaxation::relaxationPlayWindowBeforeHistory()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_relaxation_win.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("宇多田ヒカル"));
    const qint64 alb1 = DbHelper::insertAlbum(
        qDb, QStringLiteral("First Love"), QStringLiteral("宇多田ヒカル"), 1999);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, alb1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Automatic"), QStringLiteral("宇多田ヒカル"),
        QStringLiteral("First Love"));
    DbHelper::addTrackArtist(qDb, t1, art1);

    // Play event in 2026-09
    const qint64 peTime = QDateTime(QDate(2026, 9, 15), QTime(12, 0), QTimeZone::systemTimeZone())
                              .toMSecsSinceEpoch();
    DbHelper::insertPlayEvent(qDb, t1, peTime);

    const QueryRunner runner(db, library::PlayCountRule { });

    // Query window: 2025-12-01..2026-02-28
    Query query;
    query.entity = Entity::Track;
    query.rule.playedFrom = QDate(2025, 12, 1);
    query.rule.playedTo = QDate(2026, 2, 28);
    query.rule.conditions.append(library::SmartCondition {
        .field = library::SmartField::PlayCount,
        .op = library::SmartOp::Greater,
        .value = 0,
        .value2 = { },
    });

    const auto initialRun = runner.run(query);
    QVERIFY(initialRun.ok());
    QVERIFY(initialRun.value().isEmpty());

    const auto analysisRes = analyzeEmptyResult(db, runner, query);
    QVERIFY(analysisRes.ok());
    const auto &analysis = analysisRes.value();

    QCOMPARE(analysis.windowBeforeHistory, true);
    QVERIFY(analysis.firstPlayed.has_value());
    if (analysis.firstPlayed.has_value()) {
        QCOMPARE(analysis.firstPlayed.value(), QDate(2026, 9, 15));
    }
}

} // namespace

QTEST_MAIN(TstRelaxation)
#include "tst_Relaxation.moc"
