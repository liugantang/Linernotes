// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <library/Scanner.h>
#include <library/SearchIndex.h>

namespace {

using linernotes::library::CoverStore;
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

    const QString sub1 = targetDir + QStringLiteral("/sub1");
    QDir().mkpath(sub1);
    QFile::copy(src + QStringLiteral("/flac_vorbis.flac"), sub1 + QStringLiteral("/sub_flac.flac"));

    // Non-audio and hidden files to verify exclusion
    QFile cover(targetDir + QStringLiteral("/cover.jpg"));
    if (cover.open(QIODevice::WriteOnly)) {
        cover.write("fake jpeg image content");
    }
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
            records.append(TrackDbRecord {
                .path = q.value(0).toString(),
                .size = q.value(1).toLongLong(),
                .contentHash = q.value(2).toString(),
                .title = q.value(3).toString(),
            });
        }
    }
    return records;
}

class TstScanner : public QObject {
    Q_OBJECT

private slots:
    void firstScanAndIncrementalRescan();
    void fileModificationsMovesDeletesAndRestore();
    void excludesDisabledRootsAndSubtreeScan();
    void cancelAndRescanConsistency();
    void asyncScanEmitsProgressAndFinished();
    void albumArtistAndCoverIntegration();
};

void TstScanner::firstScanAndIncrementalRescan()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 injectedTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return injectedTime; };
    Scanner scanner(db, opts);

    // 1. First scan: adds files, extracts tags, populates search index
    const auto res1 = scanner.scanBlocking();
    QVERIFY(res1.ok());
    const auto &stats1 = res1.value();
    QVERIFY(!stats1.cancelled && stats1.found > 0 && stats1.added > 0 && stats1.failed > 0);
    QCOMPARE(stats1.unchanged, 0);

    const auto qDb = db.connection().value();

    // Verify non-audio / hidden excluded
    QSqlQuery q(qDb);
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM files WHERE path LIKE '%cover.jpg' OR path LIKE '%/.hidden/%'")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // Search index populated and search_dirty clean
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM search_dirty")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);
    const linernotes::library::SearchIndex searchIndex(qDb);
    const auto searchRes = searchIndex.search(QStringLiteral("cxwg"));
    QVERIFY(searchRes.ok() && !searchRes.value().isEmpty());

    // 2. Incremental rescan without modifications: 0 added/updated, all unchanged
    injectedTime = 2000000;
    const auto res2 = scanner.scanBlocking();
    QVERIFY(res2.ok());
    const auto &stats2 = res2.value();
    QCOMPARE(stats2.unchanged, stats2.found);
    QCOMPARE(stats2.added, 0);
    QCOMPARE(stats2.updated, 0);
}

void TstScanner::fileModificationsMovesDeletesAndRestore()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    qint64 currentTime = 1000000;
    Scanner::Options opts;
    opts.nowMs = [&]() { return currentTime; };
    Scanner scanner(db, opts);
    QVERIFY(scanner.scanBlocking().ok());

    const auto qDb = db.connection().value();
    const QString targetFlac = musicDir + QStringLiteral("/flac_vorbis.flac");

    qint64 origFileId = 0;
    qint64 origTrackId = 0;
    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT f.id, t.id FROM files f JOIN tracks t ON f.id = t.file_id WHERE f.path = ?"));
        q.addBindValue(targetFlac);
        QVERIFY(q.exec() && q.next());
        origFileId = q.value(0).toLongLong();
        origTrackId = q.value(1).toLongLong();
    }

    // 1. Move file: rename to sub1/moved.flac -> moved = 1, IDs preserved
    const QString movedFlac = musicDir + QStringLiteral("/sub1/moved.flac");
    QVERIFY(QFile::rename(targetFlac, movedFlac));
    currentTime = 2000000;
    const auto moveRes = scanner.scanBlocking();
    QVERIFY(moveRes.ok() && moveRes.value().moved == 1);

    {
        QSqlQuery q(qDb);
        q.prepare(QStringLiteral(
            "SELECT f.id, t.id FROM files f JOIN tracks t ON f.id = t.file_id WHERE f.path = ?"));
        q.addBindValue(movedFlac);
        QVERIFY(q.exec() && q.next());
        QCOMPARE(q.value(0).toLongLong(), origFileId);
        QCOMPARE(q.value(1).toLongLong(), origTrackId);
    }

    // 2. Duplicate copy: not a move, added = 1
    const QString copyFlac = musicDir + QStringLiteral("/copy.flac");
    QVERIFY(QFile::copy(movedFlac, copyFlac));
    currentTime = 3000000;
    const auto copyRes = scanner.scanBlocking();
    QVERIFY(copyRes.ok() && copyRes.value().added == 1 && copyRes.value().moved == 0);

    // 3. Delete file: missing = 1; Restore file: restored = 1
    const QString opusFile = musicDir + QStringLiteral("/opus.opus");
    const QDateTime origMtime = QFileInfo(opusFile).lastModified();
    const QString backupOpus = tempDir.filePath(QStringLiteral("opus_bak.opus"));
    QVERIFY(QFile::copy(opusFile, backupOpus) && QFile::remove(opusFile));

    currentTime = 4000000;
    const auto delRes = scanner.scanBlocking();
    QVERIFY(delRes.ok() && delRes.value().missing == 1);

    QVERIFY(QFile::copy(backupOpus, opusFile));
    {
        QFile f(opusFile);
        QVERIFY(f.open(QIODevice::ReadWrite)
            && f.setFileTime(origMtime, QFileDevice::FileModificationTime));
    }
    currentTime = 5000000;
    const auto restRes = scanner.scanBlocking();
    QVERIFY(restRes.ok() && restRes.value().restored == 1);
}

