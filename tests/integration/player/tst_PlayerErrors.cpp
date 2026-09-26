// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <player/MpvHandle.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>

using linernotes::player::Player;
using linernotes::player::PlayMode;

namespace {

class TstPlayerErrors : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void corruptFirstItemIsSkipped();
    void missingFileInMiddleIsSkipped();
    void allItemsFailStopsWithoutLooping();
    void repeatOneSkipsBrokenItem();
    void openFileWithMissingFileReportsErrorAndStops();
    void userNextToBrokenItemSkipsIt();
    void errorCounterResetsAfterSuccess();
};

void TstPlayerErrors::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
    qRegisterMetaType<PlayMode>();
    qRegisterMetaType<linernotes::player::PlayMode>();
}

void TstPlayerErrors::corruptFirstItemIsSkipped()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(corrupt));
    QVERIFY(QFile::exists(p440));

    player.queue()->setItems({ { .source = corrupt }, { .source = p440 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);
    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.at(0).at(0).toString(), corrupt);
    QVERIFY(!errorSpy.at(0).at(1).toString().isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QCOMPARE(player.currentSource(), p440);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 6000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayerErrors::missingFileInMiddleIsSkipped()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString missing = tempDir.filePath(QStringLiteral("non_existent_track.flac"));
    QVERIFY(!QFile::exists(missing));

    player.queue()->setItems({ { .source = p440 }, { .source = missing }, { .source = p660 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);
    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    player.playIndex(0);

    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.currentSource(), p440);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 2, 8000);
    QCOMPARE(player.currentSource(), p660);

    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), missing);
    QVERIFY(!errorSpy.at(0).at(1).toString().isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 6000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
}

void TstPlayerErrors::allItemsFailStopsWithoutLooping()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString missing = tempDir.filePath(QStringLiteral("missing_item.flac"));

    player.queue()->setMode(PlayMode::RepeatAll);
    player.queue()->setItems({ { .source = corrupt }, { .source = missing } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);

    QTest::qWait(1500);

    QCOMPARE(errorSpy.count(), 2);
    QCOMPARE(player.state(), Player::PlaybackState::Stopped);
}

void TstPlayerErrors::repeatOneSkipsBrokenItem()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setMode(PlayMode::RepeatOne);
    player.queue()->setItems({ { .source = corrupt }, { .source = p440 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.at(0).at(0).toString(), corrupt);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QCOMPARE(player.currentSource(), p440);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayerErrors::openFileWithMissingFileReportsErrorAndStops()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString missing = tempDir.filePath(QStringLiteral("non_existent_file.flac"));

    QSignalSpy errorSpy(&player, &Player::playbackError);
    QSignalSpy finishSpy(&player, &Player::playbackFinished);

    player.openFile(missing);

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.at(0).at(0).toString(), missing);
    QVERIFY(!errorSpy.at(0).at(1).toString().isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Stopped, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 5000);
}

void TstPlayerErrors::userNextToBrokenItemSkipsIt()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setItems({ { .source = silence }, { .source = corrupt }, { .source = p440 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
    QCOMPARE(player.queue()->currentIndex(), 0);

    player.next();

    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.at(0).at(0).toString(), corrupt);

    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 2, 5000);
    QCOMPARE(player.currentSource(), p440);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

void TstPlayerErrors::errorCounterResetsAfterSuccess()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setMode(PlayMode::RepeatAll);
    player.queue()->setItems({ { .source = corrupt }, { .source = p440 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);

    // 播放足以绕回一轮以上（至少两次看到 440 开始）：
    // 第一次：corrupt 出错 (error 1) -> 440 播放 (1s) -> corrupt 出错 (error 2) -> 440 再次播放
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 2, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 5000);
    QCOMPARE(player.currentSource(), p440);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);
}

} // namespace

QTEST_MAIN(TstPlayerErrors)
#include "tst_PlayerErrors.moc"
