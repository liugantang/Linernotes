// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <guide/LyricsTranslationStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::guide::LyricsTranslationStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

struct TestDbHelper {
    static qint64 insertTrack(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(QStringLiteral("/music"));
        q.addBindValue(1000);
        if (!q.exec()) {
            return -1;
        }
        const qint64 rootId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1024, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(QStringLiteral("/music/test.flac"));
        if (!q.exec()) {
            return -1;
        }
        const qint64 fileId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, tags_read_at, created_at) VALUES (?, 2000, 2000);"));
        q.addBindValue(fileId);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }
};

class TstLyricsTranslationStore : public QObject {
    Q_OBJECT

private slots:
    void saveAndGetHit();
    void hashMismatchReturnsNullopt();
    void nonExistentTrackReturnsNullopt();
};

void TstLyricsTranslationStore::saveAndGetHit()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("test_trans.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    const ManualClock clock(5000);
    LyricsTranslationStore store(db, clock);

    const QString hash = QStringLiteral("sha1_hash_abc123");
    const QStringList lines
        = { QStringLiteral("Line 1 Trans"), QStringLiteral(""), QStringLiteral("Line 3 Trans") };

    // Save
    const auto saveRes = store.save(trackId, hash, lines, QStringLiteral("gpt-4o"), 1);
    QVERIFY(saveRes.ok());

    // Get
    const auto getRes = store.get(trackId, hash);
    QVERIFY(getRes.ok());
    QVERIFY(getRes.value().has_value());
    if (!getRes.value().has_value()) {
        return;
    }
    QCOMPARE(*getRes.value(), lines);
}

void TstLyricsTranslationStore::hashMismatchReturnsNullopt()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("test_trans2.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    const ManualClock clock(5000);
    LyricsTranslationStore store(db, clock);

    const QString hash = QStringLiteral("sha1_hash_orig");
    const QStringList lines = { QStringLiteral("Line 1 Trans") };

    QVERIFY(store.save(trackId, hash, lines, QStringLiteral("gpt-4o"), 1).ok());

    // Query with different hash -> nullopt
    const auto getRes = store.get(trackId, QStringLiteral("sha1_hash_modified"));
    QVERIFY(getRes.ok());
    QVERIFY(!getRes.value().has_value());
}

void TstLyricsTranslationStore::nonExistentTrackReturnsNullopt()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("test_trans3.db")));
    QVERIFY(db.open(Migrator()).ok());

    const ManualClock clock(5000);
    LyricsTranslationStore store(db, clock);

    const auto getRes = store.get(999999, QStringLiteral("any_hash"));
    QVERIFY(getRes.ok());
    QVERIFY(!getRes.value().has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstLyricsTranslationStore)

#include "tst_LyricsTranslationStore.moc"
