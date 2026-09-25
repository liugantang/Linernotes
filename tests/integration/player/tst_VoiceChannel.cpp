// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/Player.h>
#include <player/VoiceChannel.h>

using linernotes::player::Player;
using linernotes::player::VoiceChannel;

namespace {

class TstVoiceChannel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void playSucceedsAndFinishes();
    void playMissingFileFails();
    void playsConcurrentlyWithMusic();
    void stopAbortsPlaybackWithoutFinished();
    void setVolumeClamping();
};

void TstVoiceChannel::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
}

void TstVoiceChannel::playSucceedsAndFinishes()
{
    VoiceChannel voice({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(voice.isValid());
    QCOMPARE(voice.isPlaying(), false);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));

    QSignalSpy startSpy(&voice, &VoiceChannel::started);
    QSignalSpy finishSpy(&voice, &VoiceChannel::finished);
    QSignalSpy playSpy(&voice, &VoiceChannel::playingChanged);

    voice.play(path);

    // Should emit started and playing becomes true
    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(), 1, 3000);
    QCOMPARE(voice.isPlaying(), true);
    QVERIFY(playSpy.count() >= 1);
    QCOMPARE(playSpy.first().at(0).toBool(), true);

    // Eventually reaches EOF and finishes
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QCOMPARE(voice.isPlaying(), false);
    QCOMPARE(playSpy.last().at(0).toBool(), false);
}

void TstVoiceChannel::playMissingFileFails()
{
    VoiceChannel voice({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(voice.isValid());

    QSignalSpy failSpy(&voice, &VoiceChannel::failed);
    QSignalSpy finishSpy(&voice, &VoiceChannel::finished);

    voice.play(QStringLiteral("/nonexistent/audio/tone_missing_123.flac"));

    QTRY_COMPARE_WITH_TIMEOUT(failSpy.count(), 1, 3000);
    QCOMPARE(voice.isPlaying(), false);
    QCOMPARE(finishSpy.count(), 0);
}

void TstVoiceChannel::playsConcurrentlyWithMusic()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    VoiceChannel voice({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QVERIFY(voice.isValid());

    const QString musicPath
        = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString voicePath
        = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(musicPath));
    QVERIFY(QFile::exists(voicePath));

    player.openFile(musicPath);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QSignalSpy voiceFinishSpy(&voice, &VoiceChannel::finished);

    voice.play(voicePath);
    QTRY_VERIFY_WITH_TIMEOUT(voice.isPlaying(), 3000);

    // Both are playing simultaneously
    QCOMPARE(player.state(), Player::PlaybackState::Playing);
    QCOMPARE(voice.isPlaying(), true);

    // Wait for voice to finish (1s tone vs 5s silence)
    QTRY_COMPARE_WITH_TIMEOUT(voiceFinishSpy.count(), 1, 5000);
    QCOMPARE(voice.isPlaying(), false);

    // Player should still be playing
    QCOMPARE(player.state(), Player::PlaybackState::Playing);
}

void TstVoiceChannel::stopAbortsPlaybackWithoutFinished()
{
    VoiceChannel voice({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(voice.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    QVERIFY(QFile::exists(path));

    QSignalSpy startSpy(&voice, &VoiceChannel::started);
    QSignalSpy finishSpy(&voice, &VoiceChannel::finished);
    QSignalSpy failSpy(&voice, &VoiceChannel::failed);
    QSignalSpy playSpy(&voice, &VoiceChannel::playingChanged);

    voice.play(path);
    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(), 1, 3000);
    QCOMPARE(voice.isPlaying(), true);

    // Stop midway
    voice.stop();
    QCOMPARE(voice.isPlaying(), false);
    QCOMPARE(playSpy.last().at(0).toBool(), false);

    // Wait and verify no finished/failed signal emitted
    QTest::qWait(200);
    QCOMPARE(finishSpy.count(), 0);
    QCOMPARE(failSpy.count(), 0);
}

void TstVoiceChannel::setVolumeClamping()
{
    VoiceChannel voice({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(voice.isValid());
    QCOMPARE(voice.volume(), 100);

    QSignalSpy volSpy(&voice, &VoiceChannel::volumeChanged);

    voice.setVolume(50);
    QCOMPARE(voice.volume(), 50);
    QCOMPARE(volSpy.count(), 1);
    QCOMPARE(volSpy.at(0).at(0).toInt(), 50);

    voice.setVolume(150);
    QCOMPARE(voice.volume(), 100);
    QCOMPARE(volSpy.count(), 2);
    QCOMPARE(volSpy.at(1).at(0).toInt(), 100);

    voice.setVolume(-20);
    QCOMPARE(voice.volume(), 0);
    QCOMPARE(volSpy.count(), 3);
    QCOMPARE(volSpy.at(2).at(0).toInt(), 0);

    // Setting same volume again should not emit volumeChanged
    voice.setVolume(0);
    QCOMPARE(volSpy.count(), 3);
}

} // namespace

QTEST_MAIN(TstVoiceChannel)
#include "tst_VoiceChannel.moc"
