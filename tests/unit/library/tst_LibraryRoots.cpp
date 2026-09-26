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
    void invalidAndOverlappingRoots();
    void cascadingDeleteFiles();
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

    // 2. Set enabled & excludes
    const QStringList excludes = { QStringLiteral("Podcasts/*"), QStringLiteral("*.wav") };
    QVERIFY(roots.setEnabled(root.id, false).ok());
    QVERIFY(roots.setExcludes(root.id, excludes).ok());

    auto listRes = roots.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    QCOMPARE(listRes.value().first().enabled, false);
    QCOMPARE(listRes.value().first().excludes, excludes);

    // 3. Remove
    QVERIFY(roots.remove(root.id).ok());
    listRes = roots.list();
    QVERIFY(listRes.ok());
    QVERIFY(listRes.value().isEmpty());
}

void TstLibraryRoots::invalidAndOverlappingRoots()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    LibraryRoots roots(db);

    // Invalid path
    const auto badRes = roots.add(tempDir.filePath(QStringLiteral("no_such_directory")));
    QVERIFY(!badRes.ok());
    QCOMPARE(badRes.error().code, errc::kRootInvalid);

    // Overlapping paths
    const QString parentDir = tempDir.filePath(QStringLiteral("parent"));
    const QString childDir = tempDir.filePath(QStringLiteral("parent/child"));
    QVERIFY(QDir().mkpath(childDir));

    const auto parentRes = roots.add(parentDir);
    QVERIFY(parentRes.ok());

    const auto childRes = roots.add(childDir);
    QVERIFY(!childRes.ok());
    QCOMPARE(childRes.error().code, errc::kRootOverlap);
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

    // Remove root and verify cascade deletion
    QVERIFY(roots.remove(rootId).ok());
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM files;")) && q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
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

    // Directly corrupt excludes column in DB
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        q.prepare(
            QStringLiteral("UPDATE library_roots SET excludes = 'invalid-json' WHERE id = ?"));
        q.addBindValue(addRes.value().id);
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
