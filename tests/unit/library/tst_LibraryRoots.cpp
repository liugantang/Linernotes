// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::LibraryRoots;
using linernotes::library::Migrator;
namespace errc = linernotes::library::errc;

class TstLibraryRoots : public QObject {
    Q_OBJECT

private slots:
    void roundTripCrud();
    void duplicateAddReturnsSameId();
    void invalidPathReturnsRootInvalid();
    void overlappingRootsReturnRootOverlap();
    void pathNormalization();
    void cascadingDeleteFiles();
    void unicodeAndSpecialCharExcludes();
    void corruptedJsonHandledGracefully();
};

void TstLibraryRoots::roundTripCrud()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    LibraryRoots roots(db);

    // 1. Add
    const auto addRes = roots.add(musicDir);
    QVERIFY(addRes.ok());
    const auto &root = addRes.value();
    QVERIFY(root.id > 0);
    QCOMPARE(root.path, QDir::cleanPath(musicDir));
    QCOMPARE(root.enabled, true);
    QVERIFY(root.excludes.isEmpty());

    // 2. List
    auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    QCOMPARE(listRes.value().first().id, root.id);
    QCOMPARE(listRes.value().first().path, root.path);
    QCOMPARE(listRes.value().first().enabled, true);

    // 3. Set enabled
    QVERIFY(roots.setEnabled(root.id, false).ok());
    listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    QCOMPARE(listRes.value().first().enabled, false);

    // 4. Set excludes
    const QStringList excludes = { QStringLiteral("Podcasts/*"), QStringLiteral("*.wav") };
    QVERIFY(roots.setExcludes(root.id, excludes).ok());
    listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().first().excludes, excludes);

    // 5. Remove
    QVERIFY(roots.remove(root.id).ok());
    listRes = roots.list();
    QVERIFY(listRes.ok());
    QVERIFY(listRes.value().isEmpty());
}

void TstLibraryRoots::duplicateAddReturnsSameId()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicDir));

    LibraryRoots roots(db);

    const auto res1 = roots.add(musicDir);
    QVERIFY(res1.ok());

    const auto res2 = roots.add(musicDir);
    QVERIFY(res2.ok());

    QCOMPARE(res1.value().id, res2.value().id);
    QCOMPARE(res1.value().path, res2.value().path);

    const auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
}

void TstLibraryRoots::invalidPathReturnsRootInvalid()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);

    // Non-existent directory
    const QString nonExistent = tempDir.filePath(QStringLiteral("no_such_directory"));
    const auto res1 = roots.add(nonExistent);
    QVERIFY(!res1.ok());
    QCOMPARE(res1.error().code, errc::kRootInvalid);

    // Ordinary file instead of directory
    const QString filePath = tempDir.filePath(QStringLiteral("plain_file.txt"));
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("text");
    }
    const auto res2 = roots.add(filePath);
    QVERIFY(!res2.ok());
    QCOMPARE(res2.error().code, errc::kRootInvalid);
}

void TstLibraryRoots::overlappingRootsReturnRootOverlap()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString parentDir = tempDir.filePath(QStringLiteral("parent"));
    const QString childDir = tempDir.filePath(QStringLiteral("parent/child"));
    QVERIFY(QDir().mkpath(childDir));

    LibraryRoots roots(db);

    // Case 1: Add parent first, then child
    const auto parentRes = roots.add(parentDir);
    QVERIFY(parentRes.ok());

    const auto childRes = roots.add(childDir);
    QVERIFY(!childRes.ok());
    QCOMPARE(childRes.error().code, errc::kRootOverlap);

    // Remove parent
    QVERIFY(roots.remove(parentRes.value().id).ok());

    // Case 2: Add child first, then parent
    const auto childRes2 = roots.add(childDir);
    QVERIFY(childRes2.ok());

    const auto parentRes2 = roots.add(parentDir);
    QVERIFY(!parentRes2.ok());
    QCOMPARE(parentRes2.error().code, errc::kRootOverlap);
}

void TstLibraryRoots::pathNormalization()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString baseMusic = tempDir.filePath(QStringLiteral("music_norm"));
    QVERIFY(QDir().mkpath(baseMusic));

    LibraryRoots roots(db);

    const QString withTrailingSlash = baseMusic + QStringLiteral("/");
    const QString withDotSegment = tempDir.path() + QStringLiteral("/./music_norm");

    const auto res1 = roots.add(withTrailingSlash);
    QVERIFY(res1.ok());
    QCOMPARE(res1.value().path, QDir::cleanPath(baseMusic));

    const auto res2 = roots.add(withDotSegment);
    QVERIFY(res2.ok());
    QCOMPARE(res2.value().id, res1.value().id);
    QCOMPARE(res2.value().path, QDir::cleanPath(baseMusic));

    const auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
}

void TstLibraryRoots::cascadingDeleteFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString musicDir = tempDir.filePath(QStringLiteral("music_cascade"));
    QVERIFY(QDir().mkpath(musicDir));

    LibraryRoots roots(db);
    const auto addRes = roots.add(musicDir);
    QVERIFY(addRes.ok());
    const qint64 rootId = addRes.value().id;

    // Insert a dummy record into files referencing rootId
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1024, 1000, 1000, 1000)"));
        q.addBindValue(rootId);
        q.addBindValue(QString(musicDir + QStringLiteral("/track1.mp3")));
        QVERIFY(q.exec());
    }

    // Verify row exists in files
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM files;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // Remove root
    QVERIFY(roots.remove(rootId).ok());

    // Verify row in files is cascade deleted
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM files;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
}

void TstLibraryRoots::unicodeAndSpecialCharExcludes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString musicDir = tempDir.filePath(QStringLiteral("unicode_music"));
    QVERIFY(QDir().mkpath(musicDir));

    LibraryRoots roots(db);
    const QStringList specialExcludes = { QStringLiteral("周杰伦/*"),
        QStringLiteral("Special [Chars] & %/?/*"), QStringLiteral("Emoji 🎵/*") };

    const auto addRes = roots.add(musicDir, specialExcludes);
    QVERIFY(addRes.ok());
    QCOMPARE(addRes.value().excludes, specialExcludes);

    const auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    QCOMPARE(listRes.value().first().excludes, specialExcludes);
}

void TstLibraryRoots::corruptedJsonHandledGracefully()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString musicDir = tempDir.filePath(QStringLiteral("corrupted_json_dir"));
    QVERIFY(QDir().mkpath(musicDir));

    LibraryRoots roots(db);
    const auto addRes = roots.add(musicDir);
    QVERIFY(addRes.ok());
    const qint64 rootId = addRes.value().id;

    // Directly corrupt excludes column in DB
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        q.prepare(
            QStringLiteral("UPDATE library_roots SET excludes = 'invalid-json' WHERE id = ?"));
        q.addBindValue(rootId);
        QVERIFY(q.exec());
    }

    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("Corrupted excludes JSON in library_roots.*")));

    const auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    QVERIFY(listRes.value().first().excludes.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstLibraryRoots)

#include "tst_LibraryRoots.moc"
