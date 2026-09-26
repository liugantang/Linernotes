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
    void testFirstRunCompletedSettings();
    void testLanguageSettings();
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

void TstSettingsController::testFirstRunCompletedSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl(settings, player);
    QCOMPARE(ctrl.firstRunCompleted(), false);

    QSignalSpy spy(&ctrl, &SettingsController::firstRunCompletedChanged);
    ctrl.setFirstRunCompleted(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(ctrl.firstRunCompleted(), true);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);
    QCOMPARE(ctrl2.firstRunCompleted(), true);
}

void TstSettingsController::testLanguageSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);
    QCOMPARE(ctrl1.language(), SettingsController::Language::System);

    QSignalSpy languageSpy(&ctrl1, &SettingsController::languageChanged);

    ctrl1.setLanguage(SettingsController::Language::Chinese);
    QCOMPARE(languageSpy.count(), 1);
    QCOMPARE(ctrl1.language(), SettingsController::Language::Chinese);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);

    QCOMPARE(ctrl2.language(), SettingsController::Language::Chinese);

    ctrl2.setLanguage(SettingsController::Language::English);
    QCOMPARE(ctrl2.language(), SettingsController::Language::English);

    SettingsController ctrl3(settings, player2);
    QCOMPARE(ctrl3.language(), SettingsController::Language::English);
}

} // namespace

QTEST_MAIN(TstSettingsController)
#include "tst_SettingsController.moc"
