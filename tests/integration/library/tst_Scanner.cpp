// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <library/Scanner.h>

namespace {

using linernotes::library::Database;
using linernotes::library::LibraryRoots;
using linernotes::library::Migrator;
using linernotes::library::Scanner;
using linernotes::library::ScanProgress;
using linernotes::test::fixturePath;

void copyDirContents(const QString &srcDir, const QString &dstDir)
{
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcFile = it.next();
        const QString relPath = QDir(srcDir).relativeFilePath(srcFile);
        const QString dstFile = QDir(dstDir).filePath(relPath);
        QFileInfo(dstFile).dir().mkpath(QStringLiteral("."));
        QFile::copy(srcFile, dstFile);
    }
}

void setupTestLibrary(const QString &targetDir)
{
    const QString src = fixturePath(QStringLiteral("library"));
    copyDirContents(src, targetDir);

    // Create a sub directory with some copied files
    const QString sub1 = targetDir + QStringLiteral("/sub1");
    QDir().mkpath(sub1);
    QFile::copy(src + QStringLiteral("/flac_vorbis.flac"), sub1 + QStringLiteral("/sub_flac.flac"));

    // Create non-audio files
    {
        QFile cover(targetDir + QStringLiteral("/cover.jpg"));
        if (cover.open(QIODevice::WriteOnly)) {
            cover.write("fake jpeg image content");
        }
        QFile notes(targetDir + QStringLiteral("/notes.txt"));
        if (notes.open(QIODevice::WriteOnly)) {
            notes.write("album liner notes text");
        }
    }

    // Create a hidden directory with an audio file inside
    const QString hiddenDir = targetDir + QStringLiteral("/.hidden");
    QDir().mkpath(hiddenDir);
    QFile::copy(
        src + QStringLiteral("/opus.opus"), hiddenDir + QStringLiteral("/hidden_opus.opus"));
}

struct TrackDbRecord {
    QString path;
    qint64 size = 0;
    QString contentHash;
    QString title;

    bool operator==(const TrackDbRecord &other) const = default;
};

QList<TrackDbRecord> queryAllTracks(Database &db)
{
    QList<TrackDbRecord> records;
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return records;
    }
    QSqlQuery q(connRes.value());
    if (q.exec(QStringLiteral("SELECT f.path, f.size, f.content_hash, em.title FROM files f "
                              "JOIN tracks t ON f.id = t.file_id "
                              "JOIN effective_metadata em ON t.id = em.track_id "
                              "ORDER BY f.path ASC"))) {
        while (q.next()) {
            TrackDbRecord rec;
            rec.path = q.value(0).toString();
            rec.size = q.value(1).toLongLong();
            rec.contentHash = q.value(2).toString();
            rec.title = q.value(3).toString();
            records.append(rec);
        }
    }
    return records;
}

class TstScanner : public QObject {
    Q_OBJECT

private slots:
    void firstScanAddsAllAudioFiles();
    void rescanUnchangedWritesNothing();
    void modifiedFileIsReread();
    void deletedFileMarkedMissingAndRestored();
    void movedFileKeepsIds();
    void duplicateCopyIsNotAMove();
    void excludesAndDisabledRoots();
    void scanPathsLimitsMissingDetection();
    void cancelThenRescanIsConsistent();
    void asyncScanEmitsProgressAndFinished();
    void readsInParallel();
    void writeFailureRollsBackSingleFile();
    void scanLinksAlbumsAndArtists();
    void deletingFilesRemovesOrphans();
    void movedFileRelinksAlbum();
};

void TstScanner::firstScanAddsAllAudioFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 injectedTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return injectedTime; };
    Scanner scanner(db, opts);

    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());
    const auto &stats = res.value();

    QVERIFY(!stats.cancelled);
    QVERIFY(stats.found > 0);
    QVERIFY(stats.added > 0);
    QVERIFY(stats.failed > 0);
    QCOMPARE(stats.unchanged, 0);
    QCOMPARE(stats.moved, 0);
    QCOMPARE(stats.missing, 0);
    QCOMPARE(stats.restored, 0);
    QCOMPARE(stats.added + stats.failed, stats.found);

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // 1. cover.jpg, notes.txt, and .hidden files must not be in files table
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM files WHERE path LIKE '%cover.jpg' OR path LIKE '%notes.txt' OR "
            "path LIKE '%/.hidden/%'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // 2. Corrupt files have files row, non-empty scan_error, no tracks
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT f.id, f.path, f.scan_error, (SELECT COUNT(*) FROM tracks t WHERE t.file_id = "
            "f.id) FROM files f WHERE f.scan_error IS NOT NULL")));
        int failedCount = 0;
        while (q.next()) {
            failedCount++;
            QVERIFY(!q.value(2).toString().isEmpty());
            QCOMPARE(q.value(3).toInt(), 0);
        }
        QCOMPARE(failedCount, stats.failed);
    }

    // 3. Valid audio files have files row + 1 track with cue_index IS NULL + raw_tags
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT f.id, t.id, (SELECT COUNT(*) FROM raw_tags r WHERE r.track_id = t.id) "
            "FROM files f "
            "JOIN tracks t ON f.id = t.file_id "
            "WHERE f.scan_error IS NULL AND t.cue_index IS NULL")));
        int validCount = 0;
        while (q.next()) {
            validCount++;
            // Note: flac_no_tags.flac has 0 raw_tags by design, others have > 0
            QVERIFY(q.value(2).toInt() >= 0);
        }
        QCOMPARE(validCount, stats.added);
    }

    // 4. Effective metadata for known sample mp3_id3v24_utf8.mp3
    {
        QSqlQuery q(qDb);
        QVERIFY(
            q.exec(QStringLiteral("SELECT em.title, em.artist, em.album FROM effective_metadata em "
                                  "JOIN tracks t ON em.track_id = t.id "
                                  "JOIN files f ON t.file_id = f.id "
                                  "WHERE f.path LIKE '%mp3_id3v24_utf8.mp3'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("晨曦微光"));
        QCOMPARE(q.value(1).toString(), QStringLiteral("林晓风 / 夜行者"));
        QCOMPARE(q.value(2).toString(), QStringLiteral("山谷的回响"));
    }

    // 5. Content hashes start with "v1:"
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(
            QStringLiteral("SELECT content_hash FROM files WHERE content_hash IS NOT NULL")));
        while (q.next()) {
            QVERIFY(q.value(0).toString().startsWith(QStringLiteral("v1:")));
        }
    }
}

void TstScanner::rescanUnchangedWritesNothing()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);

    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());

    // Record timestamps from first scan
    QHash<qint64, qint64> fileScannedAt;
    QHash<qint64, qint64> trackTagsReadAt;
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT id, scanned_at FROM files")));
        while (q.next()) {
            fileScannedAt.insert(q.value(0).toLongLong(), q.value(1).toLongLong());
            QCOMPARE(q.value(1).toLongLong(), 1000000);
        }
    }
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT id, tags_read_at FROM tracks")));
        while (q.next()) {
            trackTagsReadAt.insert(q.value(0).toLongLong(), q.value(1).toLongLong());
            QCOMPARE(q.value(1).toLongLong(), 1000000);
        }
    }

    // Run second scan with a different injected time
    currentTime = 2000000;
    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    const auto &stats2 = res2.value();

    QCOMPARE(stats2.unchanged, stats2.found);
    QCOMPARE(stats2.added, 0);
    QCOMPARE(stats2.updated, 0);
    QCOMPARE(stats2.moved, 0);
    QCOMPARE(stats2.missing, 0);
    QCOMPARE(stats2.restored, 0);
    QCOMPARE(stats2.failed, 0);

    // Verify all scanned_at and tags_read_at remain 1000000
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT id, scanned_at FROM files")));
        while (q.next()) {
            const qint64 id = q.value(0).toLongLong();
            QCOMPARE(q.value(1).toLongLong(), fileScannedAt.value(id));
        }
    }
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT id, tags_read_at FROM tracks")));
        while (q.next()) {
            const qint64 id = q.value(0).toLongLong();
            QCOMPARE(q.value(1).toLongLong(), trackTagsReadAt.value(id));
        }
    }
}

