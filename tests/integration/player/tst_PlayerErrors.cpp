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

using linernotes::player::MpvHandle;
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
    void preloadErrorWhenSyncPreloadExecutedAdvancesQueue();
    void preloadErrorWithoutStartFileAdvancesQueue();
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

void TstPlayerErrors::preloadErrorWhenSyncPreloadExecutedAdvancesQueue()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));

    player.queue()->setMode(PlayMode::RepeatAll);
    player.queue()->setItems({ { .source = corrupt }, { .source = p440 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);
    // 初始状态下 mpv 包含 [corrupt (1), 440 (2)]
    QTRY_COMPARE(player.mpvPlaylistCount(), 2);

    // 模拟 corrupt(1) 结束后切换到 440(2) 并成功加载
    player.testInjectStartFile(2);
    player.testInjectFileLoaded();
    QTRY_COMPARE(player.queue()->currentIndex(), 1);
    // 等待 440 建立预加载 [440 (2), corrupt (3)]
    QTRY_COMPARE(player.mpvPlaylistCount(), 2);

    // 模拟 440(2) 结束后自然无缝切到 corrupt(3)
    player.testInjectStartFile(3);
    QCOMPARE(player.queue()->currentIndex(), 0);

    // 等待 syncPreload 异步执行完毕：此时 corrupt(3) 建立了预加载 440(4)，使得 m_preloadEntryId !=
    // -1
    QTRY_COMPARE(player.mpvPlaylistCount(), 2);

    // 模拟 corrupt(3) 解码失败报错 endFile(Error)
    player.testInjectEndFile(
        3, MpvHandle::EndFileReason::Error, QStringLiteral("unrecognized file format"));

    // 在修复前：handleEndFileError 误以为 m_preloadEntryId != -1 会自动切，什么都不做，导致停在 0
    // 在修复后：必须推进到 440 (index 1) 并开始播放
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 1, 3000);
    QCOMPARE(player.currentSource(), p440);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 3000);
}

void TstPlayerErrors::preloadErrorWithoutStartFileAdvancesQueue()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString corrupt = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));

    player.queue()->setItems({ { .source = p440 }, { .source = corrupt }, { .source = p660 } });

    QSignalSpy errorSpy(&player, &Player::playbackError);

    player.playIndex(0);
    // 等待初始加载与预加载完成：[p440 (1), corrupt (2)]
    QTRY_COMPARE(player.mpvPlaylistCount(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);

    // 模拟预加载项 corrupt(2) 在后台直接解码失败 (endFile Error)，且此时 startFile(2) 从未发出
    player.testInjectEndFile(
        2, MpvHandle::EndFileReason::Error, QStringLiteral("unrecognized file format"));

    // 验证错误被记录
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), corrupt);

    // 模拟当前项 p440(1) 正常播完到达 EOF
    player.testInjectEndFile(1, MpvHandle::EndFileReason::Eof, QString());

    // 期望：p440 结束后不能因预加载项已报错而停滞，必须推进到下下一项 p660 (index 2) 并播放
    QTRY_COMPARE_WITH_TIMEOUT(player.queue()->currentIndex(), 2, 3000);
    QCOMPARE(player.currentSource(), p660);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 3000);
}

} // namespace

QTEST_MAIN(TstPlayerErrors)
#include "tst_PlayerErrors.moc"
