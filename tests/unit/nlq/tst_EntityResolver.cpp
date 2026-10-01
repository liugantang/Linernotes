// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/SmartRule.h>
#include <nlq/EntityResolver.h>
#include <nlq/NlqQuery.h>

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

    static qint64 insertArtist(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1)"));
        q.addBindValue(name);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static void addArtistAlias(const QSqlDatabase &db, qint64 artistId, const QString &alias)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO artist_aliases (artist_id, alias, locale, kind, source, created_at) "
            "VALUES (?, ?, 'en', 'variant', 'tag', 1)"));
        q.addBindValue(artistId);
        q.addBindValue(alias);
        q.exec();
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
};

class TstEntityResolver : public QObject {
    Q_OBJECT

private slots:
    void aliasExactMatchUnique();
    void duplicateNameClarificationAndApplyChoice();
    void fuzzyMultipleAndZero();
};

void TstEntityResolver::aliasExactMatchUnique()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_exact.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 artId = DbHelper::insertArtist(qDb, QStringLiteral("周杰伦"));
    DbHelper::addArtistAlias(qDb, artId, QStringLiteral("Jay Chou"));

    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("晴天"), QStringLiteral("周杰伦"), QStringLiteral("叶惠美"));
    DbHelper::addTrackArtist(qDb, t1, artId);

    Query q;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Artist,
            .op = library::SmartOp::Contains,
            .value = QStringLiteral("Jay Chou"),
            .value2 = { },
        },
    };

    const auto res = resolveArtists(db, q);
    QVERIFY(res.ok());
    const auto &resolution = res.value();

    // No clarification needed
    QVERIFY(resolution.clarifications.isEmpty());

    // Condition rewritten to "artist is 周杰伦"
    QCOMPARE(resolution.query.rule.conditions.size(), 1);
    const auto &cond = resolution.query.rule.conditions.at(0);
    QCOMPARE(cond.field, library::SmartField::Artist);
    QCOMPARE(cond.op, library::SmartOp::Is);
    QCOMPARE(cond.value.toString(), QStringLiteral("周杰伦"));
}

void TstEntityResolver::duplicateNameClarificationAndApplyChoice()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_dup.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("John Williams"));
    const qint64 art2 = DbHelper::insertArtist(qDb, QStringLiteral("John Williams"));

    // art1 has 2 tracks
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Track 1"), QStringLiteral("John Williams"),
        QStringLiteral("Alb 1"));
    DbHelper::addTrackArtist(qDb, t1, art1);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(qDb, t2, QStringLiteral("Track 2"), QStringLiteral("John Williams"),
        QStringLiteral("Alb 1"));
    DbHelper::addTrackArtist(qDb, t2, art1);

    // art2 has 1 track
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(qDb, t3, QStringLiteral("Track 3"), QStringLiteral("John Williams"),
        QStringLiteral("Alb 2"));
    DbHelper::addTrackArtist(qDb, t3, art2);

    Query q;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Artist,
            .op = library::SmartOp::Is,
            .value = QStringLiteral("John Williams"),
            .value2 = { },
        },
    };

    const auto res = resolveArtists(db, q);
    QVERIFY(res.ok());
    const auto &resolution = res.value();

    // 1 clarification with 2 candidates sorted by trackCount DESC
    QCOMPARE(resolution.clarifications.size(), 1);
    const auto &clarification = resolution.clarifications.at(0);
    QCOMPARE(clarification.conditionIndex, 0);
    QCOMPARE(clarification.mention, QStringLiteral("John Williams"));
    QCOMPARE(clarification.candidates.size(), 2);
    QCOMPARE(clarification.candidates.at(0).artistId, art1);
    QCOMPARE(clarification.candidates.at(0).name, QStringLiteral("John Williams"));
    QCOMPARE(clarification.candidates.at(0).trackCount, 2);
    QCOMPARE(clarification.candidates.at(1).artistId, art2);
    QCOMPARE(clarification.candidates.at(1).name, QStringLiteral("John Williams"));
    QCOMPARE(clarification.candidates.at(1).trackCount, 1);

    // applyArtistChoice
    const auto updated = applyArtistChoice(
        resolution.query, clarification.conditionIndex, clarification.candidates.at(1));
    QCOMPARE(updated.rule.conditions.size(), 1);
    const auto &cond = updated.rule.conditions.at(0);
    QCOMPARE(cond.field, library::SmartField::Artist);
    QCOMPARE(cond.op, library::SmartOp::Is);
    QCOMPARE(cond.value.toString(), QStringLiteral("John Williams"));
}