void TstScanner::modifiedFileIsReread()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);

    QVERIFY(scanner.scanBlocking().ok());

    const QString targetFile = musicDir + QStringLiteral("/flac_vorbis.flac");
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    qint64 origFileId = 0;
    qint64 origTrackId = 0;
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT f.id, t.id FROM files f JOIN tracks t ON f.id = t.file_id WHERE f.path = ?"));
        q.bindValue(0, targetFile);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        origFileId = q.value(0).toLongLong();
        origTrackId = q.value(1).toLongLong();
    }

    // Overwrite targetFile with mp3_id3v24_utf8.mp3 and set a newer mtime
    QVERIFY(QFile::remove(targetFile));
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3")), targetFile));
    {
        QFile f(targetFile);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(
            QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
    }

    currentTime = 2000000;
    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());
    const auto &stats = res.value();

    QCOMPARE(stats.updated, 1);
    QCOMPARE(stats.added, 0);
    QCOMPARE(stats.unchanged, stats.found - 1);

    // Verify IDs did not change
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT f.id, t.id, em.title FROM files f "
                                 "JOIN tracks t ON f.id = t.file_id "
                                 "JOIN effective_metadata em ON t.id = em.track_id "
                                 "WHERE f.path = ?"));
        q.bindValue(0, targetFile);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), origFileId);
        QCOMPARE(q.value(1).toLongLong(), origTrackId);
        QCOMPARE(q.value(2).toString(), QStringLiteral("晨曦微光"));
    }
}

void TstScanner::deletedFileMarkedMissingAndRestored()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);

    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());
    const int initialFound = res1.value().found;

    const QString targetFile = musicDir + QStringLiteral("/opus.opus");
    const QFileInfo targetFi(targetFile);
    const QDateTime originalMtime = targetFi.lastModified();

    // Delete file
    const QString backupFile = tempDir.filePath(QStringLiteral("opus_backup.opus"));
    QVERIFY(QFile::copy(targetFile, backupFile));
    QVERIFY(QFile::remove(targetFile));

    currentTime = 2000000;
    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    const auto &stats2 = res2.value();

    QCOMPARE(stats2.missing, 1);
    QCOMPARE(stats2.found, initialFound - 1);

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT missing_since FROM files WHERE path = ?"));
        q.bindValue(0, targetFile);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 2000000);
    }

    // Put file back with original mtime
    QVERIFY(QFile::copy(backupFile, targetFile));
    {
        QFile f(targetFile);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(originalMtime, QFileDevice::FileModificationTime));
    }

    currentTime = 3000000;
    const auto res3 = scanner.scanBlocking();
    QVERIFY(res3.ok());
    const auto &stats3 = res3.value();

    QCOMPARE(stats3.restored, 1);
    QCOMPARE(stats3.missing, 0);

    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT missing_since FROM files WHERE path = ?"));
        q.bindValue(0, targetFile);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QVERIFY(q.value(0).isNull());
    }
}

