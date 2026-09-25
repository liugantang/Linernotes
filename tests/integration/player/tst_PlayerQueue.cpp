// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/MpvHandle.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>

#include <cmath>

using linernotes::player::Player;
using linernotes::player::PlayMode;
using linernotes::player::PlayQueue;

namespace {

class TstPlayerQueue : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void playsQueueInOrder();
    void gaplessTransitionDoesNotStop();
    void mpvPlaylistNeverExceedsTwoEntries();
    void nextAndPreviousNavigate();
    void previousRestartsAfterThreshold();
    void insertNextDuringPlaybackIsPlayedNext();
    void removingPreloadedNextSkipsIt();
    void removingCurrentKeepsPlaying();
    void repeatOnePlaysTrackAgainGaplessly();
    void stopThenPlayResumesCurrentItem();
    void openFileReplacesQueue();
};

void TstPlayerQueue::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
    qRegisterMetaType<PlayMode>();
    qRegisterMetaType<linernotes::player::PlayMode>();
}

void TstPlayerQueue::playsQueueInOrder()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));
    QVERIFY(QFile::exists(p440));
    QVERIFY(QFile::exists(p660));
    QVERIFY(QFile::exists(p880));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 }, { .source = p880 } });

    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    player.playIndex(0);

    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.currentSource(), p440);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QCOMPARE(player.currentSource(), p660);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 2, 5000);
    QCOMPARE(player.currentSource(), p880);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QCOMPARE(player.queue()->currentIndex(), -1);
}

void TstPlayerQueue::gaplessTransitionDoesNotStop()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 } });

    QSignalSpy stateSpy(&player, &Player::stateChanged);
    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    QElapsedTimer timer;
    connect(&player, &Player::stateChanged, [&](Player::PlaybackState st) {
        if (st == Player::PlaybackState::Playing && !timer.isValid()) {
            timer.start();
        }
    });

    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QVERIFY(timer.isValid());

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 6000);
    const qint64 elapsedMs = timer.elapsed();

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);

    // ao=null 会先消耗掉一个缓冲区，结束事件比真实播放时长提前约 0.2–0.35 s，因此下限放宽；
    // 上限用于发现切换时的间隙。
    QVERIFY2(elapsedMs >= 1400 && elapsedMs <= 2600,
        qPrintable(
            QStringLiteral("Elapsed time was %1 ms (expected [1400, 2600])").arg(elapsedMs)));

    QVERIFY(!stateSpy.isEmpty());
    for (int i = 0; i < stateSpy.count() - 1; ++i) {
        const auto st = stateSpy.at(i).at(0).value<Player::PlaybackState>();
        QVERIFY2(st != Player::PlaybackState::Stopped, "Stopped must only occur at the end");
    }
    QCOMPARE(stateSpy.last().at(0).value<Player::PlaybackState>(), Player::PlaybackState::Stopped);
}

void TstPlayerQueue::mpvPlaylistNeverExceedsTwoEntries()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 }, { .source = p880 } });

    int maxPlaylistCount = 0;
    connect(player.queue(), &PlayQueue::currentIndexChanged, [&]() {
        const int cnt = player.mpvPlaylistCount();
        if (cnt > maxPlaylistCount) {
            maxPlaylistCount = cnt;
        }
        QVERIFY(cnt <= 2);
    });

    QSignalSpy finishSpy(&player, &Player::playbackFinished);
    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);

    QVERIFY(maxPlaylistCount > 0);
    QVERIFY(maxPlaylistCount <= 2);
}

void TstPlayerQueue::nextAndPreviousNavigate()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    player.queue()->setItems(
        { { .source = silence }, { .source = silence }, { .source = silence } });

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.queue()->currentIndex(), 0);

    player.next();
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QVERIFY(player.position() < 3.0);
    player.previous();
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.previous();
    QCOMPARE(player.queue()->currentIndex(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() < 0.5, 5000);
}

void TstPlayerQueue::previousRestartsAfterThreshold()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    player.queue()->setItems({ { .source = silence } });

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Paused, 5000);

    player.seek(4.0);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 4.0) <= 0.3, 5000);

    player.previous();
    QCOMPARE(player.queue()->currentIndex(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() < 0.5, 5000);
}

void TstPlayerQueue::insertNextDuringPlaybackIsPlayedNext()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 } });

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.currentSource(), p440);

    player.queue()->insertNext({ { .source = p880 } });

    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), p880, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), p660, 5000);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayerQueue::removingPreloadedNextSkipsIt()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 }, { .source = p880 } });

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.currentSource(), p440);

    player.queue()->remove(1, 1);

    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), p880, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayerQueue::removingCurrentKeepsPlaying()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setItems({ { .source = silence }, { .source = p440 } });

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.currentSource(), silence);

    player.queue()->remove(0, 1);

    QCOMPARE(player.state(), Player::PlaybackState::Playing);
    QCOMPARE(player.currentSource(), silence);

    player.seek(4.5);

    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), p440, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayerQueue::repeatOnePlaysTrackAgainGaplessly()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 } });
    player.queue()->setMode(PlayMode::RepeatOne);

    QSignalSpy stateSpy(&player, &Player::stateChanged);

    auto *mpv = player.findChild<linernotes::player::MpvHandle *>();
    QVERIFY(mpv != nullptr);
    QSignalSpy startFileSpy(mpv, &linernotes::player::MpvHandle::startFile);

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QTRY_VERIFY_WITH_TIMEOUT(startFileSpy.count() >= 2, 5000);
    QCOMPARE(player.currentSource(), p440);
    QCOMPARE(player.state(), Player::PlaybackState::Playing);

    for (const auto &emission : stateSpy) {
        const auto st = emission.at(0).value<Player::PlaybackState>();
        QVERIFY2(st != Player::PlaybackState::Stopped,
            "No Stopped state should occur during RepeatOne loop");
    }

    player.next();
    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), p660, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayerQueue::stopThenPlayResumesCurrentItem()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 }, { .source = silence }, { .source = p880 } });

    player.playIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QCOMPARE(player.currentSource(), silence);

    player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QCOMPARE(player.queue()->currentIndex(), 1);

    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QCOMPARE(player.currentSource(), silence);
}

void TstPlayerQueue::openFileReplacesQueue()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 } });
    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.openFile(p880);
    QCOMPARE(player.queue()->count(), 1);
    QCOMPARE(player.queue()->at(0).source, p880);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.currentSource(), p880);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

} // namespace

QTEST_MAIN(TstPlayerQueue)
#include "tst_PlayerQueue.moc"
