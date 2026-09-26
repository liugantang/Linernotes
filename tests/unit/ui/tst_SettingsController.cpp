// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <core/Settings.h>
#include <player/Player.h>
#include <ui/AppSettings.h>
#include <ui/SettingsController.h>

namespace {

using linernotes::core::Settings;
using linernotes::player::Player;
using linernotes::ui::SettingsController;

class TstSettingsController : public QObject {
    Q_OBJECT

private slots:
    void testThemeAndReplayGainSettings();
    void testAudioDeviceAndGaplessSettings();
};

void TstSettingsController::testThemeAndReplayGainSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);

    QCOMPARE(ctrl1.themeMode(), SettingsController::ThemeMode::System);
    QCOMPARE(ctrl1.replayGainMode(), Player::ReplayGainMode::Track);
    QCOMPARE(player.replayGainMode(), Player::ReplayGainMode::Track);

    QSignalSpy themeSpy(&ctrl1, &SettingsController::themeModeChanged);
    QSignalSpy replayGainSpy(&ctrl1, &SettingsController::replayGainModeChanged);

    ctrl1.setThemeMode(SettingsController::ThemeMode::Dark);
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(ctrl1.themeMode(), SettingsController::ThemeMode::Dark);

    ctrl1.setReplayGainMode(Player::ReplayGainMode::Album);
    QCOMPARE(replayGainSpy.count(), 1);
    QCOMPARE(ctrl1.replayGainMode(), Player::ReplayGainMode::Album);
    QCOMPARE(player.replayGainMode(), Player::ReplayGainMode::Album);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);

    QCOMPARE(ctrl2.themeMode(), SettingsController::ThemeMode::Dark);
    QCOMPARE(ctrl2.replayGainMode(), Player::ReplayGainMode::Album);
    QCOMPARE(player2.replayGainMode(), Player::ReplayGainMode::Album);
}

void TstSettingsController::testAudioDeviceAndGaplessSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl(settings, player);

    QSignalSpy gaplessSpy(&ctrl, &SettingsController::gaplessChanged);
    QSignalSpy exclSpy(&ctrl, &SettingsController::exclusiveModeChanged);
    QSignalSpy accentSpy(&ctrl, &SettingsController::accentFromCoverChanged);

    ctrl.setGapless(false);
    QCOMPARE(gaplessSpy.count(), 1);
    QCOMPARE(ctrl.gapless(), false);
    QCOMPARE(player.gapless(), false);

    ctrl.setExclusiveMode(true);
    QCOMPARE(exclSpy.count(), 1);
    QCOMPARE(ctrl.exclusiveMode(), true);

    ctrl.setAccentFromCover(true);
    QCOMPARE(accentSpy.count(), 1);
    QCOMPARE(ctrl.accentFromCover(), true);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);
    QCOMPARE(ctrl2.gapless(), false);
    QCOMPARE(player2.gapless(), false);
    QCOMPARE(ctrl2.exclusiveMode(), true);
    QCOMPARE(ctrl2.accentFromCover(), true);
}

} // namespace

QTEST_MAIN(TstSettingsController)
#include "tst_SettingsController.moc"