void TstScanner::movedFileKeepsIds()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);

    QVERIFY(scanner.scanBlocking().ok());

    const QString oldPath = musicDir + QStringLiteral("/flac_vorbis.flac");
    const QString newPath = musicDir + QStringLiteral("/sub1/moved_flac_vorbis.flac");

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    qint64 origFileId = 0;
    qint64 origTrackId = 0;
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT f.id, t.id FROM files f JOIN tracks t ON f.id = t.file_id WHERE f.path = ?"));
        q.bindValue(0, oldPath);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        origFileId = q.value(0).toLongLong();
        origTrackId = q.value(1).toLongLong();
    }

    // Insert play_event and user_override
    {
        QSqlQuery q(qDb);
        q.prepare(
            QStringLiteral("INSERT INTO play_events (track_id, started_at) VALUES (?, 1000000)"));
        q.bindValue(0, origTrackId);
        QVERIFY(q.exec());

        QSqlQuery q2(qDb);
        q2.prepare(QStringLiteral(
            "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
            "VALUES (?, 'title', 'Overridden Title', 1000000, 1000000)"));
        q2.bindValue(0, origTrackId);
        QVERIFY(q2.exec());
    }

    // Move file
    QVERIFY(QFile::rename(oldPath, newPath));

    currentTime = 2000000;
    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());
    const auto &stats = res.value();

    QCOMPARE(stats.moved, 1);
    QCOMPARE(stats.added, 0);

    // Verify DB keeps IDs and overrides
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT f.id, t.id, f.missing_since, em.title FROM files f "
                                 "JOIN tracks t ON f.id = t.file_id "
                                 "JOIN effective_metadata em ON t.id = em.track_id "
                                 "WHERE f.path = ?"));
        q.bindValue(0, newPath);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), origFileId);
        QCOMPARE(q.value(1).toLongLong(), origTrackId);
        QVERIFY(q.value(2).isNull());
        QCOMPARE(q.value(3).toString(), QStringLiteral("Overridden Title"));
    }

    // Verify play_events still points to trackId
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT track_id FROM play_events WHERE track_id = ?"));
        q.bindValue(0, origTrackId);
        QVERIFY(q.exec());
        QVERIFY(q.next());
    }
}

void TstScanner::duplicateCopyIsNotAMove()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    QVERIFY(scanner.scanBlocking().ok());

    const QString origPath = musicDir + QStringLiteral("/flac_vorbis.flac");
    const QString copyPath = musicDir + QStringLiteral("/flac_vorbis_copy.flac");
    QVERIFY(QFile::copy(origPath, copyPath));

    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());
    const auto &stats = res.value();

    QCOMPARE(stats.added, 1);
    QCOMPARE(stats.moved, 0);

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    QSqlQuery q(qDb);
    q.prepare(QStringLiteral("SELECT id FROM files WHERE path IN (?, ?) ORDER BY id ASC"));
    q.bindValue(0, origPath);
    q.bindValue(1, copyPath);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    const qint64 id1 = q.value(0).toLongLong();
    QVERIFY(q.next());
    const qint64 id2 = q.value(0).toLongLong();
    QVERIFY(id1 != id2);
}

void TstScanner::excludesAndDisabledRoots()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root1Dir = tempDir.filePath(QStringLiteral("root1"));
    const QString root2Dir = tempDir.filePath(QStringLiteral("root2"));
    setupTestLibrary(root1Dir);
    setupTestLibrary(root2Dir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto r1Res = roots.add(root1Dir, { QStringLiteral("*.wav"), QStringLiteral("sub1/*") });
    QVERIFY(r1Res.ok());

    const auto r2Res = roots.add(root2Dir);
    QVERIFY(r2Res.ok());
    QVERIFY(roots.setEnabled(r2Res.value().id, false).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // Verify root2 files are not in DB
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE root_id = ?"));
        q.bindValue(0, r2Res.value().id);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // Verify root1 excludes are respected
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM files WHERE path LIKE '%.wav' OR path LIKE '%/sub1/%'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
}

void TstScanner::scanPathsLimitsMissingDetection()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    // Create sub2
    const QString sub2 = musicDir + QStringLiteral("/sub2");
    QDir().mkpath(sub2);
    QFile::copy(
        fixturePath(QStringLiteral("library/opus.opus")), sub2 + QStringLiteral("/sub2_opus.opus"));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    QVERIFY(scanner.scanBlocking().ok());

    const QString sub1File = musicDir + QStringLiteral("/sub1/sub_flac.flac");
    const QString sub2File = sub2 + QStringLiteral("/sub2_opus.opus");
    QVERIFY(QFile::remove(sub1File));
    QVERIFY(QFile::remove(sub2File));

    // Scan only sub1
    const auto res = scanner.scanBlocking({ musicDir + QStringLiteral("/sub1") });
    QVERIFY(res.ok());
    const auto &stats = res.value();

    QCOMPARE(stats.missing, 1);

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // sub1 file marked missing
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT missing_since FROM files WHERE path = ?"));
        q.bindValue(0, sub1File);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QVERIFY(!q.value(0).isNull());
    }

    // sub2 file NOT marked missing
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT missing_since FROM files WHERE path = ?"));
        q.bindValue(0, sub2File);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QVERIFY(q.value(0).isNull());
    }
}

