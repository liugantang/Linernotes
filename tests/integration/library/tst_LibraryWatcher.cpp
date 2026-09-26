// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/FsType.h>
#include <library/LibraryRoots.h>
#include <library/LibraryWatcher.h>
#include <library/Migrator.h>
#include <library/Scanner.h>

namespace {

using linernotes::library::Database;
using linernotes::library::FsKind;
using linernotes::library::LibraryRoots;
using linernotes::library::LibraryWatcher;
using linernotes::library::Migrator;
using linernotes::library::Scanner;
using linernotes::test::fixturePath;

bool isFileInDb(Database &db, const QString &path)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return false;
    }
    QSqlQuery q(connRes.value());
    q.prepare(
        QStringLiteral("SELECT COUNT(*) FROM files WHERE path = ? AND missing_since IS NULL"));
    q.bindValue(0, path);
    if (!q.exec() || !q.next()) {
        return false;
    }
    return q.value(0).toInt() > 0;
}

bool isFileMissingInDb(Database &db, const QString &path)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return false;
    }
    QSqlQuery q(connRes.value());
    q.prepare(
        QStringLiteral("SELECT COUNT(*) FROM files WHERE path = ? AND missing_since IS NOT NULL"));
    q.bindValue(0, path);
    if (!q.exec() || !q.next()) {
        return false;
    }
    return q.value(0).toInt() > 0;
}

class TstLibraryWatcher : public QObject {
    Q_OBJECT

private slots:
    void inotifyDetectsNewFile();
    void deletedFileMarkedMissing();
    void newSubdirWatchesAndIngestsFiles();
    void debounceMultipleEvents();
    void networkFsUsesPolling();
    void maxWatchesExceededDegradesToPolling();
    void busyScannerDoesNotDropChanges();
    void mergeSubDirectoriesLogic();
};

void TstLibraryWatcher::inotifyDetectsNewFile()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 100;
    opts.pollIntervalMs = 60000;
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    LibraryWatcher watcher(db, scanner, opts);
    QCOMPARE(watcher.modeForRoot(rootRes.value().id), LibraryWatcher::Mode::Inotify);

    const QString newFile = musicDir + QStringLiteral("/flac_sample.flac");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/flac_vorbis.flac")), newFile));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, newFile), 5000);
}

void TstLibraryWatcher::deletedFileMarkedMissing()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    const QString sampleFile = musicDir + QStringLiteral("/flac_sample.flac");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/flac_vorbis.flac")), sampleFile));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    QVERIFY(scanner.scanBlocking().ok());
    QVERIFY(isFileInDb(db, sampleFile));

    LibraryWatcher::Options opts;
    opts.debounceMs = 100;
    opts.pollIntervalMs = 60000;
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    LibraryWatcher watcher(db, scanner, opts);
    QCOMPARE(watcher.modeForRoot(rootRes.value().id), LibraryWatcher::Mode::Inotify);

    QVERIFY(QFile::remove(sampleFile));

    QTRY_VERIFY_WITH_TIMEOUT(isFileMissingInDb(db, sampleFile), 5000);
}

void TstLibraryWatcher::newSubdirWatchesAndIngestsFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 100;
    opts.pollIntervalMs = 60000;
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    LibraryWatcher watcher(db, scanner, opts);

    const QString subDir = musicDir + QStringLiteral("/artist_album");
    QVERIFY(QDir().mkpath(subDir));
    const QString file1 = subDir + QStringLiteral("/track1.flac");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/flac_vorbis.flac")), file1));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, file1), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!scanner.isRunning(), 5000);

    // Short wait for inotify watch registration to settle
    QTest::qWait(50);

    const QString file2 = subDir + QStringLiteral("/track2.mp3");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3")), file2));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, file2), 5000);
}

void TstLibraryWatcher::debounceMultipleEvents()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 200;
    opts.pollIntervalMs = 60000;
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    LibraryWatcher watcher(db, scanner, opts);
    QSignalSpy spy(&watcher, &LibraryWatcher::rescanRequested);

    const QString src = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    QStringList createdFiles;
    for (int i = 0; i < 5; ++i) {
        const QString f = musicDir + QStringLiteral("/track_%1.flac").arg(i);
        createdFiles.append(f);
        QVERIFY(QFile::copy(src, f));
        QTest::qWait(15);
    }

    for (const auto &f : createdFiles) {
        QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, f), 5000);
    }

    QVERIFY(spy.count() >= 1);
    QVERIFY(spy.count() < 5);
}

