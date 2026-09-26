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

    void initialStateIsStopped();
    void openFilePlays();
    void pauseAndResume();
    void openFileAfterPauseStartsPlaying();
    void stopResetsState();
    void seekChangesPosition();
    void volumeIsClampedAndNotifiesOnce_data();
    void volumeIsClampedAndNotifiesOnce();
    void muteToggles();
    void playbackFinishedEmittedAtEof();
    void stateChangedNotEmittedRedundantly();
    void audioDevicesContainsAuto();
    void defaultAudioDeviceIsAuto();
    void selectingUnknownDeviceFails();
    void selectingListedDeviceSucceeds();
    void exclusiveModeToggles();
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

void TstPlayer::initialStateIsStopped()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.state(), Player::PlaybackState::Stopped);
    QCOMPARE(player.position(), 0.0);
    QCOMPARE(player.duration(), 0.0);
    QVERIFY(player.currentSource().isEmpty());
    QCOMPARE(player.volume(), 100);
    QCOMPARE(player.isMuted(), false);
}

void TstPlayer::openFilePlays()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));

    QSignalSpy stateSpy(&player, &Player::stateChanged);
    QSignalSpy sourceSpy(&player, &Player::currentSourceChanged);

    player.openFile(path);

    QCOMPARE(player.currentSource(), path);
    QCOMPARE(sourceSpy.count(), 1);
    QCOMPARE(sourceSpy.at(0).at(0).toString(), path);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.duration() - 1.0) <= 0.1, 5000);
}

void TstPlayer::pauseAndResume()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);

    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);

    player.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayer::openFileAfterPauseStartsPlaying()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path1 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString path2 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));
    QVERIFY(QFile::exists(path1));
    QVERIFY(QFile::exists(path2));

    player.openFile(path1);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);

    player.openFile(path2);
    QCOMPARE(player.currentSource(), path2);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayer::stopResetsState()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0.05, 5000);

    player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QCOMPARE(player.position(), 0.0);
    QCOMPARE(player.duration(), 0.0);
    QCOMPARE(player.currentSource(), path);

    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayer::seekChangesPosition()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0.5, 5000);

    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);

    player.seek(0.5);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 0.5) <= 0.1, 5000);
}

void TstPlayer::volumeIsClampedAndNotifiesOnce_data()
{
    QTest::addColumn<int>("input");
    QTest::addColumn<int>("expected");

    QTest::newRow("clamp below min") << -10 << 0;
    QTest::newRow("clamp above max") << 150 << 100;
    QTest::newRow("normal value") << 50 << 50;
}

void TstPlayer::volumeIsClampedAndNotifiesOnce()
{
    QFETCH(int, input);
    QFETCH(int, expected);

    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    if (player.volume() == expected) {
        const int intermediate = (expected == 50) ? 70 : 50;
        player.setVolume(intermediate);
        QTRY_COMPARE_WITH_TIMEOUT(player.volume(), intermediate, 2000);
    }

    QSignalSpy spy(&player, &Player::volumeChanged);
    player.setVolume(input);
    QTRY_COMPARE_WITH_TIMEOUT(player.volume(), expected, 2000);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), expected);

    player.setVolume(input);
    QVERIFY(!spy.wait(50));
    QCOMPARE(spy.count(), 1);
}

void TstPlayer::muteToggles()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.isMuted(), false);

    QSignalSpy spy(&player, &Player::mutedChanged);

    player.setMuted(true);
    QTRY_COMPARE_WITH_TIMEOUT(player.isMuted(), true, 2000);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    player.setMuted(true);
    QVERIFY(!spy.wait(50));
    QCOMPARE(spy.count(), 1);

    player.setMuted(false);
    QTRY_COMPARE_WITH_TIMEOUT(player.isMuted(), false, 2000);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TstPlayer::playbackFinishedEmittedAtEof()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayer::stateChangedNotEmittedRedundantly()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QSignalSpy stateSpy(&player, &Player::stateChanged);
    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    player.openFile(path);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);

    QVERIFY(!stateSpy.isEmpty());
    for (int i = 1; i < stateSpy.count(); ++i) {
        const auto prev = stateSpy.at(i - 1).at(0).value<Player::PlaybackState>();
        const auto curr = stateSpy.at(i).at(0).value<Player::PlaybackState>();
        QVERIFY2(prev != curr, "Adjacent stateChanged emissions must not have the same state");
    }
}

void TstPlayer::audioDevicesContainsAuto()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QTRY_VERIFY_WITH_TIMEOUT(!player.audioDevices().isEmpty(), 5000);

    const QVariantList devices = player.audioDevices();
    QVERIFY(!devices.isEmpty());

    bool foundAuto = false;
    for (const QVariant &devVar : devices) {
        QCOMPARE(devVar.metaType().id(), QMetaType::QVariantMap);
        const QVariantMap dev = devVar.toMap();
        QVERIFY(dev.contains(QStringLiteral("name")));
        QVERIFY(dev.contains(QStringLiteral("description")));
        const QString name = dev.value(QStringLiteral("name")).toString();
        const QString desc = dev.value(QStringLiteral("description")).toString();
        QVERIFY(!name.isEmpty());
        QVERIFY(!desc.isEmpty());
        if (name == QStringLiteral("auto")) {
            foundAuto = true;
        }
    }
    QVERIFY(foundAuto);
}

void TstPlayer::defaultAudioDeviceIsAuto()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.audioDevice(), QStringLiteral("auto"));
    QCOMPARE(player.exclusiveMode(), false);
}

void TstPlayer::selectingUnknownDeviceFails()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QSignalSpy spy(&player, &Player::audioDeviceChanged);
    const QString origDevice = player.audioDevice();

    const bool result = player.selectAudioDevice(QStringLiteral("nonexistent_device_xyz_123"));
    QCOMPARE(result, false);
    QCOMPARE(player.audioDevice(), origDevice);
    QVERIFY(!spy.wait(50));
    QCOMPARE(spy.count(), 0);
}

void TstPlayer::selectingListedDeviceSucceeds()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(!player.audioDevices().isEmpty(), 5000);

    const QVariantList devices = player.audioDevices();
    const QVariantMap targetDev = devices.last().toMap();
    const QString targetName = targetDev.value(QStringLiteral("name")).toString();
    QVERIFY(!targetName.isEmpty());

    QSignalSpy spy(&player, &Player::audioDeviceChanged);
    const QString initialDevice = player.audioDevice();

    const bool result = player.selectAudioDevice(targetName);
    QCOMPARE(result, true);

    QTRY_COMPARE_WITH_TIMEOUT(player.audioDevice(), targetName, 5000);

    if (targetName != initialDevice) {
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), targetName);
    } else {
        QCOMPARE(spy.count(), 0);
    }
}

void TstPlayer::exclusiveModeToggles()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.exclusiveMode(), false);

    QSignalSpy spy(&player, &Player::exclusiveModeChanged);

    player.setExclusiveMode(true);
    QTRY_COMPARE_WITH_TIMEOUT(player.exclusiveMode(), true, 5000);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    // Setting same value again should not emit signal
    player.setExclusiveMode(true);
    QVERIFY(!spy.wait(50));
    QCOMPARE(spy.count(), 1);

    player.setExclusiveMode(false);
    QTRY_COMPARE_WITH_TIMEOUT(player.exclusiveMode(), false, 5000);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

} // namespace

QTEST_MAIN(TstPlayer)
#include "tst_Player.moc"