void TstScanner::excludesDisabledRootsAndSubtreeScan()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString root1Dir = tempDir.filePath(QStringLiteral("root1"));
    const QString root2Dir = tempDir.filePath(QStringLiteral("root2"));
    setupTestLibrary(root1Dir);
    setupTestLibrary(root2Dir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);

    const auto r1Res = roots.add(root1Dir, { QStringLiteral("*.wav"), QStringLiteral("sub1/*") });
    QVERIFY(r1Res.ok());
    const auto r2Res = roots.add(root2Dir);
    QVERIFY(r2Res.ok() && roots.setEnabled(r2Res.value().id, false).ok());

    Scanner scanner(db, Scanner::Options { });
    QVERIFY(scanner.scanBlocking().ok());

    const auto qDb = db.connection().value();
    QSqlQuery q(qDb);

    // Root 2 files not present
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE root_id = ?"));
    q.addBindValue(r2Res.value().id);
    QVERIFY(q.exec() && q.next() && q.value(0).toInt() == 0);

    // Root 1 excludes respected
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM files WHERE path LIKE '%.wav' OR path LIKE '%/sub1/%'")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // Subtree path-scoped missing detection
    const QString root1Sub = root1Dir + QStringLiteral("/opus.opus");
    QVERIFY(QFile::remove(root1Sub));
    const auto subRes = scanner.scanBlocking({ root1Dir });
    QVERIFY(subRes.ok() && subRes.value().missing == 1);
}

void TstScanner::cancelAndRescanConsistency()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music_300"));
    QDir().mkpath(musicDir);

    const QString srcFlac = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    const QString srcMp3 = fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3"));

    for (int i = 0; i < 300; ++i) {
        const QString name
            = QStringLiteral("track_%1.%2")
                  .arg(QString::number(i), 3, QLatin1Char('0'))
                  .arg((i % 2 == 0) ? QStringLiteral("flac") : QStringLiteral("mp3"));
        QFile::copy((i % 2 == 0) ? srcFlac : srcMp3, musicDir + QStringLiteral("/") + name);
    }

    Database db1(tempDir.filePath(QStringLiteral("db1.db")));
    QVERIFY(db1.open(Migrator()).ok());
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

    // Resume/complete scan on db1
    QVERIFY(scanner1.scanBlocking().ok());

    // Fresh scan on db2
    Database db2(tempDir.filePath(QStringLiteral("db2.db")));
    QVERIFY(db2.open(Migrator()).ok());
    LibraryRoots roots2(db2);
    QVERIFY(roots2.add(musicDir).ok());
    Scanner scanner2(db2, opts);
    QVERIFY(scanner2.scanBlocking().ok());

    const auto list1 = queryAllTracks(db1);
    const auto list2 = queryAllTracks(db2);
    QCOMPARE(list1.size(), 300);
    QCOMPARE(list1, list2);
}

void TstScanner::asyncScanEmitsProgressAndFinished()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    setupTestLibrary(musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    QSignalSpy progressSpy(&scanner, &Scanner::progress);
    QSignalSpy finishedSpy(&scanner, &Scanner::finished);

    QVERIFY(scanner.start());
    QVERIFY(!scanner.start()); // Cannot start twice
    QTRY_VERIFY_WITH_TIMEOUT(!scanner.isRunning(), 10000);
    QCOMPARE(finishedSpy.size(), 1);
    QVERIFY(!progressSpy.isEmpty());
}

void TstScanner::albumArtistAndCoverIntegration()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    const QString cacheDir = tempDir.filePath(QStringLiteral("cache"));
    QDir().mkpath(musicDir);
    QDir().mkpath(cacheDir);

    const QString src = fixturePath(QStringLiteral("library"));
    QVERIFY(QFile::copy(src + QStringLiteral("/cover_1600_embed.mp3"),
        musicDir + QStringLiteral("/cover_1600_embed.mp3")));
    QVERIFY(QFile::copy(src + QStringLiteral("/cover_1600_embed.flac"),
        musicDir + QStringLiteral("/cover_1600_embed.flac")));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    CoverStore coverStore(cacheDir);
    Scanner::Options opts;
    opts.coverStore = &coverStore;
    Scanner scanner(db, opts);

    const auto res = scanner.scanBlocking();
    QVERIFY(res.ok());
    const auto qDb = db.connection().value();

    // 1. Cover deduplicated into 1 cover row and thumbnails generated
    QString coverHash;
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT hash FROM covers")));
        QVERIFY(q.next());
        coverHash = q.value(0).toString();
        QVERIFY(!q.next()); // Deduplicated to 1
    }
    QVERIFY(QFile::exists(coverStore.thumbnailPath(coverHash, 128)));

    // 2. Albums and Artists linked
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = '大封面专辑'")));
        QVERIFY(q.next() && q.value(0).toInt() == 1);
    }

    // 3. Deleting all files cleans up orphan covers and albums
    QVERIFY(QFile::remove(musicDir + QStringLiteral("/cover_1600_embed.mp3")));
    QVERIFY(QFile::remove(musicDir + QStringLiteral("/cover_1600_embed.flac")));
    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("DELETE FROM files")));
    }
    QVERIFY(scanner.scanBlocking().ok());

    {
        QSqlQuery q(qDb);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM covers")));
        QVERIFY(q.next() && q.value(0).toInt() == 0);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")));
        QVERIFY(q.next() && q.value(0).toInt() == 0);
    }
    QVERIFY(!QFile::exists(coverStore.thumbnailPath(coverHash, 128)));
}

} // namespace

QTEST_GUILESS_MAIN(TstScanner)

#include "tst_Scanner.moc"
