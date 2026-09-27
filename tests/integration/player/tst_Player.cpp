// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/Player.h>

#include <cmath>

using linernotes::player::Player;

namespace {

class TstPlayer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void initialStateAndControls();
    void playbackLifecycle();
    void playbackFinishedAtEof();
    void audioDevicesEmptyUntilRefreshed();
    void selectAutoWithoutRefresh();
    void refreshPopulatesDevices();
};

void TstPlayer::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
}

void TstPlayer::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("af-command")));
}

void TstPlayer::initialStateAndControls()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.state(), Player::PlaybackState::Stopped);
    QCOMPARE(player.position(), 0.0);
    QCOMPARE(player.duration(), 0.0);
    QVERIFY(player.currentSource().isEmpty());
    QCOMPARE(player.volume(), 100);
    QCOMPARE(player.isMuted(), false);
    QCOMPARE(player.audioDevice(), QStringLiteral("auto"));
    QCOMPARE(player.exclusiveMode(), false);

    // Volume clamping & signal
    QSignalSpy volSpy(&player, &Player::volumeChanged);
    player.setVolume(75);
    QTRY_COMPARE_WITH_TIMEOUT(player.volume(), 75, 2000);
    QCOMPARE(volSpy.count(), 1);

    player.setVolume(150);
    QTRY_COMPARE_WITH_TIMEOUT(player.volume(), 100, 2000);

    // Mute
    QSignalSpy muteSpy(&player, &Player::mutedChanged);
    player.setMuted(true);
    QTRY_COMPARE_WITH_TIMEOUT(player.isMuted(), true, 2000);
    QCOMPARE(muteSpy.count(), 1);

    // Exclusive mode
    QSignalSpy exclSpy(&player, &Player::exclusiveModeChanged);
    player.setExclusiveMode(true);
    QTRY_COMPARE_WITH_TIMEOUT(player.exclusiveMode(), true, 2000);
    QCOMPARE(exclSpy.count(), 1);
}

void TstPlayer::playbackLifecycle()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));

    QSignalSpy sourceSpy(&player, &Player::currentSourceChanged);
    player.openFile(path);

    QCOMPARE(player.currentSource(), path);
    QCOMPARE(sourceSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.duration() - 1.0) <= 0.1, 5000);

    // Pause and Resume
    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);
    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    // Seek
    player.seek(0.5);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 0.5) <= 0.1, 5000);

    // Stop
    player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QCOMPARE(player.position(), 0.0);
}

void TstPlayer::playbackFinishedAtEof()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayer::audioDevicesEmptyUntilRefreshed()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QVERIFY(player.audioDevices().isEmpty());
}

void TstPlayer::selectAutoWithoutRefresh()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QVERIFY(player.audioDevices().isEmpty());

    QSignalSpy spy(&player, &Player::audioDeviceChanged);
    const bool result = player.selectAudioDevice(QStringLiteral("auto"));
    QCOMPARE(result, true);
    QCOMPARE(player.audioDevice(), QStringLiteral("auto"));
    QVERIFY(player.audioDevices().isEmpty());
    QCOMPARE(spy.count(), 0);
}

void TstPlayer::refreshPopulatesDevices()
{
    if (!qEnvironmentVariableIsEmpty("LINERNOTES_NO_AUDIO_SERVER")) {
        QSKIP("LINERNOTES_NO_AUDIO_SERVER set: audio device enumeration may deadlock in "
              "libpipewire without audio server");
    }

    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QVERIFY(player.audioDevices().isEmpty());

    QSignalSpy spy(&player, &Player::audioDevicesChanged);
    player.refreshAudioDevices();

    QTRY_VERIFY_WITH_TIMEOUT(!player.audioDevices().isEmpty(), 5000);
    QVERIFY(spy.count() >= 1);
}

} // namespace

QTEST_MAIN(TstPlayer)
#include "tst_Player.moc"
