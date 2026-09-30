// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <library/AlbumCovers.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::CoverStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::setOnlineAlbumCover;

class TstAlbumCovers : public QObject {
    Q_OBJECT

private slots:
    void setsOnlineCoverForAlbum();
};

void TstAlbumCovers::setsOnlineCoverForAlbum()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    Database db(dbPath);
    QVERIFY(db.open(Migrator()).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // 1. Insert two albums without cover
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO albums (id, grouping_key, title, created_at) "
                                      "VALUES (1, 'g1', 'Album 1', 1000);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO albums (id, grouping_key, title, created_at) "
                                      "VALUES (2, 'g2', 'Album 2', 1000);")));
    }

    // 2. Set online cover for album 1 (initially has no cover)
    CoverStore::Info info1;
    info1.hash = QStringLiteral("hash_online_1");
    info1.mime = QStringLiteral("image/jpeg");
    info1.width = 1400;
    info1.height = 1400;
    const QUrl url1(QStringLiteral("https://example.com/cover1.jpg"));

    const auto res1 = setOnlineAlbumCover(db, 1, info1, url1, 2000);
    QVERIFY(res1.ok());

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT a.cover_id, c.hash, c.mime, c.width, c.height, c.source, c.source_path "
            "FROM albums a JOIN covers c ON a.cover_id = c.id WHERE a.id = 1;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(1).toString(), QStringLiteral("hash_online_1"));
        QCOMPARE(q.value(2).toString(), QStringLiteral("image/jpeg"));
        QCOMPARE(q.value(3).toInt(), 1400);
        QCOMPARE(q.value(4).toInt(), 1400);
        QCOMPARE(q.value(5).toString(), QStringLiteral("online"));
        QCOMPARE(q.value(6).toString(), url1.toString());
    }

    // 3. Replace cover for album 1 with another cover
    CoverStore::Info info2;
    info2.hash = QStringLiteral("hash_online_2");
    info2.mime = QStringLiteral("image/jpeg");
    info2.width = 1400;
    info2.height = 1400;
    const QUrl url2(QStringLiteral("https://example.com/cover2.jpg"));

    const auto res2 = setOnlineAlbumCover(db, 1, info2, url2, 3000);
    QVERIFY(res2.ok());

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("SELECT a.cover_id, c.hash, c.source FROM albums a JOIN "
                                      "covers c ON a.cover_id = c.id WHERE a.id = 1;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(1).toString(), QStringLiteral("hash_online_2"));
        QCOMPARE(q.value(2).toString(), QStringLiteral("online"));
    }

    // 4. Set cover for album 2 using same hash_online_1 -> should not duplicate row in covers table
    const auto res3 = setOnlineAlbumCover(db, 2, info1, url1, 4000);
    QVERIFY(res3.ok());

    {
        QSqlQuery q(conn);
        QVERIFY(
            q.exec(QStringLiteral("SELECT COUNT(*) FROM covers WHERE hash = 'hash_online_1';")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);

        QVERIFY(q.exec(QStringLiteral("SELECT a.cover_id, c.hash FROM albums a JOIN covers c ON "
                                      "a.cover_id = c.id WHERE a.id = 2;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(1).toString(), QStringLiteral("hash_online_1"));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstAlbumCovers)
#include "tst_AlbumCovers.moc"