void TstEntityResolver::fuzzyMultipleAndZero()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    library::Database db(dir.filePath(QStringLiteral("test_fuzzy.db")));
    QVERIFY(db.open(library::Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 art1 = DbHelper::insertArtist(qDb, QStringLiteral("陈奕迅"));
    const qint64 art2 = DbHelper::insertArtist(qDb, QStringLiteral("陈绮贞"));

    // art1 has 2 tracks
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("十年"), QStringLiteral("陈奕迅"), QStringLiteral("黑白灰"));
    DbHelper::addTrackArtist(qDb, t1, art1);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("K歌之王"), QStringLiteral("陈奕迅"), QStringLiteral("打得火热"));
    DbHelper::addTrackArtist(qDb, t2, art1);

    // art2 has 1 track
    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(qDb, t3, QStringLiteral("旅行的意义"), QStringLiteral("陈绮贞"),
        QStringLiteral("华丽的冒险"));
    DbHelper::addTrackArtist(qDb, t3, art2);

    // 1. Fuzzy match multiple: "陈" matches "陈奕迅" and "陈绮贞" -> Clarification
    {
        Query q;
        q.rule.conditions = {
            library::SmartCondition {
                .field = library::SmartField::Artist,
                .op = library::SmartOp::Contains,
                .value = QStringLiteral("陈"),
                .value2 = { },
            },
        };

        const auto res = resolveArtists(db, q);
        QVERIFY(res.ok());
        const auto &resolution = res.value();

        QCOMPARE(resolution.clarifications.size(), 1);
        const auto &clarification = resolution.clarifications.at(0);
        QCOMPARE(clarification.conditionIndex, 0);
        QCOMPARE(clarification.mention, QStringLiteral("陈"));
        QCOMPARE(clarification.candidates.size(), 2);
        QCOMPARE(clarification.candidates.at(0).artistId, art1);
        QCOMPARE(clarification.candidates.at(0).name, QStringLiteral("陈奕迅"));
        QCOMPARE(clarification.candidates.at(0).trackCount, 2);
        QCOMPARE(clarification.candidates.at(1).artistId, art2);
        QCOMPARE(clarification.candidates.at(1).name, QStringLiteral("陈绮贞"));
        QCOMPARE(clarification.candidates.at(1).trackCount, 1);
    }

    // 2. Fuzzy match 0: "林" matches nothing -> condition preserved as-is
    {
        Query qZero;
        qZero.rule.conditions = {
            library::SmartCondition {
                .field = library::SmartField::Artist,
                .op = library::SmartOp::Contains,
                .value = QStringLiteral("林"),
                .value2 = { },
            },
        };

        const auto res = resolveArtists(db, qZero);
        QVERIFY(res.ok());
        const auto &resolution = res.value();

        QVERIFY(resolution.clarifications.isEmpty());
        QCOMPARE(resolution.query.rule.conditions.size(), 1);
        const auto &cond = resolution.query.rule.conditions.at(0);
        QCOMPARE(cond.field, library::SmartField::Artist);
        QCOMPARE(cond.op, library::SmartOp::Contains);
        QCOMPARE(cond.value.toString(), QStringLiteral("林"));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstEntityResolver)
#include "tst_EntityResolver.moc"
