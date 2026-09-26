// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>

namespace {

using linernotes::library::Database;
using linernotes::library::LibraryRoots;
using linernotes::library::Migrator;
using linernotes::player::Player;
using linernotes::player::PlayMode;
using linernotes::test::fixturePath;
using linernotes::ui::AppContext;

class TstAppContext : public QObject {
    Q_OBJECT

private slots:
    void startSuccessNoRoots();
    void startFailureInvalidPath();
    void startWithLibraryRootScansAndEmitsChanged();
    void destructorCancelsScanningGracefully();
    void playerOptionsAoNull();
    void playbackStatePersistence();
};

void TstAppContext::startSuccessNoRoots()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    AppContext::Options options {
        .databasePath = tempDir.filePath(QStringLiteral("library.db")),
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);

    QCOMPARE(ctx.isLibraryReady(), false);
    QVERIFY(ctx.startupError().isEmpty());
    QCOMPARE(ctx.isScanning(), false);
    QVERIFY(ctx.player() != nullptr);
    QVERIFY(ctx.database() == nullptr);

    const auto res = ctx.start();
    QVERIFY(res.ok());
    QCOMPARE(ctx.isLibraryReady(), true);
    QVERIFY(ctx.startupError().isEmpty());
    QCOMPARE(ctx.isScanning(), false);
    QVERIFY(ctx.database() != nullptr);

    const auto connRes = ctx.database()->connection();
    QVERIFY(connRes.ok());

    const auto curVer = Migrator::currentVersion(connRes.value());
    QVERIFY(curVer.ok());
    const auto latestVer = Migrator().latestVersion();
    QVERIFY(latestVer.ok());
    QCOMPARE(curVer.value(), latestVer.value());
}

void TstAppContext::startFailureInvalidPath()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath(QStringLiteral("file.txt"));
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("data");
    f.close();

    AppContext::Options options {
        .databasePath = filePath + QStringLiteral("/sub/library.db"),
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);

    QSignalSpy spyError(&ctx, &AppContext::startupErrorChanged);
    QSignalSpy spyReady(&ctx, &AppContext::libraryReadyChanged);

    const auto res = ctx.start();
    QVERIFY(!res.ok());
    QCOMPARE(ctx.isLibraryReady(), false);
    QVERIFY(!ctx.startupError().isEmpty());
    QVERIFY(ctx.player() != nullptr);
    QVERIFY(ctx.database() == nullptr);
    QCOMPARE(spyError.count(), 1);
    QCOMPARE(spyReady.count(), 1);
}

void TstAppContext::startWithLibraryRootScansAndEmitsChanged()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("library.db"));
    const QString cacheDir = tempDir.filePath(QStringLiteral("covers"));

    // Pre-populate database with a library root
    {
        Database db(dbPath);
        const auto openRes = db.open(Migrator());
        QVERIFY(openRes.ok());
        LibraryRoots roots(db);
        const auto addRes = roots.add(fixturePath(QStringLiteral("library")));
        QVERIFY(addRes.ok());
    }

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = cacheDir,
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);
    QSignalSpy spyScanning(&ctx, &AppContext::scanningChanged);
    QSignalSpy spyLibrary(&ctx, &AppContext::libraryChanged);

    const auto res = ctx.start();
    QVERIFY(res.ok());

    // Wait for the scan to complete
    QTRY_VERIFY_WITH_TIMEOUT(!ctx.isScanning(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(spyLibrary.count(), 1, 10000);
    QVERIFY(spyScanning.count() >= 2); // Transitioned to true, then to false

    // Create a second AppContext instance opening the same database
    AppContext ctx2(options);
    QSignalSpy spyLibrary2(&ctx2, &AppContext::libraryChanged);

    const auto res2 = ctx2.start();
    QVERIFY(res2.ok());

    // Wait for second scan to complete
    QTRY_VERIFY_WITH_TIMEOUT(!ctx2.isScanning(), 10000);
    // Since files are unchanged, libraryChanged should not be emitted
    QCOMPARE(spyLibrary2.count(), 0);
}

void TstAppContext::destructorCancelsScanningGracefully()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("library.db"));
    const QString cacheDir = tempDir.filePath(QStringLiteral("covers"));

    {
        Database db(dbPath);
        const auto openRes = db.open(Migrator());
        QVERIFY(openRes.ok());
        LibraryRoots roots(db);
        const auto addRes = roots.add(fixturePath(QStringLiteral("library")));
        QVERIFY(addRes.ok());
    }

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = cacheDir,
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    auto ctx = std::make_unique<AppContext>(options);
    const auto res = ctx->start();
    QVERIFY(res.ok());

    // Destroy immediately while scanning may be in progress or just starting
    ctx.reset();
    QVERIFY(ctx == nullptr);
}

void TstAppContext::playerOptionsAoNull()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    AppContext::Options options {
        .databasePath = tempDir.filePath(QStringLiteral("library.db")),
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);
    QVERIFY(ctx.player() != nullptr);
    QVERIFY(ctx.player()->isValid());
    QCOMPARE(ctx.player()->state(), Player::PlaybackState::Stopped);
}

void TstAppContext::playbackStatePersistence()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString statePath = tempDir.filePath(QStringLiteral("playback-state.json"));

    AppContext::Options options {
        .databasePath = tempDir.filePath(QStringLiteral("library.db")),
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = statePath,
    };

    {
        AppContext ctx1(options);
        QVERIFY(ctx1.player() != nullptr);
        QVERIFY(ctx1.player()->queue() != nullptr);

        ctx1.player()->queue()->append({
            { .source = tempDir.filePath(QStringLiteral("track1.flac")) },
            { .source = tempDir.filePath(QStringLiteral("track2.flac")) },
        });
        ctx1.player()->setVolume(40);
        ctx1.player()->queue()->setMode(PlayMode::RepeatAll);

        QTRY_COMPARE(ctx1.player()->volume(), 40);
        ctx1.saveState();
        QVERIFY(QFile::exists(statePath));
    }

    {
        AppContext ctx2(options);
        QVERIFY(ctx2.player() != nullptr);
        QVERIFY(ctx2.player()->queue() != nullptr);

        QCOMPARE(ctx2.player()->queue()->count(), 2);
        QCOMPARE(ctx2.player()->queue()->mode(), PlayMode::RepeatAll);
        QTRY_COMPARE_WITH_TIMEOUT(ctx2.player()->volume(), 40, 5000);
    }
}

} // namespace

QTEST_MAIN(TstAppContext)
#include "tst_AppContext.moc"