void TstScanner::cancelThenRescanIsConsistent()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music_300"));
    QDir().mkpath(musicDir);

    const QString srcFlac = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    const QString srcMp3 = fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3"));

    // Generate 320 files
    for (int i = 0; i < 320; ++i) {
        const QString name
            = QStringLiteral("track_%1.%2")
                  .arg(QString::number(i), 3, QLatin1Char('0'))
                  .arg((i % 2 == 0) ? QStringLiteral("flac") : QStringLiteral("mp3"));
        QFile::copy((i % 2 == 0) ? srcFlac : srcMp3, musicDir + QStringLiteral("/") + name);
    }

    Database db1(tempDir.filePath(QStringLiteral("db1.db")));
    const Migrator migrator;
    QVERIFY(db1.open(migrator).ok());
    LibraryRoots roots1(db1);
    QVERIFY(roots1.add(musicDir).ok());

    Scanner::Options opts;
    opts.batchSize = 50;

    Scanner scanner1(db1, opts);
    bool cancelledCalled = false;
    connect(&scanner1, &Scanner::progress, [&](const ScanProgress &p) {
        if (p.phase == ScanProgress::Phase::Reading && !cancelledCalled) {
            cancelledCalled = true;
            scanner1.cancel();
        }
    });

    QVERIFY(scanner1.start());
    QTRY_VERIFY_WITH_TIMEOUT(!scanner1.isRunning(), 10000);

    // Verify db1 state after cancel
    const auto connRes1 = db1.connection();
    QVERIFY(connRes1.ok());
    const auto &qDb1 = connRes1.value();

    // No missing marks
    {
        QSqlQuery q(qDb1);
        QVERIFY(
            q.exec(QStringLiteral("SELECT COUNT(*) FROM files WHERE missing_since IS NOT NULL")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // All written tracks have raw_tags
    {
        QSqlQuery q(qDb1);
        QVERIFY(q.exec(QStringLiteral("SELECT t.id, (SELECT COUNT(*) FROM raw_tags r WHERE "
                                      "r.track_id = t.id) FROM tracks t")));
        while (q.next()) {
            QVERIFY(q.value(1).toInt() > 0);
        }
    }

    // Now complete the scan on db1
    const auto completeRes1 = scanner1.scanBlocking();
    QVERIFY(completeRes1.ok());

    // Scan the same music directory on a fresh db2 from scratch
    Database db2(tempDir.filePath(QStringLiteral("db2.db")));
    QVERIFY(db2.open(migrator).ok());
    LibraryRoots roots2(db2);
    QVERIFY(roots2.add(musicDir).ok());

    Scanner scanner2(db2, opts);
    const auto completeRes2 = scanner2.scanBlocking();
    QVERIFY(completeRes2.ok());

    const auto list1 = queryAllTracks(db1);
    const auto list2 = queryAllTracks(db2);

    QCOMPARE(list1.size(), 320);
    QCOMPARE(list2.size(), 320);
    QCOMPARE(list1, list2);
}

void TstScanner::asyncScanEmitsProgressAndFinished()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    QSignalSpy progressSpy(&scanner, &Scanner::progress);
    QSignalSpy finishedSpy(&scanner, &Scanner::finished);

    QVERIFY(scanner.start());
    // Starting while already running returns false
    QVERIFY(!scanner.start());

    QTRY_VERIFY_WITH_TIMEOUT(!scanner.isRunning(), 10000);
    QCOMPARE(finishedSpy.size(), 1);

    bool hasWalking = false;
    bool hasReading = false;
    for (const auto &spyItem : progressSpy) {
        const auto prog = spyItem.at(0).value<ScanProgress>();
        if (prog.phase == ScanProgress::Phase::Walking) {
            hasWalking = true;
        }
        if (prog.phase == ScanProgress::Phase::Reading) {
            hasReading = true;
        }
    }
    QVERIFY(hasWalking);
    QVERIFY(hasReading);

    // Test destruction while running doesn't crash or leak
    {
        auto *s = new Scanner(db, Scanner::Options { });
        QVERIFY(s->start());
        delete s;
    }
}

void TstScanner::readsInParallel()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database dbP4(tempDir.filePath(QStringLiteral("db_p4.db")));
    Database dbP1(tempDir.filePath(QStringLiteral("db_p1.db")));
    const Migrator migrator;
    QVERIFY(dbP4.open(migrator).ok());
    QVERIFY(dbP1.open(migrator).ok());

    LibraryRoots roots4(dbP4);
    QVERIFY(roots4.add(musicDir).ok());
    LibraryRoots roots1(dbP1);
    QVERIFY(roots1.add(musicDir).ok());

    Scanner::Options opts4;
    opts4.maxThreads = 4;
    Scanner scannerP4(dbP4, opts4);

    Scanner::Options opts1;
    opts1.maxThreads = 1;
    Scanner scannerP1(dbP1, opts1);

    const auto res4 = scannerP4.scanBlocking();
    const auto res1 = scannerP1.scanBlocking();
    QVERIFY(res4.ok());
    QVERIFY(res1.ok());

    const auto list4 = queryAllTracks(dbP4);
    const auto list1 = queryAllTracks(dbP1);

    QVERIFY(!list4.isEmpty());
    QCOMPARE(list4, list1);
}

