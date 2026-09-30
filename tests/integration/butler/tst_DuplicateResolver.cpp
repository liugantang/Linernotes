// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <butler/DuplicateFinder.h>
#include <butler/DuplicateResolver.h>
#include <butler/DuplicateSource.h>
#include <butler/Errors.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::DuplicateResolver;
using linernotes::butler::DuplicateSource;
using linernotes::butler::FileTrash;
using linernotes::butler::findDuplicates;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;

class TestFileTrash final : public FileTrash {
public:
    explicit TestFileTrash(QString trashDir, QString failPath = QString())
        : m_trashDir(std::move(trashDir))
        , m_failPath(std::move(failPath))
    {
    }

    linernotes::core::Result<void> moveToTrash(const QString &path) override
    {
        if (!m_failPath.isEmpty() && path == m_failPath) {
            return linernotes::core::Error {
                .code = QString(linernotes::butler::errc::kDuplicateTrashFailed),
                .message = QStringLiteral("Simulated trash failure"),
                .detail = path,
            };
        }

        const QFileInfo fi(path);
        const QString targetPath = QDir(m_trashDir).filePath(fi.fileName());
        if (QFile::rename(path, targetPath)) {
            return { };
        }
        if (QFile::copy(path, targetPath) && QFile::remove(path)) {
            return { };
        }
        return linernotes::core::Error {
            .code = QString(linernotes::butler::errc::kDuplicateTrashFailed),
            .message = QStringLiteral("Failed to move file to test trash dir"),
            .detail = path,
        };
    }

private:
    QString m_trashDir;
    QString m_failPath;
};

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QString &contentHash = QStringLiteral("hash_same"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, codec, sample_rate, "
            "bit_depth, bitrate, duration_ms, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, ?, 'flac', 44100, 16, 0, 8000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 1000, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertDuplicateGroup(const QSqlDatabase &db, qint64 groupId,
        const QString &kind = QStringLiteral("exact"), qint64 createdAt = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO duplicate_groups (id, kind, created_at) VALUES (?, ?, ?);"));
        q.addBindValue(groupId);
        q.addBindValue(kind);
        q.addBindValue(createdAt);
        return q.exec();
    }

    static bool insertDuplicateMember(
        const QSqlDatabase &db, qint64 groupId, qint64 trackId, double keepScore, int recommended)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO duplicate_members (group_id, track_id, keep_score, recommended) "
            "VALUES (?, ?, ?, ?);"));
        q.addBindValue(groupId);
        q.addBindValue(trackId);
        q.addBindValue(keepScore);
        q.addBindValue(recommended);
        return q.exec();
    }
};

class TstDuplicateResolver : public QObject {
    Q_OBJECT

private slots:
    void resolveHandlesPartialFailuresAndRetainsGroup();
    void dismissRecordsDismissalsAndRemovesGroup();
};

void TstDuplicateResolver::resolveHandlesPartialFailuresAndRetainsGroup()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QTemporaryDir trashDir;
    QVERIFY(trashDir.isValid());

    const QString fixtureMelody = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString f1Path = musicDir.filePath(QStringLiteral("song1.flac"));
    const QString f2Path = musicDir.filePath(QStringLiteral("song2.flac"));
    const QString f3Path = musicDir.filePath(QStringLiteral("song3.flac"));

    QVERIFY(QFile::copy(fixtureMelody, f1Path));
    QVERIFY(QFile::copy(fixtureMelody, f2Path));
    QVERIFY(QFile::copy(fixtureMelody, f3Path));

    Database db(tempDir.filePath(QStringLiteral("test_dup_resolver.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn, musicDir.path());
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, f1Path);
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, f2Path);
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, f3Path);

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);

    const qint64 groupId = 1;
    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, groupId, QStringLiteral("exact")));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t1, 100.0, 1));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t2, 90.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t3, 80.0, 0));

    // Test trash makes f3 fail
    TestFileTrash trash(trashDir.path(), f3Path);
    const ManualClock clock(1000);
    const DuplicateResolver resolver(db, trash, clock);

    // Resolve keeping t1
    const auto res = resolver.resolve(groupId, t1);
    QVERIFY(res.ok());
    const auto &outcome = res.value();

    // t2 succeeded, t3 failed
    QCOMPARE(outcome.removedTrackIds, (QList<qint64> { t2 }));
    QCOMPARE(outcome.failedPaths, (QStringList { f3Path }));

    // Group still exists in duplicate_groups because f3 failed
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM duplicate_groups WHERE id = ?;"));
    q.addBindValue(groupId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Second file moved out of musicDir and exists in trashDir
    QVERIFY(!QFile::exists(f2Path));
    QVERIFY(QFile::exists(QDir(trashDir.path()).filePath(QStringLiteral("song2.flac"))));

    // Third file still in musicDir
    QVERIFY(QFile::exists(f3Path));

    // t2 removed from database, t3 still in library
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(t2);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(t3);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstDuplicateResolver::dismissRecordsDismissalsAndRemovesGroup()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QTemporaryDir trashDir;
    QVERIFY(trashDir.isValid());

    const QString fixtureMelody = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString f1Path = musicDir.filePath(QStringLiteral("trackA.flac"));
    const QString f2Path = musicDir.filePath(QStringLiteral("trackB.flac"));
    const QString f3Path = musicDir.filePath(QStringLiteral("trackC.flac"));

    QVERIFY(QFile::copy(fixtureMelody, f1Path));
    QVERIFY(QFile::copy(fixtureMelody, f2Path));
    QVERIFY(QFile::copy(fixtureMelody, f3Path));

    Database db(tempDir.filePath(QStringLiteral("test_dup_dismiss.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn, musicDir.path());
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, f1Path, QStringLiteral("hash_common"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, f2Path, QStringLiteral("hash_common"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, f3Path, QStringLiteral("hash_common"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);

    const qint64 groupId = 10;
    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, groupId, QStringLiteral("exact")));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t1, 100.0, 1));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t2, 90.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, groupId, t3, 80.0, 0));

    TestFileTrash trash(trashDir.path());
    const ManualClock clock(5000);
    const DuplicateResolver resolver(db, trash, clock);

    // Call dismiss
    const auto disRes = resolver.dismiss(groupId);
    QVERIFY(disRes.ok());

    // Group should be deleted
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM duplicate_groups WHERE id = ?;"));
    q.addBindValue(groupId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Check duplicate_dismissals has 3 pairs: (t1, t2), (t1, t3), (t2, t3)
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM duplicate_dismissals;"));
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 3);

    // DuplicateSource loadDismissals
    const DuplicateSource source(db, clock);
    const auto disSetRes = source.loadDismissals();
    QVERIFY(disSetRes.ok());
    const auto &disSet = disSetRes.value();
    QCOMPARE(disSet.size(), 3);
    QVERIFY(disSet.contains(qMakePair(std::min(t1, t2), std::max(t1, t2))));
    QVERIFY(disSet.contains(qMakePair(std::min(t1, t3), std::max(t1, t3))));
    QVERIFY(disSet.contains(qMakePair(std::min(t2, t3), std::max(t2, t3))));

    // findDuplicates with dismissed set should not group them
    const auto tracksRes = source.loadTracks(true);
    QVERIFY(tracksRes.ok());
    const auto groups = findDuplicates(tracksRes.value(), disSet);
    QVERIFY(groups.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstDuplicateResolver)

#include "tst_DuplicateResolver.moc"
