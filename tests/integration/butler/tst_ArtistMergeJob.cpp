// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/ArtistCluster.h>
#include <butler/ArtistMerge.h>
#include <butler/ArtistMergeSource.h>
#include <common/ManualClock.h>
#include <library/ArtistAliasCorrections.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::ArtistCluster;
using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistLink;
using linernotes::butler::ArtistLinkKind;
using linernotes::butler::ArtistMergeSource;
using linernotes::butler::clusterProposals;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }

    static qint64 getArtistId(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?;"));
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    }

    static QList<qint64> getTrackArtistIds(const QSqlDatabase &db, qint64 trackId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT artist_id FROM track_artists WHERE track_id = ? ORDER BY position;"));
        q.addBindValue(trackId);
        QList<qint64> ids;
        if (q.exec()) {
            while (q.next()) {
                ids.append(q.value(0).toLongLong());
            }
        }
        return ids;
    }
};

class TstArtistMergeJob : public QObject {
    Q_OBJECT

private slots:
    void clusterMergeAndAutoAccept();
};

void TstArtistMergeJob::clusterMergeAndAutoAccept()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_merge_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));
    const qint64 f4 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.mp3"));
    const qint64 f5 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/5.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);
    const qint64 t5 = TestDbHelper::insertTrack(conn, f5);

    // Amamiya Sora: 2 tracks (t1, t2)
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Amamiya Sora"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Amamiya Sora"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // Sora Amamiya: 1 track (t3)
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Song 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Sora Amamiya"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    // Yuki Kajiura: 1 track (t4)
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("TITLE"), QStringLiteral("Song 4"));
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("ARTIST"), QStringLiteral("Yuki Kajiura"));
    TestDbHelper::updateTagsReadAt(conn, t4);

    // Yuki Kaji: 1 track (t5)
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("TITLE"), QStringLiteral("Song 5"));
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("ARTIST"), QStringLiteral("Yuki Kaji"));
    TestDbHelper::updateTagsReadAt(conn, t5);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());
    QVERIFY(linker.linkTrack(t4).ok());
    QVERIFY(linker.linkTrack(t5).ok());

    const qint64 amamiyaSoraId = TestDbHelper::getArtistId(conn, QStringLiteral("Amamiya Sora"));
    const qint64 soraAmamiyaId = TestDbHelper::getArtistId(conn, QStringLiteral("Sora Amamiya"));
    const qint64 yukiKajiuraId = TestDbHelper::getArtistId(conn, QStringLiteral("Yuki Kajiura"));
    const qint64 yukiKajiId = TestDbHelper::getArtistId(conn, QStringLiteral("Yuki Kaji"));

    QVERIFY(amamiyaSoraId > 0);
    QVERIFY(soraAmamiyaId > 0);
    QVERIFY(yukiKajiuraId > 0);
    QVERIFY(yukiKajiId > 0);

    // Find items without MusicBrainz
    const ArtistMergeSource source(db);
    const auto itemsRes = source.findItems(false);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    int clusterItemCount = 0;
    QJsonObject clusterObj;

    for (const auto &itemStr : items) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() == QLatin1StringView("cluster")) {
            ++clusterItemCount;
            clusterObj = obj;
        }
    }

    // Only 1 cluster item found (Amamiya Sora / Sora Amamiya)
    QCOMPARE(clusterItemCount, 1);

    const auto idsArr = clusterObj.value(QStringLiteral("ids")).toArray();
    QCOMPARE(idsArr.size(), 2);
    const QList<qint64> clusterIds = { idsArr.at(0).toInteger(), idsArr.at(1).toInteger() };
    QVERIFY(clusterIds.contains(amamiyaSoraId));
    QVERIFY(clusterIds.contains(soraAmamiyaId));

    // Build cluster proposals and apply with autoAccept threshold 0.9 (since Romanized has 0.85,
    // test autoAccept=0.8)
    const ArtistCluster cluster {
        .members = {
            ArtistEntry {
                .artistId = amamiyaSoraId, .name = QStringLiteral("Amamiya Sora"), .trackCount = 2
            },
            ArtistEntry {
                .artistId = soraAmamiyaId, .name = QStringLiteral("Sora Amamiya"), .trackCount = 1
            },
        },
        .links = {
            ArtistLink { .a = amamiyaSoraId, .b = soraAmamiyaId, .kind = ArtistLinkKind::Romanized }
        },
    };

    const auto proposals = clusterProposals(cluster);
    QCOMPARE(proposals.size(), 1);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(
        CorrectionKind::ArtistMerge, QStringLiteral("Auto-merge romanized cluster"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    // Auto-accept proposals with threshold 0.8 (since confidence is 0.85)
    const auto addRes = store.addArtistAliasProposals(batchId, proposals, 0.8);
    QVERIFY(addRes.ok());

    // Track 3 is now linked to Amamiya Sora (amamiyaSoraId)
    const auto t3Artists = TestDbHelper::getTrackArtistIds(conn, t3);
    QCOMPARE(t3Artists.size(), 1);
    QCOMPARE(t3Artists.first(), amamiyaSoraId);

    // Sora Amamiya entity is merged and no longer exists
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Sora Amamiya")), -1);
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Amamiya Sora")), amamiyaSoraId);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMergeJob)

#include "tst_ArtistMergeJob.moc"
