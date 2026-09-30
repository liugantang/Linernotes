// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <core/Settings.h>
#include <library/ArtistNamePreference.h>
#include <library/PlayCountRule.h>
#include <player/Player.h>
#include <ui/AppSettings.h>
#include <ui/SettingsController.h>

namespace {

using linernotes::core::Settings;
using linernotes::library::ArtistNamePreference;
using linernotes::library::PlayCountRule;
using linernotes::player::Player;
using linernotes::ui::SettingsController;

class TstSettingsController : public QObject {
    Q_OBJECT

private slots:
    void testThemeAndReplayGainSettings();
    void testAudioDeviceAndGaplessSettings();
    void testFirstRunCompletedSettings();
    void testLanguageSettings();
    void testPlayCountRuleSettings();
    void testTrayAndNotificationSettings();
    void testArtistNamePreferenceSettings();
    void testShowTranslationsSettings();
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

void TstSettingsController::testPlayCountRuleSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);

    // 1. Defaults
    QCOMPARE(ctrl1.countMinPercent(), 50);
    QCOMPARE(ctrl1.countMinSeconds(), 240);
    const PlayCountRule defaultRule { .minPercent = 50, .minSeconds = 240 };
    QCOMPARE(ctrl1.playCountRule(), defaultRule);

    QSignalSpy percentSpy(&ctrl1, &SettingsController::countMinPercentChanged);
    QSignalSpy secondsSpy(&ctrl1, &SettingsController::countMinSecondsChanged);
    QSignalSpy ruleSpy(&ctrl1, &SettingsController::playCountRuleChanged);

    // 2. Out-of-bounds clamping
    // minPercent > 100 clamped to 100
    ctrl1.setCountMinPercent(150);
    QCOMPARE(ctrl1.countMinPercent(), 100);
    QCOMPARE(percentSpy.count(), 1);
    QCOMPARE(ruleSpy.count(), 1);

    // Setting same clamped value -> no signal emitted
    ctrl1.setCountMinPercent(100);
    QCOMPARE(percentSpy.count(), 1);
    QCOMPARE(ruleSpy.count(), 1);

    // minPercent < 1 clamped to 1
    ctrl1.setCountMinPercent(0);
    QCOMPARE(ctrl1.countMinPercent(), 1);
    QCOMPARE(percentSpy.count(), 2);
    QCOMPARE(ruleSpy.count(), 2);

    // minSeconds < 0 clamped to 0
    ctrl1.setCountMinSeconds(-50);
    QCOMPARE(ctrl1.countMinSeconds(), 0);
    QCOMPARE(secondsSpy.count(), 1);
    QCOMPARE(ruleSpy.count(), 3);

    // minSeconds > 3600 clamped to 3600
    ctrl1.setCountMinSeconds(9999);
    QCOMPARE(ctrl1.countMinSeconds(), 3600);
    QCOMPARE(secondsSpy.count(), 2);
    QCOMPARE(ruleSpy.count(), 4);

    // Setting valid custom values
    ctrl1.setCountMinPercent(70);
    QCOMPARE(ctrl1.countMinPercent(), 70);
    QCOMPARE(percentSpy.count(), 3);
    QCOMPARE(ruleSpy.count(), 5);

    ctrl1.setCountMinSeconds(180);
    QCOMPARE(ctrl1.countMinSeconds(), 180);
    QCOMPARE(secondsSpy.count(), 3);
    QCOMPARE(ruleSpy.count(), 6);

    const PlayCountRule customRule { .minPercent = 70, .minSeconds = 180 };
    QCOMPARE(ctrl1.playCountRule(), customRule);

    // 3. Persistence
    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);

    QCOMPARE(ctrl2.countMinPercent(), 70);
    QCOMPARE(ctrl2.countMinSeconds(), 180);
    QCOMPARE(ctrl2.playCountRule(), customRule);
}

void TstSettingsController::testTrayAndNotificationSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);

    // 1. Defaults
    QCOMPARE(ctrl1.trayIcon(), true);
    QCOMPARE(ctrl1.closeToTray(), false);
    QCOMPARE(ctrl1.trackChangeNotifications(), true);

    QSignalSpy traySpy(&ctrl1, &SettingsController::trayIconChanged);
    QSignalSpy closeSpy(&ctrl1, &SettingsController::closeToTrayChanged);
    QSignalSpy notifySpy(&ctrl1, &SettingsController::trackChangeNotificationsChanged);

    // 2. Modifying properties
    ctrl1.setTrayIcon(false);
    QCOMPARE(traySpy.count(), 1);
    QCOMPARE(ctrl1.trayIcon(), false);

    ctrl1.setCloseToTray(true);
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(ctrl1.closeToTray(), true);

    ctrl1.setTrackChangeNotifications(false);
    QCOMPARE(notifySpy.count(), 1);
    QCOMPARE(ctrl1.trackChangeNotifications(), false);

    // Setting same values -> no extra signals
    ctrl1.setTrayIcon(false);
    QCOMPARE(traySpy.count(), 1);
    ctrl1.setCloseToTray(true);
    QCOMPARE(closeSpy.count(), 1);
    ctrl1.setTrackChangeNotifications(false);
    QCOMPARE(notifySpy.count(), 1);

    // 3. Persistence
    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);

    QCOMPARE(ctrl2.trayIcon(), false);
    QCOMPARE(ctrl2.closeToTray(), true);
    QCOMPARE(ctrl2.trackChangeNotifications(), false);
}

void TstSettingsController::testArtistNamePreferenceSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);
    QCOMPARE(ctrl1.artistNamePreference(), ArtistNamePreference::Original);

    QSignalSpy prefSpy(&ctrl1, &SettingsController::artistNamePreferenceChanged);

    ctrl1.setArtistNamePreference(ArtistNamePreference::SimplifiedChinese);
    QCOMPARE(prefSpy.count(), 1);
    QCOMPARE(ctrl1.artistNamePreference(), ArtistNamePreference::SimplifiedChinese);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);

    QCOMPARE(ctrl2.artistNamePreference(), ArtistNamePreference::SimplifiedChinese);
}

void TstSettingsController::testShowTranslationsSettings()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    Player player({ { QStringLiteral("ao"), QStringLiteral("null") } });

    SettingsController ctrl1(settings, player);
    QCOMPARE(ctrl1.showTranslations(), false);

    QSignalSpy spy(&ctrl1, &SettingsController::showTranslationsChanged);

    ctrl1.setShowTranslations(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(ctrl1.showTranslations(), true);

    Player player2({ { QStringLiteral("ao"), QStringLiteral("null") } });
    SettingsController ctrl2(settings, player2);
    QCOMPARE(ctrl2.showTranslations(), true);
}

} // namespace

QTEST_MAIN(TstSettingsController)
#include "tst_SettingsController.moc"
