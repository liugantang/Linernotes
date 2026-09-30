// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Settings.h>

namespace linernotes::ui {

using namespace Qt::StringLiterals;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kPlaybackReplayGain { u"playback/replayGain", u"track"_s };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kPlaybackGapless { u"playback/gapless", true };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kPlaybackAudioDevice { u"playback/audioDevice", u""_s };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kPlaybackExclusive { u"playback/exclusive", false };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<int> kPlaybackCountMinPercent { u"playback/countMinPercent", 50 };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<int> kPlaybackCountMinSeconds { u"playback/countMinSeconds", 240 };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kAppearanceLanguage { u"appearance/language", u"system"_s };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kAppearanceTheme { u"appearance/theme", u"system"_s };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kAppearanceAccentFromCover { u"appearance/accentFromCover",
    false };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kAppearanceTrayIcon { u"appearance/trayIcon", true };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kAppearanceCloseToTray { u"appearance/closeToTray", false };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kNotificationsTrackChange { u"notifications/trackChange",
    true };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kAppFirstRunCompleted { u"app/firstRunCompleted", false };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<int> kLibraryBackupKeep { u"library/backupKeep", 7 };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const core::SettingKey<QString> kLibraryArtistNamePreference {
    u"library/artistNamePreference", u"original"_s
};
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<double> kButlerAutoAccept { u"butler/autoAccept", 0.0 };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kButlerAutoCleanup { u"butler/autoCleanup", true };
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<bool> kUiShowTranslations { u"ui/showTranslations", false };

} // namespace linernotes::ui
