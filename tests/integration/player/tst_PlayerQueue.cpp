// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/MpvHandle.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>

#include <algorithm>

using linernotes::player::Player;
using linernotes::player::PlayMode;
using linernotes::player::PlayQueue;

namespace {

class TstPlayerQueue : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void gaplessTransitionAdvancesQueue();
    void mpvPlaylistLimitsPreloadToTwoEntries();
    void openFileReplacesQueueAndPlays();
    void repeatOneLoops();
};

void TstPlayerQueue::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
    qRegisterMetaType<PlayMode>();
    qRegisterMetaType<linernotes::player::PlayMode>();
}

void TstPlayerQueue::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("af-command")));
}

void TstPlayerQueue::gaplessTransitionAdvancesQueue()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    QVERIFY(QFile::exists(p440));
    QVERIFY(QFile::exists(p660));

    player.queue()->setItems({ { .source = p440 }, { .source = p660 } });

    QSignalSpy finishSpy(&player, &Player::playbackFinished);
    player.playIndex(0);

    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.currentSource(), p440);

    // Queue advances to next track gaplessly
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QCOMPARE(player.currentSource(), p660);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QCOMPARE(player.queue()->currentIndex(), -1);
}

void TstPlayerQueue::mpvPlaylistLimitsPreloadToTwoEntries()
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
        maxPlaylistCount = std::max(cnt, maxPlaylistCount);
        QVERIFY(cnt <= 2);
    });

    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    player.stop();

    QVERIFY(maxPlaylistCount > 0);
    QVERIFY(maxPlaylistCount <= 2);
}

void TstPlayerQueue::openFileReplacesQueueAndPlays()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p880 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));

    player.queue()->setItems({ { .source = p440 } });
    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.openFile(p880);
    QCOMPARE(player.queue()->count(), 1);
    QCOMPARE(player.queue()->at(0).source, p880);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.currentSource(), p880);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayerQueue::repeatOneLoops()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setItems({ { .source = p440 } });
    player.queue()->setMode(PlayMode::RepeatOne);

    auto *mpv = player.findChild<linernotes::player::MpvHandle *>();
    QVERIFY(mpv != nullptr);
    QSignalSpy startFileSpy(mpv, &linernotes::player::MpvHandle::startFile);

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QTRY_VERIFY_WITH_TIMEOUT(startFileSpy.count() >= 2, 5000);
    QCOMPARE(player.currentSource(), p440);
    QCOMPARE(player.state(), Player::PlaybackState::Playing);
}

} // namespace

QTEST_MAIN(TstPlayerQueue)
#include "tst_PlayerQueue.moc"
