// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/PlayQueue.h>
#include <player/Player.h>

#include <cmath>

using linernotes::player::Player;

namespace {

class TstDucking : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void duckChangesGainButNotVolume();
    void unduckRestores();
    void retargetMidRampIsContinuous();
    void duckPersistsAcrossTrackChange();
    void duckGainChangedIsThrottled();
    void buildsDuckFilterAfterUserFilters();
};

void TstDucking::initTestCase()
{
    qRegisterMetaType<Player::PlaybackState>();
    qRegisterMetaType<linernotes::player::Player::PlaybackState>();
}

void TstDucking::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("af-command")));
}

void TstDucking::duckChangesGainButNotVolume()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());
    QCOMPARE(player.duckGain(), 1.0);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    QVERIFY(QFile::exists(path));

    QSignalSpy volSpy(&player, &Player::volumeChanged);
    QSignalSpy duckSpy(&player, &Player::duckFinished);

    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    const int initialVolume = player.volume();

    player.duckTo(0.2, 200);

    QTRY_COMPARE_WITH_TIMEOUT(duckSpy.count(), 1, 3000);
    QCOMPARE(duckSpy.at(0).at(0).toDouble(), 0.2);
    QVERIFY(std::abs(player.duckGain() - 0.2) < 0.01);
    QCOMPARE(player.volume(), initialVolume);
    QCOMPARE(volSpy.count(), 0);
}

void TstDucking::unduckRestores()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    player.duckTo(0.2, 100);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.duckGain() - 0.2) < 0.01, 3000);

    QSignalSpy duckSpy(&player, &Player::duckFinished);
    player.unduck(200);

    QTRY_COMPARE_WITH_TIMEOUT(duckSpy.count(), 1, 3000);
    QCOMPARE(duckSpy.at(0).at(0).toDouble(), 1.0);
    QVERIFY(std::abs(player.duckGain() - 1.0) < 0.01);
}

void TstDucking::retargetMidRampIsContinuous()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QSignalSpy gainSpy(&player, &Player::duckGainChanged);

    // Start a long ramp down
    player.duckTo(0.0, 400);

    // Wait until several gain changes have arrived
    QTRY_VERIFY_WITH_TIMEOUT(gainSpy.count() >= 3, 2000);

    const double lastGainBeforeRetarget = gainSpy.last().at(0).toDouble();
    const auto countBeforeRetarget = gainSpy.count();

    // Retarget mid-ramp towards 1.0
    player.duckTo(1.0, 400);

    // Wait for the next gain change to be emitted
    QTRY_VERIFY_WITH_TIMEOUT(gainSpy.count() > countBeforeRetarget, 2000);

    const double firstNewGain = gainSpy.at(countBeforeRetarget).at(0).toDouble();
    // Verify continuity: no jump
    QVERIFY2(std::abs(firstNewGain - lastGainBeforeRetarget) < 0.1,
        "Gain must transition continuously when retargeted mid-ramp");

    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.duckGain() - 1.0) < 0.01, 3000);
}

void TstDucking::duckPersistsAcrossTrackChange()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path1 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString path2 = linernotes::test::fixturePath(QStringLiteral("audio/tone_880_1s.ogg"));
    QVERIFY(QFile::exists(path1));
    QVERIFY(QFile::exists(path2));

    player.queue()->setItems({ { .source = path1 }, { .source = path2 } }, 0);
    player.playIndex(0);

    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    // Duck immediately to 0.3
    player.duckTo(0.3, 0);
    QCOMPARE(player.duckGain(), 0.3);

    QTRY_VERIFY_WITH_TIMEOUT(player.duckApplyCount() >= 1, 3000);
    const int applyCountBefore = player.duckApplyCount();

    // Switch to next track
    player.next();

    QTRY_COMPARE_WITH_TIMEOUT(player.currentSource(), path2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    // MpvHandle::audioReconfigured after track change re-applies duck gain
    QTRY_VERIFY_WITH_TIMEOUT(player.duckApplyCount() > applyCountBefore, 5000);

    // duckGain must remain 0.3
    QCOMPARE(player.duckGain(), 0.3);

    // Verify mpv af filter contains duck label
    const QVariant afVar = player.mpvAudioFilters();
    bool hasDuck = false;
    if (afVar.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = afVar.toList();
        for (const QVariant &item : list) {
            if (item.metaType().id() == QMetaType::QVariantMap) {
                const QVariantMap map = item.toMap();
                if (map.value(QStringLiteral("label")).toString() == QStringLiteral("duck")
                    || map.value(QStringLiteral("name"))
                        .toString()
                        .contains(QStringLiteral("duck"))) {
                    hasDuck = true;
                    break;
                }
            } else if (item.toString().contains(QStringLiteral("duck"))) {
                hasDuck = true;
                break;
            }
        }
    } else if (afVar.toString().contains(QStringLiteral("duck"))) {
        hasDuck = true;
    }
    QVERIFY2(hasDuck, "mpv af property must contain duck label");
}

void TstDucking::duckGainChangedIsThrottled()
{
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    QSignalSpy gainSpy(&player, &Player::duckGainChanged);
    QSignalSpy finishSpy(&player, &Player::duckFinished);

    player.duckTo(0.2, 300);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 3000);
    QCOMPARE(finishSpy.at(0).at(0).toDouble(), 0.2);

    // Check throttled signal emission count (e.g. <= 40)
    QVERIFY2(gainSpy.count() <= 40, "duckGainChanged emissions must be throttled");
    QVERIFY2(gainSpy.count() >= 1, "duckGainChanged must emit at least once");
    QCOMPARE(gainSpy.last().at(0).toDouble(), 0.2);
}

void TstDucking::buildsDuckFilterAfterUserFilters()
{
    Player player({
        { QStringLiteral("ao"), QStringLiteral("null") },
        { QStringLiteral("af"), QStringLiteral("lavfi=[anull]") },
    });
    QVERIFY(player.isValid());

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));

    player.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), Player::PlaybackState::Playing, 5000);

    const QVariant afVar = player.mpvAudioFilters();
    bool hasUserFilter = false;
    bool hasDuck = false;

    auto checkString = [&](const QString &str) {
        if (str.contains(QStringLiteral("anull"))) {
            hasUserFilter = true;
        }
        if (str.contains(QStringLiteral("duck"))) {
            hasDuck = true;
        }
    };

    if (afVar.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = afVar.toList();
        for (const QVariant &item : list) {
            if (item.metaType().id() == QMetaType::QVariantMap) {
                const QVariantMap map = item.toMap();
                checkString(map.value(QStringLiteral("name")).toString());
                checkString(map.value(QStringLiteral("label")).toString());
                const QVariantMap params = map.value(QStringLiteral("params")).toMap();
                for (auto it = params.cbegin(); it != params.cend(); ++it) {
                    checkString(it.value().toString());
                }
            } else {
                checkString(item.toString());
            }
        }
    } else {
        checkString(afVar.toString());
    }

    QVERIFY2(hasUserFilter, "mpv af property must contain user filter (anull)");
    QVERIFY2(hasDuck, "mpv af property must contain duck label");

    const int initialApplyCount = player.duckApplyCount();
    player.duckTo(0.4, 0);
    QTRY_VERIFY_WITH_TIMEOUT(player.duckApplyCount() > initialApplyCount, 3000);
    QCOMPARE(player.duckGain(), 0.4);
}

} // namespace

QTEST_MAIN(TstDucking)
#include "tst_Ducking.moc"
