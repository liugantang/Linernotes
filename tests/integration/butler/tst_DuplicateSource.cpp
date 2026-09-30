// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/DuplicateFinder.h>
#include <butler/DuplicateSource.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/FingerprintStore.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::DuplicateKind;
using linernotes::butler::DuplicateSource;
using linernotes::butler::findDuplicates;
using linernotes::library::Database;
using linernotes::library::FingerprintStore;
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

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QString &contentHash, const QString &codec = QStringLiteral("flac"),
        int sampleRate = 44100, int bitDepth = 16, int bitrate = 0, qint64 durationMs = 200000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, codec, sample_rate, "
            "bit_depth, bitrate, duration_ms, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, ?, ?, ?, ?, ?, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash);
        q.addBindValue(codec);
        q.addBindValue(sampleRate);
        q.addBindValue(bitDepth);
        q.addBindValue(bitrate);
        q.addBindValue(durationMs);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertWork(
        const QSqlDatabase &db, qint64 workId, const QString &groupingKey, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO works (id, grouping_key, title, created_at) VALUES (?, ?, ?, 1000);"));
        q.addBindValue(workId);
        q.addBindValue(groupingKey);
        q.addBindValue(title);
        return q.exec();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId, qint64 workId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, work_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, ?, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(workId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertTrackVersion(const QSqlDatabase &db, qint64 trackId, const QString &baseTitle,
        const QString &versionType = QStringLiteral("studio"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_versions (track_id, base_title, version_type, unresolved, "
            "updated_at) "
            "VALUES (?, ?, ?, 0, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(baseTitle);
        q.addBindValue(versionType);
        return q.exec();
    }
};

class TstDuplicateSource : public QObject {
    Q_OBJECT

private slots:
    void detectsDuplicatesAndSavesGroups();
};

void TstDuplicateSource::detectsDuplicatesAndSavesGroups()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_dup_source.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.flac"),
        QStringLiteral("hash1"), QStringLiteral("flac"), 44100, 16, 0, 200000);
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"),
        QStringLiteral("hash2"), QStringLiteral("flac"), 44100, 16, 0, 200500);
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.flac"),
        QStringLiteral("hash3"), QStringLiteral("flac"), 44100, 16, 0, 201000);

    QVERIFY(f1 > 0);
    QVERIFY(f2 > 0);
    QVERIFY(f3 > 0);

    const qint64 workId = 1;
    QVERIFY(TestDbHelper::insertWork(
        conn, workId, QStringLiteral("work_song_a"), QStringLiteral("Song A")));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1, workId);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2, workId);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3, workId);

    QVERIFY(t1 > 0);
    QVERIFY(t2 > 0);
    QVERIFY(t3 > 0);

    QVERIFY(TestDbHelper::insertTrackVersion(conn, t1, QStringLiteral("Song A")));
    QVERIFY(TestDbHelper::insertTrackVersion(conn, t2, QStringLiteral("Song A")));
    QVERIFY(TestDbHelper::insertTrackVersion(conn, t3, QStringLiteral("Song A")));

    const ManualClock clock(1000);
    FingerprintStore fpStore(db, clock);

    QList<quint32> fpItems;
    fpItems.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        fpItems.append(0x12345678U ^ (i * 0x9e3779b9U));
    }

    QVERIFY(fpStore.save(f1, 1, fpItems).ok());
    QVERIFY(fpStore.save(f2, 1, fpItems).ok());

    const DuplicateSource source(db, clock);

    // 1. fingerprintCandidates should only return file 3 (which lacks fingerprint)
    const auto candRes = source.fingerprintCandidates();
    QVERIFY(candRes.ok());
    QCOMPARE(candRes.value(), (QList<qint64> { f3 }));

    // 2. loadTracks
    const auto tracksRes = source.loadTracks(true);
    QVERIFY(tracksRes.ok());
    const auto &tracks = tracksRes.value();
    QCOMPARE(tracks.size(), 3);
    QVERIFY(tracks.at(0).fingerprint.has_value());
    QVERIFY(tracks.at(1).fingerprint.has_value());
    QVERIFY(!tracks.at(2).fingerprint.has_value());

    const auto tracksWithoutFpRes = source.loadTracks(false);
    QVERIFY(tracksWithoutFpRes.ok());
    const auto &tracksWithoutFp = tracksWithoutFpRes.value();
    QCOMPARE(tracksWithoutFp.size(), 3);
    QVERIFY(!tracksWithoutFp.at(0).fingerprint.has_value());
    QVERIFY(!tracksWithoutFp.at(1).fingerprint.has_value());
    QVERIFY(!tracksWithoutFp.at(2).fingerprint.has_value());

    // 3. findDuplicates
    const auto groups = findDuplicates(tracks);
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().kind, DuplicateKind::SameRecording);
    QCOMPARE(groups.first().trackIds, (QList<qint64> { t1, t2 }));

    // 4. saveGroups
    const auto saveRes = source.saveGroups(groups, tracks);
    QVERIFY(saveRes.ok());

    // 5. countGroups
    const auto countRes = source.countGroups();
    QVERIFY(countRes.ok());
    const auto &counts = countRes.value();
    QCOMPARE(counts.value(DuplicateKind::SameRecording), 1);
    QCOMPARE(counts.value(DuplicateKind::Exact), 0);
    QCOMPARE(counts.value(DuplicateKind::Suspect), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstDuplicateSource)

#include "tst_DuplicateSource.moc"