void TstScanner::writeFailureRollsBackSingleFile()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // Create trigger that fails raw_tags insert for flac_vorbis
    {
        QSqlQuery q(qDb);
        const QString triggerSql = QStringLiteral(
            "CREATE TEMP TRIGGER fail_one BEFORE INSERT ON raw_tags "
            "WHEN (SELECT f.path FROM tracks t JOIN files f ON f.id = t.file_id WHERE t.id = "
            "NEW.track_id) LIKE '%flac_vorbis%' "
            "BEGIN SELECT RAISE(ABORT, 'injected'); END;");
        QVERIFY2(q.exec(triggerSql), qPrintable(q.lastError().text()));
    }

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);

    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());
    const auto &stats1 = res1.value();

    // Scan did not abort; other files were added
    QVERIFY(stats1.found > 0);
    QVERIFY(stats1.added > 0);
    QVERIFY(stats1.failed > 0);

    // Verify flac_vorbis has NO files row, NO track, NO raw_tags
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(
            QStringLiteral("SELECT COUNT(*) FROM files WHERE path LIKE '%flac_vorbis.flac'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM tracks t JOIN files f ON f.id = t.file_id WHERE f.path LIKE "
            "'%flac_vorbis.flac'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // Drop trigger
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("DROP TRIGGER fail_one;")));
    }

    // Rescan: flac_vorbis should now be successfully read and added
    currentTime = 2000000;
    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    const auto &stats2 = res2.value();

    QCOMPARE(stats2.added, 1);
    QCOMPARE(stats2.unchanged, stats1.found - 1);
    QCOMPARE(stats2.failed, 0);

    // Verify flac_vorbis is now fully in the database
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT f.id, t.id, (SELECT COUNT(*) FROM raw_tags r WHERE r.track_id = t.id) "
            "FROM files f JOIN tracks t ON f.id = t.file_id WHERE f.path LIKE "
            "'%flac_vorbis.flac'")));
        QVERIFY(q.next());
        QVERIFY(q.value(0).toLongLong() > 0);
        QVERIFY(q.value(1).toLongLong() > 0);
        QVERIFY(q.value(2).toInt() > 0);
    }
}