void TstLibraryWatcher::networkFsUsesPolling()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 100;
    opts.pollIntervalMs = 300;
    opts.fsKind = [](const QString &) { return FsKind::Network; };

    LibraryWatcher watcher(db, scanner, opts);
    QCOMPARE(watcher.modeForRoot(rootRes.value().id), LibraryWatcher::Mode::Polling);

    const QString sampleFile = musicDir + QStringLiteral("/network_song.flac");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/flac_vorbis.flac")), sampleFile));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, sampleFile), 5000);
}

void TstLibraryWatcher::maxWatchesExceededDegradesToPolling()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));
    for (int i = 1; i <= 4; ++i) {
        QVERIFY(QDir().mkpath(musicDir + QStringLiteral("/sub%1").arg(i)));
    }

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 100;
    opts.pollIntervalMs = 300;
    opts.maxWatches = 2; // 2 * 0.8 = 1.6 < 5 dirs
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral(".*Inotify watch limit reached.*")));

    LibraryWatcher watcher(db, scanner, opts);
    QCOMPARE(watcher.modeForRoot(rootRes.value().id), LibraryWatcher::Mode::Polling);

    const QString sampleFile = musicDir + QStringLiteral("/sub1/song.flac");
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("library/flac_vorbis.flac")), sampleFile));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, sampleFile), 5000);
}

void TstLibraryWatcher::busyScannerDoesNotDropChanges()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    const QString srcFlac = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    for (int i = 0; i < 40; ++i) {
        QVERIFY(QFile::copy(srcFlac, musicDir + QStringLiteral("/init_%1.flac").arg(i)));
    }

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);
    const auto rootRes = roots.add(musicDir);
    QVERIFY(rootRes.ok());

    Scanner scanner(db, Scanner::Options { });
    LibraryWatcher::Options opts;
    opts.debounceMs = 50;
    opts.pollIntervalMs = 60000;
    opts.fsKind = [](const QString &) { return FsKind::Local; };

    LibraryWatcher watcher(db, scanner, opts);

    QVERIFY(scanner.start());

    const QString lateFile = musicDir + QStringLiteral("/late_arrival.flac");
    QVERIFY(QFile::copy(srcFlac, lateFile));

    QTRY_VERIFY_WITH_TIMEOUT(isFileInDb(db, lateFile), 10000);
}

void TstLibraryWatcher::mergeSubDirectoriesLogic()
{
    using linernotes::library::detail::mergeSubDirectories;

    QCOMPARE(
        mergeSubDirectories({ QStringLiteral("/a/b"), QStringLiteral("/a"), QStringLiteral("/c") }),
        QStringList({ QStringLiteral("/a"), QStringLiteral("/c") }));

    QCOMPARE(mergeSubDirectories(
                 { QStringLiteral("/a/b/c"), QStringLiteral("/a/b"), QStringLiteral("/a") }),
        QStringList({ QStringLiteral("/a") }));

    QCOMPARE(mergeSubDirectories(
                 { QStringLiteral("/x/y"), QStringLiteral("/x/z"), QStringLiteral("/w") }),
        QStringList({ QStringLiteral("/w"), QStringLiteral("/x/y"), QStringLiteral("/x/z") }));

    QCOMPARE(mergeSubDirectories({ QStringLiteral("/a/b/"), QStringLiteral("/a") }),
        QStringList({ QStringLiteral("/a") }));

    QCOMPARE(mergeSubDirectories({ QStringLiteral("/"), QStringLiteral("/a/b") }),
        QStringList({ QStringLiteral("/") }));

    QCOMPARE(mergeSubDirectories({ }), QStringList({ }));
}

} // namespace

QTEST_GUILESS_MAIN(TstLibraryWatcher)

#include "tst_LibraryWatcher.moc"
