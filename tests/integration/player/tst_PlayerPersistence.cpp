// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QList>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/PlaybackSnapshot.h>
#include <player/Player.h>

#include <cmath>

using linernotes::player::PlaybackSnapshot;
using linernotes::player::PlaybackStateStore;
using linernotes::player::Player;
using linernotes::player::PlayMode;
using linernotes::player::PlayQueue;

namespace {

class TstPlayerPersistence : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void snapshotCapturesState();
    void restoreResumesPausedAtPosition();
    void restoreWithMissingFileReportsError();
    void restoreEmptySnapshot();
    void endToEndStoreSaveLoadAndRestore();
};

void TstPlayerPersistence::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
    qRegisterMetaType<PlayMode>();
    qRegisterMetaType<linernotes::player::PlayMode>();
    qRegisterMetaType<PlaybackSnapshot>();
    qRegisterMetaType<linernotes::player::PlaybackSnapshot>();
}

void TstPlayerPersistence::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("af-command")));
}

void TstPlayerPersistence::snapshotCapturesState()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    QVERIFY(QFile::exists(p440));
    QVERIFY(QFile::exists(silence));
    QVERIFY(QFile::exists(p660));

    player.queue()->setItems({ { .source = p440 }, { .source = silence }, { .source = p660 } });
    player.queue()->setMode(PlayMode::RepeatAll);
    player.setVolume(40);
    player.setMuted(true);

    player.playIndex(1); // silence_5s
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 1.0, 5000);

    player.seek(2.0);
    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 2.0) <= 0.3, 5000);

    const PlaybackSnapshot snap = player.snapshot();
    QCOMPARE(snap.items.size(), 3);
    QCOMPARE(snap.items.at(0).source, p440);
    QCOMPARE(snap.items.at(1).source, silence);
    QCOMPARE(snap.items.at(2).source, p660);
    QCOMPARE(snap.currentIndex, 1);
    QVERIFY2(std::abs(snap.position - 2.0) <= 0.3,
        qPrintable(QStringLiteral("Expected position ~2.0, got %1").arg(snap.position)));
    QCOMPARE(snap.mode, PlayMode::RepeatAll);
    QCOMPARE(snap.volume, 40);
    QCOMPARE(snap.muted, true);
    QCOMPARE(snap.audioDevice, QStringLiteral("auto"));
}

void TstPlayerPersistence::restoreResumesPausedAtPosition()
{
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    QVERIFY(QFile::exists(p440));
    QVERIFY(QFile::exists(silence));
    QVERIFY(QFile::exists(p660));

    PlaybackSnapshot snap;
    snap.items = { { .source = p440 }, { .source = silence }, { .source = p660 } };
    snap.currentIndex = 1;
    snap.position = 2.0;
    snap.mode = PlayMode::RepeatAll;
    snap.volume = 40;
    snap.muted = true;
    snap.audioDevice = QStringLiteral("auto");

    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QList<Player::PlaybackState> observedStates;
    connect(&player, &Player::stateChanged,
        [&](Player::PlaybackState st) { observedStates.append(st); });

    player.restore(snap);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QCOMPARE(player.queue()->count(), 3);
    QCOMPARE(player.queue()->mode(), PlayMode::RepeatAll);
    QTRY_COMPARE_WITH_TIMEOUT(player.volume(), 40, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.isMuted(), true, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 2.0) <= 0.2, 5000);

    // Verify state never became Playing during restore
    for (Player::PlaybackState st : observedStates) {
        QVERIFY2(st != Player::PlaybackState::Playing,
            "Player state must never become Playing during restore");
    }

    // Now call play() -> transitions to Playing
    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() >= 2.0, 5000);
}

void TstPlayerPersistence::restoreWithMissingFileReportsError()
{
    PlaybackSnapshot snap;
    snap.items = { { .source = QStringLiteral("/nonexistent/path/does_not_exist.flac") } };
    snap.currentIndex = 0;
    snap.position = 0.0;
    snap.mode = PlayMode::Sequential;

    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.restore(snap);

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 5000);
    QCOMPARE(
        errorSpy.at(0).at(0).toString(), QStringLiteral("/nonexistent/path/does_not_exist.flac"));
}

void TstPlayerPersistence::restoreEmptySnapshot()
{
    PlaybackSnapshot snap;
    snap.items = { };
    snap.currentIndex = -1;
    snap.position = 0.0;

    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    player.restore(snap);

    QCOMPARE(player.state(), Player::PlaybackState::Stopped);
    QCOMPARE(player.queue()->count(), 0);
    QCOMPARE(player.queue()->currentIndex(), -1);
}

void TstPlayerPersistence::endToEndStoreSaveLoadAndRestore()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString storePath = tempDir.filePath(QStringLiteral("playback_state.json"));
    PlaybackStateStore store(storePath);

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));

    // 1. Setup initial player, play, pause, save to file
    {
        Player player1({ { QStringLiteral("ao"), QStringLiteral("null") } });
        player1.queue()->setItems({ { .source = p440 }, { .source = silence } });
        player1.queue()->setMode(PlayMode::RepeatOne);
        player1.setVolume(55);
        player1.setMuted(false);

        player1.playIndex(1);
        QTRY_COMPARE_WITH_TIMEOUT(player1.state(), Player::PlaybackState::Playing, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player1.duration() > 1.0, 5000);

        player1.seek(1.5);
        player1.pause();
        QTRY_COMPARE_WITH_TIMEOUT(player1.state(), Player::PlaybackState::Paused, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(player1.position() - 1.5) <= 0.3, 5000);

        const PlaybackSnapshot snap = player1.snapshot();
        const bool saved = store.save(snap);
        QVERIFY(saved);
        QVERIFY(QFile::exists(storePath));
    }

    // 2. In a new Player, load from file and restore
    {
        const auto loadedSnap = store.load();
        QVERIFY(loadedSnap.has_value());

        Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
        QVERIFY(player2.isValid());

        player2.restore(*loadedSnap);

        QTRY_COMPARE_WITH_TIMEOUT(player2.state(), Player::PlaybackState::Paused, 5000);
        QCOMPARE(player2.queue()->currentIndex(), 1);
        QCOMPARE(player2.queue()->count(), 2);
        QCOMPARE(player2.queue()->mode(), PlayMode::RepeatOne);
        QTRY_COMPARE_WITH_TIMEOUT(player2.volume(), 55, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(player2.isMuted(), false, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(player2.position() - 1.5) <= 0.2, 5000);

        player2.play();
        QTRY_COMPARE_WITH_TIMEOUT(player2.state(), Player::PlaybackState::Playing, 5000);
    }
}

} // namespace

QTEST_MAIN(TstPlayerPersistence)
#include "tst_PlayerPersistence.moc"