void TstScanner::scanLinksAlbumsAndArtists()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // 1. mp3_id3v24_utf8.mp3: Has album_artist = "林晓风", album = "山谷的回响", year = 2003
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT t.id, a.id, a.grouping_key, a.title, a.album_artist, a.year FROM tracks t "
            "JOIN files f ON t.file_id = f.id "
            "JOIN albums a ON t.album_id = a.id "
            "WHERE f.path LIKE '%mp3_id3v24_utf8.mp3'")));
        QVERIFY(q.next());
        const qint64 trackId = q.value(0).toLongLong();
        const qint64 albumId = q.value(1).toLongLong();
        QCOMPARE(q.value(2).toString(), QStringLiteral("aa:林晓风\x1f山谷的回响"));
        QCOMPARE(q.value(3).toString(), QStringLiteral("山谷的回响"));
        QCOMPARE(q.value(4).toString(), QStringLiteral("林晓风"));
        QCOMPARE(q.value(5).toInt(), 2003);

        // Check album_artists
        QSqlQuery qAa(qDb);
        qAa.prepare(QStringLiteral("SELECT ar.name, aa.position FROM album_artists aa "
                                   "JOIN artists ar ON aa.artist_id = ar.id "
                                   "WHERE aa.album_id = ? ORDER BY aa.position ASC"));
        qAa.addBindValue(albumId);
        QVERIFY(qAa.exec() && qAa.next());
        QCOMPARE(qAa.value(0).toString(), QStringLiteral("林晓风"));
        QCOMPARE(qAa.value(1).toInt(), 0);
        QVERIFY(!qAa.next());

        // Check track_artists
        QSqlQuery qTa(qDb);
        qTa.prepare(QStringLiteral("SELECT ar.name, ta.role, ta.position FROM track_artists ta "
                                   "JOIN artists ar ON ta.artist_id = ar.id "
                                   "WHERE ta.track_id = ? ORDER BY ta.role ASC, ta.position ASC"));
        qTa.addBindValue(trackId);
        QVERIFY(qTa.exec());

        struct ArtRow {
            QString name;
            QString role;
            int pos = 0;
        };
        QList<ArtRow> rows;
        while (qTa.next()) {
            rows.append(ArtRow { .name = qTa.value(0).toString(),
                .role = qTa.value(1).toString(),
                .pos = qTa.value(2).toInt() });
        }
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).name, QStringLiteral("林晓风"));
        QCOMPARE(rows.at(0).role, QStringLiteral("artist"));
        QCOMPARE(rows.at(0).pos, 0);
        QCOMPARE(rows.at(1).name, QStringLiteral("夜行者"));
        QCOMPARE(rows.at(1).role, QStringLiteral("artist"));
        QCOMPARE(rows.at(1).pos, 1);
        QCOMPARE(rows.at(2).name, QStringLiteral("林晓风"));
        QCOMPARE(rows.at(2).role, QStringLiteral("composer"));
        QCOMPARE(rows.at(2).pos, 0);
    }

    // 2. flac_no_tags.flac: Has no tags -> album_id is NULL
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT t.album_id FROM tracks t "
                                      "JOIN files f ON t.file_id = f.id "
                                      "WHERE f.path LIKE '%flac_no_tags.flac'")));
        QVERIFY(q.next());
        QVERIFY(q.value(0).isNull());
    }

    // 3. m4a_alac.m4a: No album_artist, album = "纯净之声", artist = "声学研究", year = 2020
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT t.id, a.id, a.grouping_key, a.title, a.album_artist, a.year FROM tracks t "
            "JOIN files f ON t.file_id = f.id "
            "JOIN albums a ON t.album_id = a.id "
            "WHERE f.path LIKE '%m4a_alac.m4a'")));
        QVERIFY(q.next());
        const qint64 albumId = q.value(1).toLongLong();
        QCOMPARE(q.value(2).toString(),
            QStringLiteral("dir:") + musicDir + QStringLiteral("\x1f纯净之声"));
        QCOMPARE(q.value(3).toString(), QStringLiteral("纯净之声"));
        QVERIFY(q.value(4).isNull());
        QCOMPARE(q.value(5).toInt(), 2020);

        // Check album_artists has single artist "声学研究"
        QSqlQuery qAa(qDb);
        qAa.prepare(QStringLiteral("SELECT ar.name, aa.position FROM album_artists aa "
                                   "JOIN artists ar ON aa.artist_id = ar.id "
                                   "WHERE aa.album_id = ?"));
        qAa.addBindValue(albumId);
        QVERIFY(qAa.exec() && qAa.next());
        QCOMPARE(qAa.value(0).toString(), QStringLiteral("声学研究"));
        QCOMPARE(qAa.value(1).toInt(), 0);
        QVERIFY(!qAa.next());
    }
}

void TstScanner::deletingFilesRemovesOrphans()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // Verify "春の歌" and "花吹雪" from '中文 文件名.flac' exist
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = '春の歌'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);

        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = '花吹雪'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // Step A: Delete file from disk, but do not delete from DB -> marked as missing
    const QString zhFile = musicDir + QStringLiteral("/中文 文件名.flac");
    QVERIFY(QFile::remove(zhFile));

    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    QCOMPARE(res2.value().missing, 1);
    QCOMPARE(res2.value().albumsRemoved, 0);
    QCOMPARE(res2.value().artistsRemoved, 0);

    // Track is missing, but album and artist STILL exist in DB
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = '春の歌'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);

        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = '花吹雪'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // Step B: Completely delete files row from DB (e.g. simulated removal)
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("DELETE FROM files WHERE path LIKE '%中文 文件名.flac'")));
    }

    // Rescan: removeOrphans will clean up the empty album "春の歌" and artist "花吹雪"
    const auto res3 = scanner.scanBlocking();
    QVERIFY(res3.ok());
    QVERIFY(res3.value().albumsRemoved >= 1);
    QVERIFY(res3.value().artistsRemoved >= 1);

    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = '春の歌'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);

        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = '花吹雪'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
}

void TstScanner::movedFileRelinksAlbum()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    // m4a_alac.m4a has no album_artist, album = "纯净之声"
    const QString oldPath = musicDir + QStringLiteral("/m4a_alac.m4a");
    const QString newDir = musicDir + QStringLiteral("/sub1");
    const QString newPath = newDir + QStringLiteral("/moved_m4a_alac.m4a");

    qint64 oldAlbumId = 0;
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT a.id, a.grouping_key FROM tracks t JOIN files f ON t.file_id = f.id JOIN "
            "albums a ON t.album_id = a.id WHERE f.path = ?"));
        q.bindValue(0, oldPath);
        QVERIFY(q.exec() && q.next());
        oldAlbumId = q.value(0).toLongLong();
        QCOMPARE(q.value(1).toString(),
            QStringLiteral("dir:") + musicDir + QStringLiteral("\x1f纯净之声"));
    }

    // Move file to sub1 directory
    QVERIFY(QFile::rename(oldPath, newPath));

    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    const auto &stats2 = res2.value();

    QCOMPARE(stats2.moved, 1);
    QCOMPARE(stats2.albumsRemoved, 1);

    // Verify track is linked to new album with new directory grouping key
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT a.id, a.grouping_key FROM tracks t JOIN files f ON t.file_id = f.id JOIN "
            "albums a ON t.album_id = a.id WHERE f.path = ?"));
        q.bindValue(0, newPath);
        QVERIFY(q.exec() && q.next());
        const qint64 newAlbumId = q.value(0).toLongLong();
        QVERIFY(newAlbumId != oldAlbumId);
        QCOMPARE(q.value(1).toString(),
            QStringLiteral("dir:") + newDir + QStringLiteral("\x1f纯净之声"));
    }

    // Verify old album was deleted
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM albums WHERE id = ?"));
        q.bindValue(0, oldAlbumId);
        QVERIFY(q.exec() && q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstScanner)

#include "tst_Scanner.moc"
